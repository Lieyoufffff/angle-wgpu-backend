// Pipeline Cache Benchmark: cold start vs hot (cached) pipeline creation.
// Measures the cost of first draw (shader compile + pipeline create) vs subsequent draws.
// Build: g++ -std=c++17 -o benchmark_pipeline_cache benchmark_pipeline_cache.cpp \
//        -I../../include -L. -lEGL -lGLESv2 -Wl,-rpath,'$ORIGIN'

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <chrono>
#include <cstdio>
#include <cstring>

static const char *kVertSrc =
    "attribute vec2 aPos;\n"
    "void main() {\n"
    "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";

static const char *kFragSrc1 =
    "precision mediump float;\n"
    "void main() {\n"
    "  gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
    "}\n";

static const char *kFragSrc2 =
    "precision mediump float;\n"
    "uniform float uTime;\n"
    "void main() {\n"
    "  gl_FragColor = vec4(0.0, uTime, 1.0, 1.0);\n"
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
        fprintf(stderr, "Shader error: %s\n", log);
        return 0;
    }
    return s;
}

static GLuint createProgram(const char *fragSrc) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, kVertSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragSrc);
    if (!vs || !fs) return 0;
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "aPos");
    glLinkProgram(prog);
    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        fprintf(stderr, "Link error: %s\n", log);
        return 0;
    }
    return prog;
}

using Clock = std::chrono::high_resolution_clock;

static double ms(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

int main() {
    EGLAttrib displayAttribs[] = {
        EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_WEBGPU_ANGLE,
        EGL_NONE
    };
    EGLDisplay display = eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE,
        EGL_DEFAULT_DISPLAY, displayAttribs);
    EGLint major, minor;
    if (!eglInitialize(display, &major, &minor)) {
        fprintf(stderr, "eglInitialize failed\n");
        return 1;
    }

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

    GLuint fbo, rbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA4, 64, 64);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);
    glViewport(0, 0, 64, 64);

    float verts[] = { 0.0f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f };
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
    glEnableVertexAttribArray(0);

    printf("=== Pipeline Cache Benchmark ===\n");
    printf("Backend: %s\n", glGetString(GL_RENDERER));
    printf("\n");

    // --- Program 1: cold start ---
    GLuint prog1 = createProgram(kFragSrc1);
    if (!prog1) return 1;
    glUseProgram(prog1);

    glClear(GL_COLOR_BUFFER_BIT);

    auto t0 = Clock::now();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish();
    auto t1 = Clock::now();
    double coldStart1 = ms(t0, t1);

    // --- Program 1: hot runs (cache hit) ---
    const int HOT_RUNS = 100;
    auto t2 = Clock::now();
    for (int i = 0; i < HOT_RUNS; i++) {
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFinish();
    }
    auto t3 = Clock::now();
    double hotAvg = ms(t2, t3) / HOT_RUNS;

    // --- Program 2: different shader, new cold start ---
    GLuint prog2 = createProgram(kFragSrc2);
    if (!prog2) return 1;
    glUseProgram(prog2);
    GLint timeLoc = glGetUniformLocation(prog2, "uTime");
    glUniform1f(timeLoc, 0.5f);

    auto t4 = Clock::now();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish();
    auto t5 = Clock::now();
    double coldStart2 = ms(t4, t5);

    // --- Output ---
    printf("%-18s %-12s %s\n", "Scenario", "Time(ms)", "Notes");
    printf("-----------------------------------------------\n");
    printf("%-18s %-12.3f %s\n", "Cold start #1", coldStart1,
           "shader compile + pipeline create");
    printf("%-18s %-12.3f %s\n", "Hot (avg 100)", hotAvg, "pipeline cache hit");
    printf("%-18s %-12.3f %s\n", "Cold start #2", coldStart2,
           "different shader, new pipeline");
    printf("\n");
    printf("Cache speedup: %.1fx (cold1/hot)\n", coldStart1 / hotAvg);
    printf("\n[Result] First draw triggers full GLSL->SPIRV->WGSL compilation\n");
    printf("and WebGPU render pipeline creation. Subsequent draws with the\n");
    printf("same state hit the PipelineCache (unordered_map lookup).\n");

    eglTerminate(display);
    return 0;
}
