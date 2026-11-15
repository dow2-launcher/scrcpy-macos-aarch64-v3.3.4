package com.genymobile.scrcpy.opengl;

import com.genymobile.scrcpy.web.QuestParams;

import android.opengl.GLES11Ext;
import android.opengl.GLES20;

import java.nio.FloatBuffer;

/**
 * Meta Quest picture processing on the device GPU: lens correction, mirrored rotation, merge of the two eyes (or a single eye) and crop to a
 * view preset, in one pass (one texture fetch per output pixel).
 * <p>
 * The math is the same as in the scrcpy client (app/src/quest.c) and in the web page shader, in normalized units. The seam between the two
 * eyes is a straight vertical cut (the "fixed" seam mode of the client).
 */
public class QuestOpenGLFilter implements OpenGLFilter {

    // The same function is used by the web page (server/src/main/resources/questweb/index.html)
    // @formatter:off
    public static final String QUEST_MAP_GLSL = ""
            + "uniform float u_eye;\n" // 0: both, 1: left, 2: right
            + "uniform vec2 u_cs;\n" // cos and sin of the rotation angle
            + "uniform vec2 u_k;\n" // lens correction k1, k2
            + "uniform float u_off;\n" // offset of the right eye in the merged picture (fraction of the eye width)
            + "uniform float u_cut;\n" // position of the cut in the merged picture (fraction of the eye width)
            + "uniform vec4 u_crop;\n" // x0, y0, x1, y1
            + "uniform float u_aspect;\n" // eye width / eye height
            // uv: position in the output picture (x to the right, y down)
            // result: position in the raw buffer containing both eyes (x to the right, y down), and 1.0 if it is valid
            + "vec3 quest_map(vec2 uv) {\n"
            + "    float out_w = u_eye < 0.5 ? 1.0 + u_off : 1.0;\n"
            + "    float ox = mix(u_crop.x, u_crop.z, uv.x) * out_w;\n"
            + "    float oy = mix(u_crop.y, u_crop.w, uv.y);\n"
            + "    float eye = u_eye - 1.0;\n"
            + "    if (u_eye < 0.5) {\n"
            + "        if (ox < u_cut) {\n"
            + "            eye = 0.0;\n"
            + "        } else {\n"
            + "            eye = 1.0;\n"
            + "            ox -= u_off;\n"
            + "        }\n"
            + "    }\n"
            // un-rotate (left eye: by -angle, right eye: by +angle), then apply the lens correction
            + "    float sa = eye < 0.5 ? -u_cs.y : u_cs.y;\n"
            + "    vec2 c = vec2(0.5 * u_aspect, 0.5);\n"
            + "    vec2 d = vec2(ox * u_aspect, oy) - c;\n"
            + "    vec2 u = vec2(d.x * u_cs.x - d.y * sa, d.x * sa + d.y * u_cs.x);\n"
            + "    float r2 = dot(u, u) / dot(c, c);\n"
            + "    vec2 s = c + u * (1.0 + u_k.x * r2 + u_k.y * r2 * r2);\n"
            + "    float valid = abs(u.x) <= c.x && abs(u.y) <= c.y\n"
            + "            && s.x >= 0.0 && s.y >= 0.0 && s.x <= u_aspect && s.y <= 1.0 ? 1.0 : 0.0;\n"
            + "    return vec3((clamp(s.x / u_aspect, 0.0005, 0.9995) + eye) * 0.5, s.y, valid);\n"
            + "}\n";
    // @formatter:on

    private final QuestParams params;
    private final float aspect;
    private final float[] uniforms = new float[QuestParams.UNIFORMS_LENGTH];

    private int program;
    private FloatBuffer vertexBuffer;
    private FloatBuffer texCoordsBuffer;

    private int vertexPosLoc;
    private int texCoordsInLoc;
    private int texLoc;
    private int texMatrixLoc;
    private int eyeLoc;
    private int csLoc;
    private int kLoc;
    private int offLoc;
    private int cutLoc;
    private int cropLoc;
    private int aspectLoc;

    /**
     * @param params        the (live) parameters
     * @param displayWidth  width of the display (both eyes)
     * @param displayHeight height of the display
     */
    public QuestOpenGLFilter(QuestParams params, int displayWidth, int displayHeight) {
        this.params = params;
        this.aspect = (displayWidth / 2f) / displayHeight;
    }

