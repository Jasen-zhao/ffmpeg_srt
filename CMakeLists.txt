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


add_executable(publish  MQTT_demo/MQTTClient_publish.c)
add_executable(subscribe  MQTT_demo/MQTTClient_subscribe.c)


# 链接lib库
target_link_libraries(publish libpaho-mqtt3a.so;libpaho-mqtt3c.so)
target_link_libraries(subscribe libpaho-mqtt3a.so;libpaho-mqtt3c.so)

