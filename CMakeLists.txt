cmake_minimum_required(VERSION 3.25)

project(Symbolon
    VERSION 0.0.0
    DESCRIPTION "FT8 selective responder for measuring bidirectional HF propagation"
    LANGUAGES C CXX
)

# core/ and hal/ are C11 with no compiler extensions -- they must compile unchanged for a
# future ARMv7 (X6200) target, so nothing here should rely on GNU/MSVC C dialect extensions.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()
include(CTest)

# Vendored code is configured before warning flags are turned on below, so those flags never
# apply to third_party/ -- we don't police upstream's code, only our own.
add_subdirectory(third_party)

if(NOT MSVC)
    add_compile_options(-Wall -Wextra)
endif()

add_subdirectory(hal)
add_subdirectory(core)
add_subdirectory(app)
add_subdirectory(tests)
