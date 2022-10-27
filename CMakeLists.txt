# cmake版本
cmake_minimum_required(VERSION 3.5.1)
project(READ_CAMERA)

# 添加编译器选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
SET(CMAKE_BUILD_TYPE "Debug")
SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -Wall -g")

# 打印相关信息和错误信息
message(STATUS "Cmake Version: " ${CMAKE_VERSION})
add_definitions("-Wall -g")

# 设置可执行文件的目录
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR})
# 添加ffmpeg头文件引用
include_directories(/home/zhaofachuan/ffmpeg/include)
# ffmpeg的lib库目录
link_directories(/home/zhaofachuan/ffmpeg/lib)
# linux下调用pthread
set(CMAKE_CXX_FLAGS -pthread)


# 可执行文件的输出名
# add_executable(camera_show  Read_camera.c)
# add_executable(send  src/sendfile.cpp)
# add_executable(recv  src/recvfile.cpp)
# add_executable(send_client  send_client.c)
# add_executable(recv_client  recv_client.c)
add_executable(h265_encode  h265_encode.c)

# 链接lib库
# target_link_libraries(camera_show libavformat.so;libavdevice.so;libavcodec.so;libavutil.so;libswresample.so;libavfilter.so;libpostproc.so;libswscale.so;libSDL.so)
# target_link_libraries(send libsrt.so)
# target_link_libraries(recv libsrt.so)
# target_link_libraries(send_client libsrt.so)
# target_link_libraries(recv_client libsrt.so)
target_link_libraries(h265_encode libavformat.so;libavdevice.so;libavcodec.so;libavutil.so;libswresample.so;libavfilter.so;libpostproc.so;libswscale.so;libSDL.so)
