cmake_minimum_required(VERSION 3.12)

project(PotatorSDL2)

find_package(SDL2 REQUIRED)

include_directories(${SDL2_INCLUDE_DIR})

add_executable(PotatorSDL2
    main.c
    ../../common/m6502/m6502.c
    ../../common/m6502/m6502.h
    ../../common/m6502/tables.h
    ../../common/controls.c
    ../../common/controls.h
    ../../common/gpu.c
    ../../common/gpu.h
    ../../common/memorymap.c
    ../../common/memorymap.h
    ../../common/sound.c
    ../../common/sound.h
    ../../common/supervision.h
    ../../common/timer.c
    ../../common/timer.h
    ../../common/types.h
    ../../common/watara.c
)

target_link_libraries(PotatorSDL2 ${SDL2_LIBRARY})