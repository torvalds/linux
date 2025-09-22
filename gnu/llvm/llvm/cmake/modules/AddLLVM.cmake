include(GNUInstallDirs)
include(LLVMDistributionSupport)
include(LLVMProcessSources)
include(LLVM-Config)
include(DetermineGCCCompatible)

# get_subproject_title(titlevar)
#   Set ${outvar} to the title of the current LLVM subproject (Clang, MLIR ...)
# 
# The title is set in the subproject's top-level using the variable
# LLVM_SUBPROJECT_TITLE. If it does not exist, it is assumed it is LLVM itself.
# The title is not semantically significant, but use to create folders in
# CMake-generated IDE projects (Visual Studio/XCode).
function(get_subproject_title outvar)
  if (LLVM_SUBPROJECT_TITLE)
    set(${outvar} "${LLVM_SUBPROJECT_TITLE}" PARENT_SCOPE) 
  else ()
    set(${outvar} "LLVM" PARENT_SCOPE)
  endif ()
endfunction(get_subproject_title)

function(llvm_update_compile_flags name)
  get_property(sources TARGET ${name} PROPERTY SOURCES)
  if("${sources}" MATCHES "\\.c(;|$)")
    set(update_src_props ON)
  endif()

  list(APPEND LLVM_COMPILE_CFLAGS " ${LLVM_COMPILE_FLAGS}")

  # LLVM_REQUIRES_EH is an internal flag that individual targets can use to
  # force EH
  if(LLVM_REQUIRES_EH OR LLVM_ENABLE_EH)
    if(NOT (LLVM_REQUIRES_RTTI OR LLVM_ENABLE_RTTI))
      message(AUTHOR_WARNING "Exception handling requires RTTI. Enabling RTTI for ${name}")
      set(LLVM_REQUIRES_RTTI ON)
    endif()
    if(MSVC)
      list(APPEND LLVM_COMPILE_FLAGS "/EHsc")
    endif()
  else()
    if(LLVM_COMPILER_IS_GCC_COMPATIBLE)
      list(APPEND LLVM_COMPILE_FLAGS "-fno-exceptions")
      if(LLVM_ENABLE_UNWIND_TABLES)
        list(APPEND LLVM_COMPILE_FLAGS "-funwind-tables")
      else()
        list(APPEND LLVM_COMPILE_FLAGS "-fno-unwind-tables")
        list(APPEND LLVM_COMPILE_FLAGS "-fno-asynchronous-unwind-tables")
      endif()
    elseif(MSVC)
      list(APPEND LLVM_COMPILE_DEFINITIONS _HAS_EXCEPTIONS=0)
      list(APPEND LLVM_COMPILE_FLAGS "/EHs-c-")
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "XL")
      list(APPEND LLVM_COMPILE_FLAGS "-qnoeh")
    endif()
  endif()

  # LLVM_REQUIRES_RTTI is an internal flag that individual
  # targets can use to force RTTI
  set(LLVM_CONFIG_HAS_RTTI YES CACHE INTERNAL "")
  if(NOT (LLVM_REQUIRES_RTTI OR LLVM_ENABLE_RTTI))
    set(LLVM_CONFIG_HAS_RTTI NO CACHE INTERNAL "")
    list(APPEND LLVM_COMPILE_DEFINITIONS GTEST_HAS_RTTI=0)
    if (LLVM_COMPILER_IS_GCC_COMPATIBLE)
      list(APPEND LLVM_COMPILE_FLAGS "-fno-rtti")
    elseif (MSVC)
      list(APPEND LLVM_COMPILE_FLAGS "/GR-")
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "XL")
      list(APPEND LLVM_COMPILE_FLAGS "-qnortti")
    endif ()
  elseif(MSVC)
    list(APPEND LLVM_COMPILE_FLAGS "/GR")
  endif()

  # Assume that;
  #   - LLVM_COMPILE_FLAGS is list.
  #   - PROPERTY COMPILE_FLAGS is string.
  string(REPLACE ";" " " target_compile_flags " ${LLVM_COMPILE_FLAGS}")
  string(REPLACE ";" " " target_compile_cflags " ${LLVM_COMPILE_CFLAGS}")

  if(update_src_props)
    foreach(fn ${sources})
      get_filename_component(suf ${fn} EXT)
      if("${suf}" STREQUAL ".cpp")
        set_property(SOURCE ${fn} APPEND_STRING PROPERTY
          COMPILE_FLAGS "${target_compile_flags}")
      endif()
      if("${suf}" STREQUAL ".c")
        set_property(SOURCE ${fn} APPEND_STRING PROPERTY
          COMPILE_FLAGS "${target_compile_cflags}")
      endif()
    endforeach()
  else()
    # Update target props, since all sources are C++.
    set_property(TARGET ${name} APPEND_STRING PROPERTY
      COMPILE_FLAGS "${target_compile_flags}")
  endif()

  set_property(TARGET ${name} APPEND PROPERTY COMPILE_DEFINITIONS ${LLVM_COMPILE_DEFINITIONS})
endfunction()

function(add_llvm_symbol_exports target_name export_file)
  if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(native_export_file "${target_name}.exports")
    add_custom_command(OUTPUT ${native_export_file}
      COMMAND sed -e "s/^/_/" < ${export_file} > ${native_export_file}
      DEPENDS ${export_file}
      VERBATIM
      COMMENT "Creating export file for ${target_name}")
    set_property(TARGET ${target_name} APPEND_STRING PROPERTY
                 LINK_FLAGS " -Wl,-exported_symbols_list,\"${CMAKE_CURRENT_BINARY_DIR}/${native_export_file}\"")
  elseif(${CMAKE_SYSTEM_NAME} MATCHES "AIX")
    # FIXME: `-Wl,-bE:` bypasses whatever handling there is in the build
    # compiler driver to defer to the specified export list.
    set(native_export_file "${export_file}")
    set_property(TARGET ${target_name} APPEND_STRING PROPERTY
                 LINK_FLAGS " -Wl,-bE:${export_file}")
  elseif(LLVM_HAVE_LINK_VERSION_SCRIPT)
    # Gold and BFD ld require a version script rather than a plain list.
    set(native_export_file "${target_name}.exports")
    # FIXME: Don't write the "local:" line on OpenBSD.
    # in the export file, also add a linker script to version LLVM symbols (form: LLVM_N.M)
    add_custom_command(OUTPUT ${native_export_file}
      COMMAND "${Python3_EXECUTABLE}" "-c"
      "import sys; \
       lines = ['    ' + l.rstrip() for l in sys.stdin] + ['  local: *;']; \
       print('LLVM_${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR} {'); \
       print('  global:') if len(lines) > 1 else None; \
       print(';\\n'.join(lines) + '\\n};')"
      < ${export_file} > ${native_export_file}
      DEPENDS ${export_file}
      VERBATIM
      COMMENT "Creating export file for ${target_name}")
    if (${LLVM_LINKER_IS_SOLARISLD})
      set_property(TARGET ${target_name} APPEND_STRING PROPERTY
                   LINK_FLAGS "  -Wl,-M,\"${CMAKE_CURRENT_BINARY_DIR}/${native_export_file}\"")
    else()
      set_property(TARGET ${target_name} APPEND_STRING PROPERTY
                   LINK_FLAGS "  -Wl,--version-script,\"${CMAKE_CURRENT_BINARY_DIR}/${native_export_file}\"")
    endif()
  elseif(WIN32)
    set(native_export_file "${target_name}.def")

    add_custom_command(OUTPUT ${native_export_file}
      COMMAND "${Python3_EXECUTABLE}" -c "import sys;print(''.join(['EXPORTS\\n']+sys.stdin.readlines(),))"
        < ${export_file} > ${native_export_file}
      DEPENDS ${export_file}
      VERBATIM
      COMMENT "Creating export file for ${target_name}")
    set(export_file_linker_flag "${CMAKE_CURRENT_BINARY_DIR}/${native_export_file}")
    if(MSVC)
      # cl.exe or clang-cl, i.e. MSVC style command line interface
      set(export_file_linker_flag "/DEF:\"${export_file_linker_flag}\"")
    elseif(CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
      # clang in msvc mode, calling a link.exe/lld-link style linker
      set(export_file_linker_flag "-Wl,/DEF:\"${export_file_linker_flag}\"")
    elseif(MINGW)
      # ${export_file_linker_flag}, which is the plain file name, works as is
      # when passed to the compiler driver, which then passes it on to the
      # linker as an input file.
      set(export_file_linker_flag "\"${export_file_linker_flag}\"")
    else()
      message(FATAL_ERROR "Unsupported Windows toolchain")
    endif()
    set_property(TARGET ${target_name} APPEND_STRING PROPERTY
                 LINK_FLAGS " ${export_file_linker_flag}")
  endif()

  add_custom_target(${target_name}_exports DEPENDS ${native_export_file})
  get_subproject_title(subproject_title)
  set_target_properties(${target_name}_exports PROPERTIES FOLDER "${subproject_title}/API")

  get_property(srcs TARGET ${target_name} PROPERTY SOURCES)
  foreach(src ${srcs})
    get_filename_component(extension ${src} EXT)
    if(extension STREQUAL ".cpp")
      set(first_source_file ${src})
      break()
    endif()
  endforeach()

  # Force re-linking when the exports file changes. Actually, it
  # forces recompilation of the source file. The LINK_DEPENDS target
  # property only works for makefile-based generators.
  # FIXME: This is not safe because this will create the same target
  # ${native_export_file} in several different file:
  # - One where we emitted ${target_name}_exports
  # - One where we emitted the build command for the following object.
  # set_property(SOURCE ${first_source_file} APPEND PROPERTY
  #   OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${native_export_file})

  set_property(DIRECTORY APPEND
    PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${native_export_file})

  add_dependencies(${target_name} ${target_name}_exports)

  # Add dependency to *_exports later -- CMake issue 14747
  list(APPEND LLVM_COMMON_DEPENDS ${target_name}_exports)
  set(LLVM_COMMON_DEPENDS ${LLVM_COMMON_DEPENDS} PARENT_SCOPE)
endfunction(add_llvm_symbol_exports)

if (NOT DEFINED LLVM_LINKER_DETECTED AND NOT WIN32)
  # Detect what linker we have here.
  if(APPLE)
    # Linkers with ld64-compatible flags.
    set(version_flag "-Wl,-v")
  else()
    # Linkers with BFD ld-compatible flags.
    set(version_flag "-Wl,--version")
  endif()

  if (CMAKE_HOST_WIN32)
    set(DEVNULL "NUL")
  else()
    set(DEVNULL "/dev/null")
  endif()

  if(LLVM_USE_LINKER)
    set(command ${CMAKE_C_COMPILER} -fuse-ld=${LLVM_USE_LINKER} ${version_flag} -o ${DEVNULL})
  else()
    separate_arguments(flags UNIX_COMMAND "${CMAKE_EXE_LINKER_FLAGS}")
    set(command ${CMAKE_C_COMPILER} ${flags} ${version_flag} -o ${DEVNULL})
  endif()
  execute_process(
    COMMAND ${command}
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    )

  if(APPLE)
    if("${stderr}" MATCHES "PROGRAM:ld")
      set(LLVM_LINKER_DETECTED YES CACHE INTERNAL "")
      set(LLVM_LINKER_IS_APPLE YES CACHE INTERNAL "")
      message(STATUS "Linker detection: Apple")
    elseif("${stderr}" MATCHES "^LLD" OR
           "${stdout}" MATCHES "^LLD")
      set(LLVM_LINKER_DETECTED YES CACHE INTERNAL "")
      set(LLVM_LINKER_IS_LLD YES CACHE INTERNAL "")
      message(STATUS "Linker detection: lld")
    else()
      set(LLVM_LINKER_DETECTED NO CACHE INTERNAL "")
      message(STATUS "Linker detection: unknown")
    endif()
  else()
    if("${stdout}" MATCHES "^mold")
      set(LLVM_LINKER_DETECTED YES CACHE INTERNAL "")
      set(LLVM_LINKER_IS_MOLD YES CACHE INTERNAL "")
      message(STATUS "Linker detection: mold")
    elseif("${stdout}" MATCHES "GNU gold")
      set(LLVM_LINKER_DETECTED YES CACHE INTERNAL "")
      set(LLVM_LINKER_IS_GOLD YES CACHE INTERNAL "")
      message(STATUS "Linker detection: GNU Gold")
    elseif("${stdout}" MATCHES "^LLD")
      set(LLVM_LINKER_DETECTED YES CACHE INTERNAL "")
      set(LLVM_LINKER_IS_LLD YES CACHE INTERNAL "")
      message(STATUS "Linker detection: LLD")
    elseif("${stdout}" MATCHES "GNU ld")
      set(LLVM_LINKER_DETECTED YES CACHE INTERNAL "")
      set(LLVM_LINKER_IS_GNULD YES CACHE INTERNAL "")
      message(STATUS "Linker detection: GNU ld")
    elseif("${stderr}" MATCHES "(illumos)" OR
           "${stdout}" MATCHES "(illumos)")
      set(LLVM_LINKER_DETECTED YES CACHE INTERNAL "")
      set(LLVM_LINKER_IS_SOLARISLD YES CACHE INTERNAL "")
      set(LLVM_LINKER_IS_SOLARISLD_ILLUMOS YES CACHE INTERNAL "")
      message(STATUS "Linker detection: Solaris ld (illumos)")
    elseif("${stderr}" MATCHES "Solaris Link Editors" OR
           "${stdout}" MATCHES "Solaris Link Editors")
      set(LLVM_LINKER_DETECTED YES CACHE INTERNAL "")
      set(LLVM_LINKER_IS_SOLARISLD YES CACHE INTERNAL "")
      message(STATUS "Linker detection: Solaris ld")
    else()
      set(LLVM_LINKER_DETECTED NO CACHE INTERNAL "")
      message(STATUS "Linker detection: unknown")
    endif()
  endif()

  if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    include(CheckLinkerFlag)
    # Linkers that support Darwin allow a setting to internalize all symbol exports,
    # aiding in reducing binary size and often is applicable for executables.
    check_linker_flag(C "-Wl,-no_exported_symbols" LLVM_LINKER_SUPPORTS_NO_EXPORTED_SYMBOLS)

    if (NOT LLVM_USE_LINKER)
      # Apple's linker complains about duplicate libraries, which CMake likes to do
      # to support ELF platforms. To silence that warning, we can use
      # -no_warn_duplicate_libraries, but only in versions of the linker that
      # support that flag.
      check_linker_flag(C "-Wl,-no_warn_duplicate_libraries" LLVM_LINKER_SUPPORTS_NO_WARN_DUPLICATE_LIBRARIES)
    else()
      set(LLVM_LINKER_SUPPORTS_NO_WARN_DUPLICATE_LIBRARIES OFF CACHE INTERNAL "")
    endif()

  else()
    set(LLVM_LINKER_SUPPORTS_NO_EXPORTED_SYMBOLS OFF CACHE INTERNAL "")
  endif()
endif()

function(add_link_opts target_name)
  get_llvm_distribution(${target_name} in_distribution in_distribution_var)
  if(NOT in_distribution)
    # Don't LTO optimize targets that aren't part of any distribution.
    if (LLVM_ENABLE_LTO)
      # We may consider avoiding LTO altogether by using -fembed-bitcode
      # and teaching the linker to select machine code from .o files, see
      # https://lists.llvm.org/pipermail/llvm-dev/2021-April/149843.html
      if((UNIX OR MINGW) AND LINKER_IS_LLD)
        set_property(TARGET ${target_name} APPEND_STRING PROPERTY
                      LINK_FLAGS " -Wl,--lto-O0")
      elseif(LINKER_IS_LLD_LINK)
        set_property(TARGET ${target_name} APPEND_STRING PROPERTY
                      LINK_FLAGS " /opt:lldlto=0")
      elseif(APPLE AND NOT uppercase_LLVM_ENABLE_LTO STREQUAL "THIN")
        set_property(TARGET ${target_name} APPEND_STRING PROPERTY
                      LINK_FLAGS " -Wl,-mllvm,-O0")
      endif()
    endif()
  endif()

  # Don't use linker optimizations in debug builds since it slows down the
  # linker in a context where the optimizations are not important.
  if (NOT uppercase_CMAKE_BUILD_TYPE STREQUAL "DEBUG")
    if(NOT LLVM_NO_DEAD_STRIP)
      if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        # ld64's implementation of -dead_strip breaks tools that use plugins.
        set_property(TARGET ${target_name} APPEND_STRING PROPERTY
                     LINK_FLAGS " -Wl,-dead_strip")
      elseif(${CMAKE_SYSTEM_NAME} MATCHES "SunOS" AND LLVM_LINKER_IS_SOLARISLD)
        # Support for ld -z discard-unused=sections was only added in
        # Solaris 11.4.  GNU ld ignores it, but warns every time.
        include(LLVMCheckLinkerFlag)
        llvm_check_linker_flag(CXX "-Wl,-z,discard-unused=sections" LINKER_SUPPORTS_Z_DISCARD_UNUSED)
        if (LINKER_SUPPORTS_Z_DISCARD_UNUSED)
          set_property(TARGET ${target_name} APPEND_STRING PROPERTY
                       LINK_FLAGS " -Wl,-z,discard-unused=sections")
        endif()
      elseif(NOT MSVC AND NOT CMAKE_SYSTEM_NAME MATCHES "AIX|OS390")
        # TODO Revisit this later on z/OS.
        set_property(TARGET ${target_name} APPEND_STRING PROPERTY
                     LINK_FLAGS " -Wl,--gc-sections")
      endif()
    else() #LLVM_NO_DEAD_STRIP
      if(${CMAKE_SYSTEM_NAME} MATCHES "AIX")
        set_property(TARGET ${target_name} APPEND_STRING PROPERTY
                     LINK_FLAGS " -Wl,-bnogc")
      endif()
    endif()
  endif()

  if(LLVM_LINKER_SUPPORTS_NO_WARN_DUPLICATE_LIBRARIES)
    set_property(TARGET ${target_name} APPEND_STRING PROPERTY
                 LINK_FLAGS " -Wl,-no_warn_duplicate_libraries")
  endif()

  if(ARG_SUPPORT_PLUGINS AND ${CMAKE_SYSTEM_NAME} MATCHES "AIX")
    set_property(TARGET ${target_name} APPEND_STRING PROPERTY
                 LINK_FLAGS " -Wl,-brtl")
  endif()
endfunction(add_link_opts)

# Set each output directory according to ${CMAKE_CONFIGURATION_TYPES}.
# Note: Don't set variables CMAKE_*_OUTPUT_DIRECTORY any more,
# or a certain builder, for eaxample, msbuild.exe, would be confused.
function(set_output_directory target)
  cmake_parse_arguments(ARG "" "BINARY_DIR;LIBRARY_DIR" "" ${ARGN})

  # module_dir -- corresponding to LIBRARY_OUTPUT_DIRECTORY.
  # It affects output of add_library(MODULE).
  if(WIN32 OR CYGWIN)
    # DLL platform
    set(module_dir ${ARG_BINARY_DIR})
  else()
    set(module_dir ${ARG_LIBRARY_DIR})
  endif()
  if(NOT "${CMAKE_CFG_INTDIR}" STREQUAL ".")
    foreach(build_mode ${CMAKE_CONFIGURATION_TYPES})
      string(TOUPPER "${build_mode}" CONFIG_SUFFIX)
      if(ARG_BINARY_DIR)
        string(REPLACE ${CMAKE_CFG_INTDIR} ${build_mode} bi ${ARG_BINARY_DIR})
        set_target_properties(${target} PROPERTIES "RUNTIME_OUTPUT_DIRECTORY_${CONFIG_SUFFIX}" ${bi})
      endif()
      if(ARG_LIBRARY_DIR)
        string(REPLACE ${CMAKE_CFG_INTDIR} ${build_mode} li ${ARG_LIBRARY_DIR})
        set_target_properties(${target} PROPERTIES "ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_SUFFIX}" ${li})
      endif()
      if(module_dir)
        string(REPLACE ${CMAKE_CFG_INTDIR} ${build_mode} mi ${module_dir})
        set_target_properties(${target} PROPERTIES "LIBRARY_OUTPUT_DIRECTORY_${CONFIG_SUFFIX}" ${mi})
      endif()
    endforeach()
  else()
    if(ARG_BINARY_DIR)
      set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ARG_BINARY_DIR})
    endif()
    if(ARG_LIBRARY_DIR)
      set_target_properties(${target} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${ARG_LIBRARY_DIR})
    endif()
    if(module_dir)
      set_target_properties(${target} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${module_dir})
    endif()
  endif()
