set(LLVM_LINK_COMPONENTS
  Support
  TargetParser
  )

# By default MSVC has a 2^16 limit on the number of sections in an object
# file, and Transforms.cpp needs more than that.
if (MSVC)
  set_source_files_properties(Transforms.cpp PROPERTIES COMPILE_FLAGS /bigobj)
endif()

add_clang_library(clangARCMigrate
  ARCMT.cpp
  ARCMTActions.cpp
  FileRemapper.cpp
  ObjCMT.cpp
  PlistReporter.cpp
  TransAPIUses.cpp
  TransARCAssign.cpp
  TransAutoreleasePool.cpp
  TransBlockObjCVariable.cpp
  TransEmptyStatementsAndDealloc.cpp
  TransGCAttrs.cpp
  TransGCCalls.cpp
  TransProperties.cpp
  TransProtectedScope.cpp
  TransRetainReleaseDealloc.cpp
  TransUnbridgedCasts.cpp
  TransUnusedInitDelegate.cpp
  TransZeroOutPropsInDealloc.cpp
  TransformActions.cpp
  Transforms.cpp

  LINK_LIBS
  clangAST
  clangAnalysis
  clangBasic
  clangEdit
  clangFrontend
  clangLex
  clangRewrite
  clangSema
  clangSerialization

  DEPENDS
  omp_gen
  ClangDriverOptions
  )
