// pixel_compare.cpp — Pixel-level comparison: ANGLE Vulkan backend vs WebGPU backend
// Same GL code, same GPU, different translation path. Pixel match = correct translation.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

#define EGL_PLATFORM_ANGLE_ANGLE          0x3202
#define EGL_PLATFORM_ANGLE_TYPE_ANGLE     0x3203
#define EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE 0x3450
#define EGL_PLATFORM_ANGLE_TYPE_WEBGPU_ANGLE 0x34DD

static const int W = 256, H = 256;

// ---- Shaders ----
static const char* kVS_flat = R"(
attribute vec2 aPos;
attribute vec3 aColor;
varying vec3 vColor;
void main(){
    gl_Position = vec4(aPos, 0.0, 1.0);
    vColor = aColor;
}
)";

static const char* kFS_flat = R"(
precision mediump float;
varying vec3 vColor;
void main(){
    gl_FragColor = vec4(vColor, 1.0);
}
)";

static const char* kVS_3d = R"(
attribute vec3 aPos;
attribute vec3 aColor;
uniform mat4 uMVP;
varying vec3 vColor;
void main(){
    gl_Position = uMVP * vec4(aPos, 1.0);
    vColor = aColor;
}
)";

static const char* kFS_3d = R"(
precision mediump float;
varying vec3 vColor;
void main(){
    gl_FragColor = vec4(vColor, 1.0);
}
)";

// ---- Matrix helpers ----
static void mat4_identity(float* m){ memset(m,0,64); m[0]=m[5]=m[10]=m[15]=1; }
static void mat4_multiply(float* out, const float* a, const float* b){
    float t[16];
    for(int i=0;i<4;i++) for(int j=0;j<4;j++){
        t[j*4+i]=0;
        for(int k=0;k<4;k++) t[j*4+i]+=a[k*4+i]*b[j*4+k];
    }
    memcpy(out,t,64);
}
static void mat4_rotateY(float* m, float a){
    mat4_identity(m); m[0]=cosf(a); m[2]=sinf(a); m[8]=-sinf(a); m[10]=cosf(a);
}
static void mat4_rotateX(float* m, float a){
    mat4_identity(m); m[5]=cosf(a); m[6]=-sinf(a); m[9]=sinf(a); m[10]=cosf(a);
}
static void mat4_perspective(float* m, float fov, float aspect, float near, float far){
    memset(m,0,64);
    float f=1.0f/tanf(fov*0.5f);
    m[0]=f/aspect; m[5]=f; m[10]=(far+near)/(near-far); m[11]=-1; m[14]=2*far*near/(near-far);
}
static void mat4_translate(float* m, float x, float y, float z){
    mat4_identity(m); m[12]=x; m[13]=y; m[14]=z;
}

// ---- GL helpers ----
static GLuint compileShader(GLenum type, const char* src){
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if(!ok){ char buf[512]; glGetShaderInfoLog(s,512,0,buf); printf("  Shader error: %s\n",buf); }
    return s;
}

