include(CMakeParseArguments)
include(CompilerRTUtils)
include(BuiltinTests)

set(CMAKE_LIPO "lipo" CACHE PATH "path to the lipo tool")

# On OS X SDKs can be installed anywhere on the base system and xcode-select can
# set the default Xcode to use. This function finds the SDKs that are present in
# the current Xcode.
function(find_darwin_sdk_dir var sdk_name)
  set(DARWIN_${sdk_name}_CACHED_SYSROOT "" CACHE STRING "Darwin SDK path for SDK ${sdk_name}.")
  set(DARWIN_PREFER_PUBLIC_SDK OFF CACHE BOOL "Prefer Darwin public SDK, even when an internal SDK is present.")

  if(DARWIN_${sdk_name}_CACHED_SYSROOT)
    set(${var} ${DARWIN_${sdk_name}_CACHED_SYSROOT} PARENT_SCOPE)
    return()
  endif()
  if(NOT DARWIN_PREFER_PUBLIC_SDK)
    # Let's first try the internal SDK, otherwise use the public SDK.
    execute_process(
      COMMAND xcrun --sdk ${sdk_name}.internal --show-sdk-path
      RESULT_VARIABLE result_process
      OUTPUT_VARIABLE var_internal
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_FILE /dev/null
    )
  endif()
  if((NOT result_process EQUAL 0) OR "" STREQUAL "${var_internal}")
    execute_process(
      COMMAND xcrun --sdk ${sdk_name} --show-sdk-path
      RESULT_VARIABLE result_process
      OUTPUT_VARIABLE var_internal
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_FILE /dev/null
    )
  else()
    set(${var}_INTERNAL ${var_internal} PARENT_SCOPE)
  endif()
  if(result_process EQUAL 0)
    set(${var} ${var_internal} PARENT_SCOPE)
  endif()
  message(STATUS "Checking DARWIN_${sdk_name}_SYSROOT - '${var_internal}'")
  set(DARWIN_${sdk_name}_CACHED_SYSROOT ${var_internal} CACHE STRING "Darwin SDK path for SDK ${sdk_name}." FORCE)
endfunction()

function(find_darwin_sdk_version var sdk_name)
  if (DARWIN_${sdk_name}_OVERRIDE_SDK_VERSION)
    message(WARNING "Overriding ${sdk_name} SDK version to ${DARWIN_${sdk_name}_OVERRIDE_SDK_VERSION}")
    set(${var} "${DARWIN_${sdk_name}_OVERRIDE_SDK_VERSION}" PARENT_SCOPE)
    return()
  endif()
  set(result_process 1)
  if(NOT DARWIN_PREFER_PUBLIC_SDK)
    # Let's first try the internal SDK, otherwise use the public SDK.
    execute_process(
      COMMAND xcrun --sdk ${sdk_name}.internal --show-sdk-version
      RESULT_VARIABLE result_process
      OUTPUT_VARIABLE var_internal
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_FILE /dev/null
    )
  endif()
  if((NOT ${result_process} EQUAL 0) OR "" STREQUAL "${var_internal}")
    execute_process(
      COMMAND xcrun --sdk ${sdk_name} --show-sdk-version
      RESULT_VARIABLE result_process
      OUTPUT_VARIABLE var_internal
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_FILE /dev/null
    )
  endif()
  if(NOT result_process EQUAL 0)
    message(FATAL_ERROR
      "Failed to determine SDK version for \"${sdk_name}\" SDK")
  endif()
  # Check reported version looks sane.
  if (NOT "${var_internal}" MATCHES "^[0-9]+\\.[0-9]+(\\.[0-9]+)?$")
    message(FATAL_ERROR
      "Reported SDK version \"${var_internal}\" does not look like a version")
  endif()
  set(${var} ${var_internal} PARENT_SCOPE)
endfunction()

# There isn't a clear mapping of what architectures are supported with a given
# target platform, but ld's version output does list the architectures it can
# link for.
function(darwin_get_toolchain_supported_archs output_var)
  execute_process(
    COMMAND "${CMAKE_LINKER}" -v
    ERROR_VARIABLE LINKER_VERSION)

  string(REGEX MATCH "configured to support archs: ([^\n]+)"
         ARCHES_MATCHED "${LINKER_VERSION}")
  if(ARCHES_MATCHED)
    set(ARCHES "${CMAKE_MATCH_1}")
    message(STATUS "Got ld supported ARCHES: ${ARCHES}")
    string(REPLACE " " ";" ARCHES ${ARCHES})
  else()
    # If auto-detecting fails, fall back to a default set
    message(WARNING "Detecting supported architectures from 'ld -v' failed. Returning default set.")
    set(ARCHES "i386;x86_64;armv7;armv7s;arm64")
  endif()
  set(${output_var} ${ARCHES} PARENT_SCOPE)
