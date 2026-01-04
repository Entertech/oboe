/*
 * Copyright 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "common/OboeDebug.h"
#include "FullDuplexHfpLoopback.h"

#define SECONDS_TO_RECORD_HFP 300  // 5 minutes max

oboe::Result FullDuplexHfpLoopback::start() {
    mPlaybackPosition = 0;
    mPlayedFrameCount = 0;
    mRecordedFrameCount = 0;

    int32_t outputSampleRate = getOutputStream()->getSampleRate();
    int32_t outputChannelCount = getOutputStream()->getChannelCount();
    int32_t inputChannelCount = getInputStream()->getChannelCount();
    int32_t inputSampleRate = getInputStream()->getSampleRate();

    // Create recording buffers
    mPlayedRecording = std::make_unique<MultiChannelRecording>(
            outputChannelCount,
            SECONDS_TO_RECORD_HFP * outputSampleRate);

    mRecordedRecording = std::make_unique<MultiChannelRecording>(
            inputChannelCount,
            SECONDS_TO_RECORD_HFP * inputSampleRate);

    // Setup peak detectors for input monitoring
    mNumInputChannels = inputChannelCount;
    mPeakDetectors = std::make_unique<PeakDetector[]>(mNumInputChannels);

    LOGD("FullDuplexHfpLoopback::start() - output: %d Hz, %d ch; input: %d Hz, %d ch",
         outputSampleRate, outputChannelCount, inputSampleRate, inputChannelCount);

    return FullDuplexStreamWithConversion::start();
}

double FullDuplexHfpLoopback::getPeakLevel(int index) {
    if (mPeakDetectors == nullptr) {
        LOGE("%s() called before start()", __func__);
        return -1.0;
    } else if (index < 0 || index >= mNumInputChannels) {
        LOGE("%s(), index out of range, 0 <= %d < %d", __func__, index, mNumInputChannels.load());
        return -2.0;
    }
    return mPeakDetectors[index].getLevel();
}

int32_t FullDuplexHfpLoopback::loadAudioData(const float *audioData, int32_t numFrames,
                                              int32_t channelCount, int32_t sampleRate) {
    if (audioData == nullptr || numFrames <= 0 || channelCount <= 0) {
        return -1;
    }

    int32_t totalSamples = numFrames * channelCount;
    mPlaybackBuffer.resize(totalSamples);
    memcpy(mPlaybackBuffer.data(), audioData, totalSamples * sizeof(float));

    mPlaybackNumFrames = numFrames;
    mPlaybackChannelCount = channelCount;
    mPlaybackSampleRate = sampleRate;
    mPlaybackPosition = 0;

    LOGD("FullDuplexHfpLoopback::loadAudioData() - loaded %d frames, %d channels, %d Hz",
         numFrames, channelCount, sampleRate);

    return 0;
}

oboe::DataCallbackResult FullDuplexHfpLoopback::onBothStreamsReadyFloat(
        const float *inputData,
        int numInputFrames,
        float *outputData,
        int numOutputFrames) {

    int32_t outputChannelCount = getOutputStream()->getChannelCount();
    int32_t inputChannelCount = getInputStream()->getChannelCount();

    // Process output (playback)
    float *outputFloat = outputData;
    int32_t framesWritten = 0;

    if (mPlaybackBuffer.empty() || mPlaybackNumFrames == 0) {
        // No audio loaded, output silence
        memset(outputData, 0, numOutputFrames * outputChannelCount * sizeof(float));
    } else {
        while (framesWritten < numOutputFrames) {
            if (mPlaybackPosition >= mPlaybackNumFrames) {
                if (mLoopPlayback) {
                    mPlaybackPosition = 0;
                } else {
                    // Fill remaining with silence
                    int32_t remainingFrames = numOutputFrames - framesWritten;
                    memset(outputFloat, 0, remainingFrames * outputChannelCount * sizeof(float));
                    framesWritten = numOutputFrames;
                    break;
                }
            }

            // Copy one frame, handling channel conversion
            int32_t srcOffset = mPlaybackPosition * mPlaybackChannelCount;
            for (int32_t ch = 0; ch < outputChannelCount; ch++) {
                int32_t srcChannel = ch % mPlaybackChannelCount;
                outputFloat[ch] = mPlaybackBuffer[srcOffset + srcChannel];
            }

            // Record what we're playing
            if (mPlayedRecording != nullptr) {
                mPlayedRecording->write(outputFloat, 1);
            }

            outputFloat += outputChannelCount;
            mPlaybackPosition++;
            framesWritten++;
            mPlayedFrameCount++;
        }
    }

    // Process input (recording from microphone)
    const float *inputFloatConst = inputData;
    for (int32_t i = 0; i < numInputFrames; i++) {
        // Record input - need to cast away const for write()
        if (mRecordedRecording != nullptr) {
            mRecordedRecording->write(const_cast<float*>(inputFloatConst), 1);
        }

        // Update peak detectors
        for (int32_t ch = 0; ch < inputChannelCount && ch < mNumInputChannels; ch++) {
            mPeakDetectors[ch].process(inputFloatConst[ch]);
        }

        inputFloatConst += inputChannelCount;
        mRecordedFrameCount++;
    }

    return oboe::DataCallbackResult::Continue;
}
