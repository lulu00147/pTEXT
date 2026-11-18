cmake_minimum_required(VERSION 3.10)
project(ptext C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -DPTEXT_MT -flto -fvisibility=hidden -ffunction-sections -fdata-sections -fmerge-all-constants")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-math-errno -fno-trapping-math -fno-signed-zeros")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffast-math -funsafe-math-optimizations -fno-plt -fstack-protector-strong -fomit-frame-pointer")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -flto -Wl,--gc-sections -Wl,--strip-all -Wl,--as-needed")

add_library(ptext SHARED
    main.c render.c reader.c encoding.c persist.c ui_util.c ui_reading.c ui_settings.c ui_lists.c nolog.c
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)
target_include_directories(ptext PRIVATE ${ANDROID_NDK}/sources/android/native_app_glue)
target_link_libraries(ptext PRIVATE android)