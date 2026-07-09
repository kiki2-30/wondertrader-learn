cmake_minimum_required(VERSION 3.10)
project(uft-sandbox)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ===== 平台检测 =====
if(APPLE)
    message(STATUS "Platform: macOS")
elseif(UNIX)
    message(STATUS "Platform: Linux")
endif()

# ===== Include 路径 =====
set(UFT_SRC_DIR ${CMAKE_SOURCE_DIR}/src)
include_directories(
    ${UFT_SRC_DIR}
    ${UFT_SRC_DIR}/WtUftCore
    ${UFT_SRC_DIR}/Share
    ${UFT_SRC_DIR}/Includes
    ${UFT_SRC_DIR}/WTSTools
    ${UFT_SRC_DIR}/WTSUtils
    ${UFT_SRC_DIR}/WTSUtils/yamlcpp
    ${UFT_SRC_DIR}/WTSUtils/zstdlib
    ${UFT_SRC_DIR}/FasterLibs/ankerl
    ${UFT_SRC_DIR}/FasterLibs/tsl
)

# ===== 系统依赖 =====
# WT 镜像: /home/mydeps 预编译依赖
if(EXISTS /home/mydeps/include)
    message(STATUS "Using WT prebuilt deps at /home/mydeps")
    include_directories(/home/mydeps/include)
    link_directories(/home/mydeps/lib)
else()
    find_package(Boost REQUIRED)
    include_directories(${Boost_INCLUDE_DIRS})
    find_package(spdlog REQUIRED)
    include_directories(${spdlog_INCLUDE_DIRS})
    find_path(RAPIDJSON_INCLUDE_DIR rapidjson/document.h REQUIRED)
    include_directories(${RAPIDJSON_INCLUDE_DIR})
endif()

# ===== 编译宏 =====
add_definitions(-DWIN32_LEAN_AND_MEAN)

# ===== 源文件 =====
file(GLOB_RECURSE UFT_SOURCES
    ${UFT_SRC_DIR}/WtUftCore/*.cpp
    ${UFT_SRC_DIR}/WtUftStraFact/*.cpp
    ${UFT_SRC_DIR}/WTSTools/*.cpp
    ${UFT_SRC_DIR}/WTSUtils/*.cpp
    ${UFT_SRC_DIR}/WTSUtils/yamlcpp/*.cpp
    ${UFT_SRC_DIR}/WTSUtils/zstdlib/*.cpp
)

# ===== 构建 =====
add_library(uft_core STATIC ${UFT_SOURCES})

# ===== 主入口（测试）=====
add_executable(uft_sandbox src/main.cpp)
target_link_libraries(uft_sandbox uft_core pthread dl boost_filesystem)