endfunction()

# This function takes an OS and a list of architectures and identifies the
# subset of the architectures list that the installed toolchain can target.
function(darwin_test_archs os valid_archs)
  if(${valid_archs})
    message(STATUS "Using cached valid architectures for ${os}.")
    return()
  endif()

  set(archs ${ARGN})
  if(NOT TEST_COMPILE_ONLY)
    message(STATUS "Finding valid architectures for ${os}...")
    set(SIMPLE_C ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/src.c)
    file(WRITE ${SIMPLE_C} "#include <stdio.h>\n#include <unistd.h>\nint main(void) { printf(__FILE__); fork(); return 0; }\n")

    set(os_linker_flags)
    foreach(flag ${DARWIN_${os}_LINK_FLAGS})
      set(os_linker_flags "${os_linker_flags} ${flag}")
    endforeach()

    # Disable building for i386 for macOS SDK >= 10.15. The SDK doesn't support
    # linking for i386 and the corresponding OS doesn't allow running macOS i386
    # binaries.
    if ("${os}" STREQUAL "osx")
      find_darwin_sdk_version(macosx_sdk_version "macosx")
      if ("${macosx_sdk_version}" VERSION_GREATER 10.15 OR "${macosx_sdk_version}" VERSION_EQUAL 10.15)
        message(STATUS "Disabling i386 slice for ${valid_archs}")
        list(REMOVE_ITEM archs "i386")
      endif()
    endif()
  endif()

  # The simple program will build for x86_64h on the simulator because it is
  # compatible with x86_64 libraries (mostly), but since x86_64h isn't actually
  # a valid or useful architecture for the iOS simulator we should drop it.
  if(${os} MATCHES "^(iossim|tvossim|watchossim)$")
    list(REMOVE_ITEM archs "x86_64h")
  endif()

  if(${os} MATCHES "iossim")
    message(STATUS "Disabling i386 slice for iossim")
    list(REMOVE_ITEM archs "i386")
  endif()

  if(${os} MATCHES "^ios$")
    message(STATUS "Disabling sanitizers armv7* slice for ios")
    list(FILTER archs EXCLUDE REGEX "armv7.*")
  endif()

  set(working_archs)
  foreach(arch ${archs})

    set(arch_linker_flags "-arch ${arch} ${os_linker_flags}")
    if(TEST_COMPILE_ONLY)
      # `-w` is used to surpress compiler warnings which `try_compile_only()` treats as an error.
      try_compile_only(CAN_TARGET_${os}_${arch} FLAGS -v -arch ${arch} ${DARWIN_${os}_CFLAGS} -w)
    else()
      set(SAVED_CMAKE_EXE_LINKER_FLAGS ${CMAKE_EXE_LINKER_FLAGS})
      set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${arch_linker_flags}")
      try_compile(CAN_TARGET_${os}_${arch} ${CMAKE_BINARY_DIR} ${SIMPLE_C}
                  COMPILE_DEFINITIONS "-v -arch ${arch}" ${DARWIN_${os}_CFLAGS}
                  OUTPUT_VARIABLE TEST_OUTPUT)
      set(CMAKE_EXE_LINKER_FLAGS ${SAVED_CMAKE_EXE_LINKER_FLAGS})
    endif()
    if(${CAN_TARGET_${os}_${arch}})
      list(APPEND working_archs ${arch})
    else()
      file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
        "Testing compiler for supporting ${os}-${arch}:\n"
        "${TEST_OUTPUT}\n")
    endif()
  endforeach()
  set(${valid_archs} ${working_archs}
    CACHE STRING "List of valid architectures for platform ${os}." FORCE)
endfunction()

