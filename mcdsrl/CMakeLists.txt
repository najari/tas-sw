cmake_minimum_required(VERSION 3.20)
project(mcdsrl_tas C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(mcdsrl_tas SHARED
    src/mcdsrl_impl.cpp
    src/tas_client.cpp
    src/mcdsrl_tas.def
)
target_include_directories(mcdsrl_tas PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(mcdsrl_tas PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(mcdsrl_tas PRIVATE ws2_32)

add_executable(mcdsrl_smoke tests/mcdsrl_smoke.cpp)
target_include_directories(mcdsrl_smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(mcdsrl_smoke PRIVATE _CRT_SECURE_NO_WARNINGS)

add_executable(mcdsrl_record tests/mcdsrl_record.cpp)
target_include_directories(mcdsrl_record PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(mcdsrl_record PRIVATE _CRT_SECURE_NO_WARNINGS)

add_executable(map_probe tests/map_probe.cpp src/tas_client.cpp)
target_include_directories(map_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(map_probe PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(map_probe PRIVATE ws2_32)

if(MSVC)
    target_compile_options(mcdsrl_tas PRIVATE /W3)
    target_compile_options(mcdsrl_smoke PRIVATE /W3)
    target_compile_options(mcdsrl_record PRIVATE /W3)
    target_compile_options(map_probe PRIVATE /W3)
endif()
