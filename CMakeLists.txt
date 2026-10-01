cmake_minimum_required(VERSION 3.16)

# Vita:  cmake -B build-vita -G Ninja -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
# PC:    cmake -B build-pc -G Ninja          (builds swgbtool, the headless data/render tool)
project(swgb_vita C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(SWGB_CORE_SOURCES
    src/core/audio.cpp
    src/core/blendomatic.cpp
    src/core/cpx.cpp
    src/core/drs.cpp
    src/core/elevation.cpp
    src/core/palette.cpp
    src/core/scenario.cpp
    src/core/slope_lighting.cpp
    src/core/slp.cpp
    src/core/genie_dat.cpp
    src/core/language_strings.cpp
    src/engine/assets.cpp
    src/engine/ai_script.cpp
    src/engine/game.cpp
    src/engine/pathfinding.cpp
)

if(VITA)
    include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

    set(VITA_APP_NAME "SWGB Vita")
    set(VITA_TITLEID "SWGB00001")
    set(VITA_VERSION "00.01")
    option(SWGB_VPK_LIVEAREA "Bundle LiveArea icon/background into the VPK" ON)

    add_executable(swgb
        ${SWGB_CORE_SOURCES}
        src/render/gl_renderer.cpp
        src/platform/vita/vita_audio.cpp
        src/platform/vita/main_vita.cpp
    )
    target_compile_options(swgb PRIVATE
        -Os -ffunction-sections -fdata-sections
        -Wall -Wno-psabi)
    # -Wl,-q keeps relocations; vita-elf-create needs them for a valid velf.
    target_link_options(swgb PRIVATE -Wl,-q,--gc-sections)
    target_link_libraries(swgb PRIVATE
        vitaGL vitashark SceShaccCgExt taihen_stub mathneon z
        SceGxm_stub SceDisplay_stub SceCtrl_stub SceTouch_stub SceHid_stub
        SceAudio_stub SceCommonDialog_stub SceSysmodule_stub SceMotion_stub
        SceAppUtil_stub SceAppMgr_stub SceShaccCg_stub ScePower_stub
        SceKernelDmacMgr_stub SceIme_stub
        vorbisfile vorbis ogg stdc++ m)

    # 32MB phycont budget: without it vitaGL renders nothing on this setup.
    set(VITA_MAKE_FSELF_FLAGS "${VITA_MAKE_FSELF_FLAGS} -pm 0x2000000")
    # UNSAFE = full permissions, needed for the 256MB newlib heap.
    vita_create_self(eboot.bin swgb UNSAFE)

    if(SWGB_VPK_LIVEAREA)
        vita_create_vpk(swgb.vpk ${VITA_TITLEID} eboot.bin
            VERSION ${VITA_VERSION}
            NAME ${VITA_APP_NAME}
            FILE vita/sce_sys/icon0.png sce_sys/icon0.png
                 vita/sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
                 vita/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
                 vita/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
        )
    else()
        vita_create_vpk(swgb.vpk ${VITA_TITLEID} eboot.bin VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME})
    endif()
else()
    find_package(ZLIB REQUIRED)
    add_executable(swgbtool
        tools/swgbtool.cpp
        ${SWGB_CORE_SOURCES}
        src/render/soft_renderer.cpp
    )
    target_link_libraries(swgbtool PRIVATE ZLIB::ZLIB)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(swgbtool PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
    endif()
endif()
