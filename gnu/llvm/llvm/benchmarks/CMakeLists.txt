set(LLVM_LINK_COMPONENTS
  Support)

add_benchmark(DummyYAML DummyYAML.cpp PARTIAL_SOURCES_INTENDED)
add_benchmark(xxhash xxhash.cpp PARTIAL_SOURCES_INTENDED)
