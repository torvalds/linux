# TODO: Set the install directory.

include(ExternalProject)

set(known_subdirs
  "libcxx"
  )

foreach (dir ${known_subdirs})
  if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/CMakeLists.txt)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/${dir})
  endif()
endforeach()

function(get_ext_project_build_command out_var target)
  if (CMAKE_GENERATOR MATCHES "Make")
    # Use special command for Makefiles to support parallelism.
    set(${out_var} "$(MAKE)" "${target}" PARENT_SCOPE)
  else()
    set(${out_var} ${CMAKE_COMMAND} --build . --target ${target}
                                    --config $<CONFIG> PARENT_SCOPE)
  endif()
endfunction()

set(COMPILER_RT_SRC_ROOT ${LLVM_MAIN_SRC_DIR}/projects/compiler-rt)
# Fallback to the external path, if the other one isn't available.
# This is the same behavior (try "internal", then check the LLVM_EXTERNAL_...
# variable) as in add_llvm_external_project
if(NOT EXISTS ${COMPILER_RT_SRC_ROOT})
  # We don't want to set it if LLVM_EXTERNAL_COMPILER_RT_SOURCE_DIR is ""
  if(LLVM_EXTERNAL_COMPILER_RT_SOURCE_DIR)
    set(COMPILER_RT_SRC_ROOT ${LLVM_EXTERNAL_COMPILER_RT_SOURCE_DIR})
  endif()
endif()

