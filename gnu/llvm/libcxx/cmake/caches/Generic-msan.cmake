set(LLVM_USE_SANITIZER "MemoryWithOrigins" CACHE STRING "")
set(LIBCXX_TEST_PARAMS "long_tests=False" CACHE STRING "") # MSAN is really slow and tests can sometimes timeout otherwise
set(LIBCXXABI_TEST_PARAMS "${LIBCXX_TEST_PARAMS}" CACHE STRING "")
set(LIBCXXABI_USE_LLVM_UNWINDER OFF CACHE BOOL "") # MSAN is compiled against the system unwinder, which leads to false positives
