#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <chrono>

const char* VS = "attribute vec2 a_position;uniform mat3 u_transform;void main(){vec3 p=u_transform*vec3(a_position,1.0);gl_Position=vec4(p.xy,0.0,1.0);}";
const char* FS = "precision mediump float;uniform vec4 u_color;void main(){gl_FragColor=u_color;}";

float rf(){return(float)rand()/RAND_MAX;}

void render(int n, GLint uT, GLint uC){
    glClear(GL_COLOR_BUFFER_BIT);
    for(int i=0;i<n;i++){
        float a=((float)i/n)*6.2832f;
        float sc=0.03f+rf()*0.05f;
        float tx=cosf(a)*0.5f,ty=sinf(a)*0.5f;
        float c=cosf(a),s=sinf(a);
        float m[9]={sc*c,sc*s,0,-sc*s,sc*c,0,tx,ty,1};
        glUniformMatrix3fv(uT,1,0,m);
        glUniform4f(uC,rf(),rf(),rf(),0.7f);
        glDrawArrays(GL_TRIANGLES,0,3);
    }
}

int main(){
    EGLAttrib displayAttribs[] = {
        EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_WEBGPU_ANGLE,
        EGL_NONE
    };
    EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, displayAttribs);
    if(d==EGL_NO_DISPLAY){printf("No display\n");return 1;}
    EGLint mj,mn;
    if(!eglInitialize(d,&mj,&mn)){printf("eglInitialize failed\n");return 1;}
    EGLint ca[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig cf;EGLint nc;
    eglChooseConfig(d,ca,&cf,1,&nc);
    EGLint xa[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLContext cx=eglCreateContext(d,cf,EGL_NO_CONTEXT,xa);
    eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,cx);

    printf("GL_RENDERER: %s\n",(const char*)glGetString(GL_RENDERER));
    printf("GL_VERSION:  %s\n\n",(const char*)glGetString(GL_VERSION));

    const int W=600, H=600;
    GLuint fbo, rbo;
    glGenFramebuffers(1,&fbo);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glGenRenderbuffers(1,&rbo);
    glBindRenderbuffer(GL_RENDERBUFFER,rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA4, W, H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rbo);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if(status != GL_FRAMEBUFFER_COMPLETE){printf("FBO incomplete\n");return 1;}

    GLuint vsh=glCreateShader(GL_VERTEX_SHADER);glShaderSource(vsh,1,&VS,0);glCompileShader(vsh);
    GLuint fsh=glCreateShader(GL_FRAGMENT_SHADER);glShaderSource(fsh,1,&FS,0);glCompileShader(fsh);
    GLuint p=glCreateProgram();glAttachShader(p,vsh);glAttachShader(p,fsh);glLinkProgram(p);glUseProgram(p);
    GLint uT=glGetUniformLocation(p,"u_transform"),uC=glGetUniformLocation(p,"u_color");
    GLint aP=glGetAttribLocation(p,"a_position");
    float v[]={0,0,1,0,0.5f,1};GLuint vb;glGenBuffers(1,&vb);
    glBindBuffer(GL_ARRAY_BUFFER,vb);glBufferData(GL_ARRAY_BUFFER,sizeof(v),v,GL_STATIC_DRAW);
    glEnableVertexAttribArray(aP);glVertexAttribPointer(aP,2,GL_FLOAT,0,0,0);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0,0,W,H);

    // Warmup
    for(int i=0;i<5;i++){render(100,uT,uC);glFinish();}

    printf("=== MotionMark-like WebGL Triangles Benchmark ===\n");
    printf("Backend: ANGLE WebGPU -> Dawn -> Vulkan -> NVIDIA RTX 4080 SUPER\n");
    printf("Canvas: %dx%d FBO (GL_RGBA4)\n\n", W, H);
    printf("%-12s %-12s %-10s %-15s\n","Triangles","Frame(ms)","FPS","us/drawcall");
    printf("--------------------------------------------------------\n");

    int score=0;double lastMs=0;int lastN=0;
    int cs[]={10,20,30,50,75,100,150,200,300,500,750,1000,1500,2000,3000,5000};
    int numC=16;
    for(int ci=0;ci<numC;ci++){
        int n=cs[ci];
        int fr= n<100?20 : n<500?10 : n<2000?5 : 3;

        // Warmup per level
        for(int i=0;i<2;i++){render(n,uT,uC);glFinish();}

        auto t0=std::chrono::high_resolution_clock::now();
        for(int i=0;i<fr;i++){
            render(n,uT,uC);
            glFinish();
        }
        auto t1=std::chrono::high_resolution_clock::now();
        double avg=std::chrono::duration<double,std::milli>(t1-t0).count()/fr;
        double fps=1000.0/avg;
        double usPerDraw=avg*1000.0/n;
        const char*mk=avg>16.667?" <<<":"";
        printf("%-12d %-12.3f %-10.1f %-15.1f%s\n",n,avg,fps,usPerDraw,mk);

        if(avg<=16.667){lastMs=avg;lastN=n;score=n;}
        else if(score==0 && lastN==0){
            // First frame already over budget - interpolate from 0
            double r=16.667/avg;
            score=(int)(n*r);
        }
        else if(lastN>0 && score==lastN){
            double r=(16.667-lastMs)/(avg-lastMs);
            score=lastN+(int)(r*(n-lastN));
        }

        if(avg > 10000) break; // don't waste time on impossibly slow levels
    }

    printf("\n========================================\n");
    printf("SCORE: %d triangles/frame at 60fps\n", score);
    printf("========================================\n");
    printf("\nNote: This measures the full ANGLE WebGPU translation path.\n");
    printf("Each glDrawArrays goes through: dirty-bits sync -> pipeline cache\n");
    printf("lookup -> bind group creation -> command encoding -> GPU submit.\n");
    printf("The bottleneck is per-draw-call CPU overhead in the translation layer.\n");

    eglTerminate(d);
    return 0;
}
