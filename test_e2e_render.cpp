// Standalone end-to-end rendering test for ANGLE WebGPU backend.
// Renders a red triangle on blue background, saves result as PPM image.

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK_EGL(msg) do { \
    EGLint err = eglGetError(); \
    if (err != EGL_SUCCESS) { \
        fprintf(stderr, "EGL error 0x%x at %s\n", err, msg); \
        return 1; \
    } \
} while(0)

#define CHECK_GL(msg) do { \
    GLenum err = glGetError(); \
    if (err != GL_NO_ERROR) { \
        fprintf(stderr, "GL error 0x%x at %s\n", err, msg); \
        return 1; \
    } \
} while(0)

static const char *kVertSrc =
    "attribute vec2 aPos;\n"
    "void main() {\n"
    "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";

static const char *kFragSrc =
    "precision mediump float;\n"
    "uniform vec4 uColor;\n"
    "void main() {\n"
    "  gl_FragColor = uColor;\n"
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

static void savePPM(const char *filename, unsigned char *pixels, int w, int h) {
    FILE *f = fopen(filename, "wb");
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    // OpenGL pixels are bottom-up, flip vertically
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            unsigned char *p = &pixels[(y * w + x) * 4];
            fwrite(p, 1, 3, f);  // RGB only (skip A)
        }
    }
    fclose(f);
}

int main() {
    fprintf(stderr, "=== ANGLE WebGPU E2E Rendering Test ===\n\n");

    EGLAttrib displayAttribs[] = {
        EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_WEBGPU_ANGLE,
        EGL_NONE
    };
    EGLDisplay display = eglGetPlatformDisplay(
        EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, displayAttribs);
    if (display == EGL_NO_DISPLAY) { fprintf(stderr, "No display\n"); return 1; }

    EGLint major, minor;
    if (!eglInitialize(display, &major, &minor)) { CHECK_EGL("init"); return 1; }
    fprintf(stderr, "EGL %d.%d initialized\n", major, minor);

    EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig config;
    EGLint numConfigs;
    eglChooseConfig(display, configAttribs, &config, 1, &numConfigs);

    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
    fprintf(stderr, "GL_RENDERER: %s\n", glGetString(GL_RENDERER));

    // Create 256x256 FBO
    const int W = 256, H = 256;
    GLuint fbo, rbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA4, W, H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);

    // Render
    glViewport(0, 0, W, H);
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLuint vs = compileShader(GL_VERTEX_SHADER, kVertSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragSrc);
    if (!vs || !fs) return 1;

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "aPos");
    glLinkProgram(prog);
    glUseProgram(prog);

    GLint colorLoc = glGetUniformLocation(prog, "uColor");
    glUniform4f(colorLoc, 1.0f, 0.0f, 0.0f, 1.0f);

    float verts[] = {
        0.0f,  0.8f,
       -0.7f, -0.6f,
        0.7f, -0.6f,
    };
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    CHECK_GL("draw");

    // Read pixels and save
    glFinish();
    unsigned char *pixels = new unsigned char[W * H * 4];
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    CHECK_GL("readPixels");

    savePPM("render_output.ppm", pixels, W, H);
    fprintf(stderr, "\nSaved render_output.ppm (%dx%d)\n", W, H);

    // Verify
    unsigned char *center = &pixels[(H/2 * W + W/2) * 4];
    unsigned char *corner = &pixels[0];
    fprintf(stderr, "Center: R=%d G=%d B=%d\n", center[0], center[1], center[2]);
    fprintf(stderr, "Corner: R=%d G=%d B=%d\n", corner[0], corner[1], corner[2]);

    bool ok = (center[0] > 200 && center[2] < 50) && (corner[2] > 200 && corner[0] < 50);
    fprintf(stderr, ok ? "\n[PASS]\n" : "\n[FAIL]\n");

    delete[] pixels;
    eglTerminate(display);
    return ok ? 0 : 1;
}
