# Discover the projects that use CMake in the subdirectories.
# Note that explicit cmake invocation is required every time a new project is
# added or removed.
file(GLOB entries *)
foreach(entry ${entries})
  if(IS_DIRECTORY ${entry} AND EXISTS ${entry}/CMakeLists.txt)
    if((NOT ${entry} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR}/compiler-rt) AND
       (NOT ${entry} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR}/dragonegg) AND
       (NOT ${entry} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR}/libcxx) AND
       (NOT ${entry} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR}/libcxxabi) AND
       (NOT ${entry} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR}/libunwind) AND
       (NOT ${entry} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR}/test-suite) AND
       (NOT ${entry} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR}/openmp) AND
       (NOT ${entry} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR}/cross-project-tests))
      get_filename_component(entry_name "${entry}" NAME)
      add_llvm_external_project(${entry_name})
    endif()
  endif()
endforeach(entry)

# Also add in libc++ and compiler-rt trees if present (and we have
# a sufficiently recent version of CMake where required).
if(${LLVM_BUILD_RUNTIME})
  # MSVC isn't quite working with libc++ yet, disable it until issues are
  # fixed.
  # FIXME: LLVM_FORCE_BUILD_RUNTIME is currently used by libc++ to force
  # enable the in-tree build when targeting clang-cl.
  if(NOT MSVC OR LLVM_FORCE_BUILD_RUNTIME)
    # Add the projects in reverse order of their dependencies so that the
    # dependent projects can see the target names of their dependencies.
    add_llvm_external_project(libunwind)
    add_llvm_external_project(pstl)
    add_llvm_external_project(libc)
    add_llvm_external_project(libcxxabi)
    add_llvm_external_project(libcxx)
  endif()
  if(NOT LLVM_BUILD_EXTERNAL_COMPILER_RT)
    add_llvm_external_project(compiler-rt)
  endif()
endif()

add_llvm_external_project(dragonegg)
add_llvm_external_project(openmp)

if(LLVM_INCLUDE_TESTS)
  add_llvm_external_project(cross-project-tests)
endif()
