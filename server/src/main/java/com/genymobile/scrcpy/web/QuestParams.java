package com.genymobile.scrcpy.web;

import java.util.Locale;

/**
 * Parameters of the Meta Quest picture processing in web mode.
 * <p>
 * The Quest display buffer contains both eyes side by side, each one pre-distorted for the lens and rotated. The processing (lens correction,
 * mirrored rotation, merge or single eye, crop to a view preset) is the same as in the scrcpy client (app/src/quest.c), but expressed in
 * normalized units, so that it can be executed by a fragment shader, either on the device (the stream is a flat picture, any player can
 * display it) or in the web page (the stream is the raw buffer).
 * <p>
 * This class has no Android dependency (it is unit-tested on the JVM).
 */
public final class QuestParams {

    public static final int EYE_BOTH = 0;
    public static final int EYE_LEFT = 1;
    public static final int EYE_RIGHT = 2;

    /** The value did not change (or the key/value is invalid). */
    public static final int UNCHANGED = 0;
    /** The value changed, it is applied to the next frame. */
    public static final int CHANGED = 1;
    /** The value changed, the video size may be different: the capture must be restarted. */
    public static final int CHANGED_SIZE = 2;

    private static final String[] EYE_NAMES = {"both", "left", "right"};
    private static final String PRESET_NAMES = "ABCD";

    // {x0, x1, y0, y1} as fractions of the picture (same values as in app/src/quest.c)
    private static final float[][] PRESETS = {
            {0.131f, 0.869f, 0.097f, 0.512f}, // A: wide (~1.69:1)
            {0.151f, 0.849f, 0.045f, 0.631f}, // B: (~1.13:1)
            {0.194f, 0.811f, 0.026f, 0.740f}, // C: tall (~0.82:1)
            {0f, 1f, 0f, 1f}, // D: full picture
    };

    private static final float[][] EYE_PRESETS = {
            {0.12f, 0.88f, 0.30f, 0.70f}, // A: wide (~1.78:1)
            {0.12f, 0.88f, 0.18f, 0.82f}, // B: (~1.11:1)
            {0.20f, 0.80f, 0.08f, 0.92f}, // C: tall (~0.67:1)
            {0f, 1f, 0f, 1f}, // D: full picture
    };

    private boolean device = true; // process on the device (false: the web page processes the raw buffer)
    private int eye = EYE_LEFT;
    private float angle = 20.9f; // degrees
    private float k1 = -0.26f;
    private float k2 = -0.05f;
    private float ovfrac = 0.844f;
    private float seamX;
    private int preset = 1; // B
    // The processed picture is downscaled to fit in this box (most TV decoders are limited to 1920x1080 in H.264)
    private int boxWidth = 1920;
    private int boxHeight = 1080;

    public synchronized boolean isDevice() {
        return device;
    }

    private static float clamp(float value, float min, float max) {
        return value < min ? min : value > max ? max : value;
    }

    private static int indexOf(String[] names, String value) {
        for (int i = 0; i < names.length; ++i) {
            if (names[i].equals(value)) {
                return i;
            }
        }
        return -1;
    }

    /**
     * Set a parameter from its textual representation.
     *
     * @return {@link #UNCHANGED}, {@link #CHANGED} or {@link #CHANGED_SIZE}
     */
    public synchronized int set(String key, String value) {
        try {
            switch (key) {
                case "process": {
                    boolean newDevice;
                    if ("device".equals(value)) {
                        newDevice = true;
                    } else if ("page".equals(value)) {
                        newDevice = false;
                    } else {
                        return UNCHANGED;
                    }
                    if (newDevice == device) {
                        return UNCHANGED;
                    }
                    device = newDevice;
                    return CHANGED_SIZE;
                }
                case "eye": {
                    int newEye = indexOf(EYE_NAMES, value);
                    if (newEye == -1 || newEye == eye) {
                        return UNCHANGED;
                    }
                    eye = newEye;
                    return CHANGED_SIZE;
                }
                case "preset": {
                    int newPreset = value.length() == 1 ? PRESET_NAMES.indexOf(value.toUpperCase(Locale.ENGLISH)) : -1;
                    if (newPreset == -1 || newPreset == preset) {
                        return UNCHANGED;
                    }
                    preset = newPreset;
                    return CHANGED_SIZE;
                }
                case "box": {
                    // "<width>x<height>"
                    int sep = value.indexOf('x');
                    if (sep == -1) {
                        return UNCHANGED;
                    }
                    int w = Integer.parseInt(value.substring(0, sep));
                    int h = Integer.parseInt(value.substring(sep + 1));
                    if (w < 64 || h < 64 || w > 8192 || h > 8192 || (w == boxWidth && h == boxHeight)) {
                        return UNCHANGED;
                    }
                    boxWidth = w;
                    boxHeight = h;
                    return CHANGED_SIZE;
                }
                case "ovfrac": {
                    float v = parse(value, 0.5f, 1f);
                    if (v == ovfrac) {
                        return UNCHANGED;
                    }
                    ovfrac = v;
                    // The width of the merged picture depends on the overlap
                    return eye == EYE_BOTH ? CHANGED_SIZE : CHANGED;
                }
                case "angle": {
                    float v = parse(value, -45f, 45f);
                    if (v == angle) {
                        return UNCHANGED;
                    }
                    angle = v;
                    return CHANGED;
                }
                case "k1": {
                    float v = parse(value, -1f, 1f);
                    if (v == k1) {
                        return UNCHANGED;
                    }
                    k1 = v;
                    return CHANGED;
                }
                case "k2": {
                    float v = parse(value, -1f, 1f);
                    if (v == k2) {
                        return UNCHANGED;
                    }
                    k2 = v;
                    return CHANGED;
                }
                case "seam_x": {
                    float v = parse(value, -0.5f, 0.5f);
                    if (v == seamX) {
                        return UNCHANGED;
                    }
                    seamX = v;
                    return CHANGED;
                }
                default:
                    return UNCHANGED;
            }
        } catch (NumberFormatException e) {
            return UNCHANGED;
        }
    }

