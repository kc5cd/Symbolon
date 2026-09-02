# app/ -- C++17, PC only. A future radio-side build never compiles this directory.
add_executable(symbolon
    main.cpp
    wav_decode.h
    wav_decode.cpp
    config.h
    config.cpp
    band_table.h
    band_table.cpp
    tx_playback.h
    tx_playback.cpp
)

target_link_libraries(symbolon PRIVATE sym_core sym_hal ft8_lib_wave)

if(WIN32)
    # sym_hal links Hamlib as a shared import (see third_party/CMakeLists.txt); its DLL and
    # runtime dependencies need to sit next to symbolon.exe, not just be linkable at build
    # time.
    add_custom_command(TARGET symbolon POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${HAMLIB_RUNTIME_DLLS} "$<TARGET_FILE_DIR:symbolon>"
        COMMENT "Copying Hamlib runtime DLLs next to symbolon.exe"
    )
endif()