endfunction()

# If on Windows and building with MSVC, add the resource script containing the
# VERSIONINFO data to the project.  This embeds version resource information
# into the output .exe or .dll.
# TODO: Enable for MinGW Windows builds too.
#
function(add_windows_version_resource_file OUT_VAR)
  set(sources ${ARGN})
  if (MSVC AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(resource_file ${LLVM_SOURCE_DIR}/resources/windows_version_resource.rc)
    if(EXISTS ${resource_file})
      set(sources ${sources} ${resource_file})
      source_group("Resource Files" ${resource_file})
      set(windows_resource_file ${resource_file} PARENT_SCOPE)
    endif()
  endif(MSVC AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")

  set(${OUT_VAR} ${sources} PARENT_SCOPE)
endfunction(add_windows_version_resource_file)

# set_windows_version_resource_properties(name resource_file...
#   VERSION_MAJOR int
#     Optional major version number (defaults to LLVM_VERSION_MAJOR)
#   VERSION_MINOR int
#     Optional minor version number (defaults to LLVM_VERSION_MINOR)
#   VERSION_PATCHLEVEL int
#     Optional patchlevel version number (defaults to LLVM_VERSION_PATCH)
#   VERSION_STRING
#     Optional version string (defaults to PACKAGE_VERSION)
#   PRODUCT_NAME
#     Optional product name string (defaults to "LLVM")
#   )
function(set_windows_version_resource_properties name resource_file)
  cmake_parse_arguments(ARG
    ""
    "VERSION_MAJOR;VERSION_MINOR;VERSION_PATCHLEVEL;VERSION_STRING;PRODUCT_NAME"
    ""
    ${ARGN})

  if (NOT DEFINED ARG_VERSION_MAJOR)
    if (${LLVM_VERSION_MAJOR})
      set(ARG_VERSION_MAJOR ${LLVM_VERSION_MAJOR})
    else()
      set(ARG_VERSION_MAJOR 0)
    endif()
  endif()

  if (NOT DEFINED ARG_VERSION_MINOR)
    if (${LLVM_VERSION_MINOR})
      set(ARG_VERSION_MINOR ${LLVM_VERSION_MINOR})
    else()
      set(ARG_VERSION_MINOR 0)
    endif()
  endif()

  if (NOT DEFINED ARG_VERSION_PATCHLEVEL)
    if (${LLVM_VERSION_PATCH})
      set(ARG_VERSION_PATCHLEVEL ${LLVM_VERSION_PATCH})
    else()
      set(ARG_VERSION_PATCHLEVEL 0)
    endif()
  endif()

  if (NOT DEFINED ARG_VERSION_STRING)
    if (${PACKAGE_VERSION})
      set(ARG_VERSION_STRING ${PACKAGE_VERSION})
    else()
      set(ARG_VERSION_STRING 0)
    endif()
  endif()

  if (NOT DEFINED ARG_PRODUCT_NAME)
    set(ARG_PRODUCT_NAME "LLVM")
  endif()

  set_property(SOURCE ${resource_file}
               PROPERTY COMPILE_FLAGS /nologo)
  set_property(SOURCE ${resource_file}
               PROPERTY COMPILE_DEFINITIONS
               "RC_VERSION_FIELD_1=${ARG_VERSION_MAJOR}"
               "RC_VERSION_FIELD_2=${ARG_VERSION_MINOR}"
               "RC_VERSION_FIELD_3=${ARG_VERSION_PATCHLEVEL}"
               "RC_VERSION_FIELD_4=0"
               "RC_FILE_VERSION=\"${ARG_VERSION_STRING}\""
               "RC_INTERNAL_NAME=\"${name}\""
               "RC_PRODUCT_NAME=\"${ARG_PRODUCT_NAME}\""
               "RC_PRODUCT_VERSION=\"${ARG_VERSION_STRING}\"")
endfunction(set_windows_version_resource_properties)

# llvm_add_library(name sources...
#   SHARED;STATIC
#     STATIC by default w/o BUILD_SHARED_LIBS.
#     SHARED by default w/  BUILD_SHARED_LIBS.
#   OBJECT
#     Also create an OBJECT library target. Default if STATIC && SHARED.
#   MODULE
#     Target ${name} might not be created on unsupported platforms.
#     Check with "if(TARGET ${name})".
#   DISABLE_LLVM_LINK_LLVM_DYLIB
#     Do not link this library to libLLVM, even if
#     LLVM_LINK_LLVM_DYLIB is enabled.
#   OUTPUT_NAME name
#     Corresponds to OUTPUT_NAME in target properties.
#   DEPENDS targets...
#     Same semantics as add_dependencies().
#   LINK_COMPONENTS components...
#     Same as the variable LLVM_LINK_COMPONENTS.
#   LINK_LIBS lib_targets...
#     Same semantics as target_link_libraries().
#   ADDITIONAL_HEADERS
#     May specify header files for IDE generators.
#   SONAME
#     Should set SONAME link flags and create symlinks
#   NO_INSTALL_RPATH
#     Suppress default RPATH settings in shared libraries.
#   PLUGIN_TOOL
#     The tool (i.e. cmake target) that this plugin will link against
#   COMPONENT_LIB
#      This is used to specify that this is a component library of
#      LLVM which means that the source resides in llvm/lib/ and it is a
#      candidate for inclusion into libLLVM.so.
#   )
function(llvm_add_library name)
  cmake_parse_arguments(ARG
    "MODULE;SHARED;STATIC;OBJECT;DISABLE_LLVM_LINK_LLVM_DYLIB;SONAME;NO_INSTALL_RPATH;COMPONENT_LIB"
    "OUTPUT_NAME;PLUGIN_TOOL;ENTITLEMENTS;BUNDLE_PATH"
    "ADDITIONAL_HEADERS;DEPENDS;LINK_COMPONENTS;LINK_LIBS;OBJLIBS"
    ${ARGN})
  list(APPEND LLVM_COMMON_DEPENDS ${ARG_DEPENDS})
  if(ARG_ADDITIONAL_HEADERS)
    # Pass through ADDITIONAL_HEADERS.
    set(ARG_ADDITIONAL_HEADERS ADDITIONAL_HEADERS ${ARG_ADDITIONAL_HEADERS})
  endif()
  if(ARG_OBJLIBS)
    set(ALL_FILES ${ARG_OBJLIBS})
  else()
    llvm_process_sources(ALL_FILES ${ARG_UNPARSED_ARGUMENTS} ${ARG_ADDITIONAL_HEADERS})
  endif()

  if(ARG_MODULE)
    if(ARG_SHARED OR ARG_STATIC)
      message(WARNING "MODULE with SHARED|STATIC doesn't make sense.")
    endif()
    # Plugins that link against a tool are allowed even when plugins in general are not
    if(NOT LLVM_ENABLE_PLUGINS AND NOT (ARG_PLUGIN_TOOL AND LLVM_EXPORT_SYMBOLS_FOR_PLUGINS))
      message(STATUS "${name} ignored -- Loadable modules not supported on this platform.")
      return()
    endif()
  else()
    if(ARG_PLUGIN_TOOL)
      message(WARNING "PLUGIN_TOOL without MODULE doesn't make sense.")
    endif()
    if(BUILD_SHARED_LIBS AND NOT ARG_STATIC)
      set(ARG_SHARED TRUE)
    endif()
    if(NOT ARG_SHARED)
      set(ARG_STATIC TRUE)
    endif()
  endif()

  get_subproject_title(subproject_title)

  # Generate objlib
  if((ARG_SHARED AND ARG_STATIC) OR ARG_OBJECT)
    # Generate an obj library for both targets.
    set(obj_name "obj.${name}")
    add_library(${obj_name} OBJECT EXCLUDE_FROM_ALL
      ${ALL_FILES}
      )
    llvm_update_compile_flags(${obj_name})
    if(CMAKE_GENERATOR STREQUAL "Xcode")
      set(DUMMY_FILE ${CMAKE_CURRENT_BINARY_DIR}/Dummy.c)
      file(WRITE ${DUMMY_FILE} "// This file intentionally empty\n")
      set_property(SOURCE ${DUMMY_FILE} APPEND_STRING PROPERTY COMPILE_FLAGS "-Wno-empty-translation-unit")
    endif()
    set(ALL_FILES "$<TARGET_OBJECTS:${obj_name}>" ${DUMMY_FILE})

    # Do add_dependencies(obj) later due to CMake issue 14747.
    list(APPEND objlibs ${obj_name})

    # Bring in the target include directories from our original target.
    target_include_directories(${obj_name} PRIVATE $<TARGET_PROPERTY:${name},INCLUDE_DIRECTORIES>)

    set_target_properties(${obj_name} PROPERTIES FOLDER "${subproject_title}/Object Libraries")
    if(ARG_DEPENDS)
      add_dependencies(${obj_name} ${ARG_DEPENDS})
    endif()
    # Treat link libraries like PUBLIC dependencies.  LINK_LIBS might
    # result in generating header files.  Add a dependendency so that
    # the generated header is created before this object library.
    if(ARG_LINK_LIBS)
      cmake_parse_arguments(LINK_LIBS_ARG
        ""
        ""
        "PUBLIC;PRIVATE"
        ${ARG_LINK_LIBS})
      foreach(link_lib ${LINK_LIBS_ARG_PUBLIC})
        if(LLVM_PTHREAD_LIB)
          # Can't specify a dependence on -lpthread
          if(NOT ${link_lib} STREQUAL ${LLVM_PTHREAD_LIB})
            add_dependencies(${obj_name} ${link_lib})
          endif()
        else()
          add_dependencies(${obj_name} ${link_lib})
        endif()
      endforeach()
    endif()
  endif()

  if(ARG_SHARED AND ARG_STATIC)
    # static
    set(name_static "${name}_static")
    if(ARG_OUTPUT_NAME)
      set(output_name OUTPUT_NAME "${ARG_OUTPUT_NAME}")
    endif()
    # DEPENDS has been appended to LLVM_COMMON_LIBS.
    llvm_add_library(${name_static} STATIC
      ${output_name}
      OBJLIBS ${ALL_FILES} # objlib
      LINK_LIBS ${ARG_LINK_LIBS}
      LINK_COMPONENTS ${ARG_LINK_COMPONENTS}
      )
    set_target_properties(${name_static} PROPERTIES FOLDER "${subproject_title}/Libraries")

    # Bring in the target link info from our original target.
    target_link_directories(${name_static} PRIVATE $<TARGET_PROPERTY:${name},LINK_DIRECTORIES>)
    target_link_libraries(${name_static} PRIVATE $<TARGET_PROPERTY:${name},LINK_LIBRARIES>)

    # FIXME: Add name_static to anywhere in TARGET ${name}'s PROPERTY.
    set(ARG_STATIC)
  endif()

  if(ARG_MODULE)
    add_library(${name} MODULE ${ALL_FILES})
  elseif(ARG_SHARED)
    add_windows_version_resource_file(ALL_FILES ${ALL_FILES})
    add_library(${name} SHARED ${ALL_FILES})
  else()
    add_library(${name} STATIC ${ALL_FILES})
  endif()
  set_target_properties(${name} PROPERTIES FOLDER "${subproject_title}/Libraries")

  if(ARG_COMPONENT_LIB)
    set_target_properties(${name} PROPERTIES LLVM_COMPONENT TRUE)
    set_property(GLOBAL APPEND PROPERTY LLVM_COMPONENT_LIBS ${name})
  endif()

  if(NOT ARG_NO_INSTALL_RPATH)
    if(ARG_MODULE OR ARG_SHARED)
      llvm_setup_rpath(${name})
    endif()
  endif()

  setup_dependency_debugging(${name} ${LLVM_COMMON_DEPENDS})

  if(DEFINED windows_resource_file)
    set_windows_version_resource_properties(${name} ${windows_resource_file})
    set(windows_resource_file ${windows_resource_file} PARENT_SCOPE)
  endif()

  set_output_directory(${name} BINARY_DIR ${LLVM_RUNTIME_OUTPUT_INTDIR} LIBRARY_DIR ${LLVM_LIBRARY_OUTPUT_INTDIR})
  # $<TARGET_OBJECTS> doesn't require compile flags.
  if(NOT obj_name)
    llvm_update_compile_flags(${name})
  endif()
  add_link_opts( ${name} )
  if(ARG_OUTPUT_NAME)
    set_target_properties(${name}
      PROPERTIES
      OUTPUT_NAME ${ARG_OUTPUT_NAME}
      )
  endif()

  if(ARG_MODULE)
    set_target_properties(${name} PROPERTIES
      PREFIX ""
      SUFFIX ${LLVM_PLUGIN_EXT}
      )
  endif()

  if(ARG_SHARED)
    if(MSVC)
      set_target_properties(${name} PROPERTIES
        PREFIX ""
        )
    endif()

    # Set SOVERSION on shared libraries that lack explicit SONAME
    # specifier, on *nix systems that are not Darwin.
    if(UNIX AND NOT APPLE AND NOT ARG_SONAME)
      set_target_properties(${name}
        PROPERTIES
        # Since 18.1.0, the ABI version is indicated by the major and minor version.
        SOVERSION ${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR}${LLVM_VERSION_SUFFIX}
        VERSION ${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR}${LLVM_VERSION_SUFFIX})
    endif()
  endif()

  if(ARG_MODULE OR ARG_SHARED)
    # Do not add -Dname_EXPORTS to the command-line when building files in this
    # target. Doing so is actively harmful for the modules build because it
    # creates extra module variants, and not useful because we don't use these
    # macros.
    set_target_properties( ${name} PROPERTIES DEFINE_SYMBOL "" )

    if (LLVM_EXPORTED_SYMBOL_FILE)
      add_llvm_symbol_exports( ${name} ${LLVM_EXPORTED_SYMBOL_FILE} )
    endif()
  endif()

  if(ARG_SHARED)
    if(NOT APPLE AND ARG_SONAME)
      get_target_property(output_name ${name} OUTPUT_NAME)
      if(${output_name} STREQUAL "output_name-NOTFOUND")
        set(output_name ${name})
      endif()
      set(library_name ${output_name}-${LLVM_VERSION_MAJOR}${LLVM_VERSION_SUFFIX})
      set(api_name ${output_name}-${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR}.${LLVM_VERSION_PATCH}${LLVM_VERSION_SUFFIX})
      set_target_properties(${name} PROPERTIES OUTPUT_NAME ${library_name})
      if(UNIX)
        llvm_install_library_symlink(${api_name} ${library_name} SHARED
          COMPONENT ${name})
        llvm_install_library_symlink(${output_name} ${library_name} SHARED
          COMPONENT ${name})
      endif()
    endif()
  endif()

  if(ARG_STATIC)
    set(libtype PUBLIC)
  else()
    # We can use PRIVATE since SO knows its dependent libs.
    set(libtype PRIVATE)
  endif()

  if(ARG_MODULE AND LLVM_EXPORT_SYMBOLS_FOR_PLUGINS AND ARG_PLUGIN_TOOL AND (WIN32 OR CYGWIN))
    # On DLL platforms symbols are imported from the tool by linking against it.
    set(llvm_libs ${ARG_PLUGIN_TOOL})
  elseif (NOT ARG_COMPONENT_LIB)
    if (LLVM_LINK_LLVM_DYLIB AND NOT ARG_DISABLE_LLVM_LINK_LLVM_DYLIB)
      set(llvm_libs LLVM)
    else()
      llvm_map_components_to_libnames(llvm_libs
       ${ARG_LINK_COMPONENTS}
       ${LLVM_LINK_COMPONENTS}
       )
    endif()
  else()
    # Components have not been defined explicitly in CMake, so add the
    # dependency information for this library through their name, and let
    # LLVMBuildResolveComponentsLink resolve the mapping.
    #
    # It would be nice to verify that we have the dependencies for this library
    # name, but using get_property(... SET) doesn't suffice to determine if a
    # property has been set to an empty value.
    set_property(TARGET ${name} PROPERTY LLVM_LINK_COMPONENTS ${ARG_LINK_COMPONENTS} ${LLVM_LINK_COMPONENTS})

    # This property is an internal property only used to make sure the
    # link step applied in LLVMBuildResolveComponentsLink uses the same
    # property as the target_link_libraries call below.
    set_property(TARGET ${name} PROPERTY LLVM_LIBTYPE ${libtype})
  endif()

  target_link_libraries(${name} ${libtype}
      ${ARG_LINK_LIBS}
      ${lib_deps}
      ${llvm_libs}
      )

  if(LLVM_COMMON_DEPENDS)
    add_dependencies(${name} ${LLVM_COMMON_DEPENDS})
    # Add dependencies also to objlibs.
    # CMake issue 14747 --  add_dependencies() might be ignored to objlib's user.
    foreach(objlib ${objlibs})
      add_dependencies(${objlib} ${LLVM_COMMON_DEPENDS})
    endforeach()
  endif()

  add_custom_linker_flags(${name})

  if(ARG_SHARED OR ARG_MODULE)
    llvm_externalize_debuginfo(${name})
    llvm_codesign(${name} ENTITLEMENTS ${ARG_ENTITLEMENTS} BUNDLE_PATH ${ARG_BUNDLE_PATH})
  endif()
  # clang and newer versions of ninja use high-resolutions timestamps,
  # but older versions of libtool on Darwin don't, so the archive will
  # often get an older timestamp than the last object that was added
  # or updated.  To fix this, we add a custom command to touch archive
  # after it's been built so that ninja won't rebuild it unnecessarily
  # the next time it's run.
  if(ARG_STATIC AND LLVM_TOUCH_STATIC_LIBRARIES)
    add_custom_command(TARGET ${name}
      POST_BUILD
      COMMAND touch ${LLVM_LIBRARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}${name}${CMAKE_STATIC_LIBRARY_SUFFIX}
      )
  endif()
endfunction()

function(add_llvm_install_targets target)
  cmake_parse_arguments(ARG "" "COMPONENT;PREFIX;SYMLINK" "DEPENDS" ${ARGN})
  if(ARG_COMPONENT)
    set(component_option -DCMAKE_INSTALL_COMPONENT="${ARG_COMPONENT}")
  endif()
  if(ARG_PREFIX)
    set(prefix_option -DCMAKE_INSTALL_PREFIX="${ARG_PREFIX}")
  endif()

  set(file_dependencies)
  set(target_dependencies)
  foreach(dependency ${ARG_DEPENDS})
    if(TARGET ${dependency})
      list(APPEND target_dependencies ${dependency})
    else()
      list(APPEND file_dependencies ${dependency})
    endif()
  endforeach()

  get_subproject_title(subproject_title)

  add_custom_target(${target}
                    DEPENDS ${file_dependencies}
                    COMMAND "${CMAKE_COMMAND}"
                            ${component_option}
                            ${prefix_option}
                            -P "${CMAKE_BINARY_DIR}/cmake_install.cmake"
                    USES_TERMINAL)
  set_target_properties(${target} PROPERTIES FOLDER "${subproject_title}/Installation")
  add_custom_target(${target}-stripped
                    DEPENDS ${file_dependencies}
                    COMMAND "${CMAKE_COMMAND}"
                            ${component_option}
                            ${prefix_option}
                            -DCMAKE_INSTALL_DO_STRIP=1
                            -P "${CMAKE_BINARY_DIR}/cmake_install.cmake"
                    USES_TERMINAL)
  set_target_properties(${target}-stripped PROPERTIES FOLDER "${subproject_title}/Installation")
  if(target_dependencies)
    add_dependencies(${target} ${target_dependencies})
    add_dependencies(${target}-stripped ${target_dependencies})
  endif()

  if(ARG_SYMLINK)
    add_dependencies(${target} install-${ARG_SYMLINK})
    add_dependencies(${target}-stripped install-${ARG_SYMLINK}-stripped)
  endif()
endfunction()

# Define special targets that behave like a component group. They don't have any
# source attached but other components can add themselves to them. If the
# component supports is a Target and it supports JIT compilation, HAS_JIT must
# be passed. One can use ADD_TO_COMPONENT option from add_llvm_component_library
# to link extra component into an existing group.
function(add_llvm_component_group name)
  cmake_parse_arguments(ARG "HAS_JIT" "" "LINK_COMPONENTS" ${ARGN})
  add_custom_target(${name})
  get_subproject_title(subproject_title)
  set_target_properties(${name} PROPERTIES FOLDER "${subproject_title}/Component Groups")
  if(ARG_HAS_JIT)
    set_property(TARGET ${name} PROPERTY COMPONENT_HAS_JIT ON)
  endif()
  if(ARG_LINK_COMPONENTS)
    set_property(TARGET ${name} PROPERTY LLVM_LINK_COMPONENTS ${ARG_LINK_COMPONENTS})
  endif()
endfunction()

# An LLVM component is a cmake target with the following cmake properties
# eventually set:
#   - LLVM_COMPONENT_NAME: the name of the component, which can be the name of
#     the associated library or the one specified through COMPONENT_NAME
#   - LLVM_LINK_COMPONENTS: a list of component this component depends on
#   - COMPONENT_HAS_JIT: (only for group component) whether this target group
#     supports JIT compilation
# Additionnaly, the ADD_TO_COMPONENT <component> option make it possible to add this
# component to the LLVM_LINK_COMPONENTS of <component>.
function(add_llvm_component_library name)
  cmake_parse_arguments(ARG
    ""
    "COMPONENT_NAME;ADD_TO_COMPONENT"
    ""
    ${ARGN})
  add_llvm_library(${name} COMPONENT_LIB ${ARG_UNPARSED_ARGUMENTS})
  string(REGEX REPLACE "^LLVM" "" component_name ${name})
  set_property(TARGET ${name} PROPERTY LLVM_COMPONENT_NAME ${component_name})

  if(ARG_COMPONENT_NAME)
    set_property(GLOBAL PROPERTY LLVM_COMPONENT_NAME_${ARG_COMPONENT_NAME} ${component_name})
  endif()

  if(ARG_ADD_TO_COMPONENT)
    set_property(TARGET ${ARG_ADD_TO_COMPONENT} APPEND PROPERTY LLVM_LINK_COMPONENTS ${component_name})
    get_subproject_title(subproject_title)
    set_target_properties(${name} PROPERTIES FOLDER "${subproject_title}/Libraries/${ARG_ADD_TO_COMPONENT}")
  endif()

endfunction()

macro(add_llvm_library name)
  cmake_parse_arguments(ARG
    "SHARED;BUILDTREE_ONLY;MODULE;INSTALL_WITH_TOOLCHAIN"
    ""
    ""
    ${ARGN})
  if(ARG_MODULE)
    llvm_add_library(${name} MODULE ${ARG_UNPARSED_ARGUMENTS})
  elseif( BUILD_SHARED_LIBS OR ARG_SHARED )
    llvm_add_library(${name} SHARED ${ARG_UNPARSED_ARGUMENTS})
  else()
    llvm_add_library(${name} ${ARG_UNPARSED_ARGUMENTS})
  endif()

  # Libraries that are meant to only be exposed via the build tree only are
  # never installed and are only exported as a target in the special build tree
  # config file.
  if (NOT ARG_BUILDTREE_ONLY AND NOT ARG_MODULE)
    set_property( GLOBAL APPEND PROPERTY LLVM_LIBS ${name} )
    set(in_llvm_libs YES)
  endif()

  if (ARG_MODULE AND NOT TARGET ${name})
    # Add empty "phony" target
    add_custom_target(${name})
  elseif( EXCLUDE_FROM_ALL )
    set_target_properties( ${name} PROPERTIES EXCLUDE_FROM_ALL ON)
  elseif(ARG_BUILDTREE_ONLY)
    set_property(GLOBAL APPEND PROPERTY LLVM_EXPORTS_BUILDTREE_ONLY ${name})
  else()
    if (NOT LLVM_INSTALL_TOOLCHAIN_ONLY OR ARG_INSTALL_WITH_TOOLCHAIN)
      if(in_llvm_libs)
        set(umbrella UMBRELLA llvm-libraries)
      else()
        set(umbrella)
      endif()

      get_target_export_arg(${name} LLVM export_to_llvmexports ${umbrella})
      install(TARGETS ${name}
              ${export_to_llvmexports}
              LIBRARY DESTINATION lib${LLVM_LIBDIR_SUFFIX} COMPONENT ${name}
              ARCHIVE DESTINATION lib${LLVM_LIBDIR_SUFFIX} COMPONENT ${name}
              RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT ${name})

      if (NOT LLVM_ENABLE_IDE)
        add_llvm_install_targets(install-${name}
                                 DEPENDS ${name}
                                 COMPONENT ${name})
      endif()
    endif()
    set_property(GLOBAL APPEND PROPERTY LLVM_EXPORTS ${name})
  endif()

  get_subproject_title(subproject_title)
  if (ARG_MODULE)
    set_target_properties(${name} PROPERTIES FOLDER "${subproject_title}/Loadable Modules")
  else()
    set_target_properties(${name} PROPERTIES FOLDER "${subproject_title}/Libraries")
  endif()
endmacro(add_llvm_library name)

macro(generate_llvm_objects name)
  cmake_parse_arguments(ARG "GENERATE_DRIVER" "" "DEPENDS" ${ARGN})

  llvm_process_sources( ALL_FILES ${ARG_UNPARSED_ARGUMENTS} )

  list(APPEND LLVM_COMMON_DEPENDS ${ARG_DEPENDS})

  # Generate objlib
  if(LLVM_ENABLE_OBJLIB OR (ARG_GENERATE_DRIVER AND LLVM_TOOL_LLVM_DRIVER_BUILD))
    # Generate an obj library for both targets.
    set(obj_name "obj.${name}")
    add_library(${obj_name} OBJECT EXCLUDE_FROM_ALL
      ${ALL_FILES}
      )
    llvm_update_compile_flags(${obj_name})
    set(ALL_FILES "$<TARGET_OBJECTS:${obj_name}>")
    if(ARG_DEPENDS)
      add_dependencies(${obj_name} ${ARG_DEPENDS})
    endif()

    get_subproject_title(subproject_title)
    set_target_properties(${obj_name} PROPERTIES FOLDER "${subproject_title}/Object Libraries")
  endif()

  if (ARG_GENERATE_DRIVER)
    string(REPLACE "-" "_" TOOL_NAME ${name})
    foreach(path ${CMAKE_MODULE_PATH})
      if(EXISTS ${path}/llvm-driver-template.cpp.in)
        configure_file(
          ${path}/llvm-driver-template.cpp.in
          ${CMAKE_CURRENT_BINARY_DIR}/${name}-driver.cpp)
        break()
      endif()
    endforeach()

    list(APPEND ALL_FILES ${CMAKE_CURRENT_BINARY_DIR}/${name}-driver.cpp)

    if (LLVM_TOOL_LLVM_DRIVER_BUILD
        AND (NOT LLVM_DISTRIBUTION_COMPONENTS OR ${name} IN_LIST LLVM_DISTRIBUTION_COMPONENTS)
       )
      set_property(GLOBAL APPEND PROPERTY LLVM_DRIVER_COMPONENTS ${LLVM_LINK_COMPONENTS})
      set_property(GLOBAL APPEND PROPERTY LLVM_DRIVER_DEPS ${ARG_DEPENDS} ${LLVM_COMMON_DEPENDS})
      set_property(GLOBAL APPEND PROPERTY LLVM_DRIVER_OBJLIBS "${obj_name}")

      set_property(GLOBAL APPEND PROPERTY LLVM_DRIVER_TOOLS ${name})
      set_property(GLOBAL APPEND PROPERTY LLVM_DRIVER_TOOL_ALIASES_${name} ${name})
      target_link_libraries(${obj_name} ${LLVM_PTHREAD_LIB})
      llvm_config(${obj_name} ${USE_SHARED} ${LLVM_LINK_COMPONENTS} )
    endif()
  endif()
endmacro()

macro(add_llvm_executable name)
  cmake_parse_arguments(ARG
    "DISABLE_LLVM_LINK_LLVM_DYLIB;IGNORE_EXTERNALIZE_DEBUGINFO;NO_INSTALL_RPATH;SUPPORT_PLUGINS"
    "ENTITLEMENTS;BUNDLE_PATH"
    ""
    ${ARGN})
  generate_llvm_objects(${name} ${ARG_UNPARSED_ARGUMENTS})
  add_windows_version_resource_file(ALL_FILES ${ALL_FILES})

  if(XCODE)
    # Note: the dummy.cpp source file provides no definitions. However,
    # it forces Xcode to properly link the static library.
    list(APPEND ALL_FILES "${LLVM_MAIN_SRC_DIR}/cmake/dummy.cpp")
  endif()

  if( EXCLUDE_FROM_ALL )
    add_executable(${name} EXCLUDE_FROM_ALL ${ALL_FILES})
  else()
    add_executable(${name} ${ALL_FILES})
  endif()
  get_subproject_title(subproject_title)
  set_target_properties(${name} PROPERTIES FOLDER "${subproject_title}/Executables")

  setup_dependency_debugging(${name} ${LLVM_COMMON_DEPENDS})

  if(NOT ARG_NO_INSTALL_RPATH)
    llvm_setup_rpath(${name})
  elseif(NOT "${LLVM_LOCAL_RPATH}" STREQUAL "")
    # Enable BUILD_WITH_INSTALL_RPATH unless CMAKE_BUILD_RPATH is set.
    if("${CMAKE_BUILD_RPATH}" STREQUAL "")
      set_property(TARGET ${name} PROPERTY BUILD_WITH_INSTALL_RPATH ON)
    endif()

    set_property(TARGET ${name} PROPERTY INSTALL_RPATH "${LLVM_LOCAL_RPATH}")
  endif()

  if(DEFINED windows_resource_file)
    set_windows_version_resource_properties(${name} ${windows_resource_file})
  endif()

  # $<TARGET_OBJECTS> doesn't require compile flags.
  if(NOT LLVM_ENABLE_OBJLIB)
    llvm_update_compile_flags(${name})
  endif()

  if (ARG_SUPPORT_PLUGINS AND NOT ${CMAKE_SYSTEM_NAME} MATCHES "AIX")
    set(LLVM_NO_DEAD_STRIP On)
  endif()

  add_link_opts( ${name} )

  # Do not add -Dname_EXPORTS to the command-line when building files in this
  # target. Doing so is actively harmful for the modules build because it
  # creates extra module variants, and not useful because we don't use these
  # macros.
  set_target_properties( ${name} PROPERTIES DEFINE_SYMBOL "" )

  if (LLVM_EXPORTED_SYMBOL_FILE)
    add_llvm_symbol_exports( ${name} ${LLVM_EXPORTED_SYMBOL_FILE} )
  endif(LLVM_EXPORTED_SYMBOL_FILE)

  if (DEFINED LLVM_ENABLE_EXPORTED_SYMBOLS_IN_EXECUTABLES AND
      NOT LLVM_ENABLE_EXPORTED_SYMBOLS_IN_EXECUTABLES)
    if(LLVM_LINKER_SUPPORTS_NO_EXPORTED_SYMBOLS)
      set_property(TARGET ${name} APPEND_STRING PROPERTY
        LINK_FLAGS " -Wl,-no_exported_symbols")
    else()
      message(FATAL_ERROR
        "LLVM_ENABLE_EXPORTED_SYMBOLS_IN_EXECUTABLES cannot be disabled when linker does not support \"-no_exported_symbols\"")
    endif()
  endif()

  if (LLVM_LINK_LLVM_DYLIB AND NOT ARG_DISABLE_LLVM_LINK_LLVM_DYLIB)
    set(USE_SHARED USE_SHARED)
  endif()

  set(EXCLUDE_FROM_ALL OFF)
  set_output_directory(${name} BINARY_DIR ${LLVM_RUNTIME_OUTPUT_INTDIR} LIBRARY_DIR ${LLVM_LIBRARY_OUTPUT_INTDIR})
  llvm_config( ${name} ${USE_SHARED} ${LLVM_LINK_COMPONENTS} )
  if( LLVM_COMMON_DEPENDS )
    add_dependencies( ${name} ${LLVM_COMMON_DEPENDS} )
    foreach(objlib ${obj_name})
      add_dependencies(${objlib} ${LLVM_COMMON_DEPENDS})
    endforeach()
  endif( LLVM_COMMON_DEPENDS )

  add_custom_linker_flags(${name})

  if(NOT ARG_IGNORE_EXTERNALIZE_DEBUGINFO)
    llvm_externalize_debuginfo(${name})
  endif()
  if (LLVM_PTHREAD_LIB)
    # libpthreads overrides some standard library symbols, so main
    # executable must be linked with it in order to provide consistent
    # API for all shared libaries loaded by this executable.
    target_link_libraries(${name} PRIVATE ${LLVM_PTHREAD_LIB})
  endif()

  if(HAVE_LLVM_LIBC)
    target_link_libraries(${name} PRIVATE llvmlibc)
  endif()

  llvm_codesign(${name} ENTITLEMENTS ${ARG_ENTITLEMENTS} BUNDLE_PATH ${ARG_BUNDLE_PATH})
endmacro(add_llvm_executable name)

# add_llvm_pass_plugin(name [NO_MODULE] ...)
#   Add ${name} as an llvm plugin.
#   If option LLVM_${name_upper}_LINK_INTO_TOOLS is set to ON, the plugin is registered statically.
#   Otherwise a pluggable shared library is registered.
#
#   If NO_MODULE is specified, when option LLVM_${name_upper}_LINK_INTO_TOOLS is set to OFF,
#   only an object library is built, and no module is built. This is specific to the Polly use case.
#
#   The SUBPROJECT argument contains the LLVM project the plugin belongs
#   to. If set, the plugin will link statically by default it if the
#   project was enabled.
function(add_llvm_pass_plugin name)
  cmake_parse_arguments(ARG
    "NO_MODULE" "SUBPROJECT" ""
    ${ARGN})

  string(TOUPPER ${name} name_upper)

  # Enable the plugin by default if it was explicitly enabled by the user.
  # Note: If was set to "all", LLVM's CMakeLists.txt replaces it with a
  # list of all projects, counting as explicitly enabled.
  set(link_into_tools_default OFF)
  if (ARG_SUBPROJECT AND LLVM_TOOL_${name_upper}_BUILD)
    set(link_into_tools_default ON)
  endif()
  option(LLVM_${name_upper}_LINK_INTO_TOOLS "Statically link ${name} into tools (if available)" ${link_into_tools_default})

  # If we statically link the plugin, don't use llvm dylib because we're going
  # to be part of it.
  if(LLVM_${name_upper}_LINK_INTO_TOOLS)
      list(APPEND ARG_UNPARSED_ARGUMENTS DISABLE_LLVM_LINK_LLVM_DYLIB)
  endif()

  if(LLVM_${name_upper}_LINK_INTO_TOOLS)
    list(REMOVE_ITEM ARG_UNPARSED_ARGUMENTS BUILDTREE_ONLY)
    # process_llvm_pass_plugins takes care of the actual linking, just create an
    # object library as of now
    add_llvm_library(${name} OBJECT ${ARG_UNPARSED_ARGUMENTS})
    target_compile_definitions(${name} PRIVATE LLVM_${name_upper}_LINK_INTO_TOOLS)
    set_property(TARGET ${name} APPEND PROPERTY COMPILE_DEFINITIONS LLVM_LINK_INTO_TOOLS)
    if (TARGET intrinsics_gen)
      add_dependencies(obj.${name} intrinsics_gen)
    endif()
    if (TARGET omp_gen)
      add_dependencies(obj.${name} omp_gen)
    endif()
    if (TARGET acc_gen)
      add_dependencies(obj.${name} acc_gen)
    endif()
    set_property(GLOBAL APPEND PROPERTY LLVM_STATIC_EXTENSIONS ${name})
  elseif(NOT ARG_NO_MODULE)
    add_llvm_library(${name} MODULE ${ARG_UNPARSED_ARGUMENTS})
  else()
    add_llvm_library(${name} OBJECT ${ARG_UNPARSED_ARGUMENTS})
  endif()
  message(STATUS "Registering ${name} as a pass plugin (static build: ${LLVM_${name_upper}_LINK_INTO_TOOLS})")

endfunction(add_llvm_pass_plugin)

# process_llvm_pass_plugins([GEN_CONFIG])
#
# Correctly set lib dependencies between plugins and tools, based on tools
# registered with the ENABLE_PLUGINS option.
#
# if GEN_CONFIG option is set, also generate X Macro file for extension
# handling. It provides a HANDLE_EXTENSION(extension_namespace, ExtensionProject)
# call for each extension allowing client code to define
# HANDLE_EXTENSION to have a specific code be run for each extension.
#
function(process_llvm_pass_plugins)
  cmake_parse_arguments(ARG
      "GEN_CONFIG" "" ""
    ${ARGN})

  if(ARG_GEN_CONFIG)
      get_property(LLVM_STATIC_EXTENSIONS GLOBAL PROPERTY LLVM_STATIC_EXTENSIONS)
  else()
      include(LLVMConfigExtensions)
  endif()

  # Add static plugins to the Extension component
  foreach(llvm_extension ${LLVM_STATIC_EXTENSIONS})
      set_property(TARGET LLVMExtensions APPEND PROPERTY LINK_LIBRARIES ${llvm_extension})
      set_property(TARGET LLVMExtensions APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${llvm_extension})
  endforeach()

  # Eventually generate the extension headers, and store config to a cmake file
  # for usage in third-party configuration.
  if(ARG_GEN_CONFIG)

      ## Part 1: Extension header to be included whenever we need extension
      #  processing.
      if(NOT DEFINED LLVM_INSTALL_PACKAGE_DIR)
          message(FATAL_ERROR "LLVM_INSTALL_PACKAGE_DIR must be defined and writable. GEN_CONFIG should only be passe when building LLVM proper.")
      endif()
      # LLVM_INSTALL_PACKAGE_DIR might be absolute, so don't reuse below.
      string(REPLACE "${CMAKE_CFG_INTDIR}" "." llvm_cmake_builddir "${LLVM_LIBRARY_DIR}")
      set(llvm_cmake_builddir "${llvm_cmake_builddir}/cmake/llvm")
      file(WRITE
          "${llvm_cmake_builddir}/LLVMConfigExtensions.cmake"
          "set(LLVM_STATIC_EXTENSIONS ${LLVM_STATIC_EXTENSIONS})")
      install(FILES
          ${llvm_cmake_builddir}/LLVMConfigExtensions.cmake
          DESTINATION ${LLVM_INSTALL_PACKAGE_DIR}
          COMPONENT cmake-exports)

      set(ExtensionDef "${LLVM_BINARY_DIR}/include/llvm/Support/Extension.def")
      file(WRITE "${ExtensionDef}.tmp" "//extension handlers\n")
      foreach(llvm_extension ${LLVM_STATIC_EXTENSIONS})
          file(APPEND "${ExtensionDef}.tmp" "HANDLE_EXTENSION(${llvm_extension})\n")
      endforeach()
      file(APPEND "${ExtensionDef}.tmp" "#undef HANDLE_EXTENSION\n")

      # only replace if there's an actual change
      execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${ExtensionDef}.tmp"
          "${ExtensionDef}")
      file(REMOVE "${ExtensionDef}.tmp")

      ## Part 2: Extension header that captures each extension dependency, to be
      #  used by llvm-config.
      set(ExtensionDeps "${LLVM_BINARY_DIR}/tools/llvm-config/ExtensionDependencies.inc")

      # Max needed to correctly size the required library array.
      set(llvm_plugin_max_deps_length 0)
      foreach(llvm_extension ${LLVM_STATIC_EXTENSIONS})
        get_property(llvm_plugin_deps TARGET ${llvm_extension} PROPERTY LINK_LIBRARIES)
        list(LENGTH llvm_plugin_deps llvm_plugin_deps_length)
        if(llvm_plugin_deps_length GREATER llvm_plugin_max_deps_length)
            set(llvm_plugin_max_deps_length ${llvm_plugin_deps_length})
        endif()
      endforeach()

      list(LENGTH LLVM_STATIC_EXTENSIONS llvm_static_extension_count)
      file(WRITE
          "${ExtensionDeps}.tmp"
          "#include <array>\n\
           struct ExtensionDescriptor {\n\
              const char* Name;\n\
              const char* RequiredLibraries[1 + 1 + ${llvm_plugin_max_deps_length}];\n\
           };\n\
           std::array<ExtensionDescriptor, ${llvm_static_extension_count}> AvailableExtensions{\n")

      foreach(llvm_extension ${LLVM_STATIC_EXTENSIONS})
        get_property(llvm_plugin_deps TARGET ${llvm_extension} PROPERTY LINK_LIBRARIES)

        file(APPEND "${ExtensionDeps}.tmp" "ExtensionDescriptor{\"${llvm_extension}\", {")
        foreach(llvm_plugin_dep ${llvm_plugin_deps})
            # Turn library dependency back to component name, if possible.
            # That way llvm-config can avoid redundant dependencies.
            STRING(REGEX REPLACE "^-l" ""  plugin_dep_name ${llvm_plugin_dep})
            STRING(REGEX MATCH "^LLVM" is_llvm_library ${plugin_dep_name})
            if(is_llvm_library)
                STRING(REGEX REPLACE "^LLVM" ""  plugin_dep_name ${plugin_dep_name})
                STRING(TOLOWER ${plugin_dep_name} plugin_dep_name)
            endif()
            file(APPEND "${ExtensionDeps}.tmp" "\"${plugin_dep_name}\", ")
        endforeach()

        # Self + mandatory trailing null, because the number of RequiredLibraries differs between extensions.
        file(APPEND "${ExtensionDeps}.tmp" \"${llvm_extension}\", "nullptr}},\n")
      endforeach()
      file(APPEND "${ExtensionDeps}.tmp" "};\n")

      # only replace if there's an actual change
      execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${ExtensionDeps}.tmp"
          "${ExtensionDeps}")
      file(REMOVE "${ExtensionDeps}.tmp")
  endif()
