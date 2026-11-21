# Linux and Nintendo Switch: one executable per image with the recompiled code linked in (no game code DLL, no
# compiler beside it), SDL2 for the window, input and sound, OpenGL for drawing. Included from CMakeLists.txt.
#
#   SCPT_GEN_DIR   generated code of default.xbe (default generated/game); a cross build (Switch) takes it from a
#                  native build or tools/regen.sh, which runs xbe_recomp on the host
#   SCPT_SWITCH    set by switch/build.sh: libnx platform layer, NRO
#   SCPT_CHECK_ONLY  CI: the port's own sources compiled, nothing linked and no game needed (tools/ci/check-build.sh)

option(SCPT_SWITCH "Nintendo Switch homebrew build" ${NINTENDO_SWITCH})
# char is signed on x86, where all of this was written, and unsigned on AArch64: keep x86's
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64" OR SCPT_SWITCH)
    add_compile_options(-fsigned-char)
endif()
option(SCPT_VERBOSE_LOG "the debug build's full log in a release build" OFF)
option(SCPT_CHECK_ONLY "compile the port's own sources only: no recompiled code, no executable" OFF)
set(SCPT_GPU null CACHE STRING
    "renderer: vk (Vulkan 1.3 with shader objects), gl (OpenGL 4.3), or null (draws nothing, for testing the rest)")
# the Switch's Vulkan: mesa-switch's static NVK, installed as a portlibs tree (../mesa-build/build-linux.sh)
set(SCPT_NVK_SDK "" CACHE PATH "Switch, SCPT_GPU=vk: the Mesa Horizon SDK's portlibs/switch (lib/libvulkan.a)")
set(SCPT_GEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/generated/game CACHE PATH "generated code of the image")
# the image: its recompiler config, its XBE in assets/ and the folder of its game patches (src/<dir>), which override
# recompiled functions by address and so belong to that image alone
set(SCPT_IMAGE_CONFIG config/scpt.toml CACHE STRING "the image's recompiler config")
set(SCPT_IMAGE_XBE assets/Offline.xbe CACHE STRING "the image's XBE")
set(SCPT_GAME_DIR src/scpt CACHE STRING "the image's game patches")
set(SCPT_PARTITIONS 128)

