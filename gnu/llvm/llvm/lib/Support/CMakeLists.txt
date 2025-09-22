include(GetLibraryName)

# Ensure that libSupport does not carry any static global initializer.
# libSupport can be embedded in use cases where we don't want to load all
# cl::opt unless we want to parse the command line.
# ManagedStatic can be used to enable lazy-initialization of globals.
# We don't use `add_flag_if_supported` as instead of compiling an empty file we
# check if the current platform is able to compile global std::mutex with this
# flag (Linux can, Darwin can't for example).
check_cxx_compiler_flag("-Werror=global-constructors" HAS_WERROR_GLOBAL_CTORS)
if (HAS_WERROR_GLOBAL_CTORS)
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=global-constructors")
  CHECK_CXX_SOURCE_COMPILES("
  #include <mutex>
  static std::mutex TestGlobalCtorDtor;
  static std::recursive_mutex TestGlobalCtorDtor2;
  int main() { (void)TestGlobalCtorDtor; (void)TestGlobalCtorDtor2; return 0;}
  " LLVM_HAS_NOGLOBAL_CTOR_MUTEX)
  if (NOT LLVM_HAS_NOGLOBAL_CTOR_MUTEX)
    string(REPLACE "-Werror=global-constructors" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
  endif()
endif()

if(LLVM_ENABLE_ZLIB)
  list(APPEND imported_libs ZLIB::ZLIB)
endif()

if(LLVM_ENABLE_ZSTD)
  if(TARGET zstd::libzstd_shared AND NOT LLVM_USE_STATIC_ZSTD)
    set(zstd_target zstd::libzstd_shared)
  else()
    set(zstd_target zstd::libzstd_static)
  endif()
endif()

if(LLVM_ENABLE_ZSTD)
  list(APPEND imported_libs ${zstd_target})
endif()

if( WIN32 )
  # libuuid required for FOLDERID_Profile usage in lib/Support/Windows/Path.inc.
  # advapi32 required for CryptAcquireContextW in lib/Support/Windows/Path.inc.
  # ntdll required for RtlGetLastNtStatus in lib/Support/ErrorHandling.cpp.
  set(system_libs ${system_libs} psapi shell32 ole32 uuid advapi32 ws2_32 ntdll)
elseif( CMAKE_HOST_UNIX )
  if( HAVE_LIBRT )
    set(system_libs ${system_libs} rt)
  endif()
  if( HAVE_LIBDL )
    set(system_libs ${system_libs} ${CMAKE_DL_LIBS})
  endif()
  if( HAVE_BACKTRACE AND NOT "${Backtrace_LIBRARIES}" STREQUAL "" )
    # On BSDs, CMake returns a fully qualified path to the backtrace library.
    # We need to remove the path and the 'lib' prefix, to make it look like a
    # regular short library name, suitable for appending to a -l link flag.
    get_filename_component(Backtrace_LIBFILE ${Backtrace_LIBRARIES} NAME_WE)
    STRING(REGEX REPLACE "^lib" "" Backtrace_LIBFILE ${Backtrace_LIBFILE})
    set(system_libs ${system_libs} ${Backtrace_LIBFILE})
  endif()
  set(system_libs ${system_libs} ${LLVM_ATOMIC_LIB})
  set(system_libs ${system_libs} ${LLVM_PTHREAD_LIB})
  if( UNIX AND NOT (BEOS OR HAIKU) )
    set(system_libs ${system_libs} m)
  endif()
  if( UNIX AND ${CMAKE_SYSTEM_NAME} MATCHES "SunOS" )
    set(system_libs ${system_libs} kstat socket)
  endif()
  if( FUCHSIA )
    set(system_libs ${system_libs} zircon)
  endif()
  if ( HAIKU )
    add_compile_definitions(_BSD_SOURCE)
    set(system_libs ${system_libs} bsd network)
  endif()
endif( WIN32 )

# Delay load shell32.dll if possible to speed up process startup.
set (delayload_flags)
if (MSVC)
  # When linking with Swift, `swiftc.exe` is used as the linker drive rather
  # than invoking `link.exe` directly.  In such a case, the flags should be
  # marked as `-Xlinker` to pass them directly to the linker.  As a temporary
  # workaround simply elide the delay loading.
  set (delayload_flags $<$<NOT:$<LINK_LANGUAGE:Swift>>:delayimp -delayload:shell32.dll -delayload:ole32.dll>)
endif()

# Link Z3 if the user wants to build it.
if(LLVM_WITH_Z3)
  set(system_libs ${system_libs} ${Z3_LIBRARIES})
endif()

# Override the C runtime allocator on Windows and embed it into LLVM tools & libraries
if(LLVM_INTEGRATED_CRT_ALLOC)
  if (NOT CMAKE_MSVC_RUNTIME_LIBRARY OR CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL$")
    message(FATAL_ERROR "LLVM_INTEGRATED_CRT_ALLOC only works with CMAKE_MSVC_RUNTIME_LIBRARY set to MultiThreaded or MultiThreadedDebug.")
  endif()

  string(REGEX REPLACE "(/|\\\\)$" "" LLVM_INTEGRATED_CRT_ALLOC "${LLVM_INTEGRATED_CRT_ALLOC}")

  if(NOT EXISTS "${LLVM_INTEGRATED_CRT_ALLOC}")
    message(FATAL_ERROR "Cannot find the path to `git clone` for the CRT allocator! (${LLVM_INTEGRATED_CRT_ALLOC}). Currently, rpmalloc, snmalloc and mimalloc are supported.")
  endif()

  if((LLVM_INTEGRATED_CRT_ALLOC MATCHES "rpmalloc$") OR LLVM_ENABLE_RPMALLOC)
    add_compile_definitions(ENABLE_OVERRIDE ENABLE_PRELOAD)
    set(ALLOCATOR_FILES "${LLVM_INTEGRATED_CRT_ALLOC}/rpmalloc/rpmalloc.c")
    set(delayload_flags "${delayload_flags} -INCLUDE:malloc")
  elseif(LLVM_INTEGRATED_CRT_ALLOC MATCHES "snmalloc$")
    set(ALLOCATOR_FILES "${LLVM_INTEGRATED_CRT_ALLOC}/src/snmalloc/override/new.cc")
    set(system_libs ${system_libs} "mincore.lib" "-INCLUDE:malloc")
  elseif(LLVM_INTEGRATED_CRT_ALLOC MATCHES "mimalloc$")
    set(MIMALLOC_LIB "${LLVM_INTEGRATED_CRT_ALLOC}/out/msvc-x64/Release/mimalloc-static.lib")
    if(NOT EXISTS "${MIMALLOC_LIB}")
	  message(FATAL_ERROR "Cannot find the mimalloc static library. To build it, first apply the patch from https://github.com/microsoft/mimalloc/issues/268 then build the Release x64 target through ${LLVM_INTEGRATED_CRT_ALLOC}\\ide\\vs2019\\mimalloc.sln")
    endif()
    set(system_libs ${system_libs} "${MIMALLOC_LIB}" "-INCLUDE:malloc")
  endif()
endif()

# FIXME: We are currently guarding AIX headers with _XOPEN_SOURCE=700.
# See llvm/CMakeLists.txt. However, we need _SC_NPROCESSORS_ONLN in
# unistd.h and it is guarded by _ALL_SOURCE, so we remove the _XOPEN_SOURCE
# guard here. We should remove the guards all together once AIX cleans up
# the system headers.
if (UNIX AND ${CMAKE_SYSTEM_NAME} MATCHES "AIX")
  remove_definitions("-D_XOPEN_SOURCE=700")
endif()

add_subdirectory(BLAKE3)

add_llvm_component_library(LLVMSupport
  ABIBreak.cpp
  AMDGPUMetadata.cpp
  APFixedPoint.cpp
  APFloat.cpp
  APInt.cpp
  APSInt.cpp
  ARMBuildAttrs.cpp
  ARMAttributeParser.cpp
  ARMWinEH.cpp
  Allocator.cpp
  AutoConvert.cpp
  Base64.cpp
  BalancedPartitioning.cpp
  BinaryStreamError.cpp
  BinaryStreamReader.cpp
  BinaryStreamRef.cpp
  BinaryStreamWriter.cpp
  BlockFrequency.cpp
  BranchProbability.cpp
  BuryPointer.cpp
  CachePruning.cpp
  Caching.cpp
  circular_raw_ostream.cpp
  Chrono.cpp
  COM.cpp
  CodeGenCoverage.cpp
  CommandLine.cpp
  Compression.cpp
  CRC.cpp
  ConvertUTF.cpp
  ConvertEBCDIC.cpp
  ConvertUTFWrapper.cpp
  CrashRecoveryContext.cpp
  CSKYAttributes.cpp
  CSKYAttributeParser.cpp
  DataExtractor.cpp
  Debug.cpp
  DebugCounter.cpp
  DeltaAlgorithm.cpp
  DivisionByConstantInfo.cpp
  DAGDeltaAlgorithm.cpp
  DJB.cpp
  DynamicAPInt.cpp
  ELFAttributeParser.cpp
  ELFAttributes.cpp
  Error.cpp
  ErrorHandling.cpp
  ExponentialBackoff.cpp
  ExtensibleRTTI.cpp
  FileCollector.cpp
  FileUtilities.cpp
  FileOutputBuffer.cpp
  FloatingPointMode.cpp
  FoldingSet.cpp
  FormattedStream.cpp
  FormatVariadic.cpp
  GlobPattern.cpp
  GraphWriter.cpp
  HexagonAttributeParser.cpp
  HexagonAttributes.cpp
  InitLLVM.cpp
  InstructionCost.cpp
  IntEqClasses.cpp
  IntervalMap.cpp
  JSON.cpp
  KnownBits.cpp
  LEB128.cpp
  LineIterator.cpp
  Locale.cpp
  LockFileManager.cpp
  ManagedStatic.cpp
  MathExtras.cpp
  MemAlloc.cpp
  MemoryBuffer.cpp
  MemoryBufferRef.cpp
  MD5.cpp
  MSP430Attributes.cpp
  MSP430AttributeParser.cpp
  NativeFormatting.cpp
  OptimizedStructLayout.cpp
  Optional.cpp
  PGOOptions.cpp
  Parallel.cpp
  PluginLoader.cpp
  PrettyStackTrace.cpp
  RandomNumberGenerator.cpp
  Regex.cpp
  RISCVAttributes.cpp
  RISCVAttributeParser.cpp
  RISCVISAUtils.cpp
  ScaledNumber.cpp
  ScopedPrinter.cpp
  SHA1.cpp
  SHA256.cpp
  Signposts.cpp
  SipHash.cpp
  SlowDynamicAPInt.cpp
  SmallPtrSet.cpp
  SmallVector.cpp
  SourceMgr.cpp
  SpecialCaseList.cpp
  Statistic.cpp
  StringExtras.cpp
  StringMap.cpp
  StringSaver.cpp
  StringRef.cpp
  SuffixTreeNode.cpp
  SuffixTree.cpp
  SystemUtils.cpp
  TarWriter.cpp
  ThreadPool.cpp
  TimeProfiler.cpp
  Timer.cpp
  ToolOutputFile.cpp
  Twine.cpp
  TypeSize.cpp
  Unicode.cpp
  UnicodeCaseFold.cpp
  UnicodeNameToCodepoint.cpp
  UnicodeNameToCodepointGenerated.cpp
  VersionTuple.cpp
  VirtualFileSystem.cpp
  WithColor.cpp
  YAMLParser.cpp
  YAMLTraits.cpp
  raw_os_ostream.cpp
  raw_ostream.cpp
  raw_socket_stream.cpp
  regcomp.c
  regerror.c
  regexec.c
  regfree.c
  regstrlcpy.c
  xxhash.cpp
  Z3Solver.cpp

  ${ALLOCATOR_FILES}
  $<TARGET_OBJECTS:LLVMSupportBlake3>

# System
  Atomic.cpp
  DynamicLibrary.cpp
  Errno.cpp
  Memory.cpp
  Path.cpp
  Process.cpp
  Program.cpp
  RWMutex.cpp
  Signals.cpp
  Threading.cpp
  Valgrind.cpp
  Watchdog.cpp

  ADDITIONAL_HEADER_DIRS
  Unix
  Windows
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/ADT
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Support
  ${Backtrace_INCLUDE_DIRS}

  LINK_LIBS
  ${system_libs} ${imported_libs} ${delayload_flags}

  LINK_COMPONENTS
  Demangle
  )

set(llvm_system_libs ${system_libs})

# This block is only needed for llvm-config. When we deprecate llvm-config and
# move to using CMake export, this block can be removed.
if(LLVM_ENABLE_ZLIB)
  # CMAKE_BUILD_TYPE is only meaningful to single-configuration generators.
  if(CMAKE_BUILD_TYPE)
    string(TOUPPER ${CMAKE_BUILD_TYPE} build_type)
    get_property(zlib_library TARGET ZLIB::ZLIB PROPERTY LOCATION_${build_type})
  endif()
  if(NOT zlib_library)
    get_property(zlib_library TARGET ZLIB::ZLIB PROPERTY LOCATION)
  endif()
  get_library_name(${zlib_library} zlib_library)
  set(llvm_system_libs ${llvm_system_libs} "${zlib_library}")
endif()

if(LLVM_ENABLE_ZSTD)
  # CMAKE_BUILD_TYPE is only meaningful to single-configuration generators.
  if(CMAKE_BUILD_TYPE)
    string(TOUPPER ${CMAKE_BUILD_TYPE} build_type)
    get_property(zstd_library TARGET ${zstd_target} PROPERTY LOCATION_${build_type})
  endif()
  if(NOT zstd_library)
    get_property(zstd_library TARGET ${zstd_target} PROPERTY LOCATION)
  endif()
  if (zstd_target STREQUAL zstd::libzstd_shared)
    get_library_name(${zstd_library} zstd_library)
    set(llvm_system_libs ${llvm_system_libs} "${zstd_library}")
  else()
    set(llvm_system_libs ${llvm_system_libs} "${zstd_STATIC_LIBRARY}")
  endif()
endif()

set_property(TARGET LLVMSupport PROPERTY LLVM_SYSTEM_LIBS "${llvm_system_libs}")


if(LLVM_INTEGRATED_CRT_ALLOC)
  if(LLVM_INTEGRATED_CRT_ALLOC MATCHES "snmalloc$")
    set_property(TARGET LLVMSupport PROPERTY CXX_STANDARD 17)
    add_compile_definitions(_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING)
    if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang" AND
        "${CMAKE_SYSTEM_PROCESSOR}" MATCHES "x86_64")
      set_property(TARGET LLVMSupport PROPERTY COMPILE_FLAGS "-mcx16")
    endif()
  endif()
endif()

if(LLVM_WITH_Z3)
  target_include_directories(LLVMSupport SYSTEM
    PRIVATE
    ${Z3_INCLUDE_DIR}
    )
endif()
