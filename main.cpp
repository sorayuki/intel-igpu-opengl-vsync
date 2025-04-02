#include <QApplication>
#include <QWindow>
#include <QWidget>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLExtraFunctions>
#include <QTimer>
#include <QThread>
#include <QOffscreenSurface>
#include <QLoggingCategory>

#include <iostream>
#include <mutex>

#define DISABLE_SHARE_CONTEXT 0

#define CHECKGLERR { \
    int r = glGetError(); \
    if (r != GL_NO_ERROR) \
        __debugbreak(); \
}

#define CLEARGLERR while(glGetError() != GL_NO_ERROR)

// 渲染一个不断缩小的红色方块纹理或自定义纹理
class Renderer: public QOpenGLExtraFunctions {
    uint textureid = 0;
    uint program = 0;

    float pos = -1.0f;
public:
    Renderer()
    {
    }

    void init() {
        initializeOpenGLFunctions();
        glGenTextures(1, &textureid);
        static uint8_t data[] = {
            255, 0, 0, 255
        };
        glBindTexture(GL_TEXTURE_2D, textureid);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        const char* vertexShaderSource = R"(#version 100
            attribute vec4 position;
            attribute vec2 texcoord;
            varying vec2 Texcoord;
            void main() {
                Texcoord = texcoord;
                gl_Position = position;
            }
        )";
        const char* fragmentShaderSource = R"(#version 100
            precision mediump float;
            varying vec2 Texcoord;
            uniform sampler2D tex;
            void main() {
                gl_FragColor = texture2D(tex, Texcoord);
            }
        )";
        program = glCreateProgram();
        uint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
        glCompileShader(vertexShader);
        
        GLint success;
        GLchar infoLog[512];
        
        glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
            std::cerr << "Vertex Shader Compilation Failed:" << infoLog;
        }
        
        uint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
        glCompileShader(fragmentShader);
        glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
            std::cerr << "Fragment Shader Compilation Failed:" << infoLog;
        }
        
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glLinkProgram(program);
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(program, 512, nullptr, infoLog);
            std::cerr << "Program Linking Failed:" << infoLog;
        }
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
    }

    void tick() {
        pos += 0.01f;
        if (pos >= 1.0f)
            pos = -1.0f;
    }

    void draw(int textureid = 0) {
        if (textureid == 0)
            textureid = this->textureid;
        glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        CHECKGLERR;

        float vertex[] = {
            pos,  -1.0f, 0.0f,
            1.0f, -1.0f, 0.0f,
            pos,   1.0f, 0.0f,
            1.0f,  1.0f, 0.0f,
        };
        float texcoord[] = {
            0.0f, 1.0f,
            1.0f, 1.0f,
            0.0f, 0.0f,
            1.0f, 0.0f,
        };

        glUseProgram(program);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureid);
        CHECKGLERR;

        glUniform1i(glGetUniformLocation(program, "tex"), 0);
        CHECKGLERR;

        GLint positionAttrib = glGetAttribLocation(program, "position");
        CHECKGLERR;
        glVertexAttribPointer(positionAttrib, 3, GL_FLOAT, GL_FALSE, 0, vertex);
        CHECKGLERR;
        glEnableVertexAttribArray(positionAttrib);
        CHECKGLERR;

        GLint texcoordAttrib = glGetAttribLocation(program, "texcoord");
        CHECKGLERR;
        glVertexAttribPointer(texcoordAttrib, 2, GL_FLOAT, GL_FALSE, 0, texcoord);
        CHECKGLERR;
        glEnableVertexAttribArray(texcoordAttrib);
        CHECKGLERR;
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        CHECKGLERR;
    }
};


// 在后台线程渲染好一个texture并回调
struct OffScreenRenderer: public QThread, public QOpenGLExtraFunctions {
    QOffscreenSurface *surface = nullptr;
    QOpenGLContext *context = nullptr;
    
    OffScreenRenderer()
    {
        context = new QOpenGLContext();
        surface = new QOffscreenSurface();
    }

    std::function<bool(GLuint tex, GLsync sync)> onTexAvailable;

