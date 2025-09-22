if(LLVM_EXAMPLEIRTRANSFORMS_LINK_INTO_TOOLS)
    message(WARNING "Setting LLVM_EXAMPLEIRTRANSFORMS_LINK_INTO_TOOLS=ON only makes sense for testing purpose")
endif()

# The plugin expects to not link against the Support and Core libraries,
# but expects them to exist in the process loading the plugin. This doesn't
# work with DLLs on Windows (where a shared library can't have undefined
# references), so just skip this example on Windows.
if (NOT WIN32 AND NOT CYGWIN)
  add_llvm_pass_plugin(ExampleIRTransforms
    SimplifyCFG.cpp
    DEPENDS
    intrinsics_gen
    BUILDTREE_ONLY
    )

  install(TARGETS ${name} RUNTIME DESTINATION "${LLVM_EXAMPLES_INSTALL_DIR}")
  set_target_properties(${name} PROPERTIES FOLDER "Examples")
endif()
