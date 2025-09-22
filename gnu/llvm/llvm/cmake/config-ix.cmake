if( WIN32 AND NOT CYGWIN )
  # We consider Cygwin as another Unix
  set(PURE_WINDOWS 1)
endif()

include(CheckIncludeFile)
include(CheckLibraryExists)
include(CheckSymbolExists)
include(CheckCXXSymbolExists)
include(CheckFunctionExists)
include(CheckStructHasMember)
include(CheckCCompilerFlag)
include(CheckCSourceCompiles)
include(CMakePushCheckState)

include(CheckCompilerVersion)
include(CheckProblematicConfigurations)
include(HandleLLVMStdlib)

if( UNIX AND NOT (APPLE OR BEOS OR HAIKU) )
  # Used by check_symbol_exists:
  list(APPEND CMAKE_REQUIRED_LIBRARIES "m")
endif()
# x86_64 FreeBSD 9.2 requires libcxxrt to be specified explicitly.
if( CMAKE_SYSTEM MATCHES "FreeBSD-9.2-RELEASE" AND
    CMAKE_SIZEOF_VOID_P EQUAL 8 )
  list(APPEND CMAKE_REQUIRED_LIBRARIES "cxxrt")
endif()

# Do checks with _XOPEN_SOURCE and large-file API on AIX, because we will build
# with those too.
if (UNIX AND ${CMAKE_SYSTEM_NAME} MATCHES "AIX")
          list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D_XOPEN_SOURCE=700")
          list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D_LARGE_FILE_API")
endif()

