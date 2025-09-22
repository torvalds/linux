# TODO: This file assumes the Clang toolchain so it'd be better if it lived in
# Clang, except there already is clang/runtime directory which contains
# similar although simpler functionality. We should figure out how to merge
# the two files.

set(COMMON_CMAKE_ARGS "-DHAVE_LLVM_LIT=ON;-DCLANG_RESOURCE_DIR=${CLANG_RESOURCE_DIR}")
foreach(proj ${LLVM_ENABLE_RUNTIMES})
  set(proj_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../${proj}")
  if(IS_DIRECTORY ${proj_dir} AND EXISTS ${proj_dir}/CMakeLists.txt)
    list(APPEND runtimes ${proj_dir})
  else()
    message(FATAL_ERROR "LLVM_ENABLE_RUNTIMES requests ${proj} but directory not found: ${proj_dir}")
  endif()
  string(TOUPPER "${proj}" canon_name)
  STRING(REGEX REPLACE "-" "_" canon_name ${canon_name})
  set(LLVM_EXTERNAL_${canon_name}_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../${proj}")
endforeach()

function(get_compiler_rt_path path)
  set(all_runtimes ${runtimes})
  foreach(name ${LLVM_RUNTIME_TARGETS})
    list(APPEND all_runtimes ${RUNTIMES_${name}_LLVM_ENABLE_RUNTIMES})
  endforeach()
  list(REMOVE_DUPLICATES all_runtimes)
  foreach(entry ${all_runtimes})
    get_filename_component(projName ${entry} NAME)
    if("${projName}" MATCHES "compiler-rt")
      set(${path} ${entry} PARENT_SCOPE)
      return()
    endif()
  endforeach()
endfunction()

include(LLVMExternalProjectUtils)

if(NOT LLVM_BUILD_RUNTIMES)
  set(EXTRA_ARGS EXCLUDE_FROM_ALL)
endif()

function(check_apple_target triple builtin_or_runtime)
  set(error "\
compiler-rt for Darwin builds for all platforms and architectures using a \
single configuration. Specify only a single darwin triple (e.g. x86_64-apple-darwin) \
in your targets list (and not a triple for a specific platform such as macos). \
You can use variables such as COMPILER_RT_ENABLE_IOS and DARWIN_ios_ARCHS to \
control the specific platforms and architectures to build.")

  set(seen_property ${builtin_or_runtime}_darwin_triple_seen)
  string(REPLACE "-" ";" triple_components ${triple})
  foreach(component ${triple_components})
    string(TOLOWER "${component}" component_lower)
    if(component_lower MATCHES "^darwin")
      get_property(darwin_triple_seen GLOBAL PROPERTY ${seen_property})
      if(darwin_triple_seen)
        message(FATAL_ERROR "${error}")
      endif()
      set_property(GLOBAL PROPERTY ${seen_property} YES)
      if(NOT RUNTIMES_BUILD_ALLOW_DARWIN)
        message(FATAL_ERROR "\
${error} Set RUNTIMES_BUILD_ALLOW_DARWIN to allow a single darwin triple.")
      endif()
    elseif(component_lower MATCHES "^ios|^macos|^tvos|^watchos")
      message(FATAL_ERROR "${error}")
    endif()
  endforeach()
endfunction()

macro(set_enable_per_target_runtime_dir)
  # May have been set by llvm/CMakeLists.txt.
  if (NOT DEFINED LLVM_ENABLE_PER_TARGET_RUNTIME_DIR)
    # AIX should fold 32-bit & 64-bit arch libraries into a single archive.
    if (LLVM_TARGET_TRIPLE MATCHES "aix")
      set(LLVM_ENABLE_PER_TARGET_RUNTIME_DIR OFF)
    else()
      set(LLVM_ENABLE_PER_TARGET_RUNTIME_DIR ON)
    endif()
  endif()
endmacro()

function(builtin_default_target compiler_rt_path)
  cmake_parse_arguments(ARG "" "" "DEPENDS" ${ARGN})

  set_enable_per_target_runtime_dir()

  llvm_ExternalProject_Add(builtins
                           ${compiler_rt_path}/lib/builtins
                           DEPENDS ${ARG_DEPENDS}
                           CMAKE_ARGS -DLLVM_LIBRARY_OUTPUT_INTDIR=${LLVM_LIBRARY_DIR}
                                      -DLLVM_RUNTIME_OUTPUT_INTDIR=${LLVM_TOOLS_BINARY_DIR}
                                      -DLLVM_DEFAULT_TARGET_TRIPLE=${LLVM_TARGET_TRIPLE}
                                      -DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=${LLVM_ENABLE_PER_TARGET_RUNTIME_DIR}
                                      -DCMAKE_C_COMPILER_WORKS=ON
                                      -DCMAKE_ASM_COMPILER_WORKS=ON
                                      ${COMMON_CMAKE_ARGS}
                                      ${BUILTINS_CMAKE_ARGS}
                           PASSTHROUGH_PREFIXES COMPILER_RT
                                                DARWIN
                                                SANITIZER
                           USE_TOOLCHAIN
                           TARGET_TRIPLE ${LLVM_TARGET_TRIPLE}
                           FOLDER "Compiler-RT"
                           ${EXTRA_ARGS})
endfunction()

function(builtin_register_target compiler_rt_path name)
  cmake_parse_arguments(ARG "" "" "DEPENDS;CMAKE_ARGS;EXTRA_ARGS" ${ARGN})

  set(${name}_extra_args ${ARG_CMAKE_ARGS})
  get_cmake_property(variable_names VARIABLES)
  foreach(variable_name ${variable_names})
    string(FIND "${variable_name}" "BUILTINS_${name}" out)
    if("${out}" EQUAL 0)
      string(REPLACE "BUILTINS_${name}_" "" new_name ${variable_name})
      if(new_name STREQUAL CACHE_FILES)
        foreach(cache IN LISTS ${variable_name})
          list(APPEND ${name}_extra_args -C ${cache})
        endforeach()
      else()
        string(REPLACE ";" "|" new_value "${${variable_name}}")
        list(APPEND ${name}_extra_args "-D${new_name}=${new_value}")
      endif()
    endif()
  endforeach()

  llvm_ExternalProject_Add(builtins-${name}
                           ${compiler_rt_path}/lib/builtins
                           DEPENDS ${ARG_DEPENDS}
                           CMAKE_ARGS -DLLVM_LIBRARY_OUTPUT_INTDIR=${LLVM_LIBRARY_DIR}
                                      -DLLVM_RUNTIME_OUTPUT_INTDIR=${LLVM_TOOLS_BINARY_DIR}
                                      -DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=ON
                                      -DCMAKE_C_COMPILER_WORKS=ON
                                      -DCMAKE_ASM_COMPILER_WORKS=ON
                                      -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON
                                      ${COMMON_CMAKE_ARGS}
                                      ${${name}_extra_args}
                           USE_TOOLCHAIN
                           FOLDER "Compiler-RT"
                           ${EXTRA_ARGS} ${ARG_EXTRA_ARGS})
endfunction()

# If compiler-rt is present we need to build the builtin libraries first. This
# is required because the other runtimes need the builtin libraries present
# before the just-built compiler can pass the configuration tests.
get_compiler_rt_path(compiler_rt_path)
if(compiler_rt_path)
  # If the user did not specify the targets infer them from the runtimes.
  set(builtin_targets ${LLVM_BUILTIN_TARGETS})
  if(NOT builtin_targets)
    if("compiler-rt" IN_LIST LLVM_ENABLE_RUNTIMES)
      list(APPEND builtin_targets "default")
    endif()
    foreach(name ${LLVM_RUNTIME_TARGETS})
      if("compiler-rt" IN_LIST RUNTIMES_${name}_LLVM_ENABLE_RUNTIMES)
        list(APPEND builtin_targets ${name})
      endif()
    endforeach()
  endif()
  if("default" IN_LIST builtin_targets)
    builtin_default_target(${compiler_rt_path}
      DEPENDS clang-resource-headers)
    list(REMOVE_ITEM builtin_targets "default")
  else()
    add_custom_target(builtins)
    add_custom_target(install-builtins)
    add_custom_target(install-builtins-stripped)
    set_target_properties(
      builtins install-builtins install-builtins-stripped
      PROPERTIES FOLDER "Compiler-RT"
    )
  endif()

  foreach(target ${builtin_targets})
    check_apple_target(${target} builtin)

    builtin_register_target(${compiler_rt_path} ${target}
      DEPENDS clang-resource-headers
      CMAKE_ARGS -DLLVM_DEFAULT_TARGET_TRIPLE=${target}
      EXTRA_ARGS TARGET_TRIPLE ${target})

    add_dependencies(builtins builtins-${target})
    add_dependencies(install-builtins install-builtins-${target})
    add_dependencies(install-builtins-stripped install-builtins-${target}-stripped)
  endforeach()
  set(builtins_dep builtins)
  # We don't need to depend on the builtins if we're building instrumented
  # because the next stage will use the same compiler used to build this stage.
  if(NOT LLVM_BUILD_INSTRUMENTED AND CLANG_ENABLE_BOOTSTRAP)
    add_dependencies(clang-bootstrap-deps builtins)
  endif()
endif()

function(_get_runtime_name name out_var)
  string(FIND ${name} "lib" idx)
  if(idx EQUAL 0 AND NOT ${name} STREQUAL "libc")
    string(SUBSTRING ${name} 3 -1 name)
  endif()
  set(${out_var} ${name} PARENT_SCOPE)
endfunction()

# Create a list with the names of all the runtime projects in all uppercase and
# with dashes turned to underscores. This gives us the CMake variable `prefixes`
# for all variables that will apply to runtimes.
foreach(entry ${runtimes})
  get_filename_component(name ${entry} NAME)
  string(REPLACE "-" "_" canon_name ${name})
  string(TOUPPER ${canon_name} canon_name)
  list(APPEND prefixes ${canon_name})
  if (${canon_name} STREQUAL "OPENMP")
    list(APPEND prefixes "LIBOMP" "LIBOMPTARGET")
  endif()
  # Many compiler-rt options start with SANITIZER_ and DARWIN_ rather than
  # COMPILER_RT_, so when compiler-rt is enabled, consider both.
  if(canon_name STREQUAL "COMPILER_RT")
    list(APPEND prefixes SANITIZER DARWIN)
  endif()
  if(canon_name STREQUAL "LIBC")
    list(APPEND prefixes "LLVM_LIBC")
    list(APPEND prefixes "LIBC_")
  endif()

  _get_runtime_name(${name} name)
  list(APPEND RUNTIME_NAMES ${name})
endforeach()

function(runtime_default_target)
  cmake_parse_arguments(ARG "" "" "DEPENDS;CMAKE_ARGS;PREFIXES" ${ARGN})

  include(${LLVM_BINARY_DIR}/runtimes/Components.cmake OPTIONAL)
  set(SUB_CHECK_TARGETS ${SUB_CHECK_TARGETS} PARENT_SCOPE)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${LLVM_BINARY_DIR}/runtimes/Components.cmake)

  foreach(runtime_name ${RUNTIME_NAMES})
    list(APPEND extra_targets
      ${runtime_name}
      install-${runtime_name}
      install-${runtime_name}-stripped)
    if(LLVM_INCLUDE_TESTS)
      list(APPEND test_targets check-${runtime_name})
    endif()
  endforeach()
  foreach(component ${LLVM_RUNTIME_DISTRIBUTION_COMPONENTS})
    if(NOT ${component} IN_LIST SUB_COMPONENTS)
      list(APPEND extra_targets install-${component} install-${component}-stripped)
    endif()
  endforeach()

  if(LLVM_INCLUDE_TESTS)
    set_property(GLOBAL APPEND PROPERTY LLVM_ALL_LIT_TESTSUITES "@${LLVM_BINARY_DIR}/runtimes/runtimes-bins/lit.tests")
    list(APPEND test_targets runtimes-test-depends check-runtimes)
  endif()

  set_enable_per_target_runtime_dir()

  llvm_ExternalProject_Add(runtimes
                           ${CMAKE_CURRENT_SOURCE_DIR}/../../runtimes
                           DEPENDS ${ARG_DEPENDS}
                           # Builtins were built separately above
                           CMAKE_ARGS -DCOMPILER_RT_BUILD_BUILTINS=Off
                                      -DLLVM_INCLUDE_TESTS=${LLVM_INCLUDE_TESTS}
                                      -DLLVM_DEFAULT_TARGET_TRIPLE=${LLVM_TARGET_TRIPLE}
                                      -DLLVM_ENABLE_PROJECTS_USED=${LLVM_ENABLE_PROJECTS_USED}
                                      -DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=${LLVM_ENABLE_PER_TARGET_RUNTIME_DIR}
                                      -DLLVM_BUILD_TOOLS=${LLVM_BUILD_TOOLS}
                                      -DCMAKE_C_COMPILER_WORKS=ON
                                      -DCMAKE_CXX_COMPILER_WORKS=ON
                                      -DCMAKE_ASM_COMPILER_WORKS=ON
                                      ${COMMON_CMAKE_ARGS}
                                      ${RUNTIMES_CMAKE_ARGS}
                                      ${ARG_CMAKE_ARGS}
                           PASSTHROUGH_PREFIXES LLVM_ENABLE_RUNTIMES
                                                LLVM_USE_LINKER
                                                CUDA # For runtimes that may look for the CUDA SDK (libc, offload)
                                                ${ARG_PREFIXES}
                           EXTRA_TARGETS ${extra_targets}
                                         ${test_targets}
                                         ${SUB_COMPONENTS}
                                         ${SUB_CHECK_TARGETS}
                                         ${SUB_INSTALL_TARGETS}
                           USE_TOOLCHAIN
                           TARGET_TRIPLE ${LLVM_TARGET_TRIPLE}
                           FOLDER "Runtimes"
                           ${EXTRA_ARGS})
endfunction()

# runtime_register_target(name)
#   Utility function to register external runtime target.
function(runtime_register_target name)
  cmake_parse_arguments(ARG "" "BASE_NAME" "DEPENDS;CMAKE_ARGS;EXTRA_ARGS" ${ARGN})
  include(${LLVM_BINARY_DIR}/runtimes/${name}/Components.cmake OPTIONAL)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${LLVM_BINARY_DIR}/runtimes/${name}/Components.cmake)

  set(runtime_names ${RUNTIME_NAMES})
  foreach(_name IN ITEMS ${ARG_BASE_NAME} ${name})
    if(RUNTIMES_${_name}_LLVM_ENABLE_RUNTIMES)
      set(runtime_names)
      foreach(entry ${RUNTIMES_${_name}_LLVM_ENABLE_RUNTIMES})
        _get_runtime_name(${entry} runtime_name)
        list(APPEND runtime_names ${runtime_name})
      endforeach()
    endif()
  endforeach()

  foreach(runtime_name ${runtime_names})
    set(${runtime_name}-${name} ${runtime_name})
    set(install-${runtime_name}-${name} install-${runtime_name})
    set(install-${runtime_name}-${name}-stripped install-${runtime_name}-stripped)
    list(APPEND ${name}_extra_targets ${runtime_name}-${name} install-${runtime_name}-${name} install-${runtime_name}-${name}-stripped)
    if(LLVM_INCLUDE_TESTS)
      set(check-${runtime_name}-${name} check-${runtime_name} )
      list(APPEND ${name}_test_targets check-${runtime_name}-${name})
    endif()
  endforeach()

  foreach(component IN LISTS SUB_COMPONENTS)
    set(${component}-${name} ${component})
    list(APPEND ${name}_extra_targets ${component}-${name})
  endforeach()

  foreach(target IN LISTS SUB_INSTALL_TARGETS)
    set(${target}-${name} ${target})
    set(${target}-${name}-stripped ${target}-stripped)
    list(APPEND ${name}_extra_targets ${target}-${name} ${target}-${name}-stripped)
  endforeach()

  foreach(component ${LLVM_RUNTIME_DISTRIBUTION_COMPONENTS})
    if(NOT component IN_LIST SUB_COMPONENTS)
      set(${component}-${name} ${component})
      set(install-${component}-${name} install-${component})
      set(install-${component}-${name}-stripped install-${component}-stripped)
      list(APPEND ${name}_extra_targets ${component}-${name} install-${component}-${name} install-${component}-${name}-stripped)
    endif()
  endforeach()

  if(LLVM_INCLUDE_TESTS)
    set_property(GLOBAL APPEND PROPERTY LLVM_ALL_LIT_TESTSUITES "@${LLVM_BINARY_DIR}/runtimes/runtimes-${name}-bins/lit.tests")
    set(runtimes-test-depends-${name} runtimes-test-depends)
    set(check-runtimes-${name} check-runtimes)
    list(APPEND ${name}_test_targets runtimes-test-depends-${name} check-runtimes-${name})
    list(APPEND test_targets ${${name}_test_targets})

    set(component_check_targets)
    foreach(component IN LISTS LLVM_RUNTIME_DISTRIBUTION_COMPONENTS)
      if(NOT "check-${component}" IN_LIST SUB_CHECK_TARGETS)
        list(APPEND component_check_targets "check-${component}")
      endif()
    endforeach()

    foreach(target IN LISTS SUB_CHECK_TARGETS component_check_targets)
      set(${target}-${name} ${target})
      list(APPEND ${name}_test_targets ${target}-${name})
      list(APPEND test_targets ${target}-${name})
    endforeach()
    set(test_targets "${test_targets}" PARENT_SCOPE)
  endif()

  set(${name}_extra_args ${ARG_CMAKE_ARGS})
  string(REPLACE ";" "|" LLVM_ENABLE_RUNTIMES_PASSTHROUGH "${LLVM_ENABLE_RUNTIMES}")
  list(APPEND ${name}_extra_args -DLLVM_ENABLE_RUNTIMES=${LLVM_ENABLE_RUNTIMES_PASSTHROUGH})
  list(APPEND ${name}_extra_args -DLLVM_USE_LINKER=${LLVM_USE_LINKER})

  get_cmake_property(variable_names VARIABLES)
  foreach(extra_name IN ITEMS ${ARG_BASE_NAME} ${name})
    foreach(variable_name ${variable_names})
      string(FIND "${variable_name}" "RUNTIMES_${extra_name}_" out)
      if("${out}" EQUAL 0)
        string(REPLACE "RUNTIMES_${extra_name}_" "" new_name ${variable_name})
        if(new_name STREQUAL CACHE_FILES)
          foreach(cache IN LISTS ${variable_name})
            list(APPEND ${name}_extra_args -C ${cache})
          endforeach()
        else()
          string(REPLACE ";" "|" new_value "${${variable_name}}")
          list(APPEND ${name}_extra_args "-D${new_name}=${new_value}")
        endif()
      endif()
    endforeach()
    foreach(variable_name ${${name}_extra_args})
      string(FIND "${variable_name}" "-DRUNTIMES_${extra_name}_" out)
      if("${out}" EQUAL 0)
        string(REPLACE "-DRUNTIMES_${extra_name}_" "" new_name ${variable_name})
        string(REPLACE ";" "|" new_value "${new_name}")
        list(APPEND ${name}_extra_args "-D${new_value}")
      endif()
    endforeach()
  endforeach()

  set_enable_per_target_runtime_dir()

  llvm_ExternalProject_Add(runtimes-${name}
                           ${CMAKE_CURRENT_SOURCE_DIR}/../../runtimes
                           DEPENDS ${ARG_DEPENDS}
                           # Builtins were built separately above
                           CMAKE_ARGS -DCOMPILER_RT_BUILD_BUILTINS=OFF
                                      -DLLVM_INCLUDE_TESTS=${LLVM_INCLUDE_TESTS}
                                      -DLLVM_ENABLE_PROJECTS_USED=${LLVM_ENABLE_PROJECTS_USED}
                                      -DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=${LLVM_ENABLE_PER_TARGET_RUNTIME_DIR}
                                      -DCMAKE_C_COMPILER_WORKS=ON
                                      -DCMAKE_CXX_COMPILER_WORKS=ON
                                      -DCMAKE_ASM_COMPILER_WORKS=ON
                                      -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON
                                      -DLLVM_RUNTIMES_TARGET=${name}
                                      ${COMMON_CMAKE_ARGS}
                                      ${${name}_extra_args}
                           EXTRA_TARGETS ${${name}_extra_targets}
                                         ${${name}_test_targets}
                           USE_TOOLCHAIN
                           FOLDER "Runtimes"
                           ${EXTRA_ARGS} ${ARG_EXTRA_ARGS})

  add_dependencies(runtimes runtimes-${name})
  add_dependencies(runtimes-configure runtimes-${name}-configure)
  add_dependencies(install-runtimes install-runtimes-${name})
  add_dependencies(install-runtimes-stripped install-runtimes-${name}-stripped)
  if(LLVM_INCLUDE_TESTS)
    add_dependencies(check-runtimes check-runtimes-${name})
    add_dependencies(runtimes-test-depends runtimes-test-depends-${name})
  endif()
  foreach(runtime_name ${runtime_names})
    if(NOT TARGET ${runtime_name})
      add_custom_target(${runtime_name})
      set_target_properties(${runtime_name} PROPERTIES FOLDER "${runtime_name}")
    endif()
    add_dependencies(${runtime_name} ${runtime_name}-${name})
    if(NOT TARGET install-${runtime_name})
      add_custom_target(install-${runtime_name})
      set_target_properties(install-${runtime_name} PROPERTIES FOLDER "${runtime_name}")
    endif()
    add_dependencies(install-${runtime_name} install-${runtime_name}-${name})
    if(NOT TARGET install-${runtime_name}-stripped)
      add_custom_target(install-${runtime_name}-stripped)
      set_target_properties(install-${runtime_name} PROPERTIES FOLDER "${runtime_name}")
    endif()
    add_dependencies(install-${runtime_name}-stripped install-${runtime_name}-${name}-stripped)
  endforeach()
  foreach(component ${LLVM_RUNTIME_DISTRIBUTION_COMPONENTS})
    add_dependencies(${component} ${component}-${name})
    add_dependencies(install-${component} install-${component}-${name})
    add_dependencies(install-${component}-stripped install-${component}-${name}-stripped)
  endforeach()
