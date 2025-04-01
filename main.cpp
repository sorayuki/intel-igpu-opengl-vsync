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

class Renderer: public QOpenGLFunctions {
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
        assert(glGetError() == GL_NO_ERROR);

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

        int r;

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureid);
        assert((r = glGetError()) == GL_NO_ERROR);

        glUniform1i(glGetUniformLocation(program, "tex"), 0);
        assert((r = glGetError()) == GL_NO_ERROR);

        GLint positionAttrib = glGetAttribLocation(program, "position");
        assert((r = glGetError()) == GL_NO_ERROR);
        glVertexAttribPointer(positionAttrib, 3, GL_FLOAT, GL_FALSE, 0, vertex);
        assert((r = glGetError()) == GL_NO_ERROR);
        glEnableVertexAttribArray(positionAttrib);
        assert((r = glGetError()) == GL_NO_ERROR);

        GLint texcoordAttrib = glGetAttribLocation(program, "texcoord");
        assert((r = glGetError()) == GL_NO_ERROR);
        glVertexAttribPointer(texcoordAttrib, 2, GL_FLOAT, GL_FALSE, 0, texcoord);
        assert((r = glGetError()) == GL_NO_ERROR);
        glEnableVertexAttribArray(texcoordAttrib);
        assert((r = glGetError()) == GL_NO_ERROR);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        assert((r = glGetError()) == GL_NO_ERROR);
    }
};


struct OffScreenRenderer: public QThread, public QOpenGLExtraFunctions {
    QOffscreenSurface *surface = nullptr;
    QOpenGLContext *context = nullptr;
    
    OffScreenRenderer()
    {
        context = new QOpenGLContext();
        surface = new QOffscreenSurface();
    }

    std::function<void(GLuint tex, GLsync sync)> onTexAvailable;

    void run() override {
        context->makeCurrent(surface);
        initializeOpenGLFunctions();

        Renderer renderer;
        renderer.init();

        context->doneCurrent();

        using clock = std::chrono::steady_clock;

        while (!isInterruptionRequested()) {
            context->makeCurrent(surface);

            auto begin_time = clock::now();

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
    
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                std::cerr << "Framebuffer is not complete!" << std::endl;
            }

            renderer.tick();
            glViewport(0, 0, 512, 512);
            renderer.draw();

            glBindFramebuffer(GL_FRAMEBUFFER, prevfbo);
            glDeleteFramebuffers(1, &fbo);

            auto sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

            auto endtime = clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endtime - begin_time).count();

            std::cerr << "offscreen render cost = " << duration << "us" << std::endl;

            if (!!onTexAvailable)
                onTexAvailable(tex, sync);
            else
                glDeleteSync(sync);

            context->doneCurrent();
            QThread::msleep(20);
        }
    }
};


class MyWidget: public QOpenGLWidget, public QOpenGLExtraFunctions {
    Renderer renderer_;
    OffScreenRenderer* offrenderer_;
    QTimer timer_;

    std::mutex mutex_;
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
        doneCurrent();

        offrenderer_ = new OffScreenRenderer();
        offrenderer_->context->setFormat(current->format());
        offrenderer_->context->setShareContext(current);
        offrenderer_->context->create();
        offrenderer_->context->moveToThread(offrenderer_);

        offrenderer_->surface->setFormat(current->format());
        offrenderer_->surface->create();

        offrenderer_->moveToThread(offrenderer_);

        offrenderer_->onTexAvailable = [this](auto tex, auto sync) {
            std::unique_lock lg(mutex_);
            SetTex(tex, sync);
        };
        offrenderer_->start();

        makeCurrent();

        connect(&timer_, &QTimer::timeout, [this]() {
            update();
        });
        timer_.start(30);
    }

    void paintGL() override {
        std::unique_lock lg(mutex_);
        if (tex != 0) {
            glWaitSync(sync, 0, GL_TIMEOUT_IGNORED);
            glViewport(0, 0, width(), height());
            renderer_.draw(tex);
        } else {
            renderer_.draw();
        }
    }
};


int main(int argc, char** argv){
    // QLoggingCategory::setFilterRules("*.debug=true");

    QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

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
