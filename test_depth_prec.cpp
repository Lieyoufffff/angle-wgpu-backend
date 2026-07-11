// test_depth_prec.cpp
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

#define EGL_PLATFORM_ANGLE_ANGLE 0x3202
#define EGL_PLATFORM_ANGLE_TYPE_ANGLE 0x3203
#define EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE 0x3450
#define EGL_PLATFORM_ANGLE_TYPE_WEBGPU_ANGLE 0x34DD
static const int W=256,H=256;
static const char* kVS = "attribute vec3 aPos; attribute vec3 aColor; uniform mat4 uMVP; varying vec3 vColor;\nvoid main(){ gl_Position = uMVP * vec4(aPos, 1.0); vColor = aColor; }\n";
static const char* kFS = "precision mediump float; varying vec3 vColor;\nvoid main(){ gl_FragColor = vec4(vColor, 1.0); }\n";

void mat4_identity(float*m){memset(m,0,64);m[0]=m[5]=m[10]=m[15]=1;}
void mat4_multiply(float*o,const float*a,const float*b){float t[16];for(int i=0;i<4;i++)for(int j=0;j<4;j++){t[j*4+i]=0;for(int k=0;k<4;k++)t[j*4+i]+=a[k*4+i]*b[j*4+k];}memcpy(o,t,64);}
void mat4_perspective(float*m,float fov,float a,float n,float f){memset(m,0,64);float ff=1.0f/tanf(fov*0.5f);m[0]=ff/a;m[5]=ff;m[10]=(f+n)/(n-f);m[11]=-1;m[14]=2*f*n/(n-f);}
void mat4_translate(float*m,float x,float y,float z){mat4_identity(m);m[12]=x;m[13]=y;m[14]=z;}
void mat4_rotateY(float*m,float a){mat4_identity(m);m[0]=cosf(a);m[2]=sinf(a);m[8]=-sinf(a);m[10]=cosf(a);}
void mat4_rotateX(float*m,float a){mat4_identity(m);m[5]=cosf(a);m[6]=-sinf(a);m[9]=sinf(a);m[10]=cosf(a);}

float cubeVerts[] = {
    -1,-1, 1,  1,0,0,   1,-1, 1,  1,0,0,   1, 1, 1,  1,0,0,
    -1,-1, 1,  1,0,0,   1, 1, 1,  1,0,0,  -1, 1, 1,  1,0,0,
    -1,-1,-1,  0,1,0,  -1, 1,-1,  0,1,0,   1, 1,-1,  0,1,0,
    -1,-1,-1,  0,1,0,   1, 1,-1,  0,1,0,   1,-1,-1,  0,1,0,
    -1, 1,-1,  0,0,1,  -1, 1, 1,  0,0,1,   1, 1, 1,  0,0,1,
    -1, 1,-1,  0,0,1,   1, 1, 1,  0,0,1,   1, 1,-1,  0,0,1,
    -1,-1,-1,  1,1,0,   1,-1,-1,  1,1,0,   1,-1, 1,  1,1,0,
    -1,-1,-1,  1,1,0,   1,-1, 1,  1,1,0,  -1,-1, 1,  1,1,0,
     1,-1,-1,  1,0,1,   1, 1,-1,  1,0,1,   1, 1, 1,  1,0,1,
     1,-1,-1,  1,0,1,   1, 1, 1,  1,0,1,   1,-1, 1,  1,0,1,
    -1,-1,-1,  0,1,1,  -1,-1, 1,  0,1,1,  -1, 1, 1,  0,1,1,
    -1,-1,-1,  0,1,1,  -1, 1, 1,  0,1,1,  -1, 1,-1,  0,1,1,
};

