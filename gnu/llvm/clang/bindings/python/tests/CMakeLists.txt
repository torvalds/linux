# Test target to run Python test suite from main build.

# Avoid configurations including '-include' from interfering with
# our tests by setting CLANG_NO_DEFAULT_CONFIG.
add_custom_target(check-clang-python
    COMMAND ${CMAKE_COMMAND} -E env
            CLANG_NO_DEFAULT_CONFIG=1
            CLANG_LIBRARY_PATH=$<TARGET_FILE_DIR:libclang>
            "${Python3_EXECUTABLE}" -m unittest discover
    DEPENDS libclang
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)

set(RUN_PYTHON_TESTS TRUE)
set_target_properties(check-clang-python PROPERTIES FOLDER "Clang/Tests")

# Tests require libclang.so which is only built with LLVM_ENABLE_PIC=ON
if(NOT LLVM_ENABLE_PIC)
  set(RUN_PYTHON_TESTS FALSE)
endif()

# Do not try to run if libclang was built with sanitizers because
# the sanitizer library will likely be loaded too late to perform
# interception and will then fail.
# We could use LD_PRELOAD/DYLD_INSERT_LIBRARIES but this isn't
# portable so its easier just to not run the tests when building
# with ASan.
if(NOT LLVM_USE_SANITIZER STREQUAL "")
  set(RUN_PYTHON_TESTS FALSE)
endif()

# Tests fail on Windows, and need someone knowledgeable to fix.
# It's not clear whether it's a test or a valid binding problem.
if(WIN32)
  set(RUN_PYTHON_TESTS FALSE)
endif()

# The Python FFI interface is broken on AIX: https://bugs.python.org/issue38628.
if(${CMAKE_SYSTEM_NAME} MATCHES "AIX")
  set(RUN_PYTHON_TESTS FALSE)
endif()

# AArch64, Hexagon, and Sparc have known test failures that need to be
# addressed.
# SystemZ has broken Python/FFI interface:
# https://reviews.llvm.org/D52840#1265716
if(${LLVM_NATIVE_ARCH} MATCHES "^(AArch64|Hexagon|Sparc|SystemZ)$")
  set(RUN_PYTHON_TESTS FALSE)
endif()

if(RUN_PYTHON_TESTS)
    set_property(GLOBAL APPEND PROPERTY
                 LLVM_ALL_ADDITIONAL_TEST_TARGETS check-clang-python)
endif()
