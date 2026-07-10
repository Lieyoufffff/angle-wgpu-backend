#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>

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
    if (!ok) { char log[512]; glGetShaderInfoLog(s, 512, nullptr, log); fprintf(stderr, "Shader: %s\n", log); return 0; }
    return s;
}

struct Mat4 { float m[16]; };
static Mat4 mat4Identity() { Mat4 r={}; r.m[0]=r.m[5]=r.m[10]=r.m[15]=1; return r; }

static void savePPM(const char *fn, unsigned char *px, int w, int h) {
    FILE *f = fopen(fn, "wb");
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h-1; y >= 0; y--)
        for (int x = 0; x < w; x++) { unsigned char *p = &px[(y*w+x)*4]; fwrite(p,1,3,f); }
    fclose(f);
}

int main() {
    fprintf(stderr, "=== Debug: drawElements test ===\n");

    EGLAttrib da[] = { EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_WEBGPU_ANGLE, EGL_NONE };
    EGLDisplay display = eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, da);
    EGLint maj, min; eglInitialize(display, &maj, &min);
    EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
    EGLConfig cfg; EGLint nc; eglChooseConfig(display, ca, &cfg, 1, &nc);
    EGLint cxa[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(display, cfg, EGL_NO_CONTEXT, cxa);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);

    const int W=128, H=128;
    GLuint fbo, rbo;
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenRenderbuffers(1, &rbo); glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA4, W, H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);

    GLuint vs = compileShader(GL_VERTEX_SHADER, kVertSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragSrc);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "aPos");
    glBindAttribLocation(prog, 1, "aColor");
    glLinkProgram(prog);
    GLint linked; glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) { char log[512]; glGetProgramInfoLog(prog, 512, nullptr, log); fprintf(stderr, "Link: %s\n", log); return 1; }
    glUseProgram(prog);

    GLint mvpLoc = glGetUniformLocation(prog, "uMVP");
    fprintf(stderr, "uMVP location: %d\n", mvpLoc);

    // Test 1: drawArrays with simple triangle (should work - we know this works)
    fprintf(stderr, "\n--- Test 1: drawArrays (simple triangle) ---\n");
    glViewport(0, 0, W, H);
    glClearColor(0, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    Mat4 id = mat4Identity();
    glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, id.m);

    float tri[] = {
        0.0f, 0.5f, 0.0f,   1, 0, 0,
       -0.5f,-0.5f, 0.0f,   0, 1, 0,
        0.5f,-0.5f, 0.0f,   0, 0, 1,
    };
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), tri);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), &tri[3]);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    GLenum err = glGetError();
    fprintf(stderr, "drawArrays error: 0x%x\n", err);

    glFinish();
    unsigned char px[128*128*4];
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
    unsigned char *c = &px[(64*128+64)*4];
    fprintf(stderr, "Center: R=%d G=%d B=%d\n", c[0], c[1], c[2]);

    savePPM("debug_drawArrays.ppm", px, W, H);

    // Test 2: drawElements with same triangle via indices
    fprintf(stderr, "\n--- Test 2: drawElements (indexed triangle) ---\n");
    glClearColor(0, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    unsigned short indices[] = {0, 1, 2};
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, indices);
    err = glGetError();
    fprintf(stderr, "drawElements error: 0x%x\n", err);

    glFinish();
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
    c = &px[(64*128+64)*4];
    fprintf(stderr, "Center: R=%d G=%d B=%d\n", c[0], c[1], c[2]);

    savePPM("debug_drawElements.ppm", px, W, H);

    // Test 3: drawElements with a quad (4 verts, 6 indices)
    fprintf(stderr, "\n--- Test 3: drawElements (quad, 6 indices) ---\n");
    glClearColor(0, 0, 0.1f, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    float quad[] = {
       -0.8f, -0.8f, 0.0f,  1, 0, 0,
        0.8f, -0.8f, 0.0f,  0, 1, 0,
        0.8f,  0.8f, 0.0f,  0, 0, 1,
       -0.8f,  0.8f, 0.0f,  1, 1, 0,
    };
    unsigned short quadIdx[] = {0, 1, 2, 0, 2, 3};
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), quad);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), &quad[3]);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, quadIdx);
    err = glGetError();
    fprintf(stderr, "drawElements(quad) error: 0x%x\n", err);

    glFinish();
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
    c = &px[(64*128+64)*4];
    fprintf(stderr, "Center: R=%d G=%d B=%d\n", c[0], c[1], c[2]);

    savePPM("debug_drawElements_quad.ppm", px, W, H);

    eglTerminate(display);
    return 0;
}
