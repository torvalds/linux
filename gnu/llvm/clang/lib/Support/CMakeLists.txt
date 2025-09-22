set(LLVM_COMMON_DEPENDS_OLD ${LLVM_COMMON_DEPENDS})

# Drop clang-tablegen-targets from LLVM_COMMON_DEPENDS.
# so that we could use clangSupport within clang-tblgen and other clang
# component.
list(REMOVE_ITEM LLVM_COMMON_DEPENDS clang-tablegen-targets)

set(LLVM_LINK_COMPONENTS
  Support
  )

set(clangSupport_sources
  RISCVVIntrinsicUtils.cpp
  )

add_clang_library(clangSupport ${clangSupport_sources})

if (TARGET obj.clangSupport)
  add_library(clangSupport_tablegen ALIAS obj.clangSupport)
elseif (NOT LLVM_LINK_LLVM_DYLIB)
  add_library(clangSupport_tablegen ALIAS clangSupport)
else()
  # Build a version of the support library that does not link against
  # libLLVM-*.so, to be used by clang-tblgen. This is so clang-tblgen doesn't
  # link against libLLVMSupport twice (once statically and once via
  # libLLVM-*.so).
  add_llvm_library(clangSupport_tablegen
    BUILDTREE_ONLY STATIC DISABLE_LLVM_LINK_LLVM_DYLIB
    ${clangSupport_sources})
endif()

set(LLVM_COMMON_DEPENDS ${LLVM_COMMON_DEPENDS_OLD})
