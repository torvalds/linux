set(LLVM_LINK_COMPONENTS
  ${LLVM_TARGETS_TO_BUILD}
  BitWriter
  Core
  BinaryFormat
  MC
  Target
  TransformUtils
  Analysis
  Passes
  IRReader
  Object
  Option
  Support
  TargetParser
  CodeGen
  LTO
  )

set(LLVM_TARGET_DEFINITIONS NVLinkOpts.td)
tablegen(LLVM NVLinkOpts.inc -gen-opt-parser-defs)
add_public_tablegen_target(NVLinkWrapperOpts)

if(NOT CLANG_BUILT_STANDALONE)
  set(tablegen_deps intrinsics_gen NVLinkWrapperOpts)
endif()

add_clang_tool(clang-nvlink-wrapper
  ClangNVLinkWrapper.cpp

  DEPENDS
  ${tablegen_deps}
  )

set(CLANG_NVLINK_WRAPPER_LIB_DEPS
  clangBasic
  )

target_link_libraries(clang-nvlink-wrapper
  PRIVATE
  ${CLANG_NVLINK_WRAPPER_LIB_DEPS}
  )
