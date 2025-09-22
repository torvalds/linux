# Link LibCURL if the user wants it
if (LLVM_ENABLE_CURL)
  set(imported_libs CURL::libcurl)
endif()

# Link cpp-httplib if the user wants it
if (LLVM_ENABLE_HTTPLIB)
  set(imported_libs ${imported_libs} httplib::httplib)
endif()

# Make sure pthread is linked if this is a unix host
if (CMAKE_HOST_UNIX)
  set(imported_libs ${imported_libs} ${LLVM_PTHREAD_LIB})
endif()

# Note: This isn't a component, since that could potentially add a libcurl
# dependency to libLLVM.
add_llvm_library(LLVMDebuginfod
  BuildIDFetcher.cpp
  Debuginfod.cpp
  HTTPClient.cpp
  HTTPServer.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Debuginfod

  LINK_LIBS
  ${imported_libs}

  LINK_COMPONENTS
  Support
  Symbolize
  DebugInfoDWARF
  BinaryFormat
  Object
  )
