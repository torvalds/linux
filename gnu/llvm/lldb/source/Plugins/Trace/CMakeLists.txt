option(LLDB_BUILD_INTEL_PT "Enable Building of Intel(R) Processor Trace Tool" OFF)

add_subdirectory(common)

if (LLDB_BUILD_INTEL_PT)
  add_subdirectory(intel-pt)
endif()
