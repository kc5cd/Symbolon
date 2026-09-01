# app/ -- C++17, PC only. A future radio-side build never compiles this directory.
add_executable(symbolon
    main.cpp
)

target_link_libraries(symbolon PRIVATE sym_core sym_hal)
