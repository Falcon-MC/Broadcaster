cmake_minimum_required(VERSION 3.16)
project(Falcon.Broadcaster VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    falcon_network
    GIT_REPOSITORY https://github.com/Falcon-MC/Network.git
    GIT_TAG main
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(falcon_network)

file(GLOB_RECURSE FALCON_BROADCASTER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(FalconBroadcaster ${FALCON_BROADCASTER_SOURCES})
target_include_directories(FalconBroadcaster PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(FalconBroadcaster PRIVATE FalconNetwork)

if (WIN32)
    target_compile_definitions(FalconBroadcaster PRIVATE WIN32_LEAN_AND_MEAN)
endif ()

if (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(FalconBroadcaster PRIVATE -static -fuse-ld=lld)
endif ()
