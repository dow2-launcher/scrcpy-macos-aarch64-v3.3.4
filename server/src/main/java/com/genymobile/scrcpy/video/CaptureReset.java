package com.genymobile.scrcpy.video;

import android.media.MediaCodec;
import android.os.Bundle;

import java.util.concurrent.atomic.AtomicBoolean;

public class CaptureReset implements SurfaceCapture.CaptureListener {

    private final AtomicBoolean reset = new AtomicBoolean();

    // Current instance of MediaCodec to "interrupt" on reset
    private MediaCodec runningMediaCodec;

    public boolean consumeReset() {
        return reset.getAndSet(false);
    }

    public synchronized void reset() {
        reset.set(true);
        if (runningMediaCodec != null) {
            try {
                runningMediaCodec.signalEndOfInputStream();
            } catch (IllegalStateException e) {
                // ignore
            }
        }
    }

    /**
     * Ask the running encoder (if any) to produce a key frame as soon as possible.
     */
    public synchronized void requestKeyFrame() {
        if (runningMediaCodec != null) {
            try {
                Bundle params = new Bundle();
                params.putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0);
                runningMediaCodec.setParameters(params);
            } catch (IllegalStateException e) {
                // ignore
            }
        }
    }

    public synchronized void setRunningMediaCodec(MediaCodec runningMediaCodec) {
        this.runningMediaCodec = runningMediaCodec;
    }

    @Override
    public void onInvalidated() {
        reset();
    }
}