static GLuint makeProgram(const char* vs, const char* fs){
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glBindAttribLocation(p, 0, "aPos");
    glBindAttribLocation(p, 1, "aColor");
    glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if(!ok){ char buf[512]; glGetProgramInfoLog(p,512,0,buf); printf("  Link error: %s\n",buf); }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

// ---- EGL setup for a specific backend ----
struct GLContext {
    EGLDisplay dpy;
    EGLContext ctx;
    EGLSurface srf;
    GLuint fb, rb, db;
};

static bool initBackend(GLContext& c, EGLint backendType, const char* name) {
    printf("  Initializing %s backend...\n", name);

    PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (!eglGetPlatformDisplayEXT) {
        printf("  ERROR: eglGetPlatformDisplayEXT not available\n");
        return false;
    }

    EGLint displayAttrs[] = {
        EGL_PLATFORM_ANGLE_TYPE_ANGLE, backendType,
        EGL_NONE
    };
    c.dpy = eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, displayAttrs);
    if (c.dpy == EGL_NO_DISPLAY) {
        printf("  ERROR: eglGetPlatformDisplayEXT failed\n");
        return false;
    }

    if (!eglInitialize(c.dpy, nullptr, nullptr)) {
        printf("  ERROR: eglInitialize failed: 0x%x\n", eglGetError());
        return false;
    }

    eglBindAPI(EGL_OPENGL_ES_API);

    EGLint cfgAttrs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg; EGLint ncfg;
    if (!eglChooseConfig(c.dpy, cfgAttrs, &cfg, 1, &ncfg) || ncfg == 0) {
        printf("  ERROR: eglChooseConfig failed\n");
        return false;
    }

    EGLint ctxAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    c.ctx = eglCreateContext(c.dpy, cfg, EGL_NO_CONTEXT, ctxAttrs);
    if (c.ctx == EGL_NO_CONTEXT) {
        printf("  ERROR: eglCreateContext failed: 0x%x\n", eglGetError());
        return false;
    }

    EGLint pbAttrs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    c.srf = eglCreatePbufferSurface(c.dpy, cfg, pbAttrs);

    eglMakeCurrent(c.dpy, c.srf, c.srf, c.ctx);
    printf("  Renderer: %s\n", glGetString(GL_RENDERER));

    // Create FBO for rendering
    glGenFramebuffers(1, &c.fb);
    glGenRenderbuffers(1, &c.rb);
    glGenRenderbuffers(1, &c.db);
    glBindRenderbuffer(GL_RENDERBUFFER, c.rb);
    glRenderbufferStorage(GL_RENDERBUFFER, 0x8058 /*GL_RGBA8*/, W, H);
    glBindRenderbuffer(GL_RENDERBUFFER, c.db);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, W, H);
    glBindFramebuffer(GL_FRAMEBUFFER, c.fb);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, c.rb);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, c.db);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        printf("  ERROR: FBO incomplete: 0x%x\n", status);
        return false;
    }

    glViewport(0, 0, W, H);
    return true;
}

static void destroyBackend(GLContext& c) {
    eglMakeCurrent(c.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(c.dpy, c.ctx);
    if (c.srf != EGL_NO_SURFACE) eglDestroySurface(c.dpy, c.srf);
    eglTerminate(c.dpy);
}

static void activate(GLContext& c) {
    eglMakeCurrent(c.dpy, c.srf, c.srf, c.ctx);
    glBindFramebuffer(GL_FRAMEBUFFER, c.fb);
    glViewport(0, 0, W, H);
}

static void readPixels(std::vector<uint8_t>& pixels) {
    pixels.resize(W * H * 4);
    glFinish();
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
}

// ---- Test scenes ----
static void renderTriangle() {
    glClearColor(0.0f, 0.0f, 0.5f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    float verts[] = {
        // x, y,   r, g, b
         0.0f,  0.8f,  1.0f, 0.0f, 0.0f,
        -0.8f, -0.8f,  0.0f, 1.0f, 0.0f,
         0.8f, -0.8f,  0.0f, 0.0f, 1.0f,
    };

    GLuint prog = makeProgram(kVS_flat, kFS_flat);
    glUseProgram(prog);

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 20, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 20, (void*)8);

    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish();

    glDeleteBuffers(1, &vbo);
    glDeleteProgram(prog);
}

static void render3DCube() {
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    float cubeVerts[] = {
        // front face (z=1) — red
        -1,-1, 1,  1,0,0,   1,-1, 1,  1,0,0,   1, 1, 1,  1,0,0,
        -1,-1, 1,  1,0,0,   1, 1, 1,  1,0,0,  -1, 1, 1,  1,0,0,
        // back face (z=-1) — green
        -1,-1,-1,  0,1,0,  -1, 1,-1,  0,1,0,   1, 1,-1,  0,1,0,
        -1,-1,-1,  0,1,0,   1, 1,-1,  0,1,0,   1,-1,-1,  0,1,0,
        // top face (y=1) — blue
        -1, 1,-1,  0,0,1,  -1, 1, 1,  0,0,1,   1, 1, 1,  0,0,1,
        -1, 1,-1,  0,0,1,   1, 1, 1,  0,0,1,   1, 1,-1,  0,0,1,
        // bottom face (y=-1) — yellow
        -1,-1,-1,  1,1,0,   1,-1,-1,  1,1,0,   1,-1, 1,  1,1,0,
        -1,-1,-1,  1,1,0,   1,-1, 1,  1,1,0,  -1,-1, 1,  1,1,0,
        // right face (x=1) — magenta
         1,-1,-1,  1,0,1,   1, 1,-1,  1,0,1,   1, 1, 1,  1,0,1,
         1,-1,-1,  1,0,1,   1, 1, 1,  1,0,1,   1,-1, 1,  1,0,1,
        // left face (x=-1) — cyan
        -1,-1,-1,  0,1,1,  -1,-1, 1,  0,1,1,  -1, 1, 1,  0,1,1,
        -1,-1,-1,  0,1,1,  -1, 1, 1,  0,1,1,  -1, 1,-1,  0,1,1,
    };

    GLuint prog = makeProgram(kVS_3d, kFS_3d);
    glUseProgram(prog);
    GLint uMVP = glGetUniformLocation(prog, "uMVP");

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVerts), cubeVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)12);

    float proj[16], view[16], ry[16], rx[16], model[16], tmp[16], mvp[16];
    mat4_perspective(proj, 1.0f, 1.0f, 0.1f, 100.0f);
    mat4_translate(view, 0, 0, -5);
    mat4_rotateY(ry, 0.7f);
    mat4_rotateX(rx, 0.5f);
    mat4_multiply(model, ry, rx);
    mat4_multiply(tmp, view, model);
    mat4_multiply(mvp, proj, tmp);

    glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glFinish();

    glDeleteBuffers(1, &vbo);
    glDeleteProgram(prog);
}