struct Ctx{EGLDisplay d;EGLContext c;EGLSurface s;GLuint fb,rb,db;};
bool init(Ctx &x,EGLint type,GLenum depthFmt){
    auto fn=(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLint a[]={EGL_PLATFORM_ANGLE_TYPE_ANGLE,type,EGL_NONE};
    x.d=fn(EGL_PLATFORM_ANGLE_ANGLE,EGL_DEFAULT_DISPLAY,a);
    eglInitialize(x.d,0,0);eglBindAPI(EGL_OPENGL_ES_API);
    EGLint ca[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig cfg;EGLint n;eglChooseConfig(x.d,ca,&cfg,1,&n);
    EGLint cc[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    x.c=eglCreateContext(x.d,cfg,0,cc);
    EGLint pa[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
    x.s=eglCreatePbufferSurface(x.d,cfg,pa);
    eglMakeCurrent(x.d,x.s,x.s,x.c);
    glGenFramebuffers(1,&x.fb);glGenRenderbuffers(1,&x.rb);glGenRenderbuffers(1,&x.db);
    glBindRenderbuffer(GL_RENDERBUFFER,x.rb);glRenderbufferStorage(GL_RENDERBUFFER,0x8058,W,H);
    glBindRenderbuffer(GL_RENDERBUFFER,x.db);glRenderbufferStorage(GL_RENDERBUFFER,depthFmt,W,H);
    glBindFramebuffer(GL_FRAMEBUFFER,x.fb);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,x.rb);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,x.db);
    glViewport(0,0,W,H);
    return glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
}

void renderCube(Ctx &ctx, bool useDepth) {
    eglMakeCurrent(ctx.d,ctx.s,ctx.s,ctx.c);
    glBindFramebuffer(GL_FRAMEBUFFER,ctx.fb);glViewport(0,0,W,H);
    glClearColor(0.1f,0.1f,0.1f,1);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    if(useDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    GLuint vs=glCreateShader(GL_VERTEX_SHADER);glShaderSource(vs,1,&kVS,0);glCompileShader(vs);
    GLuint fs=glCreateShader(GL_FRAGMENT_SHADER);glShaderSource(fs,1,&kFS,0);glCompileShader(fs);
    GLuint p=glCreateProgram();glAttachShader(p,vs);glAttachShader(p,fs);
    glBindAttribLocation(p,0,"aPos");glBindAttribLocation(p,1,"aColor");
    glLinkProgram(p);glUseProgram(p);
    GLuint vbo;glGenBuffers(1,&vbo);
    glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(cubeVerts),cubeVerts,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,24,(void*)0);
    glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,24,(void*)12);
    GLint uMVP=glGetUniformLocation(p,"uMVP");
    float proj[16],view[16],ry[16],rx[16],model[16],tmp[16],mvp[16];
    mat4_perspective(proj,1.0f,1.0f,0.1f,100.0f);
    mat4_translate(view,0,0,-5);
    mat4_rotateY(ry,0.7f);mat4_rotateX(rx,0.5f);
    mat4_multiply(model,ry,rx);mat4_multiply(tmp,view,model);mat4_multiply(mvp,proj,tmp);
    glUniformMatrix4fv(uMVP,1,GL_FALSE,mvp);
    glDrawArrays(GL_TRIANGLES,0,36);
    glFinish();
}

int doCompare(Ctx &vk, Ctx &wg) {
    eglMakeCurrent(vk.d,vk.s,vk.s,vk.c);glBindFramebuffer(GL_FRAMEBUFFER,vk.fb);
    std::vector<uint8_t> pv(W*H*4); glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,pv.data());
    eglMakeCurrent(wg.d,wg.s,wg.s,wg.c);glBindFramebuffer(GL_FRAMEBUFFER,wg.fb);
    std::vector<uint8_t> pw(W*H*4); glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,pw.data());
    int exact=0,diff=0;
    for(int i=0;i<W*H*4;i+=4){int md=0;for(int c=0;c<4;c++){int d=abs((int)pv[i+c]-(int)pw[i+c]);if(d>md)md=d;}if(md==0)exact++;else diff++;}
    printf("  exact=%d(%.1f%%) diff=%d(%.1f%%)\n",exact,exact*100.0/(W*H),diff,diff*100.0/(W*H));
    return diff;
}

int main(){
    printf("=== Depth precision test ===\n\n");
    // DEPTH16
    {Ctx vk,wg;
    printf("DEPTH16 + depth test:\n");
    init(vk,EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE,GL_DEPTH_COMPONENT16);
    init(wg,EGL_PLATFORM_ANGLE_TYPE_WEBGPU_ANGLE,GL_DEPTH_COMPONENT16);
    renderCube(vk,true);renderCube(wg,true);doCompare(vk,wg);}

    // DEPTH24
    {Ctx vk,wg;
    printf("DEPTH24 + depth test:\n");
    init(vk,EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE,0x81A6);
    init(wg,EGL_PLATFORM_ANGLE_TYPE_WEBGPU_ANGLE,0x81A6);
    renderCube(vk,true);renderCube(wg,true);doCompare(vk,wg);}

    // No depth test
    {Ctx vk,wg;
    printf("DEPTH16, NO depth test (painter order):\n");
    init(vk,EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE,GL_DEPTH_COMPONENT16);
    init(wg,EGL_PLATFORM_ANGLE_TYPE_WEBGPU_ANGLE,GL_DEPTH_COMPONENT16);
    renderCube(vk,false);renderCube(wg,false);doCompare(vk,wg);}

    return 0;
}
