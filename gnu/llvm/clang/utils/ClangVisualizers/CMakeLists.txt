# Do this by hand instead of using add_llvm_utilities(), which
# tries to create a corresponding executable, which we don't want.
if (LLVM_ADD_NATIVE_VISUALIZERS_TO_SOLUTION)
  set(CLANG_VISUALIZERS clang.natvis)
  add_custom_target(ClangVisualizers SOURCES ${CLANG_VISUALIZERS})
  set_target_properties(ClangVisualizers PROPERTIES FOLDER "Clang/Misc")
endif()