    void run() override {
        context->makeCurrent(surface);
        initializeOpenGLFunctions();
        CLEARGLERR;

        Renderer renderer;
        renderer.init();

        context->doneCurrent();

        using clock = std::chrono::steady_clock;

        int framecount = 0;
        auto begin_time = clock::now();
        while (!isInterruptionRequested()) {
            context->makeCurrent(surface);
            CLEARGLERR;

            auto iter_begin = clock::now();

            GLint prevfbo;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevfbo);

            GLuint tex, fbo;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
            CHECKGLERR;
    
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
            CHECKGLERR;
    
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                std::cerr << "Framebuffer is not complete!" << std::endl;
            }

            renderer.tick();
            glViewport(0, 0, 512, 512);
            CHECKGLERR;
            renderer.draw();

            glBindFramebuffer(GL_FRAMEBUFFER, prevfbo);
            glDeleteFramebuffers(1, &fbo);

            auto sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            CHECKGLERR;

            auto iter_end = clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(iter_end - iter_begin).count();

            if (duration > 10000)
                std::cerr << "offscreen render cost = " << duration << "us" << std::endl;

            if (![&]() {
                if (!onTexAvailable)
                    return false;
                return onTexAvailable(tex, sync);
            }()) {
                glDeleteTextures(1, &tex);
                glDeleteSync(sync);
            }

            context->doneCurrent();
            
            ++framecount;
            std::this_thread::sleep_until(begin_time + framecount / 100.0 * std::chrono::microseconds(1000000));
        }
    }
};


// OpenGL控件，只是按固定速率把后台线程送来的纹理展示而已
class MyWidget: public QOpenGLWidget, public QOpenGLExtraFunctions {
    Renderer renderer_;
    OffScreenRenderer* offrenderer_;
    QTimer timer_;

    GLuint tex = 0;
    GLsync sync = 0;

    void CleanTex() {
        if (tex != 0) {
            glDeleteTextures(1, &tex);
            tex = 0;
        }
        if (sync != 0) {
            glDeleteSync(sync);
            sync = 0;
        }
    }

    void SetTex(GLuint tex, GLsync sync) {
        CleanTex();
        this->tex = tex;
        this->sync = sync;
    }

public:
    MyWidget(QWidget* parent = nullptr): QOpenGLWidget(parent) {
    }

    ~MyWidget() override {
    }

protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        renderer_.init();

        QOpenGLContext *current = context();
        auto format = current->format();

        offrenderer_ = new OffScreenRenderer();
        offrenderer_->context->setFormat(format);
        if (!DISABLE_SHARE_CONTEXT)
            offrenderer_->context->setShareContext(current);
        offrenderer_->context->create();
        offrenderer_->context->moveToThread(offrenderer_);

        offrenderer_->surface->setFormat(format);
        offrenderer_->surface->create();

        offrenderer_->moveToThread(offrenderer_);

        if (!DISABLE_SHARE_CONTEXT) {
            offrenderer_->onTexAvailable = [this](auto tex, auto sync) {
                return QMetaObject::invokeMethod(this, [=]() {
                    SetTex(tex, sync);
                }, Qt::QueuedConnection);
            };
        }
        offrenderer_->start();

        connect(&timer_, &QTimer::timeout, [this]() {
            if (DISABLE_SHARE_CONTEXT)
                renderer_.tick();
            update();
        });
        timer_.start(16);
    }

    void paintGL() override {
        if (tex != 0) {
            glWaitSync(sync, 0, GL_TIMEOUT_IGNORED);
            CHECKGLERR;
            glViewport(0, 0, width(), height());
            CHECKGLERR;
            renderer_.draw(tex);
        } else {
            renderer_.draw();
        }
    }
};


int main(int argc, char** argv){
    // QLoggingCategory::setFilterRules("*.debug=true");

    QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    if (!DISABLE_SHARE_CONTEXT)
        QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    //format.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(format);

    // 加了以后会必须得用vertex buffer object不然报错
    // QSurfaceFormat format;
    // format.setVersion(4, 1);
    // format.setProfile(QSurfaceFormat::CoreProfile);
    // QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    auto w = new MyWidget();
    w->show();
    return app.exec();
}