# This function checks the host cputype/cpusubtype to filter supported
# architecture for the host OS. This is used to determine which tests are
# available for the host.
function(darwin_filter_host_archs input output)
  list_intersect(tmp_var DARWIN_osx_ARCHS ${input})
  execute_process(
    COMMAND sysctl hw.cputype
    OUTPUT_VARIABLE CPUTYPE)
  string(REGEX MATCH "hw.cputype: ([0-9]*)"
         CPUTYPE_MATCHED "${CPUTYPE}")
  set(ARM_HOST Off)
  if(CPUTYPE_MATCHED)
    # ARM cputype is (0x01000000 | 12) and X86(_64) is always 7.
    if(${CMAKE_MATCH_1} GREATER 11)
      set(ARM_HOST On)
    endif()
  endif()

  if(ARM_HOST)
    list(REMOVE_ITEM tmp_var i386)
    list(REMOVE_ITEM tmp_var x86_64)
    list(REMOVE_ITEM tmp_var x86_64h)
  else()
    list(REMOVE_ITEM tmp_var arm64)
    list(REMOVE_ITEM tmp_var arm64e)
    execute_process(
      COMMAND sysctl hw.cpusubtype
      OUTPUT_VARIABLE SUBTYPE)
    string(REGEX MATCH "hw.cpusubtype: ([0-9]*)"
           SUBTYPE_MATCHED "${SUBTYPE}")

    set(HASWELL_SUPPORTED Off)
    if(SUBTYPE_MATCHED)
      if(${CMAKE_MATCH_1} GREATER 7)
        set(HASWELL_SUPPORTED On)
      endif()
    endif()
    if(NOT HASWELL_SUPPORTED)
      list(REMOVE_ITEM tmp_var x86_64h)
    endif()
  endif()

  set(${output} ${tmp_var} PARENT_SCOPE)
endfunction()

# Read and process the exclude file into a list of symbols
function(darwin_read_list_from_file output_var file)
  if(EXISTS ${file})
    file(READ ${file} EXCLUDES)
    string(REPLACE "\n" ";" EXCLUDES ${EXCLUDES})
    set(${output_var} ${EXCLUDES} PARENT_SCOPE)
  endif()
endfunction()

# this function takes an OS, architecture and minimum version and provides a
# list of builtin functions to exclude
function(darwin_find_excluded_builtins_list output_var)
  cmake_parse_arguments(LIB
    ""
    "OS;ARCH;MIN_VERSION"
    ""
    ${ARGN})

  if(NOT LIB_OS OR NOT LIB_ARCH)
    message(FATAL_ERROR "Must specify OS and ARCH to darwin_find_excluded_builtins_list!")
  endif()

  darwin_read_list_from_file(${LIB_OS}_BUILTINS
    ${DARWIN_EXCLUDE_DIR}/${LIB_OS}.txt)
  darwin_read_list_from_file(${LIB_OS}_${LIB_ARCH}_BASE_BUILTINS
    ${DARWIN_EXCLUDE_DIR}/${LIB_OS}-${LIB_ARCH}.txt)

  if(LIB_MIN_VERSION)
    file(GLOB builtin_lists ${DARWIN_EXCLUDE_DIR}/${LIB_OS}*-${LIB_ARCH}.txt)
    foreach(builtin_list ${builtin_lists})
      string(REGEX MATCH "${LIB_OS}([0-9\\.]*)-${LIB_ARCH}.txt" VERSION_MATCHED "${builtin_list}")
      if (VERSION_MATCHED AND NOT CMAKE_MATCH_1 VERSION_LESS LIB_MIN_VERSION)
        if(NOT smallest_version)
          set(smallest_version ${CMAKE_MATCH_1})
        elseif(CMAKE_MATCH_1 VERSION_LESS smallest_version)
          set(smallest_version ${CMAKE_MATCH_1})
        endif()
      endif()
    endforeach()

    if(smallest_version)
      darwin_read_list_from_file(${LIB_ARCH}_${LIB_OS}_BUILTINS
        ${DARWIN_EXCLUDE_DIR}/${LIB_OS}${smallest_version}-${LIB_ARCH}.txt)
    endif()
  endif()

  set(${output_var}
      ${${LIB_ARCH}_${LIB_OS}_BUILTINS}
      ${${LIB_OS}_${LIB_ARCH}_BASE_BUILTINS}
      ${${LIB_OS}_BUILTINS} PARENT_SCOPE)
endfunction()