endfunction()

function(export_executable_symbols target)
  if (LLVM_EXPORTED_SYMBOL_FILE)
    # The symbol file should contain the symbols we want the executable to
    # export
    set_target_properties(${target} PROPERTIES ENABLE_EXPORTS 1)
  elseif (LLVM_EXPORT_SYMBOLS_FOR_PLUGINS)
    # Extract the symbols to export from the static libraries that the
    # executable links against.
    set_target_properties(${target} PROPERTIES ENABLE_EXPORTS 1)
    set(exported_symbol_file ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/${target}.symbols)
    # We need to consider not just the direct link dependencies, but also the
    # transitive link dependencies. Do this by starting with the set of direct
    # dependencies, then the dependencies of those dependencies, and so on.
    get_target_property(new_libs ${target} LINK_LIBRARIES)
    set(link_libs ${new_libs})
    while(NOT "${new_libs}" STREQUAL "")
      foreach(lib ${new_libs})
        if(TARGET ${lib})
          get_target_property(lib_type ${lib} TYPE)
          if("${lib_type}" STREQUAL "STATIC_LIBRARY")
            list(APPEND static_libs ${lib})
          else()
            list(APPEND other_libs ${lib})
          endif()
          get_target_property(transitive_libs ${lib} INTERFACE_LINK_LIBRARIES)
          foreach(transitive_lib ${transitive_libs})
            if(TARGET ${transitive_lib} AND NOT ${transitive_lib} IN_LIST link_libs)
              list(APPEND newer_libs ${transitive_lib})
              list(APPEND link_libs ${transitive_lib})
            endif()
          endforeach(transitive_lib)
        endif()
      endforeach(lib)
      set(new_libs ${newer_libs})
      set(newer_libs "")
    endwhile()
    list(REMOVE_DUPLICATES static_libs)
    if (MSVC)
      set(mangling microsoft)
    else()
      set(mangling itanium)
    endif()
    get_host_tool_path(llvm-nm LLVM_NM llvm_nm_exe llvm_nm_target)
    get_host_tool_path(llvm-readobj LLVM_READOBJ llvm_readobj_exe llvm_readobj_target)
    add_custom_command(OUTPUT ${exported_symbol_file}
                       COMMAND "${Python3_EXECUTABLE}"
                         ${LLVM_MAIN_SRC_DIR}/utils/extract_symbols.py
                         --mangling=${mangling} ${static_libs}
                         -o ${exported_symbol_file}
                         --nm=${llvm_nm_exe}
                         --readobj=${llvm_readobj_exe}
                       WORKING_DIRECTORY ${LLVM_LIBRARY_OUTPUT_INTDIR}
                       DEPENDS ${LLVM_MAIN_SRC_DIR}/utils/extract_symbols.py
                         ${static_libs} ${llvm_nm_target} ${llvm_readobj_target}
                       VERBATIM
                       COMMENT "Generating export list for ${target}")
    add_llvm_symbol_exports( ${target} ${exported_symbol_file} )
    # If something links against this executable then we want a
    # transitive link against only the libraries whose symbols
    # we aren't exporting.
    set_target_properties(${target} PROPERTIES INTERFACE_LINK_LIBRARIES "${other_libs}")
    # The default import library suffix that cmake uses for cygwin/mingw is
    # ".dll.a", but for clang.exe that causes a collision with libclang.dll,
    # where the import libraries of both get named libclang.dll.a. Use a suffix
    # of ".exe.a" to avoid this.
    if(CYGWIN OR MINGW)
      set_target_properties(${target} PROPERTIES IMPORT_SUFFIX ".exe.a")
    endif()
  elseif(NOT (WIN32 OR CYGWIN))
    # On Windows auto-exporting everything doesn't work because of the limit on
    # the size of the exported symbol table, but on other platforms we can do
    # it without any trouble.
    set_target_properties(${target} PROPERTIES ENABLE_EXPORTS 1)
    # CMake doesn't set CMAKE_EXE_EXPORTS_${lang}_FLAG on Solaris, so
    # ENABLE_EXPORTS has no effect.  While Solaris ld defaults to -rdynamic
    # behaviour, GNU ld needs it.
    if (APPLE OR ${CMAKE_SYSTEM_NAME} STREQUAL "SunOS")
      set_property(TARGET ${target} APPEND_STRING PROPERTY
        LINK_FLAGS " -rdynamic")
    endif()
  endif()
