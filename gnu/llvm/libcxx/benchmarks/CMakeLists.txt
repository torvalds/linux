include(ExternalProject)
include(CheckCXXCompilerFlag)

#==============================================================================
# Build Google Benchmark
#==============================================================================

set(CMAKE_FOLDER "${CMAKE_FOLDER}/Benchmarks")

set(BENCHMARK_COMPILE_FLAGS
    -Wno-unused-command-line-argument
    -nostdinc++
    -isystem "${LIBCXX_GENERATED_INCLUDE_DIR}"
    -L${LIBCXX_LIBRARY_DIR}
    -Wl,-rpath,${LIBCXX_LIBRARY_DIR}
    ${SANITIZER_FLAGS}
    )
if(LLVM_ENABLE_PER_TARGET_RUNTIME_DIR AND NOT APPLE)
  list(APPEND BENCHMARK_COMPILE_FLAGS
    -isystem "${LIBCXX_GENERATED_INCLUDE_TARGET_DIR}")
endif()
if (DEFINED LIBCXX_CXX_ABI_LIBRARY_PATH)
  list(APPEND BENCHMARK_COMPILE_FLAGS
          -L${LIBCXX_CXX_ABI_LIBRARY_PATH}
          -Wl,-rpath,${LIBCXX_CXX_ABI_LIBRARY_PATH})
endif()
split_list(BENCHMARK_COMPILE_FLAGS)

ExternalProject_Add(google-benchmark
        EXCLUDE_FROM_ALL ON
        DEPENDS cxx cxx-headers
        PREFIX google-benchmark
        SOURCE_DIR ${LLVM_THIRD_PARTY_DIR}/benchmark
        INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/google-benchmark
        CMAKE_CACHE_ARGS
          -DCMAKE_C_COMPILER:STRING=${CMAKE_C_COMPILER}
          -DCMAKE_CXX_COMPILER:STRING=${CMAKE_CXX_COMPILER}
          -DCMAKE_BUILD_TYPE:STRING=RELEASE
          -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
          -DCMAKE_CXX_FLAGS:STRING=${BENCHMARK_COMPILE_FLAGS}
          -DBENCHMARK_USE_LIBCXX:BOOL=ON
          -DBENCHMARK_ENABLE_TESTING:BOOL=OFF)