# adds a single builtin library for a single OS & ARCH
macro(darwin_add_builtin_library name suffix)
  cmake_parse_arguments(LIB
    ""
    "PARENT_TARGET;OS;ARCH"
    "SOURCES;CFLAGS;DEFS;INCLUDE_DIRS"
    ${ARGN})
  set(libname "${name}.${suffix}_${LIB_ARCH}_${LIB_OS}")
  add_library(${libname} STATIC ${LIB_SOURCES})
  if(DARWIN_${LIB_OS}_SYSROOT)
    set(sysroot_flag -isysroot ${DARWIN_${LIB_OS}_SYSROOT})
  endif()

  # Make a copy of the compilation flags.
  set(builtin_cflags ${LIB_CFLAGS})

  # Strip out any inappropriate flags for the target.
  if("${LIB_ARCH}" MATCHES "^(armv7|armv7k|armv7s)$")
    set(builtin_cflags "")
    foreach(cflag "${LIB_CFLAGS}")
      string(REPLACE "-fomit-frame-pointer" "" cflag "${cflag}")
      list(APPEND builtin_cflags ${cflag})
    endforeach(cflag)
  endif()

  if ("${LIB_OS}" MATCHES ".*sim$")
    # Pass an explicit -simulator environment to the -target option to ensure
    # that we don't rely on the architecture to infer whether we're building
    # for the simulator.
    string(REGEX REPLACE "sim" "" base_os "${LIB_OS}")
    list(APPEND builtin_cflags
         -target "${LIB_ARCH}-apple-${base_os}${DARWIN_${LIBOS}_BUILTIN_MIN_VER}-simulator")
  endif()

  if ("${COMPILER_RT_ENABLE_MACCATALYST}" AND
      "${LIB_OS}" MATCHES "^osx$")
    # Build the macOS builtins with Mac Catalyst support.
    list(APPEND builtin_cflags
      "SHELL:-target ${LIB_ARCH}-apple-macos${DARWIN_osx_BUILTIN_MIN_VER} -darwin-target-variant ${LIB_ARCH}-apple-ios13.1-macabi")
  endif()

  set_target_compile_flags(${libname}
    ${sysroot_flag}
    ${DARWIN_${LIB_OS}_BUILTIN_MIN_VER_FLAG}
    ${builtin_cflags})
  target_include_directories(${libname}
    PRIVATE ${LIB_INCLUDE_DIRS})
  set_property(TARGET ${libname} APPEND PROPERTY
      COMPILE_DEFINITIONS ${LIB_DEFS})
  set_target_properties(${libname} PROPERTIES
      OUTPUT_NAME ${libname}${COMPILER_RT_OS_SUFFIX})
  set_target_properties(${libname} PROPERTIES
    OSX_ARCHITECTURES ${LIB_ARCH})

  if(LIB_PARENT_TARGET)
    add_dependencies(${LIB_PARENT_TARGET} ${libname})
  endif()

  list(APPEND ${LIB_OS}_${suffix}_libs ${libname})
  list(APPEND ${LIB_OS}_${suffix}_lipo_flags -arch ${arch} $<TARGET_FILE:${libname}>)
  set_target_properties(${libname} PROPERTIES FOLDER "Compiler-RT/Libraries")
endmacro()

function(darwin_lipo_libs name)
  cmake_parse_arguments(LIB
    ""
    "PARENT_TARGET;OUTPUT_DIR;INSTALL_DIR"
    "LIPO_FLAGS;DEPENDS"
    ${ARGN})
  if(LIB_DEPENDS AND LIB_LIPO_FLAGS)
    add_custom_command(OUTPUT ${LIB_OUTPUT_DIR}/lib${name}.a
      COMMAND ${CMAKE_COMMAND} -E make_directory ${LIB_OUTPUT_DIR}
      COMMAND ${CMAKE_LIPO} -output
              ${LIB_OUTPUT_DIR}/lib${name}.a
              -create ${LIB_LIPO_FLAGS}
      DEPENDS ${LIB_DEPENDS}
      )
    add_custom_target(${name}
      DEPENDS ${LIB_OUTPUT_DIR}/lib${name}.a)
    set_target_properties(${name} PROPERTIES FOLDER "Compiler-RT/Misc")
    add_dependencies(${LIB_PARENT_TARGET} ${name})

    if(CMAKE_CONFIGURATION_TYPES)
      set(install_component ${LIB_PARENT_TARGET})
    else()
      set(install_component ${name})
    endif()
    install(FILES ${LIB_OUTPUT_DIR}/lib${name}.a
      DESTINATION ${LIB_INSTALL_DIR}
      COMPONENT ${install_component})
    add_compiler_rt_install_targets(${name} PARENT_TARGET ${LIB_PARENT_TARGET})
  else()
    message(WARNING "Not generating lipo target for ${name} because no input libraries exist.")
  endif()
