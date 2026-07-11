cmake_minimum_required(VERSION 3.22)
project(denimR6-dumper VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(denimR6-dumper
    src/main.cpp
    src/process.cpp
    src/scanner.cpp
    src/resolver.cpp
    src/heap_scanner.cpp
    src/chains.cpp
    src/validator.cpp
    src/exporter.cpp
    src/json_util.cpp
)

target_include_directories(denimR6-dumper PRIVATE src)

if(MSVC)
    target_compile_options(denimR6-dumper PRIVATE /W4 /permissive-)
    target_link_libraries(denimR6-dumper PRIVATE version)
else()
    target_compile_options(denimR6-dumper PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Copy signature/struct data next to the executable for runtime loading
add_custom_command(TARGET denimR6-dumper POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/signatures
        $<TARGET_FILE_DIR:denimR6-dumper>/signatures
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/structs
        $<TARGET_FILE_DIR:denimR6-dumper>/structs
)
