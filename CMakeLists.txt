cmake_minimum_required(VERSION 3.10.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
project(qtgl VERSION 0.1.0 LANGUAGES C CXX)
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

set(CMAKE_PREFIX_PATH D:/devspace/qtgl/msvc2019_64/lib/cmake/Qt5)

find_package(Qt5 REQUIRED Widgets Core OpenGL)

add_executable(qtgl main.cpp)
target_link_libraries(qtgl PRIVATE Qt5::Widgets Qt5::Core Qt5::OpenGL)
