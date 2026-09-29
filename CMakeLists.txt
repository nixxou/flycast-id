# flycast-id, built against a Flycast checkout rather than vendoring it (the flycast submodule, or any
# checkout given as FLYCAST_SOURCE_DIR).
#
# The id Flycast gives a game - the name of its per-game section in emu.cfg, [<ID>] - read the way Flycast
# reads it (see main.cpp and README.md): an arcade set found in Flycast's own table (naomi_roms.cpp), its
# files mapped from the zip or 7z by CRC or name, the cartridge built and decrypted (M1, M2, M4, Atomiswave,
# the GD-ROM image of a DIMM game, .chd beside the set), then the boot id read out of it
# (hw/naomi/naomi_cart.cpp); a legacy decrypted rom (.bin .dat .lst); a Dreamcast disc's IP.BIN. One line out:
#
#   flycast-id.exe <rom path>        ->  id<TAB><the id, bytes as Flycast has them>, exit 0
#                                    ->  a reason on stderr, exit 1
#
# Flycast's code, so GPL-2.0 like it (LICENSE).
#
# FLYCAST'S SOURCES ARE COMPILED UNMODIFIED, from the checkout - the cartridge classes, the ROM table, the
# archive readers, the disc readers, libchdr, libzip. One part of naomi_cart.cpp is taken out of it at
# configure time, between two markers (the Cartridge / NaomiCartridge / M2Cartridge methods): the rest of that
# file is the emulator's cartridge slot, which reaches half of the machine. What those sources call outside
# of themselves is supplied by stubs.cpp - found by linking, not guessed.
#
#   git submodule update --init flycast
#   git -C flycast submodule update --init core/deps/libchdr
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build

