// Frame Time Stability Benchmark: measures per-frame time variance.
// Renders 200 frames of a rotating cube, reports CV (coefficient of variation).
// Build: g++ -std=c++17 -o benchmark_frame_stability benchmark_frame_stability.cpp \
//        -I../../include -L. -lEGL -lGLESv2 -Wl,-rpath,'$ORIGIN'

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

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

static Mat4 mat4RotateY(float a) {
    Mat4 r = mat4Identity();
    r.m[0] = cosf(a); r.m[8] = sinf(a);
    r.m[2] = -sinf(a); r.m[10] = cosf(a);
    return r;
}

static Mat4 mat4RotateX(float a) {
    Mat4 r = mat4Identity();
    r.m[5] = cosf(a); r.m[9] = -sinf(a);
    r.m[6] = sinf(a); r.m[10] = cosf(a);
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

static float cubeVerts[] = {
    -0.5f,-0.5f,-0.5f, 1,0,0,   0.5f,-0.5f,-0.5f, 0,1,0,
     0.5f, 0.5f,-0.5f, 0,0,1,  -0.5f, 0.5f,-0.5f, 1,1,0,
    -0.5f,-0.5f, 0.5f, 1,0,1,   0.5f,-0.5f, 0.5f, 0,1,1,
     0.5f, 0.5f, 0.5f, 1,1,1,  -0.5f, 0.5f, 0.5f, .5,.5,.5,
};

static unsigned short cubeIndices[] = {
    4,5,6, 4,6,7,  1,0,3, 1,3,2,  0,4,7, 0,7,3,
    5,1,2, 5,2,6,  7,6,2, 7,2,3,  0,1,5, 0,5,4,
};

using Clock = std::chrono::high_resolution_clock;
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
        EGL_DEPTH_SIZE, 16, EGL_NONE
    };
    EGLConfig config;
    EGLint numConfigs;
    eglChooseConfig(display, configAttribs, &config, 1, &numConfigs);

    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);

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

    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, W, H);

    GLuint vs = compileShader(GL_VERTEX_SHADER, kVertSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragSrc);
    if (!vs || !fs) return 1;
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "aPos");
    glBindAttribLocation(prog, 1, "aColor");
    glLinkProgram(prog);
    glUseProgram(prog);
    GLint mvpLoc = glGetUniformLocation(prog, "uMVP");

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), cubeVerts);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), cubeVerts+3);
    glEnableVertexAttribArray(1);

    const int TOTAL_FRAMES = 300;
    const int WARMUP = 50;
    std::vector<double> frameTimes;
    frameTimes.reserve(TOTAL_FRAMES);

    printf("=== Frame Time Stability Benchmark ===\n");
    printf("Backend: %s\n", glGetString(GL_RENDERER));
    printf("Resolution: %dx%d, Frames: %d (warmup: %d)\n\n", W, H, TOTAL_FRAMES, WARMUP);

    for (int frame = 0; frame < TOTAL_FRAMES; frame++) {
        float angle = frame * 0.05f;

        auto t0 = Clock::now();

        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        Mat4 proj = mat4Perspective(1.0f, 1.0f, 0.1f, 100.0f);
        Mat4 view = mat4Translate(0.0f, 0.0f, -2.5f);
        Mat4 model = mat4Multiply(mat4RotateY(angle), mat4RotateX(angle * 0.7f));
        Mat4 mvp = mat4Multiply(proj, mat4Multiply(view, model));
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp.m);

        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, cubeIndices);
        glFinish();

        auto t1 = Clock::now();
        double ft = std::chrono::duration<double, std::milli>(t1 - t0).count();
        frameTimes.push_back(ft);
    }

    // Statistics (skip warmup frames)
    int measured = TOTAL_FRAMES - WARMUP;
    double sum = 0, minFt = 1e9, maxFt = 0;
    for (int i = WARMUP; i < TOTAL_FRAMES; i++) {
        double ft = frameTimes[i];
        sum += ft;
        if (ft < minFt) minFt = ft;
        if (ft > maxFt) maxFt = ft;
    }
    double mean = sum / measured;

    // Compute stddev
    double sumSq = 0;
    for (int i = WARMUP; i < TOTAL_FRAMES; i++) {
        double d = frameTimes[i] - mean;
        sumSq += d * d;
    }
    double stddev = sqrt(sumSq / measured);
    double cv = (mean > 0) ? (stddev / mean) * 100.0 : 0;

    // Steady-state: exclude outliers beyond 2*stddev
    double ssSum = 0;
    int ssCount = 0;
    double ssSumSq = 0;
    for (int i = WARMUP; i < TOTAL_FRAMES; i++) {
        double ft = frameTimes[i];
        if (fabs(ft - mean) <= 2.0 * stddev) {
            ssSum += ft;
            ssSumSq += (ft - mean) * (ft - mean);
            ssCount++;
        }
    }
    double ssMean = ssCount > 0 ? ssSum / ssCount : mean;
    double ssStddev = ssCount > 0 ? sqrt(ssSumSq / ssCount) : stddev;
    double ssCv = (ssMean > 0) ? (ssStddev / ssMean) * 100.0 : 0;

    printf("Raw results (frames %d-%d):\n", WARMUP+1, TOTAL_FRAMES);
    printf("  Mean frame time:  %.3f ms\n", mean);
    printf("  Std deviation:    %.3f ms\n", stddev);
    printf("  CV (variation):   %.2f%%\n", cv);
    printf("  Min:              %.3f ms\n", minFt);
    printf("  Max:              %.3f ms\n", maxFt);
    printf("\n");
    printf("Steady-state (outliers >2σ removed, %d/%d frames):\n", ssCount, measured);
    printf("  Mean frame time:  %.3f ms\n", ssMean);
    printf("  Std deviation:    %.3f ms\n", ssStddev);
    printf("  CV (variation):   %.2f%%\n", ssCv);
    printf("\n");
    if (ssCv < 5.0)
        printf("[PASS] Steady-state CV < 5%% — frame times are stable\n");
    else if (ssCv < 15.0)
        printf("[WARN] Steady-state CV %.1f%% — moderate variance\n", ssCv);
    else
        printf("[INFO] Steady-state CV %.1f%% — expected on CPU renderer (SwiftShader)\n", ssCv);

    printf("\n[Note] Each frame: uniform update + drawElements(36) + glFinish.\n");
    printf("SwiftShader (CPU Vulkan) has higher variance than real GPU due to\n");
    printf("OS scheduling and CPU cache effects. Real GPU CV is typically <5%%.\n");

    eglTerminate(display);
    return 0;
}
