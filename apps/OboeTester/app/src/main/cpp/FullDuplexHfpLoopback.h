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

#ifndef OBOETESTER_FULL_DUPLEX_HFP_LOOPBACK_H
#define OBOETESTER_FULL_DUPLEX_HFP_LOOPBACK_H

#include <unistd.h>
#include <sys/types.h>
#include <atomic>
#include <vector>
#include <memory>

#include "oboe/Oboe.h"
#include "analyzer/PeakDetector.h"
#include "FullDuplexStreamWithConversion.h"
#include "MultiChannelRecording.h"

/**
 * Full-duplex stream for HFP loopback testing.
 * Plays audio from a loaded file while recording from microphone.
 */
class FullDuplexHfpLoopback : public FullDuplexStreamWithConversion {
public:
    FullDuplexHfpLoopback() {
        setNumInputBurstsCushion(0);
    }

    /**
     * Called when data is available on both streams.
     */
    oboe::DataCallbackResult onBothStreamsReadyFloat(
            const float *inputData,
            int numInputFrames,
            float *outputData,
            int numOutputFrames
    ) override;

    oboe::Result start() override;

    double getPeakLevel(int index);

    /**
     * Load audio data for playback.
     * @param audioData pointer to audio samples
     * @param numFrames number of frames
     * @param channelCount number of channels in the audio data
     * @param sampleRate sample rate of the audio data
     * @return 0 on success, negative on error
     */
    int32_t loadAudioData(const float *audioData, int32_t numFrames,
                          int32_t channelCount, int32_t sampleRate);

    void setLoopPlayback(bool loop) {
        mLoopPlayback = loop;
    }

    int64_t getPlayedFrameCount() const {
        return mPlayedFrameCount.load();
    }

    int64_t getRecordedFrameCount() const {
        return mRecordedFrameCount.load();
    }

    MultiChannelRecording* getPlayedRecording() {
        return mPlayedRecording.get();
    }

    MultiChannelRecording* getRecordedRecording() {
        return mRecordedRecording.get();
    }

private:
    std::vector<float> mPlaybackBuffer;
    int32_t mPlaybackPosition = 0;
    int32_t mPlaybackNumFrames = 0;
    int32_t mPlaybackChannelCount = 1;
    int32_t mPlaybackSampleRate = 48000;
    std::atomic<bool> mLoopPlayback{true};

    std::atomic<int64_t> mPlayedFrameCount{0};
    std::atomic<int64_t> mRecordedFrameCount{0};

    std::unique_ptr<MultiChannelRecording> mPlayedRecording;
    std::unique_ptr<MultiChannelRecording> mRecordedRecording;

    std::atomic<int32_t> mNumInputChannels{0};
    std::unique_ptr<PeakDetector[]> mPeakDetectors;
};

#endif //OBOETESTER_FULL_DUPLEX_HFP_LOOPBACK_H