endfunction()

# Filter the list of builtin sources for Darwin, then delegate to the generic
# filtering.
#
# `exclude_or_include` must be one of:
#  - EXCLUDE: remove every item whose name (w/o extension) matches a name in
#    `excluded_list`.
#  - INCLUDE: keep only items whose name (w/o extension) matches something
#    in `excluded_list`.
function(darwin_filter_builtin_sources output_var name exclude_or_include excluded_list)
  if(exclude_or_include STREQUAL "EXCLUDE")
    set(filter_action GREATER)
    set(filter_value -1)
  elseif(exclude_or_include STREQUAL "INCLUDE")
    set(filter_action LESS)
    set(filter_value 0)
  else()
    message(FATAL_ERROR "darwin_filter_builtin_sources called without EXCLUDE|INCLUDE")
  endif()

  set(intermediate ${ARGN})
  foreach(_file ${intermediate})
    get_filename_component(_name_we ${_file} NAME_WE)
    list(FIND ${excluded_list} ${_name_we} _found)
    if(_found ${filter_action} ${filter_value})
      list(REMOVE_ITEM intermediate ${_file})
    endif()
  endforeach()

  filter_builtin_sources(intermediate ${name})
  set(${output_var} ${intermediate} PARENT_SCOPE)
endfunction()

# Generates builtin libraries for all operating systems specified in ARGN. Each
# OS library is constructed by lipo-ing together single-architecture libraries.
macro(darwin_add_builtin_libraries)
  set(DARWIN_EXCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Darwin-excludes)

  set(CFLAGS -fPIC -O3 -fvisibility=hidden -DVISIBILITY_HIDDEN -Wall -fomit-frame-pointer)
  set(CMAKE_C_FLAGS "")
  set(CMAKE_CXX_FLAGS "")
  set(CMAKE_ASM_FLAGS "")

  append_list_if(COMPILER_RT_HAS_ASM_LSE -DHAS_ASM_LSE CFLAGS)

  set(PROFILE_SOURCES ../profile/InstrProfiling.c
                      ../profile/InstrProfilingBuffer.c
                      ../profile/InstrProfilingPlatformDarwin.c
                      ../profile/InstrProfilingWriter.c
                      ../profile/InstrProfilingInternal.c
                      ../profile/InstrProfilingVersionVar.c)
  foreach (os ${ARGN})
    set(macosx_sdk_version 99999)
    if ("${os}" STREQUAL "osx")
      find_darwin_sdk_version(macosx_sdk_version "macosx")
    endif()
    add_security_warnings(CFLAGS ${macosx_sdk_version})

    list_intersect(DARWIN_BUILTIN_ARCHS DARWIN_${os}_BUILTIN_ARCHS BUILTIN_SUPPORTED_ARCH)

    if((arm64 IN_LIST DARWIN_BUILTIN_ARCHS OR arm64e IN_LIST DARWIN_BUILTIN_ARCHS) AND NOT TARGET lse_builtin_symlinks)
      add_custom_target(
        lse_builtin_symlinks
        BYPRODUCTS ${lse_builtins}
        ${arm64_lse_commands}
      )

      set(deps_arm64 lse_builtin_symlinks)
      set(deps_arm64e lse_builtin_symlinks)
    endif()

    foreach (arch ${DARWIN_BUILTIN_ARCHS})
      darwin_find_excluded_builtins_list(${arch}_${os}_EXCLUDED_BUILTINS
                              OS ${os}
                              ARCH ${arch}
                              MIN_VERSION ${DARWIN_${os}_BUILTIN_MIN_VER})
      check_c_source_compiles("_Float16 foo(_Float16 x) { return x; }"
                              COMPILER_RT_HAS_${arch}_FLOAT16)
      append_list_if(COMPILER_RT_HAS_${arch}_FLOAT16 -DCOMPILER_RT_HAS_FLOAT16 BUILTIN_CFLAGS_${arch})
      check_c_source_compiles("__bf16 foo(__bf16 x) { return x; }"
                              COMPILER_RT_HAS_${arch}_BFLOAT16)
      # Build BF16 files only when "__bf16" is available.
      if(COMPILER_RT_HAS_${arch}_BFLOAT16)
        list(APPEND ${arch}_SOURCES ${BF16_SOURCES})
      endif()
      darwin_filter_builtin_sources(filtered_sources
        ${os}_${arch}
        EXCLUDE ${arch}_${os}_EXCLUDED_BUILTINS
        ${${arch}_SOURCES})

      darwin_add_builtin_library(clang_rt builtins
                              OS ${os}
                              ARCH ${arch}
                              DEPS ${deps_${arch}}
                              SOURCES ${filtered_sources}
                              CFLAGS ${CFLAGS} -arch ${arch}
                              PARENT_TARGET builtins)
    endforeach()

    # Don't build cc_kext libraries for simulator platforms
    if(NOT DARWIN_${os}_SKIP_CC_KEXT)
      foreach (arch ${DARWIN_BUILTIN_ARCHS})
        # By not specifying MIN_VERSION this only reads the OS and OS-arch lists.
        # We don't want to filter out the builtins that are present in libSystem
        # because kexts can't link libSystem.
        darwin_find_excluded_builtins_list(${arch}_${os}_EXCLUDED_BUILTINS
                              OS ${os}
                              ARCH ${arch})

        darwin_filter_builtin_sources(filtered_sources
          cc_kext_${os}_${arch}
          EXCLUDE ${arch}_${os}_EXCLUDED_BUILTINS
          ${${arch}_SOURCES})

        # In addition to the builtins cc_kext includes some profile sources
        darwin_add_builtin_library(clang_rt cc_kext
                                OS ${os}
                                ARCH ${arch}
                                DEPS ${deps_${arch}}
                                SOURCES ${filtered_sources} ${PROFILE_SOURCES}
                                CFLAGS ${CFLAGS} -arch ${arch} -mkernel
                                DEFS KERNEL_USE
                                INCLUDE_DIRS ../../include
                                PARENT_TARGET builtins)
      endforeach()
      set(archive_name clang_rt.cc_kext_${os})
      if(${os} STREQUAL "osx")
        set(archive_name clang_rt.cc_kext)
      endif()
      darwin_lipo_libs(${archive_name}
                      PARENT_TARGET builtins
                      LIPO_FLAGS ${${os}_cc_kext_lipo_flags}
                      DEPENDS ${${os}_cc_kext_libs}
                      OUTPUT_DIR ${COMPILER_RT_OUTPUT_LIBRARY_DIR}
                      INSTALL_DIR ${COMPILER_RT_INSTALL_LIBRARY_DIR})
    endif()
  endforeach()

  foreach (os ${ARGN})
    darwin_lipo_libs(clang_rt.${os}
                     PARENT_TARGET builtins
                     LIPO_FLAGS ${${os}_builtins_lipo_flags}
                     DEPENDS ${${os}_builtins_libs}
                     OUTPUT_DIR ${COMPILER_RT_OUTPUT_LIBRARY_DIR}
                     INSTALL_DIR ${COMPILER_RT_INSTALL_LIBRARY_DIR})
  endforeach()
  darwin_add_embedded_builtin_libraries()
