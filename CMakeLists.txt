cmake_minimum_required(VERSION 3.22)
project(denimR6-dumper VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Common source files shared between dumper and cheat
set(COMMON_SOURCES
    src/process.cpp
    src/scanner.cpp
    src/resolver.cpp
    src/heap_scanner.cpp
    src/chains.cpp
    src/validator.cpp
    src/exporter.cpp
    src/json_util.cpp
    src/kernel.cpp
    src/kernel_process.cpp
    src/codecave.cpp
    src/math_util.cpp
    src/entity_reader.cpp
)

# ============================================================
# Dumper target (offset discovery tool — existing)
# ============================================================
add_executable(denimR6-dumper
    src/main.cpp
    ${COMMON_SOURCES}
)

target_include_directories(denimR6-dumper PRIVATE src)

if(MSVC)
    target_compile_options(denimR6-dumper PRIVATE /W4 /permissive-)
    target_link_libraries(denimR6-dumper PRIVATE version bcrypt)
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

# ============================================================
# Cheat target (external ESP cheat)
# ============================================================
add_executable(denimR6-cheat
    src/cheat_main.cpp
    ${COMMON_SOURCES}
    src/overlay.cpp
)

target_include_directories(denimR6-cheat PRIVATE src)

if(MSVC)
    target_compile_options(denimR6-cheat PRIVATE /W4 /permissive-)
    target_link_libraries(denimR6-cheat
        PRIVATE version bcrypt d3d11 d3dcompiler
    )
else()
    target_compile_options(denimR6-cheat PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(TARGET denimR6-cheat POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/signatures
        $<TARGET_FILE_DIR:denimR6-cheat>/signatures
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/structs
        $<TARGET_FILE_DIR:denimR6-cheat>/structs
)

# ============================================================
# Loader target (BYOVD driver mapper)
# ============================================================
add_executable(denimR6-loader
    loader/loader.cpp
    loader/byovd_mapper.cpp
    loader/safety.cpp
)

target_include_directories(denimR6-loader PRIVATE loader)

if(MSVC)
    target_compile_options(denimR6-loader PRIVATE /W4 /permissive-)
    target_link_libraries(denimR6-loader PRIVATE ntdll)
else()
    target_compile_options(denimR6-loader PRIVATE -Wall -Wextra -Wpedantic)
endif()
