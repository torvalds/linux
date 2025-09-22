# Flags to control each individual feature
option(LLDB_BUILD_INTEL_MPX "Enable Building of Intel(R) Memory Protection Extensions" ON)

# Return if all features are OFF
if (NOT LLDB_BUILD_INTEL_MPX)
  return()
endif()

LIST (APPEND FEATURE_LIBS "")

# Add feature specific subdirectories based on flags
if (LLDB_BUILD_INTEL_MPX AND CMAKE_SYSTEM_NAME MATCHES "Linux")
  add_subdirectory(intel-mpx)
  LIST (APPEND FEATURE_LIBS ${FEATURE_LIBS} lldbIntelMPX)
  SET (CLI_WRAPPER_PREPROCESSORS "${CLI_WRAPPER_PREPROCESSORS} -DBUILD_INTEL_MPX")
endif()

if (NOT CLI_WRAPPER_PREPROCESSORS)
  return()
endif()

set_source_files_properties(cli-wrapper.cpp PROPERTIES
                            COMPILE_FLAGS ${CLI_WRAPPER_PREPROCESSORS})

add_lldb_library(lldbIntelFeatures SHARED
  cli-wrapper.cpp

  LINK_LIBS
    ${FEATURE_LIBS}
  )

install(TARGETS lldbIntelFeatures
  LIBRARY DESTINATION lib${LLVM_LIBDIR_SUFFIX})
