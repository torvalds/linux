# Do this by hand instead of using add_llvm_utilities(), which
# tries to create a corresponding executable, which we don't want.
if (LLVM_ADD_NATIVE_VISUALIZERS_TO_SOLUTION)
  set(LLVM_VISUALIZERS llvm.natvis)
  add_custom_target(LLVMVisualizers SOURCES ${LLVM_VISUALIZERS})
  set_target_properties(LLVMVisualizers PROPERTIES FOLDER "LLVM/Misc")
endif()