#==============================================================================
# Benchmark tests configuration
#==============================================================================
add_custom_target(cxx-benchmarks)
set(BENCHMARK_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(BENCHMARK_INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/google-benchmark)

add_library(               cxx-benchmarks-flags INTERFACE)

# TODO(cmake): remove. This is a workaround to prevent older versions of GCC
# from failing the configure step because they don't support C++23.
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13.0")
  return()
endif()
#TODO(cmake): remove the `add_compile_options`. Currently we have to explicitly
# pass the `std:c++latest` flag on Windows to work around an issue where
# requesting `cxx_std_23` results in an error -- somehow CMake fails to
# translate the `c++23` flag into `c++latest`, and the highest numbered C++
# version that MSVC flags support is C++20.
if (MSVC)
  add_compile_options(/std:c++latest)
# ibm-clang does not recognize the cxx_std_23 flag, so use this as a temporary
# workaround on AIX as well.
elseif (${CMAKE_SYSTEM_NAME} MATCHES "AIX")
  add_compile_options(-std=c++23)
else()
  target_compile_features( cxx-benchmarks-flags INTERFACE cxx_std_23)
endif()

target_compile_options(cxx-benchmarks-flags INTERFACE -fsized-deallocation -nostdinc++
                                                      ${SANITIZER_FLAGS} -Wno-user-defined-literals -Wno-suggest-override)
target_include_directories(cxx-benchmarks-flags INTERFACE "${LIBCXX_GENERATED_INCLUDE_DIR}"
                                                INTERFACE "${BENCHMARK_INSTALL_DIR}/include"
                                                INTERFACE "${LIBCXX_SOURCE_DIR}/test/support")
target_link_options(cxx-benchmarks-flags INTERFACE -lm -nostdlib++
                                                   "-L${BENCHMARK_INSTALL_DIR}/lib" "-L${BENCHMARK_INSTALL_DIR}/lib64"
                                                   ${SANITIZER_FLAGS})

set(libcxx_benchmark_targets)

function(add_benchmark_test name source_file)
  set(libcxx_target ${name}_libcxx)
  list(APPEND libcxx_benchmark_targets ${libcxx_target})
  add_executable(${libcxx_target} EXCLUDE_FROM_ALL ${source_file})
  target_link_libraries(${libcxx_target} PRIVATE cxx-benchmarks-flags)
  add_dependencies(${libcxx_target} cxx google-benchmark)
  add_dependencies(cxx-benchmarks ${libcxx_target})
  if (LIBCXX_ENABLE_SHARED)
    target_link_libraries(${libcxx_target} PRIVATE cxx_shared)
  else()
    target_link_libraries(${libcxx_target} PRIVATE cxx_static)
  endif()
  target_link_libraries(${libcxx_target} PRIVATE cxx_experimental benchmark)
  if (LLVM_USE_SANITIZER)
    target_link_libraries(${libcxx_target} PRIVATE -ldl)
  endif()
  set_target_properties(${libcxx_target}
    PROPERTIES
          OUTPUT_NAME "${name}.bench.out"
          RUNTIME_OUTPUT_DIRECTORY "${BENCHMARK_OUTPUT_DIR}"
          CXX_EXTENSIONS NO)
  cxx_link_system_libraries(${libcxx_target})
endfunction()


#==============================================================================
# Register Benchmark tests
#==============================================================================
set(BENCHMARK_TESTS
    algorithms.partition_point.bench.cpp
    algorithms/count.bench.cpp
    algorithms/equal.bench.cpp
    algorithms/find.bench.cpp
    algorithms/fill.bench.cpp
    algorithms/for_each.bench.cpp
    algorithms/lower_bound.bench.cpp
    algorithms/make_heap.bench.cpp
    algorithms/make_heap_then_sort_heap.bench.cpp
    algorithms/min.bench.cpp
    algorithms/minmax.bench.cpp
    algorithms/min_max_element.bench.cpp
    algorithms/mismatch.bench.cpp
    algorithms/pop_heap.bench.cpp
    algorithms/pstl.stable_sort.bench.cpp
    algorithms/push_heap.bench.cpp
    algorithms/ranges_contains.bench.cpp
    algorithms/ranges_ends_with.bench.cpp
    algorithms/ranges_make_heap.bench.cpp
    algorithms/ranges_make_heap_then_sort_heap.bench.cpp
    algorithms/ranges_pop_heap.bench.cpp
    algorithms/ranges_push_heap.bench.cpp
    algorithms/ranges_sort.bench.cpp
    algorithms/ranges_sort_heap.bench.cpp
    algorithms/ranges_stable_sort.bench.cpp
    algorithms/set_intersection.bench.cpp
    algorithms/sort.bench.cpp
    algorithms/sort_heap.bench.cpp
    algorithms/stable_sort.bench.cpp
    atomic_wait.bench.cpp
    atomic_wait_vs_mutex_lock.bench.cpp
    libcxxabi/dynamic_cast.bench.cpp
    libcxxabi/dynamic_cast_old_stress.bench.cpp
    allocation.bench.cpp
    deque.bench.cpp
    deque_iterator.bench.cpp
    exception_ptr.bench.cpp
    filesystem.bench.cpp
    format_to_n.bench.cpp
    format_to.bench.cpp
    format.bench.cpp
    formatted_size.bench.cpp
    formatter_float.bench.cpp
    formatter_int.bench.cpp
    function.bench.cpp
    join_view.bench.cpp
    lexicographical_compare_three_way.bench.cpp
    map.bench.cpp
    monotonic_buffer.bench.cpp
    numeric/gcd.bench.cpp
    ordered_set.bench.cpp
    shared_mutex_vs_mutex.bench.cpp
    stop_token.bench.cpp
    std_format_spec_string_unicode.bench.cpp
    std_format_spec_string_unicode_escape.bench.cpp
    string.bench.cpp
    stringstream.bench.cpp
    system_error.bench.cpp
    to_chars.bench.cpp
    unordered_set_operations.bench.cpp
    util_smartptr.bench.cpp
    variant_visit_1.bench.cpp
    variant_visit_2.bench.cpp
    variant_visit_3.bench.cpp
    vector_operations.bench.cpp
    )

foreach(test_path ${BENCHMARK_TESTS})
  get_filename_component(test_file "${test_path}" NAME)
  string(REPLACE ".bench.cpp" "" test_name "${test_file}")
  if (NOT DEFINED ${test_name}_REPORTED)
    message(STATUS "Adding Benchmark: ${test_file}")
    # Only report the adding of the benchmark once.
    set(${test_name}_REPORTED ON CACHE INTERNAL "")
  endif()
  add_benchmark_test(${test_name} ${test_path})
endforeach()

if (LIBCXX_INCLUDE_TESTS)
  include(AddLLVM)

  configure_lit_site_cfg(
          ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
          ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py)

  set(BENCHMARK_LIT_ARGS "--show-all --show-xfail --show-unsupported ${LIT_ARGS_DEFAULT}")

  add_lit_target(check-cxx-benchmarks
          "Running libcxx benchmarks tests"
          ${CMAKE_CURRENT_BINARY_DIR}
          DEPENDS cxx-benchmarks cxx-test-depends
          ARGS ${BENCHMARK_LIT_ARGS})
endif()