endfunction()

# Export symbols if LLVM plugins are enabled.
function(export_executable_symbols_for_plugins target)
  if(LLVM_ENABLE_PLUGINS OR LLVM_EXPORT_SYMBOLS_FOR_PLUGINS)
    export_executable_symbols(${target})
  endif()
endfunction()

if(NOT LLVM_TOOLCHAIN_TOOLS)
  set (LLVM_TOOLCHAIN_TOOLS
    llvm-ar
    llvm-cov
    llvm-cxxfilt
    llvm-dlltool
    llvm-dwp
    llvm-ranlib
    llvm-lib
    llvm-mca
    llvm-ml
    llvm-nm
    llvm-objcopy
    llvm-objdump
    llvm-pdbutil
    llvm-rc
    llvm-readobj
    llvm-size
    llvm-strings
    llvm-strip
    llvm-profdata
    llvm-symbolizer
    # symlink version of some of above tools that are enabled by
    # LLVM_INSTALL_BINUTILS_SYMLINKS.
    addr2line
    ar
    c++filt
    ranlib
    nm
    objcopy
    objdump
    readelf
    size
    strings
    strip
    )
  # Build llvm-mt if libxml2 is enabled. Can be used by runtimes.
  if (LLVM_ENABLE_LIBXML2)
    list(APPEND LLVM_TOOLCHAIN_TOOLS llvm-mt)
  endif()
