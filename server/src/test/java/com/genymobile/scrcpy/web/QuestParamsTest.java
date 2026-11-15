package com.genymobile.scrcpy.web;

import org.junit.Assert;
import org.junit.Test;

public class QuestParamsTest {

    @Test
    public void testDefaultOutputSize() {
        QuestParams params = new QuestParams();
        // Quest 3 display: 4128x2208, left eye, preset B: 1569x1413, downscaled to fit in 1920x1080
        Assert.assertArrayEquals(new int[] {1200, 1080}, params.getOutputSize(4128, 2208));

        Assert.assertEquals(QuestParams.CHANGED_SIZE, params.set("box", "3840x2160"));
        Assert.assertArrayEquals(new int[] {1568, 1416}, params.getOutputSize(4128, 2208));

        Assert.assertEquals(QuestParams.CHANGED_SIZE, params.set("preset", "A"));
        Assert.assertArrayEquals(new int[] {1568, 880}, params.getOutputSize(4128, 2208));

        // Both eyes merged, wide preset: (2064 * 1.156) * 0.738 x 2208 * 0.415
        Assert.assertEquals(QuestParams.CHANGED_SIZE, params.set("eye", "both"));
        Assert.assertArrayEquals(new int[] {1760, 920}, params.getOutputSize(4128, 2208));
    }

    @Test
    public void testSet() {
        QuestParams params = new QuestParams();
        Assert.assertEquals(QuestParams.UNCHANGED, params.set("eye", "left"));
        Assert.assertEquals(QuestParams.UNCHANGED, params.set("eye", "middle"));
        Assert.assertEquals(QuestParams.UNCHANGED, params.set("unknown", "1"));
        Assert.assertEquals(QuestParams.UNCHANGED, params.set("angle", "abc"));
        Assert.assertEquals(QuestParams.UNCHANGED, params.set("angle", "NaN"));
        Assert.assertEquals(QuestParams.UNCHANGED, params.set("box", "10x10"));
        Assert.assertEquals(QuestParams.CHANGED, params.set("angle", "21.5"));
        Assert.assertEquals(QuestParams.CHANGED, params.set("k1", "-0.3"));
        Assert.assertEquals(QuestParams.CHANGED, params.set("seam_x", "0.01"));
        // The overlap only changes the picture size if the two eyes are merged
        Assert.assertEquals(QuestParams.CHANGED, params.set("ovfrac", "0.85"));
        Assert.assertEquals(QuestParams.CHANGED_SIZE, params.set("eye", "both"));
        Assert.assertEquals(QuestParams.CHANGED_SIZE, params.set("ovfrac", "0.84"));
        Assert.assertEquals(QuestParams.CHANGED_SIZE, params.set("process", "page"));
        Assert.assertFalse(params.isDevice());

        // Out of range values are clamped
        Assert.assertEquals(QuestParams.CHANGED, params.set("angle", "500"));
        Assert.assertTrue(params.toJson().contains("\"angle\":45.0000"));
        Assert.assertTrue(params.toJson().contains("\"process\":\"page\""));
        Assert.assertTrue(params.toJson().contains("\"eye\":\"both\""));
    }

    @Test
    public void testIsValid() {
        Assert.assertTrue(QuestParams.isValid("eye", "left")); // the default value
        Assert.assertTrue(QuestParams.isValid("eye", "both"));
        Assert.assertTrue(QuestParams.isValid("preset", "b"));
        Assert.assertTrue(QuestParams.isValid("box", "1920x1080"));
        Assert.assertTrue(QuestParams.isValid("angle", "20.9"));
        Assert.assertTrue(QuestParams.isValid("process", "page"));
        Assert.assertFalse(QuestParams.isValid("eye", "booth"));
        Assert.assertFalse(QuestParams.isValid("preset", "E"));
        Assert.assertFalse(QuestParams.isValid("box", "1920"));
        Assert.assertFalse(QuestParams.isValid("angle", "x"));
        Assert.assertFalse(QuestParams.isValid("nothing", "1"));
    }

    @Test
    public void testUniforms() {
        QuestParams params = new QuestParams();
        float[] u = new float[QuestParams.UNIFORMS_LENGTH];
        params.getUniforms(u);
        Assert.assertEquals(QuestParams.EYE_LEFT, (int) u[0]);
        Assert.assertEquals(Math.cos(Math.toRadians(20.9)), u[1], 1e-6);
        Assert.assertEquals(Math.sin(Math.toRadians(20.9)), u[2], 1e-6);
        Assert.assertEquals(-0.26f, u[3], 1e-6);
        Assert.assertEquals(-0.05f, u[4], 1e-6);
        Assert.assertEquals(0.156f, u[5], 1e-6); // offset of the right eye
        Assert.assertEquals(0.156f + 0.422f, u[6], 1e-6); // cut at the center of the overlap
        // single eye preset B: x0, y0, x1, y1
        Assert.assertArrayEquals(new float[] {0.12f, 0.18f, 0.88f, 0.82f}, new float[] {u[7], u[8], u[9], u[10]}, 1e-6f);
    }
}