if(LLVM_BUILD_EXTERNAL_COMPILER_RT AND EXISTS ${COMPILER_RT_SRC_ROOT}/)

  # Add compiler-rt as an external project.
  set(COMPILER_RT_PREFIX ${CMAKE_BINARY_DIR}/projects/compiler-rt)

  set(STAMP_DIR ${CMAKE_CURRENT_BINARY_DIR}/compiler-rt-stamps/)
  set(BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/compiler-rt-bins/)

  add_custom_target(compiler-rt-clear
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${STAMP_DIR}
    COMMENT "Clobberring compiler-rt build and stamp directories"
    )

  # Find all variables that start with COMPILER_RT and populate a variable with
  # them.
  get_cmake_property(variableNames VARIABLES)
  foreach(variableName ${variableNames})
    if(variableName MATCHES "^COMPILER_RT")
      string(REPLACE ";" "\;" value "${${variableName}}")
      list(APPEND COMPILER_RT_PASSTHROUGH_VARIABLES
        -D${variableName}=${value})
    endif()
  endforeach()

  set(compiler_rt_configure_deps)
  if(TARGET cxx-headers)
    list(APPEND compiler_rt_configure_deps "cxx-headers")
  endif()
  if(LLVM_INCLUDE_TESTS)
    list(APPEND compiler_rt_configure_deps LLVMTestingSupport)
  endif()

  include(GetClangResourceDir)
  get_clang_resource_dir(output_resource_dir PREFIX ${LLVM_BINARY_DIR})
  get_clang_resource_dir(install_resource_dir)
  ExternalProject_Add(compiler-rt
    DEPENDS llvm-config clang ${compiler_rt_configure_deps}
    PREFIX ${COMPILER_RT_PREFIX}
    SOURCE_DIR ${COMPILER_RT_SRC_ROOT}
    STAMP_DIR ${STAMP_DIR}
    BINARY_DIR ${BINARY_DIR}
    CMAKE_ARGS ${CLANG_COMPILER_RT_CMAKE_ARGS}
               -DCMAKE_C_COMPILER=${LLVM_RUNTIME_OUTPUT_INTDIR}/clang
               -DCMAKE_CXX_COMPILER=${LLVM_RUNTIME_OUTPUT_INTDIR}/clang++
               -DCMAKE_ASM_COMPILER=${LLVM_RUNTIME_OUTPUT_INTDIR}/clang
               -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
               -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
               -DCMAKE_C_COMPILER_LAUNCHER=${CMAKE_C_COMPILER_LAUNCHER}
               -DCMAKE_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER}
               -DLLVM_CONFIG_PATH=${LLVM_RUNTIME_OUTPUT_INTDIR}/llvm-config
               -DLLVM_LIT_ARGS=${LLVM_LIT_ARGS}
               -DCOMPILER_RT_OUTPUT_DIR=${output_resource_dir}
               -DCOMPILER_RT_EXEC_OUTPUT_DIR=${LLVM_RUNTIME_OUTPUT_INTDIR}
               -DCOMPILER_RT_INSTALL_PATH:PATH=${install_resource_dir}
               -DCOMPILER_RT_INCLUDE_TESTS=${LLVM_INCLUDE_TESTS}
               -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
               -DLLVM_LIBDIR_SUFFIX=${LLVM_LIBDIR_SUFFIX}
               -DLLVM_RUNTIME_OUTPUT_INTDIR=${LLVM_RUNTIME_OUTPUT_INTDIR}
               -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
               -DCMAKE_OSX_SYSROOT:PATH=${CMAKE_OSX_SYSROOT}
               ${COMPILER_RT_PASSTHROUGH_VARIABLES}
    INSTALL_COMMAND ""
    STEP_TARGETS configure build
    USES_TERMINAL_CONFIGURE 1
    USES_TERMINAL_BUILD 1
    USES_TERMINAL_INSTALL 1
    # Always run the build command so that incremental builds are correct.
    BUILD_ALWAYS 1
    )

  get_ext_project_build_command(run_clean_compiler_rt clean)
  ExternalProject_Add_Step(compiler-rt clean
    COMMAND ${run_clean_compiler_rt}
    COMMENT "Cleaning compiler-rt..."
    DEPENDEES configure
    DEPENDERS build
    DEPENDS clang
    WORKING_DIRECTORY ${BINARY_DIR}
    )

  install(CODE "execute_process\(COMMAND \${CMAKE_COMMAND} -DCMAKE_INSTALL_PREFIX=\${CMAKE_INSTALL_PREFIX} -P ${BINARY_DIR}/cmake_install.cmake \)"
    COMPONENT compiler-rt)

  add_llvm_install_targets(install-compiler-rt
                           DEPENDS compiler-rt
                           COMPONENT compiler-rt)

  # Add top-level targets that build specific compiler-rt runtimes.
  set(COMPILER_RT_RUNTIMES fuzzer asan builtins dfsan lsan msan profile tsan ubsan ubsan-minimal)
  foreach(runtime ${COMPILER_RT_RUNTIMES})
    get_ext_project_build_command(build_runtime_cmd ${runtime})
    add_custom_target(${runtime}
      COMMAND ${build_runtime_cmd}
      DEPENDS compiler-rt-configure
      WORKING_DIRECTORY ${BINARY_DIR}
      VERBATIM USES_TERMINAL)
  endforeach()

  if(LLVM_INCLUDE_TESTS)
    # Add binaries that compiler-rt tests depend on.
    set(COMPILER_RT_TEST_DEPENDENCIES
      FileCheck count not llvm-nm llvm-objdump llvm-symbolizer llvm-jitlink lli split-file)

    # Add top-level targets for various compiler-rt test suites.
    set(COMPILER_RT_TEST_SUITES
        check-asan
        check-asan-dynamic
        check-cfi
        check-cfi-and-supported
        check-dfsan
        check-fuzzer
        check-gwp_asan
        check-hwasan
        check-lsan
        check-msan
        check-profile
        check-safestack
        check-sanitizer
        check-tsan
        check-ubsan
        check-ubsan-minimal
    )
    foreach(test_suite ${COMPILER_RT_TEST_SUITES})
      get_ext_project_build_command(run_test_suite ${test_suite})
      add_custom_target(${test_suite}
        COMMAND ${run_test_suite}
        DEPENDS compiler-rt-build ${COMPILER_RT_TEST_DEPENDENCIES}
        WORKING_DIRECTORY ${BINARY_DIR}
        VERBATIM
        USES_TERMINAL
        )
    endforeach()

    # Add special target to run all compiler-rt test suites.
    get_ext_project_build_command(run_check_compiler_rt check-all)
    add_custom_target(check-compiler-rt
      COMMAND ${run_check_compiler_rt}
      DEPENDS compiler-rt-build ${COMPILER_RT_TEST_DEPENDENCIES}
      WORKING_DIRECTORY ${BINARY_DIR}
      VERBATIM USES_TERMINAL)

    # Add special target to run all compiler-rt test suites.
    get_ext_project_build_command(run_check_compiler_rt compiler-rt-test-depends)
    add_custom_target(compiler-rt-test-depends
      COMMAND ${run_check_compiler_rt}
      DEPENDS compiler-rt-build ${COMPILER_RT_TEST_DEPENDENCIES}
      WORKING_DIRECTORY ${BINARY_DIR}
      VERBATIM USES_TERMINAL)
    set_property(GLOBAL APPEND PROPERTY LLVM_ALL_ADDITIONAL_TEST_DEPENDS compiler-rt-test-depends)
    set_property(GLOBAL APPEND PROPERTY LLVM_ALL_ADDITIONAL_TEST_TARGETS check-compiler-rt)
  endif()
endif()
