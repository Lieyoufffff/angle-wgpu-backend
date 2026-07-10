// Real-time rotating cube with X11 window + EGL window surface.
// Demonstrates ANGLE WebGPU backend with live rendering.
// Build: g++ -std=c++17 -o test_realtime_cube test_realtime_cube.cpp \
//        -Iinclude -Lout/Release -lEGL -lGLESv2 -lX11 -Wl,-rpath,'$ORIGIN'
// Run:   LD_LIBRARY_PATH=out/Release ./test_realtime_cube

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <unistd.h>

// --- Shaders ---
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

// --- Math ---
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

// Cube geometry
static float cubeVerts[] = {
    -0.5f,-0.5f,-0.5f, 1.0f,0.0f,0.0f,
     0.5f,-0.5f,-0.5f, 0.0f,1.0f,0.0f,
     0.5f, 0.5f,-0.5f, 0.0f,0.0f,1.0f,
    -0.5f, 0.5f,-0.5f, 1.0f,1.0f,0.0f,
    -0.5f,-0.5f, 0.5f, 1.0f,0.0f,1.0f,
     0.5f,-0.5f, 0.5f, 0.0f,1.0f,1.0f,
     0.5f, 0.5f, 0.5f, 1.0f,1.0f,1.0f,
    -0.5f, 0.5f, 0.5f, 0.5f,0.5f,0.5f,
};

static unsigned short cubeIndices[] = {
    4,5,6, 4,6,7,
    1,0,3, 1,3,2,
    0,4,7, 0,7,3,
    5,1,2, 5,2,6,
    7,6,2, 7,2,3,
    0,1,5, 0,5,4,
};

int main() {
    const int W = 800, H = 600;

    // --- X11 Window ---
    Display *xdpy = XOpenDisplay(nullptr);
    if (!xdpy) {
        fprintf(stderr, "Cannot open X display\n");
        return 1;
    }

    Window root = DefaultRootWindow(xdpy);
    XSetWindowAttributes swa = {};
    swa.event_mask = ExposureMask | KeyPressMask | StructureNotifyMask;
    Window win = XCreateWindow(xdpy, root, 0, 0, W, H, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWEventMask, &swa);
    XStoreName(xdpy, win, "ANGLE WebGPU - Rotating Cube");
    XMapWindow(xdpy, win);

    // Wait for window to appear
    XEvent xev;
    while (1) {
        XNextEvent(xdpy, &xev);
        if (xev.type == MapNotify) break;
    }

    // --- EGL Setup (WebGPU backend) ---
    EGLAttrib displayAttribs[] = {
        EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_WEBGPU_ANGLE,
        EGL_NONE
    };
    EGLDisplay edpy = eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE,
        (void*)xdpy, displayAttribs);
    if (edpy == EGL_NO_DISPLAY) {
        fprintf(stderr, "eglGetPlatformDisplay failed\n");
        return 1;
    }

    EGLint major, minor;
    if (!eglInitialize(edpy, &major, &minor)) {
        fprintf(stderr, "eglInitialize failed: 0x%x\n", eglGetError());
        return 1;
    }
    fprintf(stderr, "EGL %d.%d initialized\n", major, minor);

    EGLint cfgAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_NONE
    };
    EGLConfig config;
    EGLint numCfg;
    eglChooseConfig(edpy, cfgAttribs, &config, 1, &numCfg);

    EGLSurface surface = eglCreateWindowSurface(edpy, config,
        (EGLNativeWindowType)win, nullptr);
    if (surface == EGL_NO_SURFACE) {
        fprintf(stderr, "eglCreateWindowSurface failed: 0x%x\n", eglGetError());
        return 1;
    }

    EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(edpy, config, EGL_NO_CONTEXT, ctxAttribs);
    eglMakeCurrent(edpy, surface, surface, ctx);

    fprintf(stderr, "GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    fprintf(stderr, "GL_VERSION:  %s\n", glGetString(GL_VERSION));

    // --- GL Setup ---
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

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glViewport(0, 0, W, H);

    fprintf(stderr, "Rendering... (press Escape or close window to quit)\n");

    // --- Render Loop ---
    float angle = 0.0f;
    bool running = true;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double lastTime = ts.tv_sec + ts.tv_nsec * 1e-9;
    int frameCount = 0;

    while (running) {
        // Handle X11 events (non-blocking)
        while (XPending(xdpy)) {
            XNextEvent(xdpy, &xev);
            if (xev.type == KeyPress) {
                KeySym key = XLookupKeysym(&xev.xkey, 0);
                if (key == XK_Escape || key == XK_q)
                    running = false;
            } else if (xev.type == DestroyNotify) {
                running = false;
            }
        }

        // Clear
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // MVP
        Mat4 proj = mat4Perspective(1.0f, (float)W/H, 0.1f, 100.0f);
        Mat4 view = mat4Translate(0.0f, 0.0f, -3.0f);
        Mat4 model = mat4Multiply(mat4RotateY(angle), mat4RotateX(angle*0.7f));
        Mat4 mvp = mat4Multiply(proj, mat4Multiply(view, model));

        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp.m);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, cubeIndices);

        eglSwapBuffers(edpy, surface);

        angle += 0.02f;
        frameCount++;

        // Print FPS every 2 seconds
        clock_gettime(CLOCK_MONOTONIC, &ts);
        double now = ts.tv_sec + ts.tv_nsec * 1e-9;
        if (now - lastTime >= 2.0) {
            fprintf(stderr, "FPS: %.1f\n", frameCount / (now - lastTime));
            frameCount = 0;
            lastTime = now;
        }
    }

    // Cleanup
    eglMakeCurrent(edpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(edpy, surface);
    eglDestroyContext(edpy, ctx);
    eglTerminate(edpy);
    XDestroyWindow(xdpy, win);
    XCloseDisplay(xdpy);

    fprintf(stderr, "Done.\n");
    return 0;
}
