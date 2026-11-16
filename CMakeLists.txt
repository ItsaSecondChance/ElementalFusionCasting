cmake_minimum_required(VERSION 3.21)

project(ElementalFusionCasting VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_PATCH_SAFETY OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
    CommonLibSSE
    GIT_REPOSITORY https://github.com/alandtse/CommonLibSSE-NG.git
    GIT_TAG 39f9d07a6ffabea8fb559eee87ab7d27cd463e8a
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(CommonLibSSE)
include("${commonlibsse_SOURCE_DIR}/cmake/CommonLibSSE.cmake")

add_commonlibsse_plugin(
    ElementalFusionCasting
    AUTHOR "Second Chance"
    VERSION 0.1.0
    SOURCES src/main.cpp
)

set_target_properties(ElementalFusionCasting PROPERTIES OUTPUT_NAME "ElementalFusionCasting")
target_compile_definitions(
    ElementalFusionCasting PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(ElementalFusionCasting PRIVATE /bigobj /utf-8)
endif()