file(GLOB RECOMP_CONFIGS CONFIGURE_DEPENDS config/*.toml)

# x86 to C++ recompiler, a host tool
if(NOT CMAKE_CROSSCOMPILING)
    file(GLOB RECOMP_SOURCES CONFIGURE_DEPENDS tools/recomp/*.cpp)
    add_executable(xbe_recomp ${RECOMP_SOURCES})
    target_include_directories(xbe_recomp PRIVATE src tools/recomp)
    target_link_libraries(xbe_recomp PRIVATE Zydis tomlplusplus::tomlplusplus)
endif()

set(GEN ${SCPT_GEN_DIR})
set(GEN_SOURCES ${GEN}/x86_func_mapping.cpp)
math(EXPR last "${SCPT_PARTITIONS} - 1")
foreach(i RANGE ${last})
    list(APPEND GEN_SOURCES ${GEN}/x86_recomp.${i}.cpp)
endforeach()
if(SCPT_CHECK_ONLY)
    if(NOT EXISTS ${GEN}/image_symbols.h)
        message(FATAL_ERROR "no image_symbols.h in ${GEN}: tools/ci/symbols_header.py makes it from the config")
    endif()
elseif(NOT CMAKE_CROSSCOMPILING)
    add_custom_command(
        OUTPUT ${GEN}/codegen.stamp
        BYPRODUCTS ${GEN_SOURCES} ${GEN}/x86_recomp_shared.h ${GEN}/image_symbols.h
        COMMAND xbe_recomp ${SCPT_IMAGE_CONFIG} ${SCPT_IMAGE_XBE} ${GEN}
        COMMAND ${CMAKE_COMMAND} -E touch ${GEN}/codegen.stamp
        DEPENDS xbe_recomp ${RECOMP_CONFIGS} ${CMAKE_CURRENT_SOURCE_DIR}/${SCPT_IMAGE_XBE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Recompiling ${SCPT_IMAGE_XBE}"
        VERBATIM)
    add_custom_target(recompile DEPENDS ${GEN}/codegen.stamp)
elseif(NOT EXISTS ${GEN}/image_symbols.h)
    message(FATAL_ERROR "no generated code in ${GEN}: run tools/regen.sh first")
endif()

# the recompiled code: strict aliasing off as on Windows, warnings off (generated), no frame pointer needed. -O3 (after
# the build type's, so it wins): 3.4% less frame time than -O2 on the Switch (JBA headquarters part 1, standing);
# SCPT_CODE_OPT=-O2 for comparing
set(SCPT_CODE_OPT "-O3" CACHE STRING "optimization flags for the recompiled game code")
if(NOT SCPT_CHECK_ONLY)
    add_library(scpt_code OBJECT ${GEN_SOURCES})
    target_include_directories(scpt_code PRIVATE src ${GEN})
    separate_arguments(SCPT_CODE_OPT_LIST UNIX_COMMAND "${SCPT_CODE_OPT}")
    target_compile_options(scpt_code PRIVATE ${SCPT_CODE_OPT_LIST} -fno-strict-aliasing -w)
    # link time optimization (SCPT_CODE_OPT with -flto): the link does it, on every core
    if(SCPT_CODE_OPT MATCHES "-flto")
        set(SCPT_CODE_LTO ON)
    endif()
    if(NOT CMAKE_CROSSCOMPILING)
        add_dependencies(scpt_code recompile)
    endif()
endif()

# the host: what has no Windows parts as it is, the ported files and the port's own layer in src/port
file(GLOB HOST_SOURCES CONFIGURE_DEPENDS src/*/*.cpp)
list(FILTER HOST_SOURCES EXCLUDE REGEX "/src/(game|versus|sc3)/")
# Windows only: the D3D12 renderer and what serves it, XAudio2, the runtime compiler, splash and handover windows,
# the main window (src/port/main.cpp has the SDL one)
list(FILTER HOST_SOURCES EXCLUDE REGEX
    "/src/(gpu/(d3d12|pipeline_cache|texturedumper)|audio/(xaudio2|voice|voice_link)|app/(main|game_code|handover|splash|cursor|focus_pause))\\.cpp$")
# gyro.cpp reads SDL3's sensors, or libnx's on the Switch: elsewhere stubs.cpp answers for it
if(NOT SCPT_SWITCH)
    list(FILTER HOST_SOURCES EXCLUDE REGEX "/src/core/gyro\\.cpp$")
endif()
if(SCPT_SWITCH)
    list(FILTER HOST_SOURCES EXCLUDE REGEX "/src/port/linux_.*\\.cpp$")
else()
    list(FILTER HOST_SOURCES EXCLUDE REGEX "/src/port/switch_.*\\.cpp$")
endif()
# gl: gpu_gl.cpp, with vk_shader.cpp's HLSL to SPIR-V that SPIRV-Cross turns into GLSL; vk: gpu_vk.cpp on vk_device.cpp,
# the SPIR-V as it is
if(SCPT_GPU STREQUAL "null")
    list(FILTER HOST_SOURCES EXCLUDE REGEX "/src/port/(gpu_gl|gpu_vk|gpu_common|gl_|vk_).*\\.cpp$")
elseif(SCPT_GPU STREQUAL "gl")
    list(FILTER HOST_SOURCES EXCLUDE REGEX "/src/port/(gpu_null|gpu_vk|vk_device)\\.cpp$")
elseif(SCPT_GPU STREQUAL "vk")
    list(FILTER HOST_SOURCES EXCLUDE REGEX "/src/port/(gpu_null|gpu_gl|gl_.*)\\.cpp$")
