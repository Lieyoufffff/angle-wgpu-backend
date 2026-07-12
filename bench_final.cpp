#include <EGL/egl.h>
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
    EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint mj,mn;eglInitialize(d,&mj,&mn);
    EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_NONE};
    EGLConfig cf;EGLint nc;eglChooseConfig(d,ca,&cf,1,&nc);
    EGLint pa[]={EGL_WIDTH,600,EGL_HEIGHT,600,EGL_NONE};
    EGLSurface sf=eglCreatePbufferSurface(d,cf,pa);
    EGLint xa[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLContext cx=eglCreateContext(d,cf,EGL_NO_CONTEXT,xa);
    eglMakeCurrent(d,sf,sf,cx);

    printf("GL_RENDERER: %s\n",(const char*)glGetString(GL_RENDERER));
    printf("GL_VERSION: %s\n\n",(const char*)glGetString(GL_VERSION));

    GLuint vsh=glCreateShader(GL_VERTEX_SHADER);glShaderSource(vsh,1,&VS,0);glCompileShader(vsh);
    GLuint fsh=glCreateShader(GL_FRAGMENT_SHADER);glShaderSource(fsh,1,&FS,0);glCompileShader(fsh);
    GLuint p=glCreateProgram();glAttachShader(p,vsh);glAttachShader(p,fsh);glLinkProgram(p);glUseProgram(p);
    GLint uT=glGetUniformLocation(p,"u_transform"),uC=glGetUniformLocation(p,"u_color");
    GLint aP=glGetAttribLocation(p,"a_position");
    float v[]={0,0,1,0,0.5f,1};GLuint vb;glGenBuffers(1,&vb);
    glBindBuffer(GL_ARRAY_BUFFER,vb);glBufferData(GL_ARRAY_BUFFER,sizeof(v),v,GL_STATIC_DRAW);
    glEnableVertexAttribArray(aP);glVertexAttribPointer(aP,2,GL_FLOAT,0,0,0);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0,0,600,600);

    unsigned char pixel[4];
    for(int i=0;i<5;i++){render(500,uT,uC);glReadPixels(300,300,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);}

    printf("=== MotionMark WebGL Triangles - ANGLE WebGPU Backend ===\n");
    printf("Sync method: glReadPixels (guaranteed pipeline flush)\n\n");
    printf("%-10s %-10s %-8s\n","Triangles","Avg(ms)","FPS");
    printf("----------------------------------\n");

    int score=0;float lastMs=0;int lastN=0;
    int cs[]={1000,5000,10000,20000,30000,40000,50000,60000,70000,80000,100000,120000,150000};
    int numC=13;
    for(int ci=0;ci<numC;ci++){
        int n=cs[ci];
        int fr=n<10000?10:n<50000?5:3;
        for(int i=0;i<2;i++){render(n,uT,uC);glReadPixels(300,300,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);}
        auto t0=std::chrono::high_resolution_clock::now();
        for(int i=0;i<fr;i++){render(n,uT,uC);glReadPixels(300,300,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);}
        auto t1=std::chrono::high_resolution_clock::now();
        double avg=std::chrono::duration<double,std::milli>(t1-t0).count()/fr;
        double fps=1000.0/avg;
        const char*mk=avg>16.667?" <<<":"";
        printf("%-10d %-10.3f %-8.1f%s\n",n,avg,fps,mk);
        if(avg<=16.667){lastMs=avg;lastN=n;score=n;}
        else if(lastN>0){double r=(16.667-lastMs)/(avg-lastMs);score=lastN+(int)(r*(n-lastN));break;}
    }
    if(score==0 && lastN==0) score=cs[0];
    printf("\n========================================\n");
    printf("SCORE: %d triangles/frame at 60fps\n",score);
    printf("========================================\n");

    eglTerminate(d);
    return 0;
}
