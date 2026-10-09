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
    src/engine/campaign.cpp
    src/engine/editor.cpp
    src/engine/editor_document.cpp
    src/engine/frontend.cpp
    src/engine/campaign_scene.cpp
    src/engine/game.cpp
    src/engine/game_save.cpp
    src/engine/game_techtree.cpp
    src/engine/menu_framework.cpp
    src/engine/pathfinding.cpp
    src/engine/rms.cpp
    src/engine/settings.cpp
    src/engine/skirmish.cpp
    src/engine/startup.cpp
    src/engine/ui_text.cpp
)

if(VITA)
    include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

    set(VITA_APP_NAME "SWGB Vita")
    set(VITA_TITLEID "SWGB00001")
    set(VITA_VERSION "00.01")
    option(SWGB_VPK_LIVEAREA "Bundle LiveArea icon/background into the VPK" ON)

    # SWGB_VITA3K builds a VPK that runs in the Vita3K emulator: vitashark's
    # SceShaccCgExt shader-compiler extensions patch libshacccg through
    # taiHEN, which Vita3K does not provide (the first draw jumps to 0).
    option(SWGB_VITA3K "Build for the Vita3K emulator (no SceShaccCgExt)" OFF)
    set(SWGB_VITA_EXTRA_SOURCES)
    if(SWGB_VITA3K)
        list(APPEND SWGB_VITA_EXTRA_SOURCES src/platform/vita/shacccg_ext_stub.c)
    endif()
    add_executable(swgb
        ${SWGB_CORE_SOURCES}
        ${SWGB_VITA_EXTRA_SOURCES}
        src/render/gl_renderer.cpp
        src/platform/vita/vita_audio.cpp
        src/platform/vita/vita_video.cpp
        src/platform/vita/main_vita.cpp
    )
    if(SWGB_VITA3K)
        target_compile_definitions(swgb PRIVATE SWGB_VITA3K=1)
        set(SWGB_SHACCCG_EXT_LIBS)
    else()
        set(SWGB_SHACCCG_EXT_LIBS SceShaccCgExt taihen_stub)
    endif()
    set(SWGB_VITA_OPT_FLAGS "-O2" CACHE STRING "Optimization flags for the Vita build (-O2 measured ~15% faster than -Os on hardware)")
    separate_arguments(SWGB_VITA_OPT_FLAG_LIST UNIX_COMMAND "${SWGB_VITA_OPT_FLAGS}")
    target_compile_options(swgb PRIVATE
        ${SWGB_VITA_OPT_FLAG_LIST} -ffunction-sections -fdata-sections
        -Wall -Wno-psabi)
    # -Wl,-q keeps relocations; vita-elf-create needs them for a valid velf.
    # __sce_headroom (read by the vitasdk linker script) leaves room after the
    # text segment for the SCE module info vita-elf-create appends there;
    # without it the build fails whenever the text happens to end within
    # ~3.5 KB of the 64 KB-aligned data segment.
    target_link_options(swgb PRIVATE -Wl,-q,--gc-sections
        -Wl,--defsym,__sce_headroom=0x4000)
    # The system on-screen keyboard for typed chat.
    target_compile_definitions(swgb PRIVATE SWGB_ENABLE_IME=1)
    target_link_libraries(swgb PRIVATE
        vitaGL vitashark ${SWGB_SHACCCG_EXT_LIBS} mathneon z
        SceGxm_stub SceDisplay_stub SceCtrl_stub SceTouch_stub SceHid_stub
        SceAudio_stub SceCommonDialog_stub SceSysmodule_stub SceMotion_stub
        SceAppUtil_stub SceAppMgr_stub SceShaccCg_stub ScePower_stub
        SceKernelDmacMgr_stub SceIme_stub SceAvPlayer_stub
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
    # The PC game: the Vita front end and main loop on SDL2 + OpenGL
    # (src/platform/pc: SDL implementations of the Vita calls it makes).
    find_package(SDL2 QUIET)
    find_package(OpenGL QUIET)
    find_library(SWGB_VORBISFILE vorbisfile)
    if(SDL2_FOUND AND OPENGL_FOUND AND SWGB_VORBISFILE)
        add_executable(swgb_pc
            ${SWGB_CORE_SOURCES}
            src/platform/vita/main_vita.cpp
            src/platform/vita/vita_audio.cpp
            src/platform/pc/pc_shim.cpp
            src/platform/pc/pc_video_stub.cpp
            src/render/gl_renderer.cpp
        )
        target_compile_definitions(swgb_pc PRIVATE SWGB_PC=1 SWGB_ROOT=".")
        target_include_directories(swgb_pc PRIVATE src/platform/pc/shim)
        if(TARGET SDL2::SDL2)
            # SDL2main (WinMain -> SDL_main) must come before SDL2; MinGW
            # also needs libmingw32 first.
            if(MINGW)
                target_link_libraries(swgb_pc PRIVATE mingw32)
            endif()
            if(TARGET SDL2::SDL2main)
                target_link_libraries(swgb_pc PRIVATE SDL2::SDL2main)
            endif()
            target_link_libraries(swgb_pc PRIVATE SDL2::SDL2)
        else()
            target_include_directories(swgb_pc PRIVATE ${SDL2_INCLUDE_DIRS})
            target_link_libraries(swgb_pc PRIVATE ${SDL2_LIBRARIES})
        endif()
        find_library(SWGB_VORBIS vorbis)
        find_library(SWGB_OGG ogg)
        find_package(Threads REQUIRED)
        target_link_libraries(swgb_pc PRIVATE OpenGL::GL ZLIB::ZLIB ${SWGB_VORBISFILE} ${SWGB_VORBIS}
                              ${SWGB_OGG} Threads::Threads)
        if(WIN32)
            set_target_properties(swgb_pc PROPERTIES WIN32_EXECUTABLE ON)
        endif()
    endif()
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