endmacro()

macro(darwin_add_embedded_builtin_libraries)
  # this is a hacky opt-out. If you can't target both intel and arm
  # architectures we bail here.
  set(DARWIN_SOFT_FLOAT_ARCHS armv6m armv7m armv7em armv7)
  set(DARWIN_HARD_FLOAT_ARCHS armv7em armv7)
  if(COMPILER_RT_SUPPORTED_ARCH MATCHES ".*armv.*")
    list(FIND COMPILER_RT_SUPPORTED_ARCH i386 i386_idx)
    if(i386_idx GREATER -1)
      list(APPEND DARWIN_HARD_FLOAT_ARCHS i386)
    endif()

    list(FIND COMPILER_RT_SUPPORTED_ARCH x86_64 x86_64_idx)
    if(x86_64_idx GREATER -1)
      list(APPEND DARWIN_HARD_FLOAT_ARCHS x86_64)
    endif()

    set(MACHO_SYM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/macho_embedded)

    set(CFLAGS -Oz -Wall -fomit-frame-pointer -ffreestanding)
    set(CMAKE_C_FLAGS "")
    set(CMAKE_CXX_FLAGS "")
    set(CMAKE_ASM_FLAGS "")

    set(SOFT_FLOAT_FLAG -mfloat-abi=soft)
    set(HARD_FLOAT_FLAG -mfloat-abi=hard)

    set(ENABLE_PIC Off)
    set(PIC_FLAG -fPIC)
    set(STATIC_FLAG -static)

    set(DARWIN_macho_embedded_ARCHS armv6m armv7m armv7em armv7 i386 x86_64)

    set(DARWIN_macho_embedded_LIBRARY_OUTPUT_DIR
      ${COMPILER_RT_OUTPUT_LIBRARY_DIR}/macho_embedded)
    set(DARWIN_macho_embedded_LIBRARY_INSTALL_DIR
      ${COMPILER_RT_INSTALL_LIBRARY_DIR}/macho_embedded)

    set(CFLAGS_armv7 -target thumbv7-apple-darwin-eabi)
    set(CFLAGS_i386 -march=pentium)

    darwin_read_list_from_file(common_FUNCTIONS ${MACHO_SYM_DIR}/common.txt)
    darwin_read_list_from_file(thumb2_FUNCTIONS ${MACHO_SYM_DIR}/thumb2.txt)
    darwin_read_list_from_file(thumb2_64_FUNCTIONS ${MACHO_SYM_DIR}/thumb2-64.txt)
    darwin_read_list_from_file(arm_FUNCTIONS ${MACHO_SYM_DIR}/arm.txt)
    darwin_read_list_from_file(i386_FUNCTIONS ${MACHO_SYM_DIR}/i386.txt)


    set(armv6m_FUNCTIONS ${common_FUNCTIONS} ${arm_FUNCTIONS})
    set(armv7m_FUNCTIONS ${common_FUNCTIONS} ${arm_FUNCTIONS} ${thumb2_FUNCTIONS})
    set(armv7em_FUNCTIONS ${common_FUNCTIONS} ${arm_FUNCTIONS} ${thumb2_FUNCTIONS})
    set(armv7_FUNCTIONS ${common_FUNCTIONS} ${arm_FUNCTIONS} ${thumb2_FUNCTIONS} ${thumb2_64_FUNCTIONS})
    set(i386_FUNCTIONS ${common_FUNCTIONS} ${i386_FUNCTIONS})
    set(x86_64_FUNCTIONS ${common_FUNCTIONS})

    foreach(arch ${DARWIN_macho_embedded_ARCHS})
      darwin_filter_builtin_sources(${arch}_filtered_sources
        macho_embedded_${arch}
        INCLUDE ${arch}_FUNCTIONS
        ${${arch}_SOURCES})
      if(NOT ${arch}_filtered_sources)
        message(WARNING "${arch}_SOURCES: ${${arch}_SOURCES}")
        message(WARNING "${arch}_FUNCTIONS: ${${arch}_FUNCTIONS}")
        message(FATAL_ERROR "Empty filtered sources!")
      endif()
    endforeach()

    foreach(float_type SOFT HARD)
      foreach(type PIC STATIC)
        string(TOLOWER "${float_type}_${type}" lib_suffix)
        foreach(arch ${DARWIN_${float_type}_FLOAT_ARCHS})
          set(DARWIN_macho_embedded_SYSROOT ${DARWIN_osx_SYSROOT})
          set(float_flag)
          if(${arch} MATCHES "^arm")
            # x86 targets are hard float by default, but the complain about the
            # float ABI flag, so don't pass it unless we're targeting arm.
            set(float_flag ${${float_type}_FLOAT_FLAG})
          endif()
          darwin_add_builtin_library(clang_rt ${lib_suffix}
                                OS macho_embedded
                                ARCH ${arch}
                                SOURCES ${${arch}_filtered_sources}
                                CFLAGS ${CFLAGS} -arch ${arch} ${${type}_FLAG} ${float_flag} ${CFLAGS_${arch}}
                                PARENT_TARGET builtins)
        endforeach()
        foreach(lib ${macho_embedded_${lib_suffix}_libs})
          set_target_properties(${lib} PROPERTIES LINKER_LANGUAGE C)
        endforeach()
        darwin_lipo_libs(clang_rt.${lib_suffix}
                      PARENT_TARGET builtins
                      LIPO_FLAGS ${macho_embedded_${lib_suffix}_lipo_flags}
                      DEPENDS ${macho_embedded_${lib_suffix}_libs}
                      OUTPUT_DIR ${DARWIN_macho_embedded_LIBRARY_OUTPUT_DIR}
                      INSTALL_DIR ${DARWIN_macho_embedded_LIBRARY_INSTALL_DIR})
      endforeach()
    endforeach()
  endif()
endmacro()