    @Override
    public void init() throws OpenGLException {
        // @formatter:off
        String vertexShaderCode = "#version 100\n"
                + "attribute vec4 vertex_pos;\n"
                + "attribute vec4 tex_coords_in;\n"
                + "varying vec2 uv;\n"
                + "void main() {\n"
                + "    gl_Position = vertex_pos;\n"
                // In OpenGL, y is up
                + "    uv = vec2(tex_coords_in.x, 1.0 - tex_coords_in.y);\n"
                + "}";

        String fragmentShaderCode = "#version 100\n"
                + "#extension GL_OES_EGL_image_external : require\n"
                + "precision highp float;\n"
                + "uniform samplerExternalOES tex;\n"
                + "uniform mat4 tex_matrix;\n"
                + "varying vec2 uv;\n"
                + QUEST_MAP_GLSL
                + "void main() {\n"
                + "    vec3 src = quest_map(uv);\n"
                + "    if (src.z > 0.5) {\n"
                + "        gl_FragColor = texture2D(tex, (tex_matrix * vec4(src.x, 1.0 - src.y, 0.0, 1.0)).xy);\n"
                + "    } else {\n"
                + "        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);\n"
                + "    }\n"
                + "}";
        // @formatter:on

        program = GLUtils.createProgram(vertexShaderCode, fragmentShaderCode);
        if (program == 0) {
            throw new OpenGLException("Cannot create OpenGL program");
        }

        float[] vertices = {
                -1, -1, // Bottom-left
                1, -1, // Bottom-right
                -1, 1, // Top-left
                1, 1, // Top-right
        };

        float[] texCoords = {
                0, 0, // Bottom-left
                1, 0, // Bottom-right
                0, 1, // Top-left
                1, 1, // Top-right
        };

        vertexBuffer = GLUtils.createFloatBuffer(vertices);
        texCoordsBuffer = GLUtils.createFloatBuffer(texCoords);

        vertexPosLoc = GLES20.glGetAttribLocation(program, "vertex_pos");
        texCoordsInLoc = GLES20.glGetAttribLocation(program, "tex_coords_in");
        texLoc = GLES20.glGetUniformLocation(program, "tex");
        texMatrixLoc = GLES20.glGetUniformLocation(program, "tex_matrix");
        eyeLoc = GLES20.glGetUniformLocation(program, "u_eye");
        csLoc = GLES20.glGetUniformLocation(program, "u_cs");
        kLoc = GLES20.glGetUniformLocation(program, "u_k");
        offLoc = GLES20.glGetUniformLocation(program, "u_off");
        cutLoc = GLES20.glGetUniformLocation(program, "u_cut");
        cropLoc = GLES20.glGetUniformLocation(program, "u_crop");
        aspectLoc = GLES20.glGetUniformLocation(program, "u_aspect");
    }

    @Override
    public void draw(int textureId, float[] texMatrix) {
        GLES20.glUseProgram(program);
        GLUtils.checkGlError();

        GLES20.glEnableVertexAttribArray(vertexPosLoc);
        GLUtils.checkGlError();
        GLES20.glEnableVertexAttribArray(texCoordsInLoc);
        GLUtils.checkGlError();

        GLES20.glVertexAttribPointer(vertexPosLoc, 2, GLES20.GL_FLOAT, false, 0, vertexBuffer);
        GLUtils.checkGlError();
        GLES20.glVertexAttribPointer(texCoordsInLoc, 2, GLES20.GL_FLOAT, false, 0, texCoordsBuffer);
        GLUtils.checkGlError();

        GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
        GLUtils.checkGlError();
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureId);
        GLUtils.checkGlError();
        GLES20.glUniform1i(texLoc, 0);
        GLUtils.checkGlError();

        GLES20.glUniformMatrix4fv(texMatrixLoc, 1, false, texMatrix, 0);
        GLUtils.checkGlError();

        // The parameters may be changed at any time from the web page
        params.getUniforms(uniforms);
        GLES20.glUniform1f(eyeLoc, uniforms[0]);
        GLES20.glUniform2f(csLoc, uniforms[1], uniforms[2]);
        GLES20.glUniform2f(kLoc, uniforms[3], uniforms[4]);
        GLES20.glUniform1f(offLoc, uniforms[5]);
        GLES20.glUniform1f(cutLoc, uniforms[6]);
        GLES20.glUniform4f(cropLoc, uniforms[7], uniforms[8], uniforms[9], uniforms[10]);
        GLES20.glUniform1f(aspectLoc, aspect);
        GLUtils.checkGlError();

        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);
        GLUtils.checkGlError();
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4);
        GLUtils.checkGlError();
    }

    @Override
    public void release() {
        GLES20.glDeleteProgram(program);
        GLUtils.checkGlError();
    }
}
