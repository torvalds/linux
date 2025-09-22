# Since these do not specify a specific PLUGIN_TOOL (which could be clang or
# clang-tidy), we cannot compile this unless the platform supports plugins with
# undefined symbols, and cannot use it unless the user has opted for clang plugins).
if(CLANG_ENABLE_STATIC_ANALYZER AND CLANG_PLUGIN_SUPPORT AND LLVM_ENABLE_PLUGINS)
  add_subdirectory(SampleAnalyzer)
  add_subdirectory(CheckerDependencyHandling)
  add_subdirectory(CheckerOptionHandling)
endif()
