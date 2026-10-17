cmake_minimum_required(VERSION 3.20)
project(mcd_tas C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(mcd_tas SHARED
    src/mcd_impl.cpp
    src/tas_client.cpp
)
target_include_directories(mcd_tas PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(mcd_tas PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(mcd_tas PRIVATE ws2_32)

add_library(mcd_proxy SHARED
    src/proxy_core.cpp
    src/proxy_auto.cpp
    src/proxy_special.cpp
)
target_include_directories(mcd_proxy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(mcd_proxy PRIVATE _CRT_SECURE_NO_WARNINGS)

add_executable(mcd_tas_smoke tests/smoke.cpp)
target_include_directories(mcd_tas_smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(mcd_tas_smoke PRIVATE _CRT_SECURE_NO_WARNINGS)

add_executable(tas_probe tests/tas_probe.cpp src/tas_client.cpp)
target_include_directories(tas_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(tas_probe PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(tas_probe PRIVATE ws2_32)

add_executable(ftdi_jtag_scan tests/ftdi_jtag_scan.cpp)
target_compile_definitions(ftdi_jtag_scan PRIVATE _CRT_SECURE_NO_WARNINGS)

if(MSVC)
    target_compile_options(mcd_tas PRIVATE /W3)
    target_compile_options(mcd_proxy PRIVATE /W3)
    target_compile_options(mcd_tas_smoke PRIVATE /W3)
    target_compile_options(tas_probe PRIVATE /W3)
    target_compile_options(ftdi_jtag_scan PRIVATE /W3)
endif()
