set(LLVM_LINK_COMPONENTS ${LLVM_TARGETS_TO_BUILD} Support)

add_clang_library(clangFuzzerInitialize fuzzer_initialize.cpp)
