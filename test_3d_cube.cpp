// 3D rotating cube rendered via ANGLE WebGPU backend.
// Uses: depth test, mat4 MVP uniform, indexed draw, multiple vertex attributes.
// Saves multiple frames as PPM images showing rotation.

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#define CHECK_GL(msg) do { \
    GLenum err = glGetError(); \
    if (err != GL_NO_ERROR) { \
        fprintf(stderr, "GL error 0x%x at %s\n", err, msg); \
        return 1; \
    } \
} while(0)

static const char *kVertSrc =
    "attribute vec3 aPos;\n"
    "attribute vec3 aColor;\n"
    "uniform mat4 uMVP;\n"
    "varying vec3 vColor;\n"
    "void main() {\n"
    "  gl_Position = uMVP * vec4(aPos, 1.0);\n"
    "  vColor = aColor;\n"
    "}\n";

static const char *kFragSrc =
    "precision mediump float;\n"
    "varying vec3 vColor;\n"
    "void main() {\n"
    "  gl_FragColor = vec4(vColor, 1.0);\n"
    "}\n";

static GLuint compileShader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        fprintf(stderr, "Shader compile error: %s\n", log);
        return 0;
    }
    return s;
}

// --- Math helpers ---
struct Mat4 { float m[16]; };

static Mat4 mat4Identity() {
    Mat4 r = {};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

static Mat4 mat4Multiply(const Mat4 &a, const Mat4 &b) {
    Mat4 r = {};
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++)
            for (int k = 0; k < 4; k++)
                r.m[col*4+row] += a.m[k*4+row] * b.m[col*4+k];
    return r;
}

static Mat4 mat4RotateY(float angle) {
    Mat4 r = mat4Identity();
    float c = cosf(angle), s = sinf(angle);
    r.m[0] = c;   r.m[8] = s;
    r.m[2] = -s;  r.m[10] = c;
    return r;
}

static Mat4 mat4RotateX(float angle) {
    Mat4 r = mat4Identity();
    float c = cosf(angle), s = sinf(angle);
    r.m[5] = c;   r.m[9] = -s;
    r.m[6] = s;   r.m[10] = c;
    return r;
}

static Mat4 mat4Perspective(float fov, float aspect, float near, float far) {
    Mat4 r = {};
    float f = 1.0f / tanf(fov * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (far + near) / (near - far);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * far * near) / (near - far);
    return r;
}

static Mat4 mat4Translate(float x, float y, float z) {
    Mat4 r = mat4Identity();
    r.m[12] = x; r.m[13] = y; r.m[14] = z;
    return r;
}

static void savePPM(const char *filename, unsigned char *pixels, int w, int h) {
    FILE *f = fopen(filename, "wb");
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--)
        for (int x = 0; x < w; x++) {
            unsigned char *p = &pixels[(y * w + x) * 4];
            fwrite(p, 1, 3, f);
        }
    fclose(f);
}

// Cube: 8 vertices with position + color
static float cubeVerts[] = {
    // pos x,y,z          color r,g,b
    -0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,  // 0 red
     0.5f, -0.5f, -0.5f,  0.0f, 1.0f, 0.0f,  // 1 green
     0.5f,  0.5f, -0.5f,  0.0f, 0.0f, 1.0f,  // 2 blue
    -0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 0.0f,  // 3 yellow
    -0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 1.0f,  // 4 magenta
     0.5f, -0.5f,  0.5f,  0.0f, 1.0f, 1.0f,  // 5 cyan
     0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 1.0f,  // 6 white
    -0.5f,  0.5f,  0.5f,  0.5f, 0.5f, 0.5f,  // 7 gray
};

static unsigned short cubeIndices[] = {
    4, 5, 6,  4, 6, 7,  // front
    1, 0, 3,  1, 3, 2,  // back
    0, 4, 7,  0, 7, 3,  // left
    5, 1, 2,  5, 2, 6,  // right
    7, 6, 2,  7, 2, 3,  // top
    0, 1, 5,  0, 5, 4,  // bottom
};

int main() {
    fprintf(stderr, "=== ANGLE WebGPU 3D Cube Rendering ===\n\n");

    EGLAttrib displayAttribs[] = {
        EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_WEBGPU_ANGLE,
        EGL_NONE
    };
    EGLDisplay display = eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, displayAttribs);
    EGLint major, minor;
    eglInitialize(display, &major, &minor);

    EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_NONE
    };
    EGLConfig config;
    EGLint numConfigs;
    eglChooseConfig(display, configAttribs, &config, 1, &numConfigs);
    fprintf(stderr, "EGL configs with depth: %d\n", numConfigs);

    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
    fprintf(stderr, "GL_RENDERER: %s\n", glGetString(GL_RENDERER));

    const int W = 512, H = 512;
    GLuint fbo, colorRbo, depthRbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenRenderbuffers(1, &colorRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, colorRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA4, W, H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorRbo);

    glGenRenderbuffers(1, &depthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, W, H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "FBO incomplete\n");
        return 1;
    }
    fprintf(stderr, "FBO complete (color + depth, %dx%d)\n", W, H);

    GLuint vs = compileShader(GL_VERTEX_SHADER, kVertSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragSrc);
    if (!vs || !fs) return 1;

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "aPos");
    glBindAttribLocation(prog, 1, "aColor");
    glLinkProgram(prog);
    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        fprintf(stderr, "Link error: %s\n", log);
        return 1;
    }
    glUseProgram(prog);
    fprintf(stderr, "Shader linked OK\n");

    GLint mvpLoc = glGetUniformLocation(prog, "uMVP");

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), cubeVerts);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), cubeVerts + 3);
    glEnableVertexAttribArray(1);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glViewport(0, 0, W, H);

    unsigned char *pixels = new unsigned char[W * H * 4];

    float angles[] = {0.0f, 0.7f, 1.4f, 2.1f};
    for (int frame = 0; frame < 4; frame++) {
        float angle = angles[frame];
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        Mat4 proj = mat4Perspective(1.0f, 1.0f, 0.1f, 100.0f);
        Mat4 view = mat4Translate(0.0f, 0.0f, -2.5f);
        Mat4 model = mat4Multiply(mat4RotateY(angle), mat4RotateX(angle * 0.7f));
        Mat4 mvp = mat4Multiply(proj, mat4Multiply(view, model));

        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp.m);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, cubeIndices);
        CHECK_GL("drawElements");

        glFinish();
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

        char filename[64];
        snprintf(filename, sizeof(filename), "cube_frame_%d.ppm", frame);
        savePPM(filename, pixels, W, H);
        fprintf(stderr, "Frame %d (angle=%.1f): saved %s\n", frame, angle, filename);
    }

    fprintf(stderr, "\n[PASS] 3D cube rendered successfully\n");
    fprintf(stderr, "Features: depth test, mat4 MVP, drawElements, 2 vertex attribs\n");

    delete[] pixels;
    eglTerminate(display);
    return 0;
}