# Do checks with _FILE_OFFSET_BITS=64 on Solaris, because we will build
# with those too.
if (UNIX AND ${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
          list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D_FILE_OFFSET_BITS=64")
endif()

# include checks
check_include_file(dlfcn.h HAVE_DLFCN_H)
check_include_file(errno.h HAVE_ERRNO_H)
check_include_file(fcntl.h HAVE_FCNTL_H)
check_include_file(link.h HAVE_LINK_H)
check_include_file(malloc/malloc.h HAVE_MALLOC_MALLOC_H)
if( NOT PURE_WINDOWS )
  check_include_file(pthread.h HAVE_PTHREAD_H)
endif()
check_include_file(signal.h HAVE_SIGNAL_H)
check_include_file(sys/ioctl.h HAVE_SYS_IOCTL_H)
check_include_file(sys/mman.h HAVE_SYS_MMAN_H)
check_include_file(sys/param.h HAVE_SYS_PARAM_H)
check_include_file(sys/resource.h HAVE_SYS_RESOURCE_H)
check_include_file(sys/stat.h HAVE_SYS_STAT_H)
check_include_file(sys/time.h HAVE_SYS_TIME_H)
check_include_file(sys/types.h HAVE_SYS_TYPES_H)
check_include_file(sysexits.h HAVE_SYSEXITS_H)
check_include_file(termios.h HAVE_TERMIOS_H)
check_include_file(unistd.h HAVE_UNISTD_H)
check_include_file(valgrind/valgrind.h HAVE_VALGRIND_VALGRIND_H)
check_include_file(fenv.h HAVE_FENV_H)
check_symbol_exists(FE_ALL_EXCEPT "fenv.h" HAVE_DECL_FE_ALL_EXCEPT)
check_symbol_exists(FE_INEXACT "fenv.h" HAVE_DECL_FE_INEXACT)
check_c_source_compiles("
        #if __has_attribute(used)
        #define LLVM_ATTRIBUTE_USED __attribute__((__used__))
        #else
        #define LLVM_ATTRIBUTE_USED
        #endif
        LLVM_ATTRIBUTE_USED void *foo() {
          return __builtin_thread_pointer();
        }
        int main(void) { return 0; }"
        HAVE_BUILTIN_THREAD_POINTER)

check_include_file(mach/mach.h HAVE_MACH_MACH_H)
check_include_file(CrashReporterClient.h HAVE_CRASHREPORTERCLIENT_H)
if(APPLE)
  check_c_source_compiles("
     static const char *__crashreporter_info__ = 0;
     asm(\".desc ___crashreporter_info__, 0x10\");
     int main(void) { return 0; }"
    HAVE_CRASHREPORTER_INFO)
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  check_include_file(linux/magic.h HAVE_LINUX_MAGIC_H)
  if(NOT HAVE_LINUX_MAGIC_H)
    # older kernels use split files
    check_include_file(linux/nfs_fs.h HAVE_LINUX_NFS_FS_H)
    check_include_file(linux/smb.h HAVE_LINUX_SMB_H)
  endif()
endif()

# library checks
if( NOT PURE_WINDOWS )
  check_library_exists(pthread pthread_create "" HAVE_LIBPTHREAD)
  if (HAVE_LIBPTHREAD)
    check_library_exists(pthread pthread_rwlock_init "" HAVE_PTHREAD_RWLOCK_INIT)
    check_library_exists(pthread pthread_mutex_lock "" HAVE_PTHREAD_MUTEX_LOCK)
  else()
    # this could be Android
    check_library_exists(c pthread_create "" PTHREAD_IN_LIBC)
    if (PTHREAD_IN_LIBC)
      check_library_exists(c pthread_rwlock_init "" HAVE_PTHREAD_RWLOCK_INIT)
      check_library_exists(c pthread_mutex_lock "" HAVE_PTHREAD_MUTEX_LOCK)
    endif()
  endif()
  check_library_exists(dl dlopen "" HAVE_LIBDL)
  check_library_exists(rt clock_gettime "" HAVE_LIBRT)
endif()

# Check for libpfm.
include(FindLibpfm)

if(HAVE_LIBPTHREAD)
  # We want to find pthreads library and at the moment we do want to
  # have it reported as '-l<lib>' instead of '-pthread'.
  # TODO: switch to -pthread once the rest of the build system can deal with it.
  set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
  set(THREADS_HAVE_PTHREAD_ARG Off)
  find_package(Threads REQUIRED)
  set(LLVM_PTHREAD_LIB ${CMAKE_THREAD_LIBS_INIT})
endif()

if(LLVM_ENABLE_ZLIB)
  if(LLVM_ENABLE_ZLIB STREQUAL FORCE_ON)
    find_package(ZLIB REQUIRED)
  elseif(NOT LLVM_USE_SANITIZER MATCHES "Memory.*")
    find_package(ZLIB)
  endif()
  if(ZLIB_FOUND)
    # Check if zlib we found is usable; for example, we may have found a 32-bit
    # library on a 64-bit system which would result in a link-time failure.
    cmake_push_check_state()
    list(APPEND CMAKE_REQUIRED_INCLUDES ${ZLIB_INCLUDE_DIRS})
    list(APPEND CMAKE_REQUIRED_LIBRARIES ${ZLIB_LIBRARY})
    check_symbol_exists(compress2 zlib.h HAVE_ZLIB)
    cmake_pop_check_state()
    if(LLVM_ENABLE_ZLIB STREQUAL FORCE_ON AND NOT HAVE_ZLIB)
      message(FATAL_ERROR "Failed to configure zlib")
    endif()
  endif()
  set(LLVM_ENABLE_ZLIB "${HAVE_ZLIB}")
else()
  set(LLVM_ENABLE_ZLIB 0)
endif()

set(zstd_FOUND 0)
if(LLVM_ENABLE_ZSTD)
  if(LLVM_ENABLE_ZSTD STREQUAL FORCE_ON)
    find_package(zstd REQUIRED)
    if(NOT zstd_FOUND)
      message(FATAL_ERROR "Failed to configure zstd, but LLVM_ENABLE_ZSTD is FORCE_ON")
    endif()
  elseif(NOT LLVM_USE_SANITIZER MATCHES "Memory.*")
    find_package(zstd QUIET)
  endif()
endif()
set(LLVM_ENABLE_ZSTD ${zstd_FOUND})

if(LLVM_ENABLE_LIBXML2)
  if(LLVM_ENABLE_LIBXML2 STREQUAL FORCE_ON)
    find_package(LibXml2 REQUIRED)
  elseif(NOT LLVM_USE_SANITIZER MATCHES "Memory.*")
    find_package(LibXml2)
  endif()
  if(LibXml2_FOUND)
    # Check if libxml2 we found is usable; for example, we may have found a 32-bit
    # library on a 64-bit system which would result in a link-time failure.
    cmake_push_check_state()
    list(APPEND CMAKE_REQUIRED_INCLUDES ${LIBXML2_INCLUDE_DIRS})
    list(APPEND CMAKE_REQUIRED_LIBRARIES ${LIBXML2_LIBRARIES})
    list(APPEND CMAKE_REQUIRED_DEFINITIONS ${LIBXML2_DEFINITIONS})
    check_symbol_exists(xmlReadMemory libxml/xmlreader.h HAVE_LIBXML2)
    cmake_pop_check_state()
    if(LLVM_ENABLE_LIBXML2 STREQUAL FORCE_ON AND NOT HAVE_LIBXML2)
      message(FATAL_ERROR "Failed to configure libxml2")
    endif()
  endif()
  set(LLVM_ENABLE_LIBXML2 "${HAVE_LIBXML2}")
endif()

if(LLVM_ENABLE_CURL)
  if(LLVM_ENABLE_CURL STREQUAL FORCE_ON)
    find_package(CURL REQUIRED)
  else()
    find_package(CURL)
  endif()
  if(CURL_FOUND)
    # Check if curl we found is usable; for example, we may have found a 32-bit
    # library on a 64-bit system which would result in a link-time failure.
    cmake_push_check_state()
    list(APPEND CMAKE_REQUIRED_LIBRARIES CURL::libcurl)
    check_symbol_exists(curl_easy_init curl/curl.h HAVE_CURL)
    cmake_pop_check_state()
    if(LLVM_ENABLE_CURL STREQUAL FORCE_ON AND NOT HAVE_CURL)
      message(FATAL_ERROR "Failed to configure curl")
    endif()
  endif()
  set(LLVM_ENABLE_CURL "${HAVE_CURL}")
endif()

if(LLVM_ENABLE_HTTPLIB)
  if(LLVM_ENABLE_HTTPLIB STREQUAL FORCE_ON)
    find_package(httplib REQUIRED)
  else()
    find_package(httplib)
  endif()
  if(HTTPLIB_FOUND)
    # Check if the "httplib" we found is usable; for example there may be another
    # library with the same name.
    cmake_push_check_state()
    list(APPEND CMAKE_REQUIRED_LIBRARIES ${HTTPLIB_LIBRARY})
    check_cxx_symbol_exists(CPPHTTPLIB_HTTPLIB_H ${HTTPLIB_HEADER_PATH} HAVE_HTTPLIB)
    cmake_pop_check_state()
    if(LLVM_ENABLE_HTTPLIB STREQUAL FORCE_ON AND NOT HAVE_HTTPLIB)
      message(FATAL_ERROR "Failed to configure cpp-httplib")
    endif()
  endif()
  set(LLVM_ENABLE_HTTPLIB "${HAVE_HTTPLIB}")
endif()

# Don't look for these libraries if we're using MSan, since uninstrumented third
# party code may call MSan interceptors like strlen, leading to false positives.
if(NOT LLVM_USE_SANITIZER MATCHES "Memory.*")
  # Don't look for these libraries on Windows.
  if (NOT PURE_WINDOWS)
    # Skip libedit if using ASan as it contains memory leaks.
    if (LLVM_ENABLE_LIBEDIT AND NOT LLVM_USE_SANITIZER MATCHES ".*Address.*")
      if(LLVM_ENABLE_LIBEDIT STREQUAL FORCE_ON)
        find_package(LibEdit REQUIRED)
      else()
        find_package(LibEdit)
      endif()
      set(HAVE_LIBEDIT "${LibEdit_FOUND}")
    else()
      set(HAVE_LIBEDIT 0)
    endif()
  else()
    set(HAVE_LIBEDIT 0)
  endif()
else()
  set(HAVE_LIBEDIT 0)
endif()

if(LLVM_HAS_LOGF128)
  include(CheckCXXSymbolExists)
  check_cxx_symbol_exists(logf128 math.h HAS_LOGF128)

  if(LLVM_HAS_LOGF128 STREQUAL FORCE_ON AND NOT HAS_LOGF128)
    message(FATAL_ERROR "Failed to configure logf128")
  endif()

  set(LLVM_HAS_LOGF128 "${HAS_LOGF128}")
endif()

# function checks
check_symbol_exists(arc4random "stdlib.h" HAVE_DECL_ARC4RANDOM)
find_package(Backtrace)
set(HAVE_BACKTRACE ${Backtrace_FOUND})
set(BACKTRACE_HEADER ${Backtrace_HEADER})

# Prevent check_symbol_exists from using API that is not supported for a given
# deployment target.
check_c_compiler_flag("-Werror=unguarded-availability-new" "C_SUPPORTS_WERROR_UNGUARDED_AVAILABILITY_NEW")
if(C_SUPPORTS_WERROR_UNGUARDED_AVAILABILITY_NEW)
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Werror=unguarded-availability-new")
endif()

# Determine whether we can register EH tables.
check_symbol_exists(__register_frame "${CMAKE_CURRENT_LIST_DIR}/unwind.h" HAVE_REGISTER_FRAME)
check_symbol_exists(__deregister_frame "${CMAKE_CURRENT_LIST_DIR}/unwind.h" HAVE_DEREGISTER_FRAME)
check_symbol_exists(__unw_add_dynamic_fde "${CMAKE_CURRENT_LIST_DIR}/unwind.h" HAVE_UNW_ADD_DYNAMIC_FDE)

check_symbol_exists(_Unwind_Backtrace "unwind.h" HAVE__UNWIND_BACKTRACE)
check_symbol_exists(getpagesize unistd.h HAVE_GETPAGESIZE)
check_symbol_exists(sysconf unistd.h HAVE_SYSCONF)
check_symbol_exists(getrusage sys/resource.h HAVE_GETRUSAGE)
check_symbol_exists(setrlimit sys/resource.h HAVE_SETRLIMIT)
check_symbol_exists(isatty unistd.h HAVE_ISATTY)
check_symbol_exists(futimens sys/stat.h HAVE_FUTIMENS)
check_symbol_exists(futimes sys/time.h HAVE_FUTIMES)
# AddressSanitizer conflicts with lib/Support/Unix/Signals.inc
# Avoid sigaltstack on Apple platforms, where backtrace() cannot handle it
# (rdar://7089625) and _Unwind_Backtrace is unusable because it cannot unwind
# past the signal handler after an assertion failure (rdar://29866587).
if( HAVE_SIGNAL_H AND NOT LLVM_USE_SANITIZER MATCHES ".*Address.*" AND NOT APPLE )
  check_symbol_exists(sigaltstack signal.h HAVE_SIGALTSTACK)
endif()
check_symbol_exists(mallctl malloc_np.h HAVE_MALLCTL)
check_symbol_exists(mallinfo malloc.h HAVE_MALLINFO)
check_symbol_exists(mallinfo2 malloc.h HAVE_MALLINFO2)
check_symbol_exists(malloc_zone_statistics malloc/malloc.h
                    HAVE_MALLOC_ZONE_STATISTICS)
check_symbol_exists(getrlimit "sys/types.h;sys/time.h;sys/resource.h" HAVE_GETRLIMIT)
check_symbol_exists(posix_spawn spawn.h HAVE_POSIX_SPAWN)
check_symbol_exists(pread unistd.h HAVE_PREAD)
check_symbol_exists(sbrk unistd.h HAVE_SBRK)
check_symbol_exists(strerror_r string.h HAVE_STRERROR_R)
check_symbol_exists(strerror_s string.h HAVE_DECL_STRERROR_S)
check_symbol_exists(setenv stdlib.h HAVE_SETENV)
if( PURE_WINDOWS )
  check_symbol_exists(_chsize_s io.h HAVE__CHSIZE_S)

  check_function_exists(_alloca HAVE__ALLOCA)
  check_function_exists(__alloca HAVE___ALLOCA)
  check_function_exists(__chkstk HAVE___CHKSTK)
  check_function_exists(__chkstk_ms HAVE___CHKSTK_MS)
  check_function_exists(___chkstk HAVE____CHKSTK)
  check_function_exists(___chkstk_ms HAVE____CHKSTK_MS)

  check_function_exists(__ashldi3 HAVE___ASHLDI3)
  check_function_exists(__ashrdi3 HAVE___ASHRDI3)
  check_function_exists(__divdi3 HAVE___DIVDI3)
  check_function_exists(__fixdfdi HAVE___FIXDFDI)
  check_function_exists(__fixsfdi HAVE___FIXSFDI)
  check_function_exists(__floatdidf HAVE___FLOATDIDF)
  check_function_exists(__lshrdi3 HAVE___LSHRDI3)
  check_function_exists(__moddi3 HAVE___MODDI3)
  check_function_exists(__udivdi3 HAVE___UDIVDI3)
  check_function_exists(__umoddi3 HAVE___UMODDI3)

  check_function_exists(__main HAVE___MAIN)
  check_function_exists(__cmpdi2 HAVE___CMPDI2)
endif()

CHECK_STRUCT_HAS_MEMBER("struct stat" st_mtimespec.tv_nsec
    "sys/types.h;sys/stat.h" HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC)
if (UNIX AND ${CMAKE_SYSTEM_NAME} MATCHES "AIX")
# The st_mtim.tv_nsec member of a `stat` structure is not reliable on some AIX
# environments.
  set(HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC 0)
else()
  CHECK_STRUCT_HAS_MEMBER("struct stat" st_mtim.tv_nsec
      "sys/types.h;sys/stat.h" HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC)
endif()

check_symbol_exists(__GLIBC__ stdio.h LLVM_USING_GLIBC)
if( LLVM_USING_GLIBC )
  add_compile_definitions(_GNU_SOURCE)
  list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D_GNU_SOURCE")
# enable 64bit off_t on 32bit systems using glibc
  if (CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_compile_definitions(_FILE_OFFSET_BITS=64)
    list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D_FILE_OFFSET_BITS=64")
  endif()
endif()

# This check requires _GNU_SOURCE.
if (NOT PURE_WINDOWS)
  if (LLVM_PTHREAD_LIB)
    list(APPEND CMAKE_REQUIRED_LIBRARIES ${LLVM_PTHREAD_LIB})
  endif()
  check_symbol_exists(pthread_getname_np pthread.h HAVE_PTHREAD_GETNAME_NP)
  check_symbol_exists(pthread_setname_np pthread.h HAVE_PTHREAD_SETNAME_NP)
  if (LLVM_PTHREAD_LIB)
    list(REMOVE_ITEM CMAKE_REQUIRED_LIBRARIES ${LLVM_PTHREAD_LIB})
  endif()
endif()

# This check requires _GNU_SOURCE.
if( HAVE_DLFCN_H )
  if( HAVE_LIBDL )
    list(APPEND CMAKE_REQUIRED_LIBRARIES dl)
  endif()
  check_symbol_exists(dlopen dlfcn.h HAVE_DLOPEN)
  check_symbol_exists(dladdr dlfcn.h HAVE_DLADDR)
  if( HAVE_LIBDL )
    list(REMOVE_ITEM CMAKE_REQUIRED_LIBRARIES dl)
  endif()
endif()

# available programs checks
function(llvm_find_program name)
  string(TOUPPER ${name} NAME)
  string(REGEX REPLACE "\\." "_" NAME ${NAME})

  find_program(LLVM_PATH_${NAME} NAMES ${ARGV})
  mark_as_advanced(LLVM_PATH_${NAME})
  if(LLVM_PATH_${NAME})
    set(HAVE_${NAME} 1 CACHE INTERNAL "Is ${name} available ?")
    mark_as_advanced(HAVE_${NAME})
  else(LLVM_PATH_${NAME})
    set(HAVE_${NAME} "" CACHE INTERNAL "Is ${name} available ?")
  endif(LLVM_PATH_${NAME})
endfunction()

if (LLVM_ENABLE_DOXYGEN)
  llvm_find_program(dot)
endif ()

if(LLVM_ENABLE_FFI)
  set(FFI_REQUIRE_INCLUDE On)
  if(LLVM_ENABLE_FFI STREQUAL FORCE_ON)
    find_package(FFI REQUIRED)
  else()
    find_package(FFI)
  endif()
  set(LLVM_ENABLE_FFI "${FFI_FOUND}")
else()
  unset(HAVE_FFI_FFI_H CACHE)
  unset(HAVE_FFI_H CACHE)
  unset(HAVE_FFI_CALL CACHE)
endif()

check_symbol_exists(proc_pid_rusage "libproc.h" HAVE_PROC_PID_RUSAGE)

# Define LLVM_HAS_ATOMICS if gcc or MSVC atomic builtins are supported.
include(CheckAtomic)

if( LLVM_ENABLE_PIC )
  set(ENABLE_PIC 1)
else()
  set(ENABLE_PIC 0)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fno-pie")
endif()

set(SUPPORTS_VARIADIC_MACROS_FLAG 0)
if (LLVM_COMPILER_IS_GCC_COMPATIBLE)
  set(SUPPORTS_VARIADIC_MACROS_FLAG 1)
endif()
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(SUPPORTS_GNU_ZERO_VARIADIC_MACRO_ARGUMENTS_FLAG 1)
else()
  set(SUPPORTS_GNU_ZERO_VARIADIC_MACRO_ARGUMENTS_FLAG 0)
endif()

set(USE_NO_MAYBE_UNINITIALIZED 0)
set(USE_NO_UNINITIALIZED 0)

# Disable gcc's potentially uninitialized use analysis as it presents lots of
# false positives.
if (CMAKE_COMPILER_IS_GNUCXX)
  # Disable all -Wuninitialized warning for old GCC versions.
  if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 12.0)
    set(USE_NO_UNINITIALIZED 1)
  else()
    set(USE_NO_MAYBE_UNINITIALIZED 1)
  endif()
endif()

if(LLVM_INCLUDE_TESTS)
  include(GetErrcMessages)
  get_errc_messages(LLVM_LIT_ERRC_MESSAGES)
endif()

# By default, we target the host, but this can be overridden at CMake
# invocation time.
include(GetHostTriple)
get_host_triple(LLVM_INFERRED_HOST_TRIPLE)

set(LLVM_HOST_TRIPLE "${LLVM_INFERRED_HOST_TRIPLE}" CACHE STRING
    "Host on which LLVM binaries will run")
message(STATUS "LLVM host triple: ${LLVM_HOST_TRIPLE}")

# Determine the native architecture.
string(TOLOWER "${LLVM_TARGET_ARCH}" LLVM_NATIVE_ARCH)
if( LLVM_NATIVE_ARCH STREQUAL "host" )
  string(REGEX MATCH "^[^-]*" LLVM_NATIVE_ARCH ${LLVM_HOST_TRIPLE})
endif ()

if (LLVM_NATIVE_ARCH MATCHES "i[2-6]86")
  set(LLVM_NATIVE_ARCH X86)
elseif (LLVM_NATIVE_ARCH STREQUAL "x86")
  set(LLVM_NATIVE_ARCH X86)
elseif (LLVM_NATIVE_ARCH STREQUAL "amd64")
  set(LLVM_NATIVE_ARCH X86)
elseif (LLVM_NATIVE_ARCH STREQUAL "x86_64")
  set(LLVM_NATIVE_ARCH X86)
elseif (LLVM_NATIVE_ARCH MATCHES "sparc")
  set(LLVM_NATIVE_ARCH Sparc)
elseif (LLVM_NATIVE_ARCH MATCHES "powerpc")
  set(LLVM_NATIVE_ARCH PowerPC)
elseif (LLVM_NATIVE_ARCH MATCHES "ppc64le")
  set(LLVM_NATIVE_ARCH PowerPC)
elseif (LLVM_NATIVE_ARCH MATCHES "aarch64")
  set(LLVM_NATIVE_ARCH AArch64)
elseif (LLVM_NATIVE_ARCH MATCHES "arm64")
  set(LLVM_NATIVE_ARCH AArch64)
elseif (LLVM_NATIVE_ARCH MATCHES "arm")
  set(LLVM_NATIVE_ARCH ARM)
elseif (LLVM_NATIVE_ARCH MATCHES "avr")
  set(LLVM_NATIVE_ARCH AVR)
elseif (LLVM_NATIVE_ARCH MATCHES "mips")
  set(LLVM_NATIVE_ARCH Mips)
elseif (LLVM_NATIVE_ARCH MATCHES "xcore")
  set(LLVM_NATIVE_ARCH XCore)
elseif (LLVM_NATIVE_ARCH MATCHES "msp430")
  set(LLVM_NATIVE_ARCH MSP430)
elseif (LLVM_NATIVE_ARCH MATCHES "hexagon")
  set(LLVM_NATIVE_ARCH Hexagon)
elseif (LLVM_NATIVE_ARCH MATCHES "s390x")
  set(LLVM_NATIVE_ARCH SystemZ)
elseif (LLVM_NATIVE_ARCH MATCHES "wasm32")
  set(LLVM_NATIVE_ARCH WebAssembly)
elseif (LLVM_NATIVE_ARCH MATCHES "wasm64")
  set(LLVM_NATIVE_ARCH WebAssembly)
elseif (LLVM_NATIVE_ARCH MATCHES "riscv32")
  set(LLVM_NATIVE_ARCH RISCV)
elseif (LLVM_NATIVE_ARCH MATCHES "riscv64")
  set(LLVM_NATIVE_ARCH RISCV)
elseif (LLVM_NATIVE_ARCH STREQUAL "m68k")
  set(LLVM_NATIVE_ARCH M68k)
elseif (LLVM_NATIVE_ARCH MATCHES "loongarch")
  set(LLVM_NATIVE_ARCH LoongArch)
else ()
  message(FATAL_ERROR "Unknown architecture ${LLVM_NATIVE_ARCH}")
endif ()

# If build targets includes "host" or "Native", then replace with native architecture.
foreach (NATIVE_KEYWORD host Native)
  list(FIND LLVM_TARGETS_TO_BUILD ${NATIVE_KEYWORD} idx)
  if( NOT idx LESS 0 )
    list(REMOVE_AT LLVM_TARGETS_TO_BUILD ${idx})
    list(APPEND LLVM_TARGETS_TO_BUILD ${LLVM_NATIVE_ARCH})
    list(REMOVE_DUPLICATES LLVM_TARGETS_TO_BUILD)
  endif()
endforeach()

if (NOT ${LLVM_NATIVE_ARCH} IN_LIST LLVM_TARGETS_TO_BUILD)
  message(STATUS
    "Native target ${LLVM_NATIVE_ARCH} is not selected; lli will not JIT code")
else ()
  message(STATUS "Native target architecture is ${LLVM_NATIVE_ARCH}")
  set(LLVM_NATIVE_TARGET LLVMInitialize${LLVM_NATIVE_ARCH}Target)
  set(LLVM_NATIVE_TARGETINFO LLVMInitialize${LLVM_NATIVE_ARCH}TargetInfo)
  set(LLVM_NATIVE_TARGETMC LLVMInitialize${LLVM_NATIVE_ARCH}TargetMC)
  set(LLVM_NATIVE_ASMPRINTER LLVMInitialize${LLVM_NATIVE_ARCH}AsmPrinter)

  # We don't have an ASM parser for all architectures yet.
  if (EXISTS ${PROJECT_SOURCE_DIR}/lib/Target/${LLVM_NATIVE_ARCH}/AsmParser/CMakeLists.txt)
    set(LLVM_NATIVE_ASMPARSER LLVMInitialize${LLVM_NATIVE_ARCH}AsmParser)
  endif ()

  # We don't have an disassembler for all architectures yet.
  if (EXISTS ${PROJECT_SOURCE_DIR}/lib/Target/${LLVM_NATIVE_ARCH}/Disassembler/CMakeLists.txt)
    set(LLVM_NATIVE_DISASSEMBLER LLVMInitialize${LLVM_NATIVE_ARCH}Disassembler)
  endif ()
endif ()

if( MSVC )
  set(SHLIBEXT ".lib")
  set(stricmp "_stricmp")
  set(strdup "_strdup")

  # Allow setting clang-cl's /winsysroot flag.
  set(LLVM_WINSYSROOT "" CACHE STRING
    "If set, argument to clang-cl's /winsysroot")

  if (LLVM_WINSYSROOT)
    set(MSVC_DIA_SDK_DIR "${LLVM_WINSYSROOT}/DIA SDK" CACHE PATH
        "Path to the DIA SDK")
  else()
    set(MSVC_DIA_SDK_DIR "$ENV{VSINSTALLDIR}DIA SDK" CACHE PATH
        "Path to the DIA SDK")
  endif()

  # See if the DIA SDK is available and usable.
  # Due to a bug in MSVC 2013's installation software, it is possible
  # for MSVC 2013 to write the DIA SDK into the Visual Studio 2012
  # install directory.  If this happens, the installation is corrupt
  # and there's nothing we can do.  It happens with enough frequency
  # though that we should handle it.  We do so by simply checking that
  # the DIA SDK folder exists.  Should this happen you will need to
  # uninstall VS 2012 and then re-install VS 2013.
  if (IS_DIRECTORY "${MSVC_DIA_SDK_DIR}")
    set(HAVE_DIA_SDK 1)
  else()
    set(HAVE_DIA_SDK 0)
  endif()

  option(LLVM_ENABLE_DIA_SDK "Use MSVC DIA SDK for debugging if available."
                             ${HAVE_DIA_SDK})

  if(LLVM_ENABLE_DIA_SDK AND NOT HAVE_DIA_SDK)
    message(FATAL_ERROR "DIA SDK not found. If you have both VS 2012 and 2013 installed, you may need to uninstall the former and re-install the latter afterwards.")
  endif()
else()
  set(LLVM_ENABLE_DIA_SDK 0)
endif( MSVC )

if( LLVM_ENABLE_THREADS )
  # Check if threading primitives aren't supported on this platform
  if( NOT HAVE_PTHREAD_H AND NOT WIN32 )
    set(LLVM_ENABLE_THREADS 0)
  endif()
endif()

if( LLVM_ENABLE_THREADS )
  message(STATUS "Threads enabled.")
else( LLVM_ENABLE_THREADS )
  message(STATUS "Threads disabled.")
endif()

if (LLVM_ENABLE_DOXYGEN)
  message(STATUS "Doxygen enabled.")
  find_package(Doxygen REQUIRED)

  if (DOXYGEN_FOUND)
    # If we find doxygen and we want to enable doxygen by default create a
    # global aggregate doxygen target for generating llvm and any/all
    # subprojects doxygen documentation.
    if (LLVM_BUILD_DOCS)
      add_custom_target(doxygen ALL)
    endif()

    option(LLVM_DOXYGEN_EXTERNAL_SEARCH "Enable doxygen external search." OFF)
    if (LLVM_DOXYGEN_EXTERNAL_SEARCH)
      set(LLVM_DOXYGEN_SEARCHENGINE_URL "" CACHE STRING "URL to use for external search.")
      set(LLVM_DOXYGEN_SEARCH_MAPPINGS "" CACHE STRING "Doxygen Search Mappings")
    endif()
  endif()
else()
  message(STATUS "Doxygen disabled.")
endif()

find_program(GOLD_EXECUTABLE NAMES ${LLVM_DEFAULT_TARGET_TRIPLE}-ld.gold ld.gold ${LLVM_DEFAULT_TARGET_TRIPLE}-ld ld DOC "The gold linker")
set(LLVM_BINUTILS_INCDIR "" CACHE PATH
    "PATH to binutils/include containing plugin-api.h for gold plugin.")

if(CMAKE_GENERATOR MATCHES "Ninja")
  execute_process(COMMAND ${CMAKE_MAKE_PROGRAM} --version
    OUTPUT_VARIABLE NINJA_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(NINJA_VERSION ${NINJA_VERSION} CACHE STRING "Ninja version number" FORCE)
  message(STATUS "Ninja version: ${NINJA_VERSION}")
endif()

if(CMAKE_GENERATOR MATCHES "Ninja" AND
    NOT "${NINJA_VERSION}" VERSION_LESS "1.9.0" AND
    CMAKE_HOST_APPLE AND CMAKE_HOST_SYSTEM_VERSION VERSION_GREATER "15.6.0")
  set(LLVM_TOUCH_STATIC_LIBRARIES ON)
endif()

if(CMAKE_HOST_APPLE AND APPLE)
  if(NOT LD64_EXECUTABLE)
    if(NOT CMAKE_XCRUN)
      find_program(CMAKE_XCRUN NAMES xcrun)
    endif()

    # First, check if there's ld-classic, which is ld64 in newer SDKs.
    if(CMAKE_XCRUN)
      execute_process(COMMAND ${CMAKE_XCRUN} -find ld-classic
        OUTPUT_VARIABLE LD64_EXECUTABLE
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    else()
      find_program(LD64_EXECUTABLE NAMES ld-classic DOC "The ld64 linker")
    endif()

    # Otherwise look for ld directly.
    if(NOT LD64_EXECUTABLE)
        if(CMAKE_XCRUN)
          execute_process(COMMAND ${CMAKE_XCRUN} -find ld
            OUTPUT_VARIABLE LD64_EXECUTABLE
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        else()
          find_program(LD64_EXECUTABLE NAMES ld DOC "The ld64 linker")
        endif()
    endif()
  endif()

  if(LD64_EXECUTABLE)
    set(LD64_EXECUTABLE ${LD64_EXECUTABLE} CACHE PATH "ld64 executable")
    message(STATUS "Found ld64 - ${LD64_EXECUTABLE}")
  endif()
endif()

# Keep the version requirements in sync with bindings/ocaml/README.txt.
set(LLVM_BINDINGS "")
include(FindOCaml)
include(AddOCaml)
if(WIN32 OR NOT LLVM_ENABLE_BINDINGS)
  message(STATUS "OCaml bindings disabled.")
else()
  find_package(OCaml)
  if( NOT OCAML_FOUND )
    message(STATUS "OCaml bindings disabled.")
  else()
    if( OCAML_VERSION VERSION_LESS "4.00.0" )
      message(STATUS "OCaml bindings disabled, need OCaml >=4.00.0.")
    else()
      find_ocamlfind_package(ctypes VERSION 0.4 OPTIONAL)
      if( HAVE_OCAML_CTYPES )
        message(STATUS "OCaml bindings enabled.")
        set(LLVM_BINDINGS "${LLVM_BINDINGS} ocaml")

        set(LLVM_OCAML_INSTALL_PATH "${OCAML_STDLIB_PATH}" CACHE STRING
            "Install directory for LLVM OCaml packages")
      else()
        message(STATUS "OCaml bindings disabled, need ctypes >=0.4.")
      endif()
    endif()
  endif()
endif()

string(REPLACE " " ";" LLVM_BINDINGS_LIST "${LLVM_BINDINGS}")

function(find_python_module module)
  string(REPLACE "." "_" module_name ${module})
  string(TOUPPER ${module_name} module_upper)
  set(FOUND_VAR PY_${module_upper}_FOUND)
  if (DEFINED ${FOUND_VAR})
    return()
  endif()

  execute_process(COMMAND "${Python3_EXECUTABLE}" "-c" "import ${module}"
    RESULT_VARIABLE status
    ERROR_QUIET)

  if(status)
    set(${FOUND_VAR} OFF CACHE BOOL "Failed to find python module '${module}'")
    message(STATUS "Could NOT find Python module ${module}")
  else()
  set(${FOUND_VAR} ON CACHE BOOL "Found python module '${module}'")
    message(STATUS "Found Python module ${module}")
  endif()
endfunction()

set (PYTHON_MODULES
  pygments
  # Some systems still don't have pygments.lexers.c_cpp which was introduced in
  # version 2.0 in 2014...
  pygments.lexers.c_cpp
  yaml
  )
foreach(module ${PYTHON_MODULES})
  find_python_module(${module})
endforeach()

if(PY_PYGMENTS_FOUND AND PY_PYGMENTS_LEXERS_C_CPP_FOUND AND PY_YAML_FOUND)
  set (LLVM_HAVE_OPT_VIEWER_MODULES 1)
else()
  set (LLVM_HAVE_OPT_VIEWER_MODULES 0)
endif()

function(llvm_get_host_prefixes_and_suffixes)
  # Not all platform files will set these variables (relying on them being
  # implicitly empty if they're unset), so unset the variables before including
  # the platform file, to prevent any values from the target system leaking.
  unset(CMAKE_STATIC_LIBRARY_PREFIX)
  unset(CMAKE_STATIC_LIBRARY_SUFFIX)
  unset(CMAKE_SHARED_LIBRARY_PREFIX)
  unset(CMAKE_SHARED_LIBRARY_SUFFIX)
  unset(CMAKE_IMPORT_LIBRARY_PREFIX)
  unset(CMAKE_IMPORT_LIBRARY_SUFFIX)
  unset(CMAKE_EXECUTABLE_SUFFIX)
  unset(CMAKE_LINK_LIBRARY_SUFFIX)
  include(Platform/${CMAKE_HOST_SYSTEM_NAME} OPTIONAL RESULT_VARIABLE _includedFile)
  if (_includedFile)
    set(LLVM_HOST_STATIC_LIBRARY_PREFIX ${CMAKE_STATIC_LIBRARY_PREFIX} PARENT_SCOPE)
    set(LLVM_HOST_STATIC_LIBRARY_SUFFIX ${CMAKE_STATIC_LIBRARY_SUFFIX} PARENT_SCOPE)
    set(LLVM_HOST_SHARED_LIBRARY_PREFIX ${CMAKE_SHARED_LIBRARY_PREFIX} PARENT_SCOPE)
    set(LLVM_HOST_SHARED_LIBRARY_SUFFIX ${CMAKE_SHARED_LIBRARY_SUFFIX} PARENT_SCOPE)
    set(LLVM_HOST_IMPORT_LIBRARY_PREFIX ${CMAKE_IMPORT_LIBRARY_PREFIX} PARENT_SCOPE)
    set(LLVM_HOST_IMPORT_LIBRARY_SUFFIX ${CMAKE_IMPORT_LIBRARY_SUFFIX} PARENT_SCOPE)
    set(LLVM_HOST_EXECUTABLE_SUFFIX ${CMAKE_EXECUTABLE_SUFFIX} PARENT_SCOPE)
    set(LLVM_HOST_LINK_LIBRARY_SUFFIX ${CMAKE_LINK_LIBRARY_SUFFIX} PARENT_SCOPE)
  endif()
endfunction()

llvm_get_host_prefixes_and_suffixes()
