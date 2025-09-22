include(ExternalProject)

# llvm_ExternalProject_BuildCmd(out_var target)
#   Utility function for constructing command lines for external project targets
function(llvm_ExternalProject_BuildCmd out_var target bin_dir)
  cmake_parse_arguments(ARG "" "CONFIGURATION" "" ${ARGN})
  if(NOT ARG_CONFIGURATION)
    set(ARG_CONFIGURATION "$<CONFIG>")
  endif()
  if (CMAKE_GENERATOR MATCHES "Make")
    # Use special command for Makefiles to support parallelism.
    set(${out_var} "$(MAKE)" "-C" "${bin_dir}" "${target}" PARENT_SCOPE)
  else()
    set(tool_args "${LLVM_EXTERNAL_PROJECT_BUILD_TOOL_ARGS}")
    if(NOT tool_args STREQUAL "")
      string(CONFIGURE "${tool_args}" tool_args @ONLY)
      string(PREPEND tool_args "-- ")
      separate_arguments(tool_args UNIX_COMMAND "${tool_args}")
    endif()
    set(${out_var} ${CMAKE_COMMAND} --build ${bin_dir} --target ${target}
                                    --config ${ARG_CONFIGURATION} ${tool_args} PARENT_SCOPE)
  endif()
endfunction()

# is_msvc_triple(out_var triple)
#   Checks whether the passed triple refers to an MSVC environment
function(is_msvc_triple out_var triple)
  if (triple MATCHES ".*-windows-msvc.*")
    set(${out_var} TRUE PARENT_SCOPE)
  else()
    set(${out_var} FALSE PARENT_SCOPE)
  endif()
endfunction()