endif()

macro(llvm_add_tool project name)
  cmake_parse_arguments(ARG "DEPENDS;GENERATE_DRIVER" "" "" ${ARGN})
  if( NOT LLVM_BUILD_TOOLS )
    set(EXCLUDE_FROM_ALL ON)
  endif()
  if(ARG_GENERATE_DRIVER
     AND LLVM_TOOL_LLVM_DRIVER_BUILD
     AND (NOT LLVM_DISTRIBUTION_COMPONENTS OR ${name} IN_LIST LLVM_DISTRIBUTION_COMPONENTS)
    )
    generate_llvm_objects(${name} ${ARGN})
    add_custom_target(${name} DEPENDS llvm-driver)
  else()
    add_llvm_executable(${name} ${ARGN})

    if ( ${name} IN_LIST LLVM_TOOLCHAIN_TOOLS OR NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
      if( LLVM_BUILD_TOOLS )
        get_target_export_arg(${name} LLVM export_to_llvmexports)
        install(TARGETS ${name}
                ${export_to_llvmexports}
                RUNTIME DESTINATION ${${project}_TOOLS_INSTALL_DIR}
                COMPONENT ${name})

        if (NOT LLVM_ENABLE_IDE)
          add_llvm_install_targets(install-${name}
                                  DEPENDS ${name}
                                  COMPONENT ${name})
        endif()
      endif()
    endif()
    if( LLVM_BUILD_TOOLS )
      set_property(GLOBAL APPEND PROPERTY LLVM_EXPORTS ${name})
    endif()
  endif()
  get_subproject_title(subproject_title)
  set_target_properties(${name} PROPERTIES FOLDER "${subproject_title}/Tools")
endmacro(llvm_add_tool project name)

macro(add_llvm_tool name)
  llvm_add_tool(LLVM ${ARGV})
endmacro()


macro(add_llvm_example name)
  if( NOT LLVM_BUILD_EXAMPLES )
    set(EXCLUDE_FROM_ALL ON)
  endif()
  add_llvm_executable(${name} ${ARGN})
  if( LLVM_BUILD_EXAMPLES )
    install(TARGETS ${name} RUNTIME DESTINATION "${LLVM_EXAMPLES_INSTALL_DIR}")
  endif()
  get_subproject_title(subproject_title)
  set_target_properties(${name} PROPERTIES FOLDER "${subproject_title}/Examples")
endmacro(add_llvm_example name)

macro(add_llvm_example_library name)
  if( NOT LLVM_BUILD_EXAMPLES )
    set(EXCLUDE_FROM_ALL ON)
    add_llvm_library(${name} BUILDTREE_ONLY ${ARGN})
  else()
    add_llvm_library(${name} ${ARGN})
  endif()

  get_subproject_title(subproject_title)
  set_target_properties(${name} PROPERTIES FOLDER "${subproject_title}/Examples")
endmacro(add_llvm_example_library name)

# This is a macro that is used to create targets for executables that are needed
# for development, but that are not intended to be installed by default.
macro(add_llvm_utility name)
  if ( NOT LLVM_BUILD_UTILS )
    set(EXCLUDE_FROM_ALL ON)
  endif()

  add_llvm_executable(${name} DISABLE_LLVM_LINK_LLVM_DYLIB ${ARGN})
  get_subproject_title(subproject_title)
  set_target_properties(${name} PROPERTIES FOLDER "${subproject_title}/Utils")
  if ( ${name} IN_LIST LLVM_TOOLCHAIN_UTILITIES OR NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
    if (LLVM_INSTALL_UTILS AND LLVM_BUILD_UTILS)
      get_target_export_arg(${name} LLVM export_to_llvmexports)
      install(TARGETS ${name}
              ${export_to_llvmexports}
              RUNTIME DESTINATION ${LLVM_UTILS_INSTALL_DIR}
              COMPONENT ${name})

      if (NOT LLVM_ENABLE_IDE)
        add_llvm_install_targets(install-${name}
                                 DEPENDS ${name}
                                 COMPONENT ${name})
      endif()
      set_property(GLOBAL APPEND PROPERTY LLVM_EXPORTS ${name})
    elseif(LLVM_BUILD_UTILS)
      set_property(GLOBAL APPEND PROPERTY LLVM_EXPORTS_BUILDTREE_ONLY ${name})
    endif()
  endif()
endmacro(add_llvm_utility name)

macro(add_llvm_fuzzer name)
  cmake_parse_arguments(ARG "" "DUMMY_MAIN" "" ${ARGN})
  get_subproject_title(subproject_title)
  if( LLVM_LIB_FUZZING_ENGINE )
    set(LLVM_OPTIONAL_SOURCES ${ARG_DUMMY_MAIN})
    add_llvm_executable(${name} ${ARG_UNPARSED_ARGUMENTS})
    target_link_libraries(${name} PRIVATE ${LLVM_LIB_FUZZING_ENGINE})
    set_target_properties(${name} PROPERTIES FOLDER "${subproject_title}/Fuzzers")
  elseif( LLVM_USE_SANITIZE_COVERAGE )
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=fuzzer")
    set(LLVM_OPTIONAL_SOURCES ${ARG_DUMMY_MAIN})
    add_llvm_executable(${name} ${ARG_UNPARSED_ARGUMENTS})
    set_target_properties(${name} PROPERTIES FOLDER "${subproject_title}/Fuzzers")
  elseif( ARG_DUMMY_MAIN )
    add_llvm_executable(${name} ${ARG_DUMMY_MAIN} ${ARG_UNPARSED_ARGUMENTS})
    set_target_properties(${name} PROPERTIES FOLDER "${subproject_title}/Fuzzers")
  endif()
endmacro()

macro(add_llvm_target target_name)
  include_directories(BEFORE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR})
  add_llvm_component_library(LLVM${target_name} ${ARGN})
  set( CURRENT_LLVM_TARGET LLVM${target_name} )
endmacro(add_llvm_target)

function(canonicalize_tool_name name output)
  string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}/" "" nameStrip ${name})
  string(REPLACE "-" "_" nameUNDERSCORE ${nameStrip})
  string(TOUPPER ${nameUNDERSCORE} nameUPPER)
  set(${output} "${nameUPPER}" PARENT_SCOPE)
endfunction(canonicalize_tool_name)

# Custom add_subdirectory wrapper
# Takes in a project name (i.e. LLVM), the subdirectory name, and an optional
# path if it differs from the name.
function(add_llvm_subdirectory project type name)
  set(add_llvm_external_dir "${ARGN}")
  if("${add_llvm_external_dir}" STREQUAL "")
    set(add_llvm_external_dir ${name})
  endif()
  canonicalize_tool_name(${name} nameUPPER)
  set(canonical_full_name ${project}_${type}_${nameUPPER})
  get_property(already_processed GLOBAL PROPERTY ${canonical_full_name}_PROCESSED)
  if(already_processed)
    return()
  endif()
  set_property(GLOBAL PROPERTY ${canonical_full_name}_PROCESSED YES)

  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${add_llvm_external_dir}/CMakeLists.txt)
    # Treat it as in-tree subproject.
    option(${canonical_full_name}_BUILD
           "Whether to build ${name} as part of ${project}" On)
    mark_as_advanced(${project}_${type}_${name}_BUILD)
    if(${canonical_full_name}_BUILD)
      add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/${add_llvm_external_dir} ${add_llvm_external_dir})
    endif()
  else()
    set(LLVM_EXTERNAL_${nameUPPER}_SOURCE_DIR
      "${LLVM_EXTERNAL_${nameUPPER}_SOURCE_DIR}"
      CACHE PATH "Path to ${name} source directory")
    set(${canonical_full_name}_BUILD_DEFAULT ON)
    if(NOT LLVM_EXTERNAL_${nameUPPER}_SOURCE_DIR OR NOT EXISTS ${LLVM_EXTERNAL_${nameUPPER}_SOURCE_DIR})
      set(${canonical_full_name}_BUILD_DEFAULT OFF)
    endif()
    if("${LLVM_EXTERNAL_${nameUPPER}_BUILD}" STREQUAL "OFF")
      set(${canonical_full_name}_BUILD_DEFAULT OFF)
    endif()
    option(${canonical_full_name}_BUILD
      "Whether to build ${name} as part of LLVM"
      ${${canonical_full_name}_BUILD_DEFAULT})
    if (${canonical_full_name}_BUILD)
      if(EXISTS ${LLVM_EXTERNAL_${nameUPPER}_SOURCE_DIR})
        add_subdirectory(${LLVM_EXTERNAL_${nameUPPER}_SOURCE_DIR} ${add_llvm_external_dir})
      elseif(NOT "${LLVM_EXTERNAL_${nameUPPER}_SOURCE_DIR}" STREQUAL "")
        message(WARNING "Nonexistent directory for ${name}: ${LLVM_EXTERNAL_${nameUPPER}_SOURCE_DIR}")
      endif()
    endif()
  endif()
endfunction()

# Add external project that may want to be built as part of llvm such as Clang,
# lld, and Polly. This adds two options. One for the source directory of the
# project, which defaults to ${CMAKE_CURRENT_SOURCE_DIR}/${name}. Another to
# enable or disable building it with everything else.
# Additional parameter can be specified as the name of directory.
macro(add_llvm_external_project name)
  add_llvm_subdirectory(LLVM TOOL ${name} ${ARGN})
endmacro()

macro(add_llvm_tool_subdirectory name)
  add_llvm_external_project(${name})
endmacro(add_llvm_tool_subdirectory)

macro(add_custom_linker_flags name)
  if (LLVM_${name}_LINKER_FLAGS)
    message(DEBUG "Applying ${LLVM_${name}_LINKER_FLAGS} to ${name}")
    target_link_options(${name} PRIVATE ${LLVM_${name}_LINKER_FLAGS})
  endif()
endmacro()

function(get_project_name_from_src_var var output)
  string(REGEX MATCH "LLVM_EXTERNAL_(.*)_SOURCE_DIR"
         MACHED_TOOL "${var}")
  if(MACHED_TOOL)
    set(${output} ${CMAKE_MATCH_1} PARENT_SCOPE)
  else()
    set(${output} PARENT_SCOPE)
  endif()
