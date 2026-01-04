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

package com.mobileer.oboetester;

import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.core.content.FileProvider;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

/**
 * Activity for HFP loopback testing.
 * - Select an audio file to play
 * - Loop playback through HFP device
 * - Record from HFP device microphone
 * - Export both played and recorded audio
 */
public class HfpLoopbackActivity extends TestInputActivity {

    private Button mSelectFileButton;
    private Button mStartButton;
    private Button mStopButton;
    private Button mExportPlayedButton;
    private Button mExportRecordedButton;
    private TextView mStatusTextView;
    private TextView mFileInfoTextView;

    private boolean mIsRunning = false;
    private String mSelectedAudioPath = null;
    private File mPlayedWavFile = null;
    private File mRecordedWavFile = null;

    private AudioOutputTester mAudioOutTester;
    private HfpLoopbackSniffer mSniffer = new HfpLoopbackSniffer();

    // Native methods for HFP loopback
    private native int loadAudioFile(String filePath);
    private native void setLoopPlayback(boolean loop);
    private native int savePlayedWaveFile(String absolutePath);
    private native int saveRecordedWaveFile(String absolutePath);
    private native long getPlayedFrameCount();
    private native long getRecordedFrameCount();

    private final ActivityResultLauncher<String> mFilePickerLauncher =
            registerForActivityResult(new ActivityResultContracts.GetContent(), uri -> {
                if (uri != null) {
                    onAudioFileSelected(uri);
                }
            });

    protected class HfpLoopbackSniffer extends NativeSniffer {
        @Override
        public void updateStatusText() {
            updateStatus();
        }
    }

    @Override
    protected void inflateActivity() {
        setContentView(R.layout.activity_hfp_loopback);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        mSelectFileButton = findViewById(R.id.button_select_file);
        mStartButton = findViewById(R.id.button_start_hfp);
        mStopButton = findViewById(R.id.button_stop_hfp);
        mExportPlayedButton = findViewById(R.id.button_export_played);
        mExportRecordedButton = findViewById(R.id.button_export_recorded);
        mStatusTextView = findViewById(R.id.text_hfp_status);
        mFileInfoTextView = findViewById(R.id.text_file_info);

        mAudioOutTester = addAudioOutputTester();

        mCommunicationDeviceView = findViewById(R.id.comm_device_view);

        updateButtonStates();
        hideSettingsViews();
    }

    @Override
    int getActivityType() {
        return TestAudioActivity.ACTIVITY_HFP_LOOPBACK;
    }

    @Override
    protected void resetConfiguration() {
        super.resetConfiguration();
        mAudioOutTester.reset();
    }

    public void onSelectFile(View view) {
        mFilePickerLauncher.launch("audio/*");
    }

    private void onAudioFileSelected(Uri uri) {
        try {
            // Copy the file to app's cache directory
            File cacheDir = getCacheDir();
            File audioFile = new File(cacheDir, "hfp_input_audio.wav");

            InputStream inputStream = getContentResolver().openInputStream(uri);
            FileOutputStream outputStream = new FileOutputStream(audioFile);

            byte[] buffer = new byte[4096];
            int bytesRead;
            while ((bytesRead = inputStream.read(buffer)) != -1) {
                outputStream.write(buffer, 0, bytesRead);
            }

            inputStream.close();
            outputStream.close();

            mSelectedAudioPath = audioFile.getAbsolutePath();

            // Load the audio file in native
            int result = loadAudioFile(mSelectedAudioPath);
            if (result >= 0) {
                mFileInfoTextView.setText("File: " + uri.getLastPathSegment() + "\nLoaded successfully");
                updateButtonStates();
            } else {
                showErrorToast("Failed to load audio file: " + result);
                mSelectedAudioPath = null;
                mFileInfoTextView.setText("Failed to load file");
            }
        } catch (IOException e) {
            showErrorToast("Error reading file: " + e.getMessage());
            mFileInfoTextView.setText("Error reading file");
        }
    }

    public void onStartHfpLoopback(View view) {
        if (mSelectedAudioPath == null) {
            showErrorToast("Please select an audio file first");
            return;
        }

        try {
            setLoopPlayback(true);
            openAudio();
            startAudio();
            mIsRunning = true;
            updateButtonStates();
            keepScreenOn(true);
            mSniffer.startSniffer();
        } catch (IOException e) {
            showErrorToast("Failed to start: " + e.getMessage());
        }
    }

    public void onStopHfpLoopback(View view) {
        mSniffer.stopSniffer();
        stopAudio();
        closeAudio();
        mIsRunning = false;
        updateButtonStates();
        keepScreenOn(false);
    }

    public void onExportPlayed(View view) {
        mPlayedWavFile = createWavFileName("hfp_played");
        int result = savePlayedWaveFile(mPlayedWavFile.getAbsolutePath());
        if (result > 0) {
            showToast("Saved played audio: " + result + " bytes");
            shareWavFile(mPlayedWavFile, "Share Played Audio");
        } else {
            showErrorToast("Failed to save played audio: " + result);
        }
    }

    public void onExportRecorded(View view) {
        mRecordedWavFile = createWavFileName("hfp_recorded");
        int result = saveRecordedWaveFile(mRecordedWavFile.getAbsolutePath());
        if (result > 0) {
            showToast("Saved recorded audio: " + result + " bytes");
            shareWavFile(mRecordedWavFile, "Share Recorded Audio");
        } else {
            showErrorToast("Failed to save recorded audio: " + result);
        }
    }

    private File createWavFileName(String prefix) {
        File dir = getExternalFilesDir(Environment.DIRECTORY_MUSIC);
        return new File(dir, prefix + "_" + AutomatedTestRunner.getTimestampString() + ".wav");
    }

    private void shareWavFile(File file, String title) {
        Intent sharingIntent = new Intent(Intent.ACTION_SEND);
        sharingIntent.setType("audio/wav");
        sharingIntent.putExtra(Intent.EXTRA_SUBJECT, file.getName());
        Uri uri = FileProvider.getUriForFile(this,
                BuildConfig.APPLICATION_ID + ".provider",
                file);
        sharingIntent.putExtra(Intent.EXTRA_STREAM, uri);
        sharingIntent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        startActivity(Intent.createChooser(sharingIntent, title));
    }

    private void updateButtonStates() {
        boolean hasFile = mSelectedAudioPath != null;

        mSelectFileButton.setEnabled(!mIsRunning);
        mStartButton.setEnabled(hasFile && !mIsRunning);
        mStopButton.setEnabled(mIsRunning);
        mExportPlayedButton.setEnabled(!mIsRunning && hasFile);
        mExportRecordedButton.setEnabled(!mIsRunning && hasFile);
    }

    private void updateStatus() {
        long playedFrames = getPlayedFrameCount();
        long recordedFrames = getRecordedFrameCount();

        StringBuilder sb = new StringBuilder();
        sb.append("Status: ").append(mIsRunning ? "Running" : "Stopped").append("\n");
        sb.append("Played frames: ").append(playedFrames).append("\n");
        sb.append("Recorded frames: ").append(recordedFrames).append("\n");

        runOnUiThread(() -> mStatusTextView.setText(sb.toString()));
    }

    @Override
    String getWaveTag() {
        return "hfp_loopback";
    }

    @Override
    boolean isOutput() {
        return false;
    }
}
