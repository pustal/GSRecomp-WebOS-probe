# WebOSClangToolchain.cmake
#
# CMake toolchain file that builds for LG webOS TVs (32-bit ARM, glibc 2.12)
# with the host's clang instead of the webosbrew toolchain's GCC 12, which
# cannot compile current Dawn. clang uses the webosbrew SDK's sysroot, GCC
# runtime and C++ library, so the result is ABI-compatible with code built by
# the SDK's GCC (like the probe itself).
#
# Set WEBOS_SDK to the unpacked arm-webos-linux-gnueabi_sdk-buildroot directory
# (environment variable or -DWEBOS_SDK=...).
#
# The C++ library is linked statically into shared libraries and executables
# because the TV's libstdc++ is older than the SDK's.

if(NOT WEBOS_SDK)
    set(WEBOS_SDK "$ENV{WEBOS_SDK}")
endif()
if(NOT WEBOS_SDK OR NOT EXISTS "${WEBOS_SDK}/arm-webos-linux-gnueabi/sysroot")
    message(FATAL_ERROR "Set WEBOS_SDK to the arm-webos-linux-gnueabi_sdk-buildroot directory")
endif()
# Pass WEBOS_SDK on to try_compile() projects.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES WEBOS_SDK)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv7l)
set(CMAKE_SYSROOT "${WEBOS_SDK}/arm-webos-linux-gnueabi/sysroot")

set(_webos_triple arm-webos-linux-gnueabi)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET ${_webos_triple})
set(CMAKE_CXX_COMPILER_TARGET ${_webos_triple})
set(CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN "${WEBOS_SDK}")
set(CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN "${WEBOS_SDK}")

# Same ABI as the SDK's GCC (configured --with-float=softfp --with-fpu=vfpv3).
set(_webos_cpu_flags "-march=armv7-a -mfloat-abi=softfp -mfpu=vfpv3 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64")
set(CMAKE_C_FLAGS_INIT "${_webos_cpu_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_webos_cpu_flags}")

set(_webos_link_flags "-fuse-ld=lld -static-libstdc++ -static-libgcc -Wl,--exclude-libs,libstdc++.a:libgcc.a:libgcc_eh.a -Wl,--disable-new-dtags")
# (--disable-new-dtags: DT_RPATH, which wins over the LD_LIBRARY_PATH webOS sets,
# so the bundled SDL-webOS is used instead of the TV's older system SDL.)
# (GSRecomp: the game library calls into the engine program, so the engine's
# own static libraries must stay exported; only the C++ runtime is hidden.)
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_webos_link_flags}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_webos_link_flags}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_webos_link_flags}")

set(CMAKE_AR "${WEBOS_SDK}/bin/${_webos_triple}-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${WEBOS_SDK}/bin/${_webos_triple}-ranlib" CACHE FILEPATH "")
set(CMAKE_STRIP "${WEBOS_SDK}/bin/${_webos_triple}-strip" CACHE FILEPATH "")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ---- GSRecomp probe additions ----
# SDL-webOS lives outside the sysroot (WEBOS_SDL_PREFIX), and the stand-in
# GL/gl.h (compat/) is searched before the sysroot.
if(NOT WEBOS_SDL_PREFIX)
    set(WEBOS_SDL_PREFIX "$ENV{WEBOS_SDL_PREFIX}")
endif()
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES WEBOS_SDL_PREFIX)
list(APPEND CMAKE_FIND_ROOT_PATH "${WEBOS_SDL_PREFIX}")
get_filename_component(_probe_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
string(APPEND CMAKE_C_FLAGS_INIT " -I${_probe_root}/compat")
string(APPEND CMAKE_CXX_FLAGS_INIT " -I${_probe_root}/compat")