cmake_minimum_required(VERSION 3.20)
project(flycast-id C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# One file, no redistributable beside it.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The submodule by default; relative to where cmake is run from otherwise.
if (NOT DEFINED FLYCAST_SOURCE_DIR)
    set(FLYCAST_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/flycast")
endif()
get_filename_component(FLYCAST_SOURCE_DIR "${FLYCAST_SOURCE_DIR}" ABSOLUTE)
if (NOT EXISTS "${FLYCAST_SOURCE_DIR}/core/emulator.cpp")
    message(FATAL_ERROR "No Flycast checkout at ${FLYCAST_SOURCE_DIR}: git submodule update --init flycast, or pass -DFLYCAST_SOURCE_DIR=<a checkout>")
endif()
set(FC "${FLYCAST_SOURCE_DIR}/core")
if (NOT EXISTS "${FC}/deps/libchdr/include/libchdr/chd.h")
    message(FATAL_ERROR "libchdr is missing: git -C \"${FLYCAST_SOURCE_DIR}\" submodule update --init core/deps/libchdr")
endif()

# ── the libraries, as Flycast's own CMakeLists.txt sets them up ─────────
option(ZLIB_BUILD_EXAMPLES "Enable Zlib Examples" OFF)
add_subdirectory("${FC}/deps/libchdr/deps/zlib-1.3.1" zlib EXCLUDE_FROM_ALL)
set_target_properties(zlibstatic PROPERTIES POSITION_INDEPENDENT_CODE ON)
set(ZLIB_INCLUDE_DIR "${FC}/deps/libchdr/deps/zlib-1.3.1" "${CMAKE_CURRENT_BINARY_DIR}/zlib")

option(ZSTD_BUILD_SHARED "BUILD SHARED LIBRARIES" OFF)
option(ZSTD_BUILD_PROGRAMS "BUILD PROGRAMS" OFF)
option(ZSTD_LEGACY_SUPPORT "LEGACY SUPPORT" OFF)
add_subdirectory("${FC}/deps/libchdr/deps/zstd-1.5.6/build/cmake" zstd EXCLUDE_FROM_ALL)
option(WITH_SYSTEM_ZSTD "Use system provided zstd library" ON)
add_subdirectory("${FC}/deps/libchdr" libchdr EXCLUDE_FROM_ALL)

option(ENABLE_OPENSSL "Enable use of OpenSSL" OFF)
option(ENABLE_WINDOWS_CRYPTO "Enable use of Windows cryptography libraries" OFF)
option(ENABLE_BZIP2 "Enable use of BZip2" OFF)
option(ENABLE_LZMA "Enable use of LZMA" OFF)
option(ENABLE_ZSTD "Enable use of Zstandard" OFF)
option(BUILD_TOOLS "Build tools in the src directory (zipcmp, zipmerge, ziptool)" OFF)
option(BUILD_REGRESS "Build regression tests" OFF)
option(BUILD_EXAMPLES "Build examples" OFF)
option(BUILD_DOC "Build documentation" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(LIBZIP_DO_INSTALL "Install libzip and the related files" OFF)
add_subdirectory("${FC}/deps/libzip" libzip EXCLUDE_FROM_ALL)

# ── the one part of naomi_cart.cpp: between the two markers ────────────
file(READ "${FC}/hw/naomi/naomi_cart.cpp" NAOMI_CART)
string(FIND "${NAOMI_CART}" "Cartridge::Cartridge(u32 size)" FROM)
string(FIND "${NAOMI_CART}" "void M2Cartridge::Deserialize(Deserializer& deser)" TO)
if (FROM EQUAL -1 OR TO EQUAL -1)
    message(FATAL_ERROR "naomi_cart.cpp: the Cartridge methods were not found between their markers - Flycast changed, look again")
endif()
string(SUBSTRING "${NAOMI_CART}" ${TO} -1 TAIL)
string(FIND "${TAIL}" "\n}" END)
math(EXPR LENGTH "${TO} - ${FROM} + ${END} + 2")
string(SUBSTRING "${NAOMI_CART}" ${FROM} ${LENGTH} METHODS)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cart_methods.cpp"
     "// Taken at configure time from ${FC}/hw/naomi/naomi_cart.cpp - do not edit, see CMakeLists.txt.\n"
     "#include \"cart_methods_prelude.h\"\n\n${METHODS}\n")

# The lzma files Flycast itself compiles (its CMakeLists.txt), for the .7z sets.
set(LZMA_SOURCES)
foreach (f 7zArcIn 7zBuf 7zCrc 7zCrcOpt 7zDec 7zFile 7zStream Alloc Bcj2 Bra86 Bra BraIA64 CpuArch Delta LzFind Lzma2Dec Lzma86Dec LzmaDec Sort)
    list(APPEND LZMA_SOURCES "${FC}/deps/lzma/${f}.c")
endforeach()

add_subdirectory("${FC}/deps/nowide" nowide EXCLUDE_FROM_ALL)

add_executable(flycast-id
    main.cpp
    stubs.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/cart_methods.cpp"
    "${FC}/hw/naomi/naomi_roms.cpp"
    "${FC}/hw/naomi/decrypt.cpp"
    "${FC}/hw/naomi/m1cartridge.cpp"
    "${FC}/hw/naomi/m4cartridge.cpp"
    "${FC}/hw/naomi/awcartridge.cpp"
    "${FC}/hw/naomi/gdcartridge.cpp"
    "${FC}/archive/archive.cpp"
    "${FC}/archive/ZipArchive.cpp"
    "${FC}/archive/7zArchive.cpp"
    "${FC}/archive/rzip.cpp"
    "${FC}/imgread/common.cpp"
    "${FC}/imgread/chd.cpp"
    "${FC}/imgread/gdi.cpp"
    "${FC}/imgread/cdi.cpp"
    "${FC}/imgread/cue.cpp"
    "${FC}/imgread/ImgReader.cpp"
    "${FC}/imgread/isofs.cpp"
    "${FC}/deps/chdpsr/cdipsr.cpp"
    "${FC}/deps/md5/md5.cpp"
    "${FC}/oslib/storage.cpp"
    "${FC}/oslib/directory.cpp"
    ${LZMA_SOURCES})

target_include_directories(flycast-id PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${FC}" "${FC}/deps" "${FC}/deps/libchdr/include" "${FC}/deps/dirent")
target_compile_definitions(flycast-id PRIVATE
    _CRT_NONSTDC_NO_WARNINGS _CRT_SECURE_NO_WARNINGS NOMINMAX _USE_MATH_DEFINES _WIN32_WINNT=0x0601 WINVER=0x0601)
if (MSVC)
    target_compile_options(flycast-id PRIVATE /GR /GS- /utf-8 /W0)
endif()
target_link_libraries(flycast-id PRIVATE chdr-static libzstd_static zlibstatic libzip::zip nowide::nowide)
