set(LLDB_SYSTEM_LIBS)

if (APPLE)
  list(APPEND LLDB_SYSTEM_LIBS
       ${FOUNDATION_LIBRARY}
       ${CORE_FOUNDATION_LIBRARY}
       ${CORE_SERVICES_LIBRARY}
       ${SECURITY_LIBRARY}
       ${DEBUG_SYMBOLS_LIBRARY})
endif()

if(NOT PURE_WINDOWS)
  list(APPEND LLDB_SYSTEM_LIBS ${CMAKE_THREAD_LIBS_INIT})
endif()

list(APPEND LLDB_SYSTEM_LIBS ${CMAKE_DL_LIBS})

if (CMAKE_SYSTEM_NAME MATCHES "Windows")
  list(APPEND LLDB_SYSTEM_LIBS ws2_32 rpcrt4)
endif ()

if (NOT HAVE_CXX_ATOMICS64_WITHOUT_LIB )
    list(APPEND LLDB_SYSTEM_LIBS atomic)
endif()

add_lldb_library(lldbUtility NO_INTERNAL_DEPENDENCIES
  AddressableBits.cpp
  ArchSpec.cpp
  Args.cpp
  Baton.cpp
  Broadcaster.cpp
  Checksum.cpp
  CompletionRequest.cpp
  Connection.cpp
  ConstString.cpp
  DataBufferHeap.cpp
  DataBufferLLVM.cpp
  DataEncoder.cpp
  DataExtractor.cpp
  Diagnostics.cpp
  Environment.cpp
  ErrorMessages.cpp
  Event.cpp
  FileSpec.cpp
  FileSpecList.cpp
  GDBRemote.cpp
  IOObject.cpp
  LLDBAssert.cpp
  LLDBLog.cpp
  Listener.cpp
  Log.cpp
  NameMatches.cpp
  ProcessInfo.cpp
  RegisterValue.cpp
  RegularExpression.cpp
  Instrumentation.cpp
  Scalar.cpp
  SelectHelper.cpp
  State.cpp
  Status.cpp
  Stream.cpp
  StreamString.cpp
  StringExtractor.cpp
  StringExtractorGDBRemote.cpp
  StringLexer.cpp
  StringList.cpp
  StructuredData.cpp
  TildeExpressionResolver.cpp
  Timer.cpp
  TraceGDBRemotePackets.cpp
  TraceIntelPTGDBRemotePackets.cpp
  UnimplementedError.cpp
  UUID.cpp
  UriParser.cpp
  UserID.cpp
  UserIDResolver.cpp
  VASprintf.cpp
  VMRange.cpp
  XcodeSDK.cpp
  ZipFile.cpp

  LINK_LIBS
    ${LLDB_SYSTEM_LIBS}
    # lldbUtility does not depend on other LLDB libraries

  LINK_COMPONENTS
    BinaryFormat
    Support
    TargetParser
  )