// ---- Comparison ----
struct CompareResult {
    int totalPixels;
    int exactMatch;
    int closeMatch;   // within tolerance
    int mismatch;
    int maxDiff;
};

static CompareResult comparePixels(const std::vector<uint8_t>& a,
                                   const std::vector<uint8_t>& b,
                                   int tolerance = 1) {
    CompareResult r = {};
    r.totalPixels = W * H;
    for (int i = 0; i < W * H * 4; i += 4) {
        int dr = abs((int)a[i] - (int)b[i]);
        int dg = abs((int)a[i+1] - (int)b[i+1]);
        int db = abs((int)a[i+2] - (int)b[i+2]);
        int da = abs((int)a[i+3] - (int)b[i+3]);
        int maxc = std::max({dr, dg, db, da});
        r.maxDiff = std::max(r.maxDiff, maxc);
        if (maxc == 0) r.exactMatch++;
        else if (maxc <= tolerance) r.closeMatch++;
        else r.mismatch++;
    }
    return r;
}

static void writePPM(const char* filename, const std::vector<uint8_t>& pixels) {
    FILE* f = fopen(filename, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    // Flip vertically (GL origin is bottom-left)
    for (int y = H - 1; y >= 0; y--) {
        for (int x = 0; x < W; x++) {
            int idx = (y * W + x) * 4;
            fputc(pixels[idx], f);
            fputc(pixels[idx+1], f);
            fputc(pixels[idx+2], f);
        }
    }
    fclose(f);
}

static void writeDiffPPM(const char* filename,
                         const std::vector<uint8_t>& a,
                         const std::vector<uint8_t>& b) {
    FILE* f = fopen(filename, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = H - 1; y >= 0; y--) {
        for (int x = 0; x < W; x++) {
            int idx = (y * W + x) * 4;
            int dr = abs((int)a[idx] - (int)b[idx]);
            int dg = abs((int)a[idx+1] - (int)b[idx+1]);
            int db = abs((int)a[idx+2] - (int)b[idx+2]);
            // Amplify differences for visibility (10x)
            fputc(std::min(255, dr * 10), f);
            fputc(std::min(255, dg * 10), f);
            fputc(std::min(255, db * 10), f);
        }
    }
    fclose(f);
}

// ---- Main ----
int main() {
    printf("=== Pixel-Level Comparison: ANGLE Vulkan vs WebGPU Backend ===\n");
    printf("Resolution: %dx%d, RGBA8, FBO offscreen rendering\n\n", W, H);

    GLContext vkCtx, wgpuCtx;

    // Initialize both backends
    printf("[1/4] Setting up Vulkan backend (ground truth):\n");
    if (!initBackend(vkCtx, EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE, "Vulkan")) {
        printf("FATAL: Cannot initialize Vulkan backend\n");
        return 1;
    }

    printf("\n[2/4] Setting up WebGPU backend (our implementation):\n");
    if (!initBackend(wgpuCtx, EGL_PLATFORM_ANGLE_TYPE_WEBGPU_ANGLE, "WebGPU")) {
        printf("FATAL: Cannot initialize WebGPU backend\n");
        return 1;
    }

    std::vector<uint8_t> vkPixels, wgpuPixels;

    // ---- Test 1: Colored Triangle ----
    printf("\n========================================\n");
    printf("Test 1: Colored Triangle (2D, no depth)\n");
    printf("========================================\n");

    activate(vkCtx);
    renderTriangle();
    readPixels(vkPixels);

    activate(wgpuCtx);
    renderTriangle();
    readPixels(wgpuPixels);

    writePPM("/root/autodl-tmp/triangle_vulkan.ppm", vkPixels);
    writePPM("/root/autodl-tmp/triangle_webgpu.ppm", wgpuPixels);

    CompareResult r1 = comparePixels(vkPixels, wgpuPixels);
    printf("  Total pixels:  %d\n", r1.totalPixels);
    printf("  Exact match:   %d (%.1f%%)\n", r1.exactMatch, r1.exactMatch*100.0/r1.totalPixels);
    printf("  Close (+-1):   %d (%.1f%%)\n", r1.closeMatch, r1.closeMatch*100.0/r1.totalPixels);
    printf("  Mismatch:      %d (%.1f%%)\n", r1.mismatch, r1.mismatch*100.0/r1.totalPixels);
    printf("  Max channel diff: %d\n", r1.maxDiff);
    if (r1.mismatch > 0)
        writeDiffPPM("/root/autodl-tmp/triangle_diff.ppm", vkPixels, wgpuPixels);
    printf("  Result: %s\n", (r1.mismatch == 0) ? "PASS" : (r1.mismatch < r1.totalPixels/100 ? "PASS (minor diffs)" : "FAIL"));

    // ---- Test 2: 3D Cube with Depth Test ----
    printf("\n========================================\n");
    printf("Test 2: 3D Cube (depth test, MVP uniform)\n");
    printf("========================================\n");

    activate(vkCtx);
    render3DCube();
    readPixels(vkPixels);

    activate(wgpuCtx);
    render3DCube();
    readPixels(wgpuPixels);

    writePPM("/root/autodl-tmp/cube_vulkan.ppm", vkPixels);
    writePPM("/root/autodl-tmp/cube_webgpu.ppm", wgpuPixels);

    CompareResult r2 = comparePixels(vkPixels, wgpuPixels);
    printf("  Total pixels:  %d\n", r2.totalPixels);
    printf("  Exact match:   %d (%.1f%%)\n", r2.exactMatch, r2.exactMatch*100.0/r2.totalPixels);
    printf("  Close (+-1):   %d (%.1f%%)\n", r2.closeMatch, r2.closeMatch*100.0/r2.totalPixels);
    printf("  Mismatch:      %d (%.1f%%)\n", r2.mismatch, r2.mismatch*100.0/r2.totalPixels);
    printf("  Max channel diff: %d\n", r2.maxDiff);
    if (r2.mismatch > 0)
        writeDiffPPM("/root/autodl-tmp/cube_diff.ppm", vkPixels, wgpuPixels);
    printf("  Result: %s\n", (r2.mismatch == 0) ? "PASS" : (r2.mismatch < r2.totalPixels/100 ? "PASS (minor diffs)" : "FAIL"));

    // ---- Summary ----
    printf("\n========================================\n");
    printf("Summary\n");
    printf("========================================\n");
    printf("  Test 1 (Triangle): %s  (exact=%.1f%%, close=%.1f%%, mismatch=%.1f%%)\n",
           (r1.mismatch == 0) ? "PASS" : "DIFF",
           r1.exactMatch*100.0/r1.totalPixels,
           r1.closeMatch*100.0/r1.totalPixels,
           r1.mismatch*100.0/r1.totalPixels);
    printf("  Test 2 (3D Cube):  %s  (exact=%.1f%%, close=%.1f%%, mismatch=%.1f%%)\n",
           (r2.mismatch == 0) ? "PASS" : "DIFF",
           r2.exactMatch*100.0/r2.totalPixels,
           r2.closeMatch*100.0/r2.totalPixels,
           r2.mismatch*100.0/r2.totalPixels);

    int totalMatch = r1.exactMatch + r1.closeMatch + r2.exactMatch + r2.closeMatch;
    int totalAll = r1.totalPixels + r2.totalPixels;
    printf("\n  Overall pixel match rate: %.2f%% (%d/%d pixels)\n",
           totalMatch*100.0/totalAll, totalMatch, totalAll);
    printf("  Ground truth: ANGLE Vulkan backend\n");
    printf("  Under test:   ANGLE WebGPU backend\n");
    printf("  Images saved to /root/autodl-tmp/*.ppm\n");

    // Cleanup
    activate(vkCtx);
    destroyBackend(vkCtx);
    activate(wgpuCtx);
    destroyBackend(wgpuCtx);

    return 0;
}