endfunction()

function(create_subdirectory_options project type)
  file(GLOB sub-dirs "${CMAKE_CURRENT_SOURCE_DIR}/*")
  foreach(dir ${sub-dirs})
    if(IS_DIRECTORY "${dir}" AND EXISTS "${dir}/CMakeLists.txt")
      canonicalize_tool_name(${dir} name)
      option(${project}_${type}_${name}_BUILD
           "Whether to build ${name} as part of ${project}" On)
      mark_as_advanced(${project}_${type}_${name}_BUILD)
    endif()
  endforeach()
endfunction(create_subdirectory_options)

function(create_llvm_tool_options)
  create_subdirectory_options(LLVM TOOL)
endfunction(create_llvm_tool_options)

function(llvm_add_implicit_projects project)
  set(list_of_implicit_subdirs "")
  file(GLOB sub-dirs "${CMAKE_CURRENT_SOURCE_DIR}/*")
  foreach(dir ${sub-dirs})
    if(IS_DIRECTORY "${dir}" AND EXISTS "${dir}/CMakeLists.txt")
      canonicalize_tool_name(${dir} name)
      # I don't like special casing things by order, but the llvm-driver ends up
      # linking the object libraries from all the tools that opt-in, so adding
      # it separately at the end is probably the simplest case.
      if("${name}" STREQUAL "LLVM_DRIVER")
        continue()
      endif()
      if (${project}_TOOL_${name}_BUILD)
        get_filename_component(fn "${dir}" NAME)
        list(APPEND list_of_implicit_subdirs "${fn}")
      endif()
    endif()
  endforeach()

  foreach(external_proj ${list_of_implicit_subdirs})
    add_llvm_subdirectory(${project} TOOL "${external_proj}" ${ARGN})
  endforeach()
endfunction(llvm_add_implicit_projects)

function(add_llvm_implicit_projects)
  llvm_add_implicit_projects(LLVM)
endfunction(add_llvm_implicit_projects)

# Generic support for adding a unittest.
function(add_unittest test_suite test_name)
  if( NOT LLVM_BUILD_TESTS )
    set(EXCLUDE_FROM_ALL ON)
  endif()

  if (SUPPORTS_VARIADIC_MACROS_FLAG)
    list(APPEND LLVM_COMPILE_FLAGS "-Wno-variadic-macros")
  endif()
  # Some parts of gtest rely on this GNU extension, don't warn on it.
  if(SUPPORTS_GNU_ZERO_VARIADIC_MACRO_ARGUMENTS_FLAG)
    list(APPEND LLVM_COMPILE_FLAGS "-Wno-gnu-zero-variadic-macro-arguments")
  endif()

  if (NOT DEFINED LLVM_REQUIRES_RTTI)
    set(LLVM_REQUIRES_RTTI OFF)
  endif()

  list(APPEND LLVM_LINK_COMPONENTS Support) # gtest needs it for raw_ostream
  add_llvm_executable(${test_name} IGNORE_EXTERNALIZE_DEBUGINFO NO_INSTALL_RPATH ${ARGN})
  get_subproject_title(subproject_title)
  set_target_properties(${test_name} PROPERTIES FOLDER "${subproject_title}/Tests/Unit")

  # The runtime benefits of LTO don't outweight the compile time costs for tests.
  if(LLVM_ENABLE_LTO)
    if((UNIX OR MINGW) AND LINKER_IS_LLD)
      if(LLVM_ENABLE_FATLTO AND NOT APPLE)
        # When using FatLTO, just use relocatable linking.
        set_property(TARGET ${test_name} APPEND_STRING PROPERTY
                      LINK_FLAGS " -Wl,--no-fat-lto-objects")
      else()
        set_property(TARGET ${test_name} APPEND_STRING PROPERTY
                      LINK_FLAGS " -Wl,--lto-O0")
      endif()
    elseif(LINKER_IS_LLD_LINK)
      set_property(TARGET ${test_name} APPEND_STRING PROPERTY
                    LINK_FLAGS " /opt:lldlto=0")
    elseif(APPLE AND NOT uppercase_LLVM_ENABLE_LTO STREQUAL "THIN")
      set_property(TARGET ${target_name} APPEND_STRING PROPERTY
                    LINK_FLAGS " -Wl,-mllvm,-O0")
    endif()
  endif()

  target_link_options(${test_name} PRIVATE "${LLVM_UNITTEST_LINK_FLAGS}")

  set(outdir ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR})
  set_output_directory(${test_name} BINARY_DIR ${outdir} LIBRARY_DIR ${outdir})
  # libpthreads overrides some standard library symbols, so main
  # executable must be linked with it in order to provide consistent
  # API for all shared libaries loaded by this executable.
  target_link_libraries(${test_name} PRIVATE llvm_gtest_main llvm_gtest ${LLVM_PTHREAD_LIB})

  add_dependencies(${test_suite} ${test_name})
endfunction()

# Use for test binaries that call llvm::getInputFileDirectory(). Use of this
# is discouraged.
function(add_unittest_with_input_files test_suite test_name)
  set(LLVM_UNITTEST_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
  configure_file(
    ${LLVM_MAIN_SRC_DIR}/unittests/unittest.cfg.in
    ${CMAKE_CURRENT_BINARY_DIR}/llvm.srcdir.txt)

  add_unittest(${test_suite} ${test_name} ${ARGN})
endfunction()

# Generic support for adding a benchmark.
function(add_benchmark benchmark_name)
  if( NOT LLVM_BUILD_BENCHMARKS )
    set(EXCLUDE_FROM_ALL ON)
  endif()

  add_llvm_executable(${benchmark_name} IGNORE_EXTERNALIZE_DEBUGINFO NO_INSTALL_RPATH ${ARGN})
  set(outdir ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR})
  set_output_directory(${benchmark_name} BINARY_DIR ${outdir} LIBRARY_DIR ${outdir})
  get_subproject_title(subproject_title)
  set_property(TARGET ${benchmark_name} PROPERTY FOLDER "${subproject_title}/Benchmarks")
  target_link_libraries(${benchmark_name} PRIVATE benchmark)
endfunction()

# This function canonicalize the CMake variables passed by names
# from CMake boolean to 0/1 suitable for passing into Python or C++,
# in place.
function(llvm_canonicalize_cmake_booleans)
  foreach(var ${ARGN})
    if(${var})
      set(${var} 1 PARENT_SCOPE)
    else()
      set(${var} 0 PARENT_SCOPE)
    endif()
  endforeach()
endfunction(llvm_canonicalize_cmake_booleans)

macro(set_llvm_build_mode)
  # Configuration-time: See Unit/lit.site.cfg.in
  if (CMAKE_CFG_INTDIR STREQUAL ".")
    set(LLVM_BUILD_MODE ".")
  else ()
    set(LLVM_BUILD_MODE "%(build_mode)s")
  endif ()
endmacro()

# Takes a list of path names in pathlist and a base directory, and returns
# a list of paths relative to the base directory in out_pathlist.
# Paths that are on a different drive than the basedir (on Windows) or that
# contain symlinks are returned absolute.
# Use with LLVM_LIT_PATH_FUNCTION below.
function(make_paths_relative out_pathlist basedir pathlist)
  # Passing ARG_PATH_VALUES as-is to execute_process() makes cmake strip
  # empty list entries. So escape the ;s in the list and do the splitting
  # ourselves. cmake has no relpath function, so use Python for that.
  string(REPLACE ";" "\\;" pathlist_escaped "${pathlist}")
  execute_process(COMMAND "${Python3_EXECUTABLE}" "-c" "\n
import os, sys\n
base = sys.argv[1]
def haslink(p):\n
    if not p or p == os.path.dirname(p): return False\n
    return os.path.islink(p) or haslink(os.path.dirname(p))\n
def relpath(p):\n
    if not p: return ''\n
    if os.path.splitdrive(p)[0] != os.path.splitdrive(base)[0]: return p\n
    if haslink(p) or haslink(base): return p\n
    return os.path.relpath(p, base)\n
if len(sys.argv) < 3: sys.exit(0)\n
sys.stdout.write(';'.join(relpath(p) for p in sys.argv[2].split(';')))"
    ${basedir}
    ${pathlist_escaped}
    OUTPUT_VARIABLE pathlist_relative
    ERROR_VARIABLE error
    RESULT_VARIABLE result)
  if (NOT result EQUAL 0)
    message(FATAL_ERROR "make_paths_relative() failed due to error '${result}', with stderr\n${error}")
  endif()
  set(${out_pathlist} "${pathlist_relative}" PARENT_SCOPE)
endfunction()

# Converts a file that's relative to the current python file to an absolute
# path. Since this uses __file__, it has to be emitted into python files that
# use it and can't be in a lit module. Use with make_paths_relative().
string(CONCAT LLVM_LIT_PATH_FUNCTION
  "# Allow generated file to be relocatable.\n"
  "import os\n"
  "import platform\n"
  "def path(p):\n"
  "    if not p: return ''\n"
  "    # Follows lit.util.abs_path_preserve_drive, which cannot be imported here.\n"
  "    if platform.system() == 'Windows':\n"
  "        return os.path.abspath(os.path.join(os.path.dirname(__file__), p))\n"
  "    else:\n"
  "        return os.path.realpath(os.path.join(os.path.dirname(__file__), p))\n"
  )

# This function provides an automatic way to 'configure'-like generate a file
# based on a set of common and custom variables, specifically targeting the
# variables needed for the 'lit.site.cfg' files. This function bundles the
# common variables that any Lit instance is likely to need, and custom
# variables can be passed in.
# The keyword PATHS is followed by a list of cmake variable names that are
# mentioned as `path("@varname@")` in the lit.cfg.py.in file. Variables in that
# list are treated as paths that are relative to the directory the generated
# lit.cfg.py file is in, and the `path()` function converts the relative
# path back to absolute form. This makes it possible to move a build directory
# containing lit.cfg.py files from one machine to another.
function(configure_lit_site_cfg site_in site_out)
  cmake_parse_arguments(ARG "" "" "MAIN_CONFIG;PATHS" ${ARGN})

  if ("${ARG_MAIN_CONFIG}" STREQUAL "")
    get_filename_component(INPUT_DIR ${site_in} DIRECTORY)
    set(ARG_MAIN_CONFIG "${INPUT_DIR}/lit.cfg")
  endif()

  foreach(c ${LLVM_TARGETS_TO_BUILD})
    set(TARGETS_BUILT "${TARGETS_BUILT} ${c}")
  endforeach(c)
  set(TARGETS_TO_BUILD ${TARGETS_BUILT})

  set(SHLIBEXT "${LTDL_SHLIB_EXT}")

  set_llvm_build_mode()

  # For standalone builds of subprojects, these might not be the build tree but
  # a provided binary tree.
  set(LLVM_SOURCE_DIR ${LLVM_MAIN_SRC_DIR})
  set(LLVM_BINARY_DIR ${LLVM_BINARY_DIR})
  string(REPLACE "${CMAKE_CFG_INTDIR}" "${LLVM_BUILD_MODE}" LLVM_TOOLS_DIR "${LLVM_TOOLS_BINARY_DIR}")
  string(REPLACE "${CMAKE_CFG_INTDIR}" "${LLVM_BUILD_MODE}" LLVM_LIBS_DIR  "${LLVM_LIBRARY_DIR}")
  # Like LLVM_{TOOLS,LIBS}_DIR, but pointing at the build tree.
  string(REPLACE "${CMAKE_CFG_INTDIR}" "${LLVM_BUILD_MODE}" CURRENT_TOOLS_DIR "${LLVM_RUNTIME_OUTPUT_INTDIR}")
  string(REPLACE "${CMAKE_CFG_INTDIR}" "${LLVM_BUILD_MODE}" CURRENT_LIBS_DIR  "${LLVM_LIBRARY_OUTPUT_INTDIR}")
  string(REPLACE "${CMAKE_CFG_INTDIR}" "${LLVM_BUILD_MODE}" SHLIBDIR "${LLVM_SHLIB_OUTPUT_INTDIR}")

  # FIXME: "ENABLE_SHARED" doesn't make sense, since it is used just for
  # plugins. We may rename it.
  if(LLVM_ENABLE_PLUGINS)
    set(ENABLE_SHARED "1")
  else()
    set(ENABLE_SHARED "0")
  endif()

  if(LLVM_ENABLE_ASSERTIONS)
    set(ENABLE_ASSERTIONS "1")
  else()
    set(ENABLE_ASSERTIONS "0")
  endif()

  set(HOST_OS ${CMAKE_SYSTEM_NAME})
  set(HOST_ARCH ${CMAKE_SYSTEM_PROCESSOR})

  set(HOST_CC "${CMAKE_C_COMPILER} ${CMAKE_C_COMPILER_ARG1}")
  set(HOST_CXX "${CMAKE_CXX_COMPILER} ${CMAKE_CXX_COMPILER_ARG1}")
  set(HOST_LDFLAGS "${CMAKE_EXE_LINKER_FLAGS}")

  string(CONCAT LIT_SITE_CFG_IN_HEADER
    "# Autogenerated from ${site_in}\n# Do not edit!\n\n"
    "${LLVM_LIT_PATH_FUNCTION}"
    )

  # Override config_target_triple (and the env)
  if(LLVM_TARGET_TRIPLE_ENV)
    # This is expanded into the heading.
    string(CONCAT LIT_SITE_CFG_IN_HEADER "${LIT_SITE_CFG_IN_HEADER}"
      "import os\n"
      "target_env = \"${LLVM_TARGET_TRIPLE_ENV}\"\n"
      "config.target_triple = config.environment[target_env] = os.environ.get(target_env, \"${LLVM_TARGET_TRIPLE}\")\n"
      )

    # This is expanded to; config.target_triple = ""+config.target_triple+""
    set(LLVM_TARGET_TRIPLE "\"+config.target_triple+\"")
  endif()

  if (ARG_PATHS)
    # Walk ARG_PATHS and collect the current value of the variables in there.
    # list(APPEND) ignores empty elements exactly if the list is empty,
    # so start the list with a dummy element and drop it, to make sure that
    # even empty values make it into the values list.
    set(ARG_PATH_VALUES "dummy")
    foreach(path ${ARG_PATHS})
      list(APPEND ARG_PATH_VALUES "${${path}}")
    endforeach()
    list(REMOVE_AT ARG_PATH_VALUES 0)

    get_filename_component(OUTPUT_DIR ${site_out} DIRECTORY)
    make_paths_relative(
        ARG_PATH_VALUES_RELATIVE "${OUTPUT_DIR}" "${ARG_PATH_VALUES}")

    list(LENGTH ARG_PATHS len_paths)
    list(LENGTH ARG_PATH_VALUES len_path_values)
    list(LENGTH ARG_PATH_VALUES_RELATIVE len_path_value_rels)
    if ((NOT ${len_paths} EQUAL ${len_path_values}) OR
        (NOT ${len_paths} EQUAL ${len_path_value_rels}))
      message(SEND_ERROR "PATHS lengths got confused")
    endif()

    # Transform variables mentioned in ARG_PATHS to relative paths for
    # the configure_file() call. Variables are copied to subscopeds by cmake,
    # so this only modifies the local copy of the variables.
    math(EXPR arg_path_limit "${len_paths} - 1")
    foreach(i RANGE ${arg_path_limit})
      list(GET ARG_PATHS ${i} val1)
      list(GET ARG_PATH_VALUES_RELATIVE ${i} val2)
      set(${val1} ${val2})
    endforeach()
  endif()

  configure_file(${site_in} ${site_out} @ONLY)

  if (EXISTS "${ARG_MAIN_CONFIG}")
    # Remember main config / generated site config for llvm-lit.in.
    get_property(LLVM_LIT_CONFIG_FILES GLOBAL PROPERTY LLVM_LIT_CONFIG_FILES)
    list(APPEND LLVM_LIT_CONFIG_FILES "${ARG_MAIN_CONFIG}" "${site_out}")
    set_property(GLOBAL PROPERTY LLVM_LIT_CONFIG_FILES ${LLVM_LIT_CONFIG_FILES})
  endif()
