cmake_minimum_required(VERSION 3.20)
project(mcdsdl C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(mcdsdl SHARED
    src/mcdsdl_impl.cpp
    src/mcdsdl.def
)
target_include_directories(mcdsdl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(mcdsdl PRIVATE _CRT_SECURE_NO_WARNINGS)

add_executable(mcdsdl_smoke tests/mcdsdl_smoke.cpp)
target_include_directories(mcdsdl_smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(mcdsdl_smoke PRIVATE _CRT_SECURE_NO_WARNINGS)

if(MSVC)
    target_compile_options(mcdsdl PRIVATE /W3)
    target_compile_options(mcdsdl_smoke PRIVATE /W3)
endif()