endfunction()

# Check if we have any runtimes to build.
if(runtimes)
  set(build_runtimes TRUE)
endif()
foreach(name ${LLVM_RUNTIME_TARGETS})
  if(RUNTIMES_${name}_LLVM_ENABLE_RUNTIMES)
    set(build_runtimes TRUE)
  endif()
endforeach()

if(build_runtimes)
  # Create a runtimes target that uses this file as its top-level CMake file.
  # The runtimes target is a configuration of all the runtime libraries
  # together in a single CMake invocation.
  set(extra_deps "")
  set(extra_cmake_args "")

  if(LLVM_INCLUDE_TESTS)
    foreach(dep FileCheck
                clang
                count
                lld
                lli
                llvm-cov
                llvm-lto
                llvm-jitlink
                llvm-nm
                llvm-objdump
                llvm-profdata
                llvm-size
                llvm-symbolizer
                llvm-xray
                not
                obj2yaml
                opt
                sancov
                sanstats
                llvm_gtest_main
                llvm_gtest
                split-file)
      if(TARGET ${dep})
        list(APPEND extra_deps ${dep})
      endif()
    endforeach()
  endif()

  # Forward user-provived system configuration to runtimes for requirement introspection.
  # CMAKE_PREFIX_PATH is the search path for CMake packages.
  if(CMAKE_PREFIX_PATH)
    list(APPEND extra_cmake_args "-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}")
  endif()
  # CMAKE_PROGRAM_PATH is the search path for executables such as python.
  if(CMAKE_PROGRAM_PATH)
    list(APPEND extra_cmake_args "-DCMAKE_PROGRAM_PATH=${CMAKE_PROGRAM_PATH}")
  endif()

  if("openmp" IN_LIST LLVM_ENABLE_RUNTIMES)
    if (${LLVM_TOOL_FLANG_BUILD})
      message(STATUS "Configuring build of omp_lib.mod and omp_lib_kinds.mod via flang-new")
      set(LIBOMP_FORTRAN_MODULES_COMPILER "${CMAKE_BINARY_DIR}/bin/flang-new")
      set(LIBOMP_MODULES_INSTALL_PATH "${CMAKE_INSTALL_INCLUDEDIR}/flang")
      # TODO: This is a workaround until flang becomes a first-class project
      # in llvm/CMakeList.txt.  Until then, this line ensures that flang-new is
      # built before "openmp" is built as a runtime project.  Besides "flang-new"
      # to build the compiler, we also need to add "module_files" to make sure
      # that all .mod files are also properly build.
      list(APPEND extra_deps "flang-new" "module_files")
    endif()
    foreach(dep opt llvm-link llvm-extract clang clang-offload-packager)
      if(TARGET ${dep})
        list(APPEND extra_deps ${dep})
      endif()
    endforeach()
  endif()
  if("libc" IN_LIST LLVM_ENABLE_PROJECTS AND
      (LLVM_LIBC_FULL_BUILD OR LLVM_LIBC_GPU_BUILD))
    if(LIBC_HDRGEN_EXE)
      set(hdrgen_exe ${LIBC_HDRGEN_EXE})
    else()
      if(TARGET ${LIBC_TABLEGEN_EXE})
        set(hdrgen_exe $<TARGET_FILE:${LIBC_TABLEGEN_EXE}>)
      else()
        set(hdrgen_exe ${LIBC_TABLEGEN_EXE})
      endif()
      set(hdrgen_deps ${LIBC_TABLEGEN_TARGET})
    endif()
    if(NOT hdrgen_exe)
      message(FATAL_ERROR "libc-hdrgen executable missing")
    endif()
    list(APPEND extra_cmake_args "-DLIBC_HDRGEN_EXE=${hdrgen_exe}")
    list(APPEND extra_deps ${hdrgen_deps})
  endif()
  if(LLVM_LIBC_GPU_BUILD)
    list(APPEND extra_cmake_args "-DLLVM_LIBC_GPU_BUILD=ON")
    if("libc" IN_LIST RUNTIMES_amdgcn-amd-amdhsa_LLVM_ENABLE_RUNTIMES)
      if(TARGET amdhsa-loader)
        list(APPEND extra_cmake_args
             "-DRUNTIMES_amdgcn-amd-amdhsa_LIBC_GPU_LOADER_EXECUTABLE=$<TARGET_FILE:amdhsa-loader>")
        list(APPEND extra_deps amdhsa-loader)
      endif()
      list(APPEND extra_cmake_args "-DRUNTIMES_amdgcn-amd-amdhsa_LLVM_LIBC_FULL_BUILD=ON")
    endif()
    if("libc" IN_LIST RUNTIMES_nvptx64-nvidia-cuda_LLVM_ENABLE_RUNTIMES)
      if(TARGET nvptx-loader)
        list(APPEND extra_cmake_args
             "-DRUNTIMES_nvptx64-nvidia-cuda_LIBC_GPU_LOADER_EXECUTABLE=$<TARGET_FILE:nvptx-loader>")
        list(APPEND extra_deps nvptx-loader)
      endif()
      list(APPEND extra_cmake_args "-DRUNTIMES_nvptx64-nvidia-cuda_LLVM_LIBC_FULL_BUILD=ON")
    endif()
    if(TARGET clang-offload-packager)
      list(APPEND extra_deps clang-offload-packager)
    endif()
  endif()
  if(LLVM_LIBC_FULL_BUILD)
    list(APPEND extra_cmake_args "-DLLVM_LIBC_FULL_BUILD=ON")
  endif()

  if(NOT LLVM_RUNTIME_TARGETS)
    runtime_default_target(
      DEPENDS ${builtins_dep} ${extra_deps}
      CMAKE_ARGS ${extra_cmake_args}
      PREFIXES ${prefixes})
    set(test_targets check-runtimes)
  else()
    if("default" IN_LIST LLVM_RUNTIME_TARGETS)
      runtime_default_target(
        DEPENDS ${builtins_dep} ${extra_deps}
        CMAKE_ARGS ${extra_cmake_args}
        PREFIXES ${prefixes})
      list(REMOVE_ITEM LLVM_RUNTIME_TARGETS "default")
    else()
      add_custom_target(runtimes)
      add_custom_target(runtimes-configure)
      add_custom_target(install-runtimes)
      add_custom_target(install-runtimes-stripped)
      set_target_properties(
        runtimes runtimes-configure install-runtimes install-runtimes-stripped
        PROPERTIES FOLDER "Runtimes"
      )
      if(LLVM_INCLUDE_TESTS)
        add_custom_target(check-runtimes)
        add_custom_target(runtimes-test-depends)
        set_target_properties(
          check-runtimes runtimes-test-depends
          PROPERTIES FOLDER "Runtimes"
        )
        set(test_targets "")
      endif()
      if(LLVM_RUNTIME_DISTRIBUTION_COMPONENTS)
        foreach(component ${LLVM_RUNTIME_DISTRIBUTION_COMPONENTS})
          add_custom_target(${component})
          add_custom_target(install-${component})
          add_custom_target(install-${component}-stripped)
          set_target_properties(
            ${component} install-${component} install-${component}-stripped
            PROPERTIES FOLDER "${component}"
          )
        endforeach()
      endif()
    endif()

    foreach(name ${LLVM_RUNTIME_TARGETS})
      if(builtins_dep)
        if (LLVM_BUILTIN_TARGETS)
          set(builtins_dep_name "${builtins_dep}-${name}")
        else()
          set(builtins_dep_name ${builtins_dep})
        endif()
      endif()

      check_apple_target(${name} runtime)

      runtime_register_target(${name}
        DEPENDS ${builtins_dep_name} ${extra_deps}
        CMAKE_ARGS -DLLVM_DEFAULT_TARGET_TRIPLE=${name} ${extra_cmake_args}
        EXTRA_ARGS TARGET_TRIPLE ${name})
    endforeach()

    foreach(multilib ${LLVM_RUNTIME_MULTILIBS})
      foreach(name ${LLVM_RUNTIME_MULTILIB_${multilib}_TARGETS})
        runtime_register_target(${name}+${multilib}
          DEPENDS runtimes-${name}
          CMAKE_ARGS -DLLVM_DEFAULT_TARGET_TRIPLE=${name}
                     -DLLVM_RUNTIMES_PREFIX=${name}/
                     -DLLVM_RUNTIMES_LIBDIR_SUBDIR=${multilib}
          BASE_NAME ${name}
          EXTRA_ARGS TARGET_TRIPLE ${name})
      endforeach()
    endforeach()
  endif()

  if(NOT LLVM_BUILD_INSTRUMENTED AND CLANG_ENABLE_BOOTSTRAP)
    # TODO: This is a hack needed because the libcxx headers are copied into the
    # build directory during configuration. Without that step the clang in the
    # build directory cannot find the C++ headers in certain configurations.
    # I need to build a mechanism for runtime projects to provide CMake code
    # that executes at LLVM configuration time to handle this case.
    add_dependencies(clang-bootstrap-deps runtimes-configure)
    # We need to add the runtimes as a dependency because compiler-rt can be
    # built as part of runtimes and we need the profile runtime for PGO
    add_dependencies(clang-bootstrap-deps runtimes)
    # The bootstrap build will attempt to configure the offload runtime
    # before the openmp project which will error out due to failing to
    # find libomp.so. We must add omp as a dependency before runtimes
    # are configured.
    if("openmp" IN_LIST LLVM_ENABLE_PROJECTS AND "offload" IN_LIST LLVM_ENABLE_RUNTIMES)
      add_dependencies(clang-bootstrap-deps omp)
    endif()
  endif()

  if(LLVM_INCLUDE_TESTS)
    set_property(GLOBAL APPEND PROPERTY LLVM_ALL_ADDITIONAL_TEST_DEPENDS runtimes-test-depends)

    foreach(target ${test_targets} ${SUB_CHECK_TARGETS})
      add_dependencies(${target} ${extra_deps})
    endforeach()

    set_property(GLOBAL APPEND PROPERTY LLVM_ALL_ADDITIONAL_TEST_TARGETS runtimes ${extra_deps})
  endif()
endif()