endfunction()

function(dump_all_cmake_variables)
  get_cmake_property(_variableNames VARIABLES)
  foreach (_variableName ${_variableNames})
    message(STATUS "${_variableName}=${${_variableName}}")
  endforeach()
endfunction()

function(get_llvm_lit_path base_dir file_name)
  cmake_parse_arguments(ARG "ALLOW_EXTERNAL" "" "" ${ARGN})

  if (ARG_ALLOW_EXTERNAL)
    set (LLVM_EXTERNAL_LIT "" CACHE STRING "Command used to spawn lit")
    if ("${LLVM_EXTERNAL_LIT}" STREQUAL "")
      set(LLVM_EXTERNAL_LIT "${LLVM_DEFAULT_EXTERNAL_LIT}")
    endif()

    if (NOT "${LLVM_EXTERNAL_LIT}" STREQUAL "")
      if (EXISTS ${LLVM_EXTERNAL_LIT})
        get_filename_component(LIT_FILE_NAME ${LLVM_EXTERNAL_LIT} NAME)
        get_filename_component(LIT_BASE_DIR ${LLVM_EXTERNAL_LIT} DIRECTORY)
        set(${file_name} ${LIT_FILE_NAME} PARENT_SCOPE)
        set(${base_dir} ${LIT_BASE_DIR} PARENT_SCOPE)
        return()
      elseif (NOT DEFINED CACHE{LLVM_EXTERNAL_LIT_MISSING_WARNED_ONCE})
        message(WARNING "LLVM_EXTERNAL_LIT set to ${LLVM_EXTERNAL_LIT}, but the path does not exist.")
        set(LLVM_EXTERNAL_LIT_MISSING_WARNED_ONCE YES CACHE INTERNAL "")
      endif()
    endif()
  endif()

  set(lit_file_name "llvm-lit")
  if (CMAKE_HOST_WIN32 AND NOT CYGWIN)
    # llvm-lit needs suffix.py for multiprocess to find a main module.
    set(lit_file_name "${lit_file_name}.py")
  endif ()
  set(${file_name} ${lit_file_name} PARENT_SCOPE)

  get_property(LLVM_LIT_BASE_DIR GLOBAL PROPERTY LLVM_LIT_BASE_DIR)
  if (NOT "${LLVM_LIT_BASE_DIR}" STREQUAL "")
    set(${base_dir} ${LLVM_LIT_BASE_DIR} PARENT_SCOPE)
  endif()

  # Allow individual projects to provide an override
  if (NOT "${LLVM_LIT_OUTPUT_DIR}" STREQUAL "")
    set(LLVM_LIT_BASE_DIR ${LLVM_LIT_OUTPUT_DIR})
  elseif(NOT "${LLVM_RUNTIME_OUTPUT_INTDIR}" STREQUAL "")
    set(LLVM_LIT_BASE_DIR ${LLVM_RUNTIME_OUTPUT_INTDIR})
  else()
    set(LLVM_LIT_BASE_DIR "")
  endif()

  # Cache this so we don't have to do it again and have subsequent calls
  # potentially disagree on the value.
  set_property(GLOBAL PROPERTY LLVM_LIT_BASE_DIR ${LLVM_LIT_BASE_DIR})
  set(${base_dir} ${LLVM_LIT_BASE_DIR} PARENT_SCOPE)
endfunction()

# A raw function to create a lit target. This is used to implement the testuite
# management functions.
function(add_lit_target target comment)
  cmake_parse_arguments(ARG "" "" "PARAMS;DEPENDS;ARGS" ${ARGN})
  set(LIT_ARGS "${ARG_ARGS} ${LLVM_LIT_ARGS}")
  separate_arguments(LIT_ARGS)
  if (NOT CMAKE_CFG_INTDIR STREQUAL ".")
    list(APPEND LIT_ARGS --param build_mode=${CMAKE_CFG_INTDIR})
  endif ()

  # Get the path to the lit to *run* tests with.  This can be overriden by
  # the user by specifying -DLLVM_EXTERNAL_LIT=<path-to-lit.py>
  get_llvm_lit_path(
    lit_base_dir
    lit_file_name
    ALLOW_EXTERNAL
    )

  set(LIT_COMMAND "${Python3_EXECUTABLE};${lit_base_dir}/${lit_file_name}")
  list(APPEND LIT_COMMAND ${LIT_ARGS})
  foreach(param ${ARG_PARAMS})
    list(APPEND LIT_COMMAND --param ${param})
  endforeach()
  if (ARG_UNPARSED_ARGUMENTS)
    add_custom_target(${target}
      COMMAND ${LIT_COMMAND} ${ARG_UNPARSED_ARGUMENTS}
      COMMENT "${comment}"
      USES_TERMINAL
      )
  else()
    add_custom_target(${target}
      COMMAND ${CMAKE_COMMAND} -E echo "${target} does nothing, no tools built.")
    message(STATUS "${target} does nothing.")
  endif()
  get_subproject_title(subproject_title)
  set_target_properties(${target} PROPERTIES FOLDER "${subproject_title}/Tests")

  if (ARG_DEPENDS)
    add_dependencies(${target} ${ARG_DEPENDS})
  endif()

  # Tests should be excluded from "Build Solution".
  set_target_properties(${target} PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD ON)
endfunction()

# Convert a target name like check-clang to a variable name like CLANG.
function(umbrella_lit_testsuite_var target outvar)
  if (NOT target MATCHES "^check-")
    message(FATAL_ERROR "umbrella lit suites must be check-*, not '${target}'")
  endif()
  string(SUBSTRING "${target}" 6 -1 var)
  string(REPLACE "-" "_" var ${var})
  string(TOUPPER "${var}" var)
  set(${outvar} "${var}" PARENT_SCOPE)
endfunction()

# Start recording all lit test suites for a combined 'check-foo' target.
# The recording continues until umbrella_lit_testsuite_end() creates the target.
function(umbrella_lit_testsuite_begin target)
  umbrella_lit_testsuite_var(${target} name)
  set_property(GLOBAL APPEND PROPERTY LLVM_LIT_UMBRELLAS ${name})
endfunction()

# Create a combined 'check-foo' target for a set of related test suites.
# It runs all suites added since the matching umbrella_lit_testsuite_end() call.
# Tests marked EXCLUDE_FROM_CHECK_ALL are not gathered.
function(umbrella_lit_testsuite_end target)
  umbrella_lit_testsuite_var(${target} name)

  get_property(testsuites GLOBAL PROPERTY LLVM_${name}_LIT_TESTSUITES)
  get_property(params GLOBAL PROPERTY LLVM_${name}_LIT_PARAMS)
  get_property(depends GLOBAL PROPERTY LLVM_${name}_LIT_DEPENDS)
  get_property(extra_args GLOBAL PROPERTY LLVM_${name}_LIT_EXTRA_ARGS)
  # Additional test targets are not gathered, but may be set externally.
  get_property(additional_test_targets
               GLOBAL PROPERTY LLVM_${name}_ADDITIONAL_TEST_TARGETS)

  string(TOLOWER ${name} name)
  add_lit_target(${target}
    "Running ${name} regression tests"
    ${testsuites}
    PARAMS ${params}
    DEPENDS ${depends} ${additional_test_targets}
    ARGS ${extra_args}
    )
endfunction()

# A function to add a set of lit test suites to be driven through 'check-*' targets.
function(add_lit_testsuite target comment)
  cmake_parse_arguments(ARG "EXCLUDE_FROM_CHECK_ALL" "" "PARAMS;DEPENDS;ARGS" ${ARGN})

  # EXCLUDE_FROM_ALL excludes the test ${target} out of check-all.
  if(NOT ARG_EXCLUDE_FROM_CHECK_ALL)
    get_property(gather_names GLOBAL PROPERTY LLVM_LIT_UMBRELLAS)
    foreach(name ${gather_names})
    # Register the testsuites, params and depends for the umbrella check rule.
      set_property(GLOBAL APPEND PROPERTY LLVM_${name}_LIT_TESTSUITES ${ARG_UNPARSED_ARGUMENTS})
      set_property(GLOBAL APPEND PROPERTY LLVM_${name}_LIT_PARAMS ${ARG_PARAMS})
      set_property(GLOBAL APPEND PROPERTY LLVM_${name}_LIT_DEPENDS ${ARG_DEPENDS})
      set_property(GLOBAL APPEND PROPERTY LLVM_${name}_LIT_EXTRA_ARGS ${ARG_ARGS})
    endforeach()
  endif()

  # Produce a specific suffixed check rule.
  add_lit_target(${target} ${comment}
    ${ARG_UNPARSED_ARGUMENTS}
    PARAMS ${ARG_PARAMS}
    DEPENDS ${ARG_DEPENDS}
    ARGS ${ARG_ARGS}
    )
endfunction()