    /**
     * Indicate whether a parameter exists and accepts this value.
     */
    public static boolean isValid(String key, String value) {
        if (new QuestParams().set(key, value) != UNCHANGED) {
            return true;
        }
        // Either the value is invalid, or it is the default one: try from a state where no parameter has its default value
        QuestParams other = new QuestParams();
        other.device = false;
        other.eye = EYE_RIGHT;
        other.preset = 3;
        other.boxWidth = 1280;
        other.boxHeight = 720;
        other.ovfrac = 0.9f;
        other.angle = 1f;
        other.k1 = 0.1f;
        other.k2 = 0.1f;
        other.seamX = 0.1f;
        return other.set(key, value) != UNCHANGED;
    }

    private static float parse(String value, float min, float max) {
        float v = Float.parseFloat(value);
        if (v != v) {
            throw new NumberFormatException("NaN");
        }
        return clamp(v, min, max);
    }

    private float[] getCrop() {
        return (eye == EYE_BOTH ? PRESETS : EYE_PRESETS)[preset];
    }

    // Width of the processed picture before cropping, as a fraction of the eye width
    private float getOutWidth() {
        return eye == EYE_BOTH ? 2f - ovfrac : 1f;
    }

    /**
     * Compute the size of the processed picture for a given display size (both eyes side by side).
     *
     * @return {width, height}, both multiple of 8
     */
    public synchronized int[] getOutputSize(int displayWidth, int displayHeight) {
        float[] crop = getCrop();
        float w = (crop[1] - crop[0]) * getOutWidth() * (displayWidth / 2f);
        float h = (crop[3] - crop[2]) * displayHeight;
        float scale = Math.min(1f, Math.min(boxWidth / w, boxHeight / h));
        return new int[] {round8(w * scale, boxWidth), round8(h * scale, boxHeight)};
    }

    private static int round8(float value, int max) {
        int v = (Math.round(value) + 4) & ~7;
        if (v > max) {
            v = max & ~7;
        }
        return Math.max(v, 16);
    }

    /**
     * Number of floats written by {@link #getUniforms(float[])}.
     */
    public static final int UNIFORMS_LENGTH = 11;

    /**
     * Write the shader parameters:
     * <pre>
     *   [0]    eye: 0 both, 1 left, 2 right
     *   [1-2]  cos and sin of the rotation angle
     *   [3-4]  k1, k2
     *   [5]    offset of the right eye in the merged picture (fraction of the eye width)
     *   [6]    position of the cut between the two eyes in the merged picture (fraction of the eye width)
     *   [7-10] crop: x0, y0, x1, y1
     * </pre>
     */
    public synchronized void getUniforms(float[] out) {
        double a = Math.toRadians(angle);
        float off = 1f - ovfrac;
        float[] crop = getCrop();
        out[0] = eye;
        out[1] = (float) Math.cos(a);
        out[2] = (float) Math.sin(a);
        out[3] = k1;
        out[4] = k2;
        out[5] = off;
        out[6] = clamp(off + ovfrac / 2f + seamX, off, 1f);
        out[7] = crop[0];
        out[8] = crop[2];
        out[9] = crop[1];
        out[10] = crop[3];
    }

    private static String num(float value) {
        return String.format(Locale.ENGLISH, "%.4f", value);
    }

    /**
     * Serialize as a JSON object (sent to the web page).
     */
    public synchronized String toJson() {
        return "{\"process\":\"" + (device ? "device" : "page") + "\",\"eye\":\"" + EYE_NAMES[eye] + "\",\"preset\":\""
                + PRESET_NAMES.charAt(preset) + "\",\"angle\":" + num(angle) + ",\"k1\":" + num(k1) + ",\"k2\":" + num(k2) + ",\"ovfrac\":"
                + num(ovfrac) + ",\"seam_x\":" + num(seamX) + ",\"box\":\"" + boxWidth + "x" + boxHeight + "\"}";
    }
}