# llvm_ExternalProject_Add(name source_dir ...
#   USE_TOOLCHAIN
#     Use just-built tools (see TOOLCHAIN_TOOLS)
#   EXCLUDE_FROM_ALL
#     Exclude this project from the all target
#   NO_INSTALL
#     Don't generate install targets for this project
#   ALWAYS_CLEAN
#     Always clean the sub-project before building
#   CMAKE_ARGS arguments...
#     Optional cmake arguments to pass when configuring the project
#   TOOLCHAIN_TOOLS targets...
#     Targets for toolchain tools (defaults to clang;lld)
#   DEPENDS targets...
#     Targets that this project depends on
#   EXTRA_TARGETS targets...
#     Extra targets in the subproject to generate targets for
#   PASSTHROUGH_PREFIXES prefix...
#     Extra variable prefixes (name is always included) to pass down
#   STRIP_TOOL path
#     Use provided strip tool instead of the default one.
#   TARGET_TRIPLE triple
#     Optional target triple to pass to the compiler
#   FOLDER
#     For IDEs, the Folder to put the targets into.
#   )
function(llvm_ExternalProject_Add name source_dir)
  cmake_parse_arguments(ARG
    "USE_TOOLCHAIN;EXCLUDE_FROM_ALL;NO_INSTALL;ALWAYS_CLEAN"
    "SOURCE_DIR;FOLDER"
    "CMAKE_ARGS;TOOLCHAIN_TOOLS;RUNTIME_LIBRARIES;DEPENDS;EXTRA_TARGETS;PASSTHROUGH_PREFIXES;STRIP_TOOL;TARGET_TRIPLE"
    ${ARGN})
  canonicalize_tool_name(${name} nameCanon)

  foreach(arg ${ARG_CMAKE_ARGS})
    if(arg MATCHES "^-DCMAKE_SYSTEM_NAME=")
      string(REGEX REPLACE "^-DCMAKE_SYSTEM_NAME=(.*)$" "\\1" _cmake_system_name "${arg}")
    endif()
  endforeach()

  # If CMAKE_SYSTEM_NAME is not set explicitly in the arguments passed to us,
  # reflect CMake's own default.
  if (NOT _cmake_system_name)
    set(_cmake_system_name "${CMAKE_HOST_SYSTEM_NAME}")
  endif()

  if(NOT ARG_TARGET_TRIPLE)
    set(target_triple ${LLVM_DEFAULT_TARGET_TRIPLE})
  else()
    set(target_triple ${ARG_TARGET_TRIPLE})
  endif()

  is_msvc_triple(is_msvc_target "${target_triple}")

  if(NOT ARG_TOOLCHAIN_TOOLS)
    set(ARG_TOOLCHAIN_TOOLS clang)
    # AIX 64-bit XCOFF and big AR format is not yet supported in some of these tools.
    if(NOT _cmake_system_name STREQUAL AIX)
      list(APPEND ARG_TOOLCHAIN_TOOLS lld llvm-ar llvm-ranlib llvm-nm llvm-objdump)
      if(_cmake_system_name STREQUAL Darwin)
        list(APPEND ARG_TOOLCHAIN_TOOLS llvm-libtool-darwin llvm-lipo)
      elseif(is_msvc_target)
        list(APPEND ARG_TOOLCHAIN_TOOLS llvm-lib llvm-rc)
        if (LLVM_ENABLE_LIBXML2)
          list(APPEND ARG_TOOLCHAIN_TOOLS llvm-mt)
        endif()
      else()
        # TODO: These tools don't fully support Mach-O format yet.
        list(APPEND ARG_TOOLCHAIN_TOOLS llvm-objcopy llvm-strip llvm-readelf)
      endif()
    endif()
  endif()
  foreach(tool ${ARG_TOOLCHAIN_TOOLS})
    if(TARGET ${tool})
      list(APPEND TOOLCHAIN_TOOLS ${tool})

      # $<TARGET_FILE:tgt> only works on add_executable or add_library targets
      # The below logic mirrors cmake's own implementation
      get_target_property(target_type "${tool}" TYPE)
      if(NOT target_type STREQUAL "OBJECT_LIBRARY" AND
         NOT target_type STREQUAL "UTILITY" AND
         NOT target_type STREQUAL "GLOBAL_TARGET" AND
         NOT target_type STREQUAL "INTERFACE_LIBRARY")
        list(APPEND TOOLCHAIN_BINS $<TARGET_FILE:${tool}>)
      endif()

    endif()
  endforeach()

  if(NOT ARG_RUNTIME_LIBRARIES)
    set(ARG_RUNTIME_LIBRARIES compiler-rt libcxx)
  endif()
  foreach(lib ${ARG_RUNTIME_LIBRARIES})
    if(TARGET ${lib})
      list(APPEND RUNTIME_LIBRARIES ${lib})
    endif()
  endforeach()

  if(ARG_ALWAYS_CLEAN)
    set(always_clean clean)
  endif()

  if(clang IN_LIST TOOLCHAIN_TOOLS)
    set(CLANG_IN_TOOLCHAIN On)
  endif()

  if(RUNTIME_LIBRARIES AND CLANG_IN_TOOLCHAIN)
    list(APPEND TOOLCHAIN_BINS ${RUNTIME_LIBRARIES})
  endif()

  set(STAMP_DIR ${CMAKE_CURRENT_BINARY_DIR}/${name}-stamps/)
  set(BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/${name}-bins/)

  add_custom_target(${name}-clear
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${STAMP_DIR}
    COMMENT "Clobbering ${name} build and stamp directories"
    USES_TERMINAL
    )
  if (ARG_FOLDER)
    set_target_properties(${name}-clear PROPERTIES FOLDER "${ARG_FOLDER}")
  endif ()

  # Find all variables that start with a prefix and propagate them through
  get_cmake_property(variableNames VARIABLES)

  list(APPEND ARG_PASSTHROUGH_PREFIXES ${nameCanon})
  foreach(prefix ${ARG_PASSTHROUGH_PREFIXES})
    foreach(variableName ${variableNames})
      if(variableName MATCHES "^${prefix}")
        string(REPLACE ";" "|" value "${${variableName}}")
        list(APPEND PASSTHROUGH_VARIABLES
          -D${variableName}=${value})
      endif()
    endforeach()
  endforeach()

  # Populate the non-project-specific passthrough variables
  foreach(variableName ${LLVM_EXTERNAL_PROJECT_PASSTHROUGH})
    if(DEFINED ${variableName})
      if("${${variableName}}" STREQUAL "")
        set(value "")
      else()
        string(REPLACE ";" "|" value "${${variableName}}")
      endif()
      list(APPEND PASSTHROUGH_VARIABLES
        -D${variableName}=${value})
    endif()
  endforeach()

  if(ARG_USE_TOOLCHAIN AND NOT CMAKE_CROSSCOMPILING)
    if(CLANG_IN_TOOLCHAIN)
      if(is_msvc_target)
        set(compiler_args -DCMAKE_C_COMPILER=${LLVM_RUNTIME_OUTPUT_INTDIR}/clang-cl${CMAKE_EXECUTABLE_SUFFIX}
                          -DCMAKE_CXX_COMPILER=${LLVM_RUNTIME_OUTPUT_INTDIR}/clang-cl${CMAKE_EXECUTABLE_SUFFIX}
                          -DCMAKE_ASM_COMPILER=${LLVM_RUNTIME_OUTPUT_INTDIR}/clang-cl${CMAKE_EXECUTABLE_SUFFIX})
      else()
        set(compiler_args -DCMAKE_C_COMPILER=${LLVM_RUNTIME_OUTPUT_INTDIR}/clang${CMAKE_EXECUTABLE_SUFFIX}
                          -DCMAKE_CXX_COMPILER=${LLVM_RUNTIME_OUTPUT_INTDIR}/clang++${CMAKE_EXECUTABLE_SUFFIX}
                          -DCMAKE_ASM_COMPILER=${LLVM_RUNTIME_OUTPUT_INTDIR}/clang${CMAKE_EXECUTABLE_SUFFIX})
      endif()
    endif()
    if(lld IN_LIST TOOLCHAIN_TOOLS)
      if(is_msvc_target)
        list(APPEND compiler_args -DCMAKE_LINKER=${LLVM_RUNTIME_OUTPUT_INTDIR}/lld-link${CMAKE_EXECUTABLE_SUFFIX})
      elseif(NOT _cmake_system_name STREQUAL Darwin)
        list(APPEND compiler_args -DCMAKE_LINKER=${LLVM_RUNTIME_OUTPUT_INTDIR}/ld.lld${CMAKE_EXECUTABLE_SUFFIX})
      endif()
    endif()
    if(llvm-ar IN_LIST TOOLCHAIN_TOOLS)
      if(is_msvc_target)
        list(APPEND compiler_args -DCMAKE_AR=${LLVM_RUNTIME_OUTPUT_INTDIR}/llvm-lib${CMAKE_EXECUTABLE_SUFFIX})
      else()
        list(APPEND compiler_args -DCMAKE_AR=${LLVM_RUNTIME_OUTPUT_INTDIR}/llvm-ar${CMAKE_EXECUTABLE_SUFFIX})
      endif()
    endif()
    if(llvm-libtool-darwin IN_LIST TOOLCHAIN_TOOLS)
      list(APPEND compiler_args -DCMAKE_LIBTOOL=${LLVM_RUNTIME_OUTPUT_INTDIR}/llvm-libtool-darwin${CMAKE_EXECUTABLE_SUFFIX})
    endif()
    if(llvm-lipo IN_LIST TOOLCHAIN_TOOLS)
      list(APPEND compiler_args -DCMAKE_LIPO=${LLVM_RUNTIME_OUTPUT_INTDIR}/llvm-lipo${CMAKE_EXECUTABLE_SUFFIX})
    endif()
    if(llvm-ranlib IN_LIST TOOLCHAIN_TOOLS)
      list(APPEND compiler_args -DCMAKE_RANLIB=${LLVM_RUNTIME_OUTPUT_INTDIR}/llvm-ranlib${CMAKE_EXECUTABLE_SUFFIX})
    endif()
    if(llvm-nm IN_LIST TOOLCHAIN_TOOLS)
      list(APPEND compiler_args -DCMAKE_NM=${LLVM_RUNTIME_OUTPUT_INTDIR}/llvm-nm${CMAKE_EXECUTABLE_SUFFIX})
    endif()
    if(llvm-objdump IN_LIST TOOLCHAIN_TOOLS)
      list(APPEND compiler_args -DCMAKE_OBJDUMP=${LLVM_RUNTIME_OUTPUT_INTDIR}/llvm-objdump${CMAKE_EXECUTABLE_SUFFIX})
    endif()
    if(llvm-objcopy IN_LIST TOOLCHAIN_TOOLS)
      list(APPEND compiler_args -DCMAKE_OBJCOPY=${LLVM_RUNTIME_OUTPUT_INTDIR}/llvm-objcopy${CMAKE_EXECUTABLE_SUFFIX})
    endif()
    if(llvm-strip IN_LIST TOOLCHAIN_TOOLS AND NOT ARG_STRIP_TOOL)
      list(APPEND compiler_args -DCMAKE_STRIP=${LLVM_RUNTIME_OUTPUT_INTDIR}/llvm-strip${CMAKE_EXECUTABLE_SUFFIX})
    endif()
    if(llvm-readelf IN_LIST TOOLCHAIN_TOOLS)
      list(APPEND compiler_args -DCMAKE_READELF=${LLVM_RUNTIME_OUTPUT_INTDIR}/llvm-readelf${CMAKE_EXECUTABLE_SUFFIX})
    endif()
    if(llvm-mt IN_LIST TOOLCHAIN_TOOLS AND is_msvc_target)
      list(APPEND compiler_args -DCMAKE_MT=${LLVM_RUNTIME_OUTPUT_INTDIR}/llvm-mt${CMAKE_EXECUTABLE_SUFFIX})
    endif()
    if(llvm-rc IN_LIST TOOLCHAIN_TOOLS AND is_msvc_target)
      list(APPEND compiler_args -DCMAKE_RC_COMPILER=${LLVM_RUNTIME_OUTPUT_INTDIR}/llvm-rc${CMAKE_EXECUTABLE_SUFFIX})
    endif()
    list(APPEND ARG_DEPENDS ${TOOLCHAIN_TOOLS})
    # Add LLVMgold.so dependency if it is available, as clang may need it for
    # LTO.
    if(CLANG_IN_TOOLCHAIN AND TARGET LLVMgold)
      list(APPEND ARG_DEPENDS LLVMgold)
    endif()
  endif()

  if(ARG_STRIP_TOOL)
    list(APPEND compiler_args -DCMAKE_STRIP=${ARG_STRIP_TOOL})
  endif()

  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}-clobber-stamp
    DEPENDS ${ARG_DEPENDS}
    COMMAND ${CMAKE_COMMAND} -E touch ${BINARY_DIR}/CMakeCache.txt
    COMMAND ${CMAKE_COMMAND} -E touch ${STAMP_DIR}/${name}-mkdir
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/${name}-clobber-stamp
    COMMENT "Clobbering bootstrap build and stamp directories"
    )

  add_custom_target(${name}-clobber
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${name}-clobber-stamp)
  if (ARG_FOLDER)
    set_target_properties(${name}-clobber PROPERTIES FOLDER "${ARG_FOLDER}")
  endif ()

  if(ARG_EXCLUDE_FROM_ALL)
    set(exclude EXCLUDE_FROM_ALL 1)
  endif()

  if(CMAKE_SYSROOT)
    set(sysroot_arg -DCMAKE_SYSROOT=${CMAKE_SYSROOT})
  endif()

  if(CMAKE_CROSSCOMPILING)
    set(compiler_args -DCMAKE_ASM_COMPILER=${CMAKE_ASM_COMPILER}
                      -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
                      -DCMAKE_LINKER=${CMAKE_LINKER}
                      -DCMAKE_AR=${CMAKE_AR}
                      -DCMAKE_RANLIB=${CMAKE_RANLIB}
                      -DCMAKE_NM=${CMAKE_NM}
                      -DCMAKE_OBJCOPY=${CMAKE_OBJCOPY}
                      -DCMAKE_OBJDUMP=${CMAKE_OBJDUMP}
                      -DCMAKE_STRIP=${CMAKE_STRIP}
                      -DCMAKE_READELF=${CMAKE_READELF})
    set(llvm_config_path ${LLVM_CONFIG_PATH})

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      string(REGEX MATCH "^[0-9]+" CLANG_VERSION_MAJOR
             ${PACKAGE_VERSION})
      if(DEFINED CLANG_RESOURCE_DIR AND NOT CLANG_RESOURCE_DIR STREQUAL "")
        set(resource_dir ${LLVM_TOOLS_BINARY_DIR}/${CLANG_RESOURCE_DIR})
      else()
        set(resource_dir "${LLVM_LIBRARY_DIR}/clang/${CLANG_VERSION_MAJOR}")
      endif()
      set(flag_types ASM C CXX MODULE_LINKER SHARED_LINKER EXE_LINKER)
      foreach(type ${flag_types})
        set(${type}_flag -DCMAKE_${type}_FLAGS_INIT=-resource-dir=${resource_dir})
      endforeach()
      string(REPLACE ";" "|" flag_string "${flag_types}")
      foreach(arg ${ARG_CMAKE_ARGS})
        if(arg MATCHES "^-DCMAKE_(${flag_string})_FLAGS")
          foreach(type ${flag_types})
            if(arg MATCHES "^-DCMAKE_${type}_FLAGS")
              string(REGEX REPLACE "^-DCMAKE_${type}_FLAGS=(.*)$" "\\1" flag_value "${arg}")
              set(${type}_flag "${${type}_flag} ${flag_value}")
            endif()
          endforeach()
        else()
          list(APPEND cmake_args ${arg})
        endif()
      endforeach()
      foreach(type ${flag_types})
        list(APPEND cmake_args ${${type}_flag})
      endforeach()
    else()
      set(cmake_args ${ARG_CMAKE_ARGS})
    endif()
  else()
    set(llvm_config_path "$<TARGET_FILE:llvm-config>")
    set(cmake_args ${ARG_CMAKE_ARGS})
  endif()

  if(ARG_TARGET_TRIPLE)
    list(APPEND compiler_args -DCMAKE_C_COMPILER_TARGET=${ARG_TARGET_TRIPLE})
    list(APPEND compiler_args -DCMAKE_CXX_COMPILER_TARGET=${ARG_TARGET_TRIPLE})
    list(APPEND compiler_args -DCMAKE_ASM_COMPILER_TARGET=${ARG_TARGET_TRIPLE})
  endif()

  if(CMAKE_VERBOSE_MAKEFILE)
    set(verbose -DCMAKE_VERBOSE_MAKEFILE=ON)
  endif()

  ExternalProject_Add(${name}
    DEPENDS ${ARG_DEPENDS} llvm-config
    ${name}-clobber
    PREFIX ${CMAKE_BINARY_DIR}/projects/${name}
    SOURCE_DIR ${source_dir}
    STAMP_DIR ${STAMP_DIR}
    BINARY_DIR ${BINARY_DIR}
    ${exclude}
    CMAKE_ARGS ${${nameCanon}_CMAKE_ARGS}
               --no-warn-unused-cli
               ${compiler_args}
               ${verbose}
               -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
               ${sysroot_arg}
               -DLLVM_BINARY_DIR=${PROJECT_BINARY_DIR}
               -DLLVM_CONFIG_PATH=${llvm_config_path}
               -DLLVM_ENABLE_WERROR=${LLVM_ENABLE_WERROR}
               -DLLVM_HOST_TRIPLE=${LLVM_HOST_TRIPLE}
               -DLLVM_HAVE_LINK_VERSION_SCRIPT=${LLVM_HAVE_LINK_VERSION_SCRIPT}
               -DLLVM_USE_RELATIVE_PATHS_IN_DEBUG_INFO=${LLVM_USE_RELATIVE_PATHS_IN_DEBUG_INFO}
               -DLLVM_USE_RELATIVE_PATHS_IN_FILES=${LLVM_USE_RELATIVE_PATHS_IN_FILES}
               -DLLVM_LIT_ARGS=${LLVM_LIT_ARGS}
               -DLLVM_SOURCE_PREFIX=${LLVM_SOURCE_PREFIX}
               -DPACKAGE_VERSION=${PACKAGE_VERSION}
               -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
               -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
               -DCMAKE_EXPORT_COMPILE_COMMANDS=1
               ${cmake_args}
               ${PASSTHROUGH_VARIABLES}
    INSTALL_COMMAND ""
    STEP_TARGETS configure build
    BUILD_ALWAYS 1
    USES_TERMINAL_CONFIGURE 1
    USES_TERMINAL_BUILD 1
    USES_TERMINAL_INSTALL 1
    LIST_SEPARATOR |
    )
  if (ARG_FOLDER)
    set_target_properties(
      ${name} ${name}-clobber ${name}-build ${name}-configure
      PROPERTIES FOLDER "${ARG_FOLDER}"
    )
  endif ()

  if(ARG_USE_TOOLCHAIN)
    set(force_deps DEPENDS ${TOOLCHAIN_BINS})
  endif()

  llvm_ExternalProject_BuildCmd(run_clean clean ${BINARY_DIR})
  ExternalProject_Add_Step(${name} clean
    COMMAND ${run_clean}
    COMMENT "Cleaning ${name}..."
    DEPENDEES configure
    ${force_deps}
    WORKING_DIRECTORY ${BINARY_DIR}
    EXCLUDE_FROM_MAIN 1
    USES_TERMINAL 1
    )
  ExternalProject_Add_StepTargets(${name} clean)
  if (ARG_FOLDER)
    set_target_properties(${name}-clean PROPERTIES FOLDER "${ARG_FOLDER}")
  endif ()

  if(ARG_USE_TOOLCHAIN)
    add_dependencies(${name}-clean ${name}-clobber)
    set_target_properties(${name}-clean PROPERTIES
      SOURCES ${CMAKE_CURRENT_BINARY_DIR}/${name}-clobber-stamp)
  endif()

  if(NOT ARG_NO_INSTALL)
    install(CODE "execute_process\(COMMAND \${CMAKE_COMMAND} -DCMAKE_INSTALL_PREFIX=\${CMAKE_INSTALL_PREFIX} -DCMAKE_INSTALL_DO_STRIP=\${CMAKE_INSTALL_DO_STRIP} -P ${BINARY_DIR}/cmake_install.cmake\)"
      COMPONENT ${name})

    add_llvm_install_targets(install-${name}
                             DEPENDS ${name}
                             COMPONENT ${name})
    if (ARG_FOLDER)
       set_target_properties(install-${name} PROPERTIES FOLDER "${ARG_FOLDER}")
    endif ()
  endif()

  # Add top-level targets
  foreach(target ${ARG_EXTRA_TARGETS})
    if(DEFINED ${target})
      set(external_target "${${target}}")
    else()
      set(external_target "${target}")
    endif()
    llvm_ExternalProject_BuildCmd(build_runtime_cmd ${external_target} ${BINARY_DIR})
    add_custom_target(${target}
      COMMAND ${build_runtime_cmd}
      DEPENDS ${name}-configure
      WORKING_DIRECTORY ${BINARY_DIR}
      VERBATIM
      USES_TERMINAL)
    if (ARG_FOLDER)
      set_target_properties(${target} PROPERTIES FOLDER "${ARG_FOLDER}")
    endif ()
  endforeach()
endfunction()
