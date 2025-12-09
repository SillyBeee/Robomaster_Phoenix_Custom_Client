cmake_minimum_required(VERSION 3.16)
project(RM_Client CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) 

set(SLINT_STYLE "cosmic" CACHE STRING "Slint style" FORCE)

find_package(Slint REQUIRED)
find_package(OpenCV REQUIRED)
find_package(SDL2 REQUIRED)
find_package(PahoMqttCpp REQUIRED)
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Protobuf REQUIRED)

# --- 查找 FFmpeg ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVUTIL REQUIRED libavutil)
pkg_check_modules(SWSCALE REQUIRED libswscale)


include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/drivers)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/utils)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/protocol)
include_directories(
    ${AVCODEC_INCLUDE_DIRS}
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${SWSCALE_INCLUDE_DIRS}
)

file(GLOB SOURCES 
"src/*.cpp" 
"src/drivers/*.cpp" 
"src/utils/*.cpp" 
"src/protocol/*.cc")


add_executable(RM_Client src/main.cpp ${SOURCES})
slint_target_sources(RM_Client ui/app-window.slint)

target_link_libraries(RM_Client PRIVATE 
Slint::Slint 
${OpenCV_LIBS} 
SDL2::SDL2
spdlog::spdlog
PahoMqttCpp::paho-mqttpp3
nlohmann_json::nlohmann_json
${Protobuf_LIBRARIES}
${AVCODEC_LIBRARIES}
${AVFORMAT_LIBRARIES}
${AVUTIL_LIBRARIES}
${SWSCALE_LIBRARIES}
)


if (WIN32)
    add_custom_command(TARGET RM_Client POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:my_application> $<TARGET_FILE_DIR:my_application> COMMAND_EXPAND_LISTS)
endif()

add_compile_definitions(PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
