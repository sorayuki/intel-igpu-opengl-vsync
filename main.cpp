#include <QApplication>
#include <QWindow>
#include <QWidget>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QTimer>

#include <iostream>

class MyWidget: public QOpenGLWidget, public QOpenGLFunctions {
    QTimer timer_;
    int pos = 0;

    uint textureid = 0;
    uint myfbo = 0;

    uint program = 0;

public:
    MyWidget(QWidget* parent = nullptr): QOpenGLWidget(parent) {
        timer_.setInterval(4);
        QObject::connect(&timer_, &QTimer::timeout, [this]() {
            pos += 2;
            if (pos > width())
                pos = 0;
            update();
        });
        timer_.start();
    }

    ~MyWidget() override {
        timer_.stop();
    }

protected:
    void initializeGL() override {
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

        // 编译opengl es 2.0 shader
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
            std::cout << "Vertex Shader Compilation Failed:" << infoLog;
        }
        
        uint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
        glCompileShader(fragmentShader);
        glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
            std::cout << "Fragment Shader Compilation Failed:" << infoLog;
        }
        
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glLinkProgram(program);
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(program, 512, nullptr, infoLog);
            std::cout << "Program Linking Failed:" << infoLog;
        }
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
    }

    void paintGL() override {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        auto fpos = 2.0f * pos / width() - 1;

        float vertex[] = {
             fpos, -1.0f, 0.0f,
             1.0f, -1.0f, 0.0f,
             fpos,  1.0f, 0.0f,
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
        glUniform1i(glGetUniformLocation(program, "tex"), 0);
        GLint positionAttrib = glGetAttribLocation(program, "position");
        glVertexAttribPointer(positionAttrib, 3, GL_FLOAT, GL_FALSE, 0, vertex);
        glEnableVertexAttribArray(positionAttrib);

        GLint texcoordAttrib = glGetAttribLocation(program, "texcoord");
        glVertexAttribPointer(texcoordAttrib, 2, GL_FLOAT, GL_FALSE, 0, texcoord);
        glEnableVertexAttribArray(texcoordAttrib);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
};


int main(int argc, char** argv){
    QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    auto w = new MyWidget();
    w->show();
    return app.exec();
}
