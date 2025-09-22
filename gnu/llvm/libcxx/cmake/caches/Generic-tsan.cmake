set(LLVM_USE_SANITIZER "Thread" CACHE STRING "")
set(LIBCXXABI_USE_LLVM_UNWINDER OFF CACHE BOOL "") # TSAN is compiled against the system unwinder, which leads to false positives