else()
    message(FATAL_ERROR "SCPT_GPU is vk, gl or null, not ${SCPT_GPU}")
endif()
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS ${SCPT_GAME_DIR}/*.cpp)

set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp)

find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)
set(GPU_LIBS)
if(NOT SCPT_GPU STREQUAL "null")
    # built from source for both hosts, the Switch has no packages of them; third_party/ holds checkouts for builds
    # without network
    foreach(dep glslang SPIRV-Cross)
        string(TOUPPER ${dep} DEP)
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/${dep}/CMakeLists.txt)
            set(FETCHCONTENT_SOURCE_DIR_${DEP} ${CMAKE_CURRENT_SOURCE_DIR}/third_party/${dep})
        endif()
    endforeach()
    FetchContent_Declare(glslang GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git GIT_TAG 15.4.0
        GIT_SHALLOW TRUE)
    FetchContent_Declare(SPIRV-Cross GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
        GIT_TAG vulkan-sdk-1.4.321.0 GIT_SHALLOW TRUE)
    set(ENABLE_HLSL ON CACHE BOOL "" FORCE)
    set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
    set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
    set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
    set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
    set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
    set(ENABLE_PCH OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    foreach(off HLSL MSL CPP REFLECT C_API UTIL)
        set(SPIRV_CROSS_ENABLE_${off} OFF CACHE BOOL "" FORCE)
    endforeach()
    set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glslang SPIRV-Cross)
    set(GPU_LIBS glslang SPIRV glslang-default-resource-limits)
    if(SCPT_GPU STREQUAL "gl")
        list(APPEND GPU_LIBS spirv-cross-glsl spirv-cross-core)
    endif()
    if(SCPT_SWITCH AND SCPT_GPU STREQUAL "gl")
        # switch-sdl2's GL is EGL on switch-mesa (nouveau); the functions come from SDL_GL_GetProcAddress
        list(APPEND GPU_LIBS EGL glapi drm_nouveau)
        set(GPU_INCLUDES $ENV{DEVKITPRO}/portlibs/switch/include)
    elseif(SCPT_SWITCH)
        # NVK linked in, no loader; not find_package(Vulkan), its config wants libelf, which devkitPro has not:
        # switch_stubs.c stands in for it and for the EGL that libSDL2.a refers to. Listed twice for the archives to
        # be searched again
        if(NOT EXISTS ${SCPT_NVK_SDK}/lib/libvulkan.a)
            message(FATAL_ERROR "SCPT_NVK_SDK must hold lib/libvulkan.a (mesa-switch's Horizon SDK), not '${SCPT_NVK_SDK}'")
        endif()
        find_library(SCPT_ZSTD zstd PATHS $ENV{DEVKITPRO}/portlibs/switch/lib NO_DEFAULT_PATH)
        set(nvk ${SCPT_NVK_SDK}/lib/libvulkan.a expat $<$<BOOL:${SCPT_ZSTD}>:zstd> z nx)
        list(APPEND GPU_LIBS ${nvk} ${nvk} stdc++ m)
        set(GPU_INCLUDES ${SCPT_NVK_SDK}/include $ENV{DEVKITPRO}/portlibs/switch/include)
        list(APPEND HOST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/port/switch_stubs.c)
    else()
        find_package(Vulkan REQUIRED)
        list(APPEND GPU_LIBS Vulkan::Vulkan)
    endif()
endif()
set(SDL_VULKAN 0)

if(SCPT_CHECK_ONLY)
    add_library(scpt OBJECT ${HOST_SOURCES} ${GAME_SOURCES} ${IMGUI_SOURCES})
else()
    add_executable(scpt ${HOST_SOURCES} ${GAME_SOURCES} ${IMGUI_SOURCES} $<TARGET_OBJECTS:scpt_code>)
endif()
target_include_directories(scpt PRIVATE src src/port/include ${GEN})
target_include_directories(scpt SYSTEM PRIVATE ${imgui_SOURCE_DIR} ${GPU_INCLUDES})
target_compile_options(scpt PRIVATE -fno-strict-aliasing -Wno-unused-parameter)
# ImGui opens no links in a shell: newlib on the Switch has no waitpid or execvp
target_compile_definitions(scpt PRIVATE SCPT_PORT IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
    $<$<BOOL:${SCPT_VERBOSE_LOG}>:SCPT_VERBOSE_LOG>
    "SCPT_CODE_OPT_NAME=\"${SCPT_CODE_OPT}\""
    $<$<STREQUAL:${SCPT_GPU},gl>:SCPT_GL> $<$<STREQUAL:${SCPT_GPU},vk>:SCPT_VK>
    $<$<AND:$<STREQUAL:${SCPT_GPU},vk>,$<NOT:$<BOOL:${SCPT_SWITCH}>>>:SCPT_SDL_VULKAN>)
target_link_libraries(scpt PRIVATE tomlplusplus::tomlplusplus SDL2::SDL2 ${GPU_LIBS} Threads::Threads)
if(SCPT_CODE_LTO)
    target_link_options(scpt PRIVATE -flto=auto)
endif()
if(SCPT_SWITCH)
    target_compile_definitions(scpt PRIVATE SCPT_SWITCH)
    target_link_options(scpt PRIVATE "LINKER:--wrap=SDL_SYS_SetThreadPriority"
        "LINKER:--wrap=threadCreate" "LINKER:--wrap=threadClose"
        "LINKER:--wrap=condvarWaitTimeout"
        # the perf diagnostics' allocation sampling (switch_profile.cpp)
        "LINKER:--wrap=malloc" "LINKER:--wrap=calloc" "LINKER:--wrap=realloc" "LINKER:--wrap=memalign")
endif()
if(SCPT_SWITCH AND NOT SCPT_CHECK_ONLY)
    # the NRO for hbmenu: sdmc:/switch/scpt/scpt.nro
    set(SCPT_NACP ${CMAKE_CURRENT_BINARY_DIR}/scpt.nacp)
    nx_generate_nacp(OUTPUT ${SCPT_NACP} NAME "Splinter Cell: Pandora Tomorrow" AUTHOR "scpt-nx"
        VERSION "${PROJECT_VERSION}")
    # your own 256x256 JPEG for hbmenu, kept out of git: switch/icon.jpg, or one given by path
    set(SCPT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/switch/icon.jpg CACHE FILEPATH "the NRO's hbmenu icon, if it exists")
    if(EXISTS ${SCPT_ICON})
        nx_create_nro(scpt NACP ${SCPT_NACP} ICON ${SCPT_ICON})
    else()
        nx_create_nro(scpt NACP ${SCPT_NACP})
    endif()
endif()
# the xbe's SHA-1: the executable checks the game it finds is the release it was made from
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${SCPT_IMAGE_XBE})
    file(SHA1 ${CMAKE_CURRENT_SOURCE_DIR}/${SCPT_IMAGE_XBE} xbe_sha1)
else()
    set(xbe_sha1 "")
endif()
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/scpt_xbe.cpp CONTENT "extern const char kImageSha1[] = \"${xbe_sha1}\";\n")
target_sources(scpt PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/scpt_xbe.cpp)

# build_info.h, as on Windows
find_package(Git QUIET)
set(BUILD_INFO ${CMAKE_CURRENT_BINARY_DIR}/build_info.h)
add_custom_target(build_info
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_CURRENT_SOURCE_DIR} -DOUT=${BUILD_INFO} -DGIT=${GIT_EXECUTABLE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_info.cmake
    BYPRODUCTS ${BUILD_INFO}
    VERBATIM)
add_dependencies(scpt build_info)
target_include_directories(scpt PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
