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
# 添加头文件引用,ffmpeg
include_directories(/home/zhaofachuan/ffmpeg/include)
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include  #这行为增加的
)
# lib库目录,ffmpeg
link_directories(/home/zhaofachuan/ffmpeg/lib)
# linux下调用pthread
set(CMAKE_CXX_FLAGS -pthread)


# srt_demo
# add_executable(send  srt_demo/sendfile.cpp)
# add_executable(recv  srt_demo/recvfile.cpp)
# add_executable(send_client  srt_demo/send_client.c)
# add_executable(recv_client  srt_demo/recv_client.c)

# ffmpeg_demo
# add_executable(camera_show  ffmpeg_demo/show_camera.c)
# add_executable(muxer  ffmpeg_demo/muxer.cpp)
# add_executable(h265_encode  ffmpeg_demo/h265_encode.c)
# add_executable(push_stream  ffmpeg_demo/push_stream.cpp)
add_executable(test  test.c)


# 链接lib库
# target_link_libraries(send libsrt.so)
# target_link_libraries(recv libsrt.so)
# target_link_libraries(send_client libsrt.so)
# target_link_libraries(recv_client libsrt.so)

# target_link_libraries(camera_show libavformat.so;libavdevice.so;libavcodec.so;libavutil.so;libswresample.so;libavfilter.so;libpostproc.so;libswscale.so;libSDL.so)
# target_link_libraries(h265_encode libavformat.so;libavdevice.so;libavcodec.so;libavutil.so;libswresample.so;libavfilter.so;libpostproc.so;libswscale.so;libSDL.so)
# target_link_libraries(muxer libavformat.so;libavdevice.so;libavcodec.so;libavutil.so;libswresample.so;libavfilter.so;libpostproc.so;libswscale.so;libSDL.so)
# target_link_libraries(push_stream libavformat.so;libavdevice.so;libavcodec.so;libavutil.so;libswresample.so;libavfilter.so;libpostproc.so;libswscale.so;libSDL.so)
target_link_libraries(test libavformat.so;libavdevice.so;libavcodec.so;libavutil.so;libswresample.so;libavfilter.so;libpostproc.so;libswscale.so;libSDL.so)