function(add_lit_testsuites project directory)
  if (NOT LLVM_ENABLE_IDE)
    cmake_parse_arguments(ARG "EXCLUDE_FROM_CHECK_ALL" "FOLDER" "PARAMS;DEPENDS;ARGS" ${ARGN})

    if (NOT ARG_FOLDER)
      get_subproject_title(subproject_title)
      set(ARG_FOLDER "${subproject_title}/Tests/LIT Testsuites")
    endif()

    # Search recursively for test directories by assuming anything not
    # in a directory called Inputs contains tests.
    file(GLOB_RECURSE to_process LIST_DIRECTORIES true ${directory}/*)
    foreach(lit_suite ${to_process})
      if(NOT IS_DIRECTORY ${lit_suite})
        continue()
      endif()
      string(FIND ${lit_suite} Inputs is_inputs)
      string(FIND ${lit_suite} Output is_output)
      if (NOT (is_inputs EQUAL -1 AND is_output EQUAL -1))
        continue()
      endif()

      # Create a check- target for the directory.
      string(REPLACE ${directory} "" name_slash ${lit_suite})
      if (name_slash)
        string(REPLACE "/" "-" name_slash ${name_slash})
        string(REPLACE "\\" "-" name_dashes ${name_slash})
        string(TOLOWER "${project}${name_dashes}" name_var)
        add_lit_target("check-${name_var}" "Running lit suite ${lit_suite}"
          ${lit_suite}
          ${EXCLUDE_FROM_CHECK_ALL}
          PARAMS ${ARG_PARAMS}
          DEPENDS ${ARG_DEPENDS}
          ARGS ${ARG_ARGS}
        )
        set_target_properties(check-${name_var} PROPERTIES FOLDER ${ARG_FOLDER})
      endif()
    endforeach()
  endif()
endfunction()

function(llvm_install_library_symlink name dest type)
  cmake_parse_arguments(ARG "FULL_DEST" "COMPONENT" "" ${ARGN})
  foreach(path ${CMAKE_MODULE_PATH})
    if(EXISTS ${path}/LLVMInstallSymlink.cmake)
      set(INSTALL_SYMLINK ${path}/LLVMInstallSymlink.cmake)
      break()
    endif()
  endforeach()

  set(component ${ARG_COMPONENT})
  if(NOT component)
    set(component ${name})
  endif()

  set(full_name ${CMAKE_${type}_LIBRARY_PREFIX}${name}${CMAKE_${type}_LIBRARY_SUFFIX})
  if (ARG_FULL_DEST)
    set(full_dest ${dest})
  else()
    set(full_dest ${CMAKE_${type}_LIBRARY_PREFIX}${dest}${CMAKE_${type}_LIBRARY_SUFFIX})
  endif()

  if(LLVM_USE_SYMLINKS)
    set(LLVM_LINK_OR_COPY create_symlink)
  else()
    set(LLVM_LINK_OR_COPY copy)
  endif()

  set(output_dir lib${LLVM_LIBDIR_SUFFIX})
  if(WIN32 AND "${type}" STREQUAL "SHARED")
    set(output_dir "${CMAKE_INSTALL_BINDIR}")
  endif()

  install(SCRIPT ${INSTALL_SYMLINK}
          CODE "install_symlink(\"${full_name}\" \"${full_dest}\" \"${output_dir}\" \"${LLVM_LINK_OR_COPY}\")"
          COMPONENT ${component})

endfunction()

function(llvm_install_symlink project name dest)
  get_property(LLVM_DRIVER_TOOLS GLOBAL PROPERTY LLVM_DRIVER_TOOLS)
  if(LLVM_TOOL_LLVM_DRIVER_BUILD
     AND ${dest} IN_LIST LLVM_DRIVER_TOOLS
     AND (NOT LLVM_DISTRIBUTION_COMPONENTS OR ${dest} IN_LIST LLVM_DISTRIBUTION_COMPONENTS)
    )
    return()
  endif()
  cmake_parse_arguments(ARG "ALWAYS_GENERATE" "COMPONENT" "" ${ARGN})
  foreach(path ${CMAKE_MODULE_PATH})
    if(EXISTS ${path}/LLVMInstallSymlink.cmake)
      set(INSTALL_SYMLINK ${path}/LLVMInstallSymlink.cmake)
      break()
    endif()
  endforeach()

  if(ARG_COMPONENT)
    set(component ${ARG_COMPONENT})
  else()
    if(ARG_ALWAYS_GENERATE)
      set(component ${dest})
    else()
      set(component ${name})
    endif()
  endif()

  set(full_name ${name}${CMAKE_EXECUTABLE_SUFFIX})
  set(full_dest ${dest}${CMAKE_EXECUTABLE_SUFFIX})
  if (${dest} STREQUAL "llvm-driver")
    set(full_dest llvm${CMAKE_EXECUTABLE_SUFFIX})
  endif()

  if(LLVM_USE_SYMLINKS)
    set(LLVM_LINK_OR_COPY create_symlink)
  else()
    set(LLVM_LINK_OR_COPY copy)
  endif()

  set(output_dir "${${project}_TOOLS_INSTALL_DIR}")

  install(SCRIPT ${INSTALL_SYMLINK}
          CODE "install_symlink(\"${full_name}\" \"${full_dest}\" \"${output_dir}\" \"${LLVM_LINK_OR_COPY}\")"
          COMPONENT ${component})

  if (NOT LLVM_ENABLE_IDE AND NOT ARG_ALWAYS_GENERATE)
    add_llvm_install_targets(install-${name}
                             DEPENDS ${name} ${dest}
                             COMPONENT ${component}
                             SYMLINK ${dest})
  endif()
endfunction()

function(llvm_add_tool_symlink project link_name target)
  cmake_parse_arguments(ARG "ALWAYS_GENERATE" "OUTPUT_DIR" "" ${ARGN})

  get_property(LLVM_DRIVER_TOOLS GLOBAL PROPERTY LLVM_DRIVER_TOOLS)

  if (${target} IN_LIST LLVM_DRIVER_TOOLS)
    set_property(GLOBAL APPEND PROPERTY LLVM_DRIVER_TOOL_ALIASES_${target} ${link_name})
  endif()
  set(dest_binary "$<TARGET_FILE:${target}>")

  # This got a bit gross... For multi-configuration generators the target
  # properties return the resolved value of the string, not the build system
  # expression. To reconstruct the platform-agnostic path we have to do some
  # magic. First we grab one of the types, and a type-specific path. Then from
  # the type-specific path we find the last occurrence of the type in the path,
  # and replace it with CMAKE_CFG_INTDIR. This allows the build step to be type
  # agnostic again.
  if(NOT ARG_OUTPUT_DIR)
    # If you're not overriding the OUTPUT_DIR, we can make the link relative in
    # the same directory.
    if(LLVM_USE_SYMLINKS)
      set(dest_binary "$<TARGET_FILE_NAME:${target}>")
    endif()
    if(CMAKE_CONFIGURATION_TYPES)
      list(GET CMAKE_CONFIGURATION_TYPES 0 first_type)
      string(TOUPPER ${first_type} first_type_upper)
      set(first_type_suffix _${first_type_upper})
    endif()
    get_target_property(target_type ${target} TYPE)
    if(${target_type} STREQUAL "STATIC_LIBRARY")
      get_target_property(ARG_OUTPUT_DIR ${target} ARCHIVE_OUTPUT_DIRECTORY${first_type_suffix})
    elseif(UNIX AND ${target_type} STREQUAL "SHARED_LIBRARY")
      get_target_property(ARG_OUTPUT_DIR ${target} LIBRARY_OUTPUT_DIRECTORY${first_type_suffix})
    else()
      get_target_property(ARG_OUTPUT_DIR ${target} RUNTIME_OUTPUT_DIRECTORY${first_type_suffix})
    endif()
    if(CMAKE_CONFIGURATION_TYPES)
      string(FIND "${ARG_OUTPUT_DIR}" "/${first_type}/" type_start REVERSE)
      string(SUBSTRING "${ARG_OUTPUT_DIR}" 0 ${type_start} path_prefix)
      string(SUBSTRING "${ARG_OUTPUT_DIR}" ${type_start} -1 path_suffix)
      string(REPLACE "/${first_type}/" "/${CMAKE_CFG_INTDIR}/"
             path_suffix ${path_suffix})
      set(ARG_OUTPUT_DIR ${path_prefix}${path_suffix})
    endif()
  endif()

  if(LLVM_USE_SYMLINKS)
    set(LLVM_LINK_OR_COPY create_symlink)
  else()
    set(LLVM_LINK_OR_COPY copy)
  endif()

  set(output_path "${ARG_OUTPUT_DIR}/${link_name}${CMAKE_EXECUTABLE_SUFFIX}")

  set(target_name ${link_name})
  if(TARGET ${link_name})
    set(target_name ${link_name}-link)
  endif()


  if(ARG_ALWAYS_GENERATE)
    set_property(DIRECTORY APPEND PROPERTY
      ADDITIONAL_MAKE_CLEAN_FILES ${dest_binary})
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E ${LLVM_LINK_OR_COPY} "${dest_binary}" "${output_path}")
  else()
    add_custom_command(OUTPUT ${output_path}
                     COMMAND ${CMAKE_COMMAND} -E ${LLVM_LINK_OR_COPY} "${dest_binary}" "${output_path}"
                     DEPENDS ${target})

    # TODO: Make use of generator expressions below once CMake 3.19 or higher is the minimum supported version.
    set(should_build_all)
    get_target_property(target_excluded_from_all ${target} EXCLUDE_FROM_ALL)
    if (NOT target_excluded_from_all)
      set(should_build_all ALL)
    endif()
    add_custom_target(${target_name} ${should_build_all} DEPENDS ${target} ${output_path})
    get_subproject_title(subproject_title)
    set_target_properties(${target_name} PROPERTIES FOLDER "${subproject_title}/Tools")

    # Make sure both the link and target are toolchain tools
    if (${link_name} IN_LIST LLVM_TOOLCHAIN_TOOLS AND ${target} IN_LIST LLVM_TOOLCHAIN_TOOLS)
      set(TOOL_IS_TOOLCHAIN ON)
    endif()

    if ((TOOL_IS_TOOLCHAIN OR NOT LLVM_INSTALL_TOOLCHAIN_ONLY) AND LLVM_BUILD_TOOLS)
      llvm_install_symlink("${project}" ${link_name} ${target})
    endif()
  endif()
endfunction()

function(add_llvm_tool_symlink link_name target)
  llvm_add_tool_symlink(LLVM ${ARGV})
endfunction()

function(llvm_externalize_debuginfo name)
  if(NOT LLVM_EXTERNALIZE_DEBUGINFO)
    return()
  endif()

  if(NOT LLVM_EXTERNALIZE_DEBUGINFO_SKIP_STRIP)
    if(APPLE)
      if(NOT CMAKE_STRIP)
        set(CMAKE_STRIP xcrun strip)
      endif()
      set(strip_command COMMAND ${CMAKE_STRIP} -S -x $<TARGET_FILE:${name}>)
    else()
      set(strip_command COMMAND ${CMAKE_STRIP} -g -x $<TARGET_FILE:${name}>)
    endif()
  endif()

  if(APPLE)
    if(LLVM_EXTERNALIZE_DEBUGINFO_EXTENSION)
      set(file_ext ${LLVM_EXTERNALIZE_DEBUGINFO_EXTENSION})
    else()
      set(file_ext dSYM)
    endif()

    set(output_name "$<TARGET_FILE_NAME:${name}>.${file_ext}")

    if(LLVM_EXTERNALIZE_DEBUGINFO_OUTPUT_DIR)
      set(output_path "-o=${LLVM_EXTERNALIZE_DEBUGINFO_OUTPUT_DIR}/${output_name}")
    else()
      set(output_path "-o=${output_name}")
    endif()

    if(CMAKE_CXX_FLAGS MATCHES "-flto"
      OR CMAKE_CXX_FLAGS_${uppercase_CMAKE_BUILD_TYPE} MATCHES "-flto")

      set(lto_object ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/${name}-lto.o)
      set_property(TARGET ${name} APPEND_STRING PROPERTY
        LINK_FLAGS " -Wl,-object_path_lto,${lto_object}")
    endif()
    if(NOT CMAKE_DSYMUTIL)
      set(CMAKE_DSYMUTIL xcrun dsymutil)
    endif()
    add_custom_command(TARGET ${name} POST_BUILD
      COMMAND ${CMAKE_DSYMUTIL} ${output_path} $<TARGET_FILE:${name}>
      ${strip_command}
      )
  else()
    add_custom_command(TARGET ${name} POST_BUILD
      COMMAND ${CMAKE_OBJCOPY} --only-keep-debug $<TARGET_FILE:${name}> $<TARGET_FILE:${name}>.debug
      ${strip_command} -R .gnu_debuglink
      COMMAND ${CMAKE_OBJCOPY} --add-gnu-debuglink=$<TARGET_FILE:${name}>.debug $<TARGET_FILE:${name}>
      )
  endif()
endfunction()

# Usage: llvm_codesign(name [FORCE] [ENTITLEMENTS file] [BUNDLE_PATH path])
function(llvm_codesign name)
  cmake_parse_arguments(ARG "FORCE" "ENTITLEMENTS;BUNDLE_PATH" "" ${ARGN})

  if(NOT LLVM_CODESIGNING_IDENTITY)
    return()
  endif()

  if(CMAKE_GENERATOR STREQUAL "Xcode")
    set_target_properties(${name} PROPERTIES
      XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ${LLVM_CODESIGNING_IDENTITY}
    )
    if(DEFINED ARG_ENTITLEMENTS)
      set_target_properties(${name} PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS ${ARG_ENTITLEMENTS}
      )
    endif()
  elseif(APPLE AND CMAKE_HOST_SYSTEM_NAME MATCHES Darwin)
    if(NOT CMAKE_CODESIGN)
      set(CMAKE_CODESIGN xcrun codesign)
    endif()
    if(NOT CMAKE_CODESIGN_ALLOCATE)
      execute_process(
        COMMAND xcrun -f codesign_allocate
        OUTPUT_STRIP_TRAILING_WHITESPACE
        OUTPUT_VARIABLE CMAKE_CODESIGN_ALLOCATE
      )
    endif()
    if(DEFINED ARG_ENTITLEMENTS)
      set(pass_entitlements --entitlements ${ARG_ENTITLEMENTS})
    endif()

    if (NOT ARG_BUNDLE_PATH)
      set(ARG_BUNDLE_PATH $<TARGET_FILE:${name}>)
    endif()

    # ld64 now always codesigns the binaries it creates. Apply the force arg
    # unconditionally so that we can - for example - add entitlements to the
    # targets that need it.
    set(force_flag "-f")

    add_custom_command(
      TARGET ${name} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E
              env CODESIGN_ALLOCATE=${CMAKE_CODESIGN_ALLOCATE}
              ${CMAKE_CODESIGN} -s ${LLVM_CODESIGNING_IDENTITY}
              ${pass_entitlements} ${force_flag} ${ARG_BUNDLE_PATH}
    )
  endif()
endfunction()

function(llvm_setup_rpath name)
  if(CMAKE_INSTALL_RPATH)
    return()
  endif()

  if(LLVM_INSTALL_PREFIX AND NOT (LLVM_INSTALL_PREFIX STREQUAL CMAKE_INSTALL_PREFIX))
    set(extra_libdir ${LLVM_LIBRARY_DIR})
  elseif(LLVM_BUILD_LIBRARY_DIR)
    set(extra_libdir ${LLVM_LIBRARY_DIR})
  endif()

  if (APPLE)
    set(_install_name_dir INSTALL_NAME_DIR "@rpath")
    set(_install_rpath "@loader_path/../lib${LLVM_LIBDIR_SUFFIX}" ${extra_libdir})
  elseif(${CMAKE_SYSTEM_NAME} MATCHES "AIX" AND BUILD_SHARED_LIBS)
    # $ORIGIN is not interpreted at link time by aix ld.
    # Since BUILD_SHARED_LIBS is only recommended for use by developers,
    # hardcode the rpath to build/install lib dir first in this mode.
    # FIXME: update this when there is better solution.
    set(_install_rpath "${LLVM_LIBRARY_OUTPUT_INTDIR}" "${CMAKE_INSTALL_PREFIX}/lib${LLVM_LIBDIR_SUFFIX}" ${extra_libdir})
  elseif(UNIX)
    set(_build_rpath "\$ORIGIN/../lib${LLVM_LIBDIR_SUFFIX}" ${extra_libdir})
    set(_install_rpath "\$ORIGIN/../lib${LLVM_LIBDIR_SUFFIX}")
    if(${CMAKE_SYSTEM_NAME} MATCHES "(FreeBSD|DragonFly)")
      set_property(TARGET ${name} APPEND_STRING PROPERTY
                   LINK_FLAGS " -Wl,-z,origin ")
    endif()
    if(LLVM_LINKER_IS_GNULD AND NOT ${LLVM_LIBRARY_OUTPUT_INTDIR} STREQUAL "")
      # $ORIGIN is not interpreted at link time by ld.bfd
      set_property(TARGET ${name} APPEND_STRING PROPERTY
                   LINK_FLAGS " -Wl,-rpath-link,${LLVM_LIBRARY_OUTPUT_INTDIR} ")
    endif()
  else()
    return()
  endif()

  # Enable BUILD_WITH_INSTALL_RPATH unless CMAKE_BUILD_RPATH is set and not
  # building for macOS or AIX, as those platforms seemingly require it.
  # On AIX, the tool chain doesn't support modifying rpaths/libpaths for XCOFF
  # on install at the moment, so BUILD_WITH_INSTALL_RPATH is required.
  if("${CMAKE_BUILD_RPATH}" STREQUAL "")
    if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin|AIX")
      set_property(TARGET ${name} PROPERTY BUILD_WITH_INSTALL_RPATH ON)
    else()
      set_property(TARGET ${name} APPEND PROPERTY BUILD_RPATH "${_build_rpath}")
    endif()
  endif()

  set_target_properties(${name} PROPERTIES
                        INSTALL_RPATH "${_install_rpath}"
                        ${_install_name_dir})
endfunction()

function(setup_dependency_debugging name)
  if(NOT LLVM_DEPENDENCY_DEBUGGING)
    return()
  endif()

  if("intrinsics_gen" IN_LIST ARGN)
    return()
  endif()

  set(deny_attributes_inc "(deny file* (literal \"${LLVM_BINARY_DIR}/include/llvm/IR/Attributes.inc\"))")
  set(deny_intrinsics_inc "(deny file* (literal \"${LLVM_BINARY_DIR}/include/llvm/IR/Intrinsics.inc\"))")

  set(sandbox_command "sandbox-exec -p '(version 1) (allow default) ${deny_attributes_inc} ${deny_intrinsics_inc}'")
  set_target_properties(${name} PROPERTIES RULE_LAUNCH_COMPILE ${sandbox_command})
endfunction()

# If the sources at the given `path` are under version control, set `out_var`
# to the the path of a file which will be modified when the VCS revision
# changes, attempting to create that file if it does not exist; if no such
# file exists and one cannot be created, instead set `out_var` to the
# empty string.
#
# If the sources are not under version control, do not define `out_var`.
function(find_first_existing_vc_file path out_var)
  if(NOT EXISTS "${path}")
    return()
  endif()
  find_package(Git)
  if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --git-dir
      WORKING_DIRECTORY ${path}
      RESULT_VARIABLE git_result
      OUTPUT_VARIABLE git_output
      ERROR_QUIET)
    if(git_result EQUAL 0)
      string(STRIP "${git_output}" git_output)
      get_filename_component(git_dir ${git_output} ABSOLUTE BASE_DIR ${path})
      # Some branchless cases (e.g. 'repo') may not yet have .git/logs/HEAD
      if (NOT EXISTS "${git_dir}/logs/HEAD")
        execute_process(COMMAND ${CMAKE_COMMAND} -E touch HEAD
          WORKING_DIRECTORY "${git_dir}/logs"
          RESULT_VARIABLE touch_head_result
          ERROR_QUIET)
        if (NOT touch_head_result EQUAL 0)
          set(${out_var} "" PARENT_SCOPE)
          return()
        endif()
      endif()
      set(${out_var} "${git_dir}/logs/HEAD" PARENT_SCOPE)
    endif()
  endif()
endfunction()

function(get_host_tool_path tool_name setting_name exe_var_name target_var_name)
  set(${setting_name}_DEFAULT "")

  if(LLVM_NATIVE_TOOL_DIR)
    if(EXISTS "${LLVM_NATIVE_TOOL_DIR}/${tool_name}${LLVM_HOST_EXECUTABLE_SUFFIX}")
      set(${setting_name}_DEFAULT "${LLVM_NATIVE_TOOL_DIR}/${tool_name}${LLVM_HOST_EXECUTABLE_SUFFIX}")
    endif()
  endif()

  set(${setting_name} "${${setting_name}_DEFAULT}" CACHE
    STRING "Host ${tool_name} executable. Saves building if cross-compiling.")

  if(${setting_name})
    set(exe_name ${${setting_name}})
    set(target_name "")
  elseif(LLVM_USE_HOST_TOOLS)
    get_native_tool_path(${tool_name} exe_name)
    set(target_name ${exe_name})
  else()
    set(exe_name $<TARGET_FILE:${tool_name}>)
    set(target_name ${tool_name})
  endif()
  set(${exe_var_name} "${exe_name}" CACHE STRING "")
  set(${target_var_name} "${target_name}" CACHE STRING "")
endfunction()

function(setup_host_tool tool_name setting_name exe_var_name target_var_name)
  get_host_tool_path(${tool_name} ${setting_name} ${exe_var_name} ${target_var_name})
  # Set up a native tool build if necessary
  if(LLVM_USE_HOST_TOOLS AND NOT ${setting_name})
    build_native_tool(${tool_name} exe_name DEPENDS ${tool_name})
    add_custom_target(${target_var_name} DEPENDS ${exe_name})
    get_subproject_title(subproject_title)
    set_target_properties(${target_var_name} PROPERTIES FOLDER "${subproject_title}/Native")
  endif()
endfunction()
