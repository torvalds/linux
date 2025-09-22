#ifndef CONFIG_H
#define CONFIG_H

// Include this header only under the llvm source tree.
// This is a private header.

/* Exported configuration */
#include "llvm/Config/llvm-config.h"

/* Bug report URL. */
#define BUG_REPORT_URL "${BUG_REPORT_URL}"

/* Define to 1 to enable backtraces, and to 0 otherwise. */
#cmakedefine01 ENABLE_BACKTRACES

/* Define to 1 to enable crash overrides, and to 0 otherwise. */
#cmakedefine01 ENABLE_CRASH_OVERRIDES

/* Define to 1 to enable crash memory dumps, and to 0 otherwise. */
#cmakedefine01 LLVM_ENABLE_CRASH_DUMPS

/* Define to 1 to prefer forward slashes on Windows, and to 0 prefer
   backslashes. */
#cmakedefine01 LLVM_WINDOWS_PREFER_FORWARD_SLASH

/* Define to 1 if you have the `backtrace' function. */
#cmakedefine HAVE_BACKTRACE ${HAVE_BACKTRACE}

#define BACKTRACE_HEADER <${BACKTRACE_HEADER}>

/* Define to 1 if you have the <CrashReporterClient.h> header file. */
#cmakedefine HAVE_CRASHREPORTERCLIENT_H

/* can use __crashreporter_info__ */
#cmakedefine01 HAVE_CRASHREPORTER_INFO

/* Define to 1 if you have the declaration of `arc4random', and to 0 if you
   don't. */
#cmakedefine01 HAVE_DECL_ARC4RANDOM

/* Define to 1 if you have the declaration of `FE_ALL_EXCEPT', and to 0 if you
   don't. */
#cmakedefine01 HAVE_DECL_FE_ALL_EXCEPT

/* Define to 1 if you have the declaration of `FE_INEXACT', and to 0 if you
   don't. */
#cmakedefine01 HAVE_DECL_FE_INEXACT

/* Define to 1 if you have the declaration of `strerror_s', and to 0 if you
   don't. */
#cmakedefine01 HAVE_DECL_STRERROR_S

/* Define to 1 if you have the <dlfcn.h> header file. */
#cmakedefine HAVE_DLFCN_H ${HAVE_DLFCN_H}

/* Define if dlopen() is available on this platform. */
#cmakedefine HAVE_DLOPEN ${HAVE_DLOPEN}

/* Define if dladdr() is available on this platform. */
#cmakedefine HAVE_DLADDR ${HAVE_DLADDR}

/* Define to 1 if we can register EH frames on this platform. */
#cmakedefine HAVE_REGISTER_FRAME ${HAVE_REGISTER_FRAME}

/* Define to 1 if we can deregister EH frames on this platform. */
#cmakedefine HAVE_DEREGISTER_FRAME ${HAVE_DEREGISTER_FRAME}

/* Define if __unw_add_dynamic_fde() is available on this platform. */
#cmakedefine HAVE_UNW_ADD_DYNAMIC_FDE ${HAVE_UNW_ADD_DYNAMIC_FDE}

/* Define to 1 if you have the <errno.h> header file. */
#cmakedefine HAVE_ERRNO_H ${HAVE_ERRNO_H}

/* Define to 1 if you have the <fcntl.h> header file. */
#cmakedefine HAVE_FCNTL_H ${HAVE_FCNTL_H}

/* Define to 1 if you have the <fenv.h> header file. */
#cmakedefine HAVE_FENV_H ${HAVE_FENV_H}

/* Define if libffi is available on this platform. */
#cmakedefine HAVE_FFI_CALL ${HAVE_FFI_CALL}

/* Define to 1 if you have the <ffi/ffi.h> header file. */
#cmakedefine HAVE_FFI_FFI_H ${HAVE_FFI_FFI_H}

/* Define to 1 if you have the <ffi.h> header file. */
#cmakedefine HAVE_FFI_H ${HAVE_FFI_H}

/* Define to 1 if you have the `futimens' function. */
#cmakedefine HAVE_FUTIMENS ${HAVE_FUTIMENS}

/* Define to 1 if you have the `futimes' function. */
#cmakedefine HAVE_FUTIMES ${HAVE_FUTIMES}

/* Define to 1 if you have the `getpagesize' function. */
#cmakedefine HAVE_GETPAGESIZE ${HAVE_GETPAGESIZE}

/* Define to 1 if you have the `getrlimit' function. */
#cmakedefine HAVE_GETRLIMIT ${HAVE_GETRLIMIT}

/* Define to 1 if you have the `getrusage' function. */
#cmakedefine HAVE_GETRUSAGE ${HAVE_GETRUSAGE}

/* Define to 1 if you have the `isatty' function. */
#cmakedefine HAVE_ISATTY 1

/* Define to 1 if you have the `edit' library (-ledit). */
#cmakedefine HAVE_LIBEDIT ${HAVE_LIBEDIT}

/* Define to 1 if you have the `pfm' library (-lpfm). */
#cmakedefine HAVE_LIBPFM ${HAVE_LIBPFM}

/* Define to 1 if the `perf_branch_entry' struct has field cycles. */
#cmakedefine LIBPFM_HAS_FIELD_CYCLES ${LIBPFM_HAS_FIELD_CYCLES}

/* Define to 1 if you have the `psapi' library (-lpsapi). */
#cmakedefine HAVE_LIBPSAPI ${HAVE_LIBPSAPI}

/* Define to 1 if you have the `pthread' library (-lpthread). */
#cmakedefine HAVE_LIBPTHREAD ${HAVE_LIBPTHREAD}

/* Define to 1 if you have the `pthread_getname_np' function. */
#cmakedefine HAVE_PTHREAD_GETNAME_NP ${HAVE_PTHREAD_GETNAME_NP}

/* Define to 1 if you have the `pthread_setname_np' function. */
#cmakedefine HAVE_PTHREAD_SETNAME_NP ${HAVE_PTHREAD_SETNAME_NP}

/* Define to 1 if you have the <link.h> header file. */
#cmakedefine HAVE_LINK_H ${HAVE_LINK_H}

/* Define to 1 if you have the <mach/mach.h> header file. */
#cmakedefine HAVE_MACH_MACH_H ${HAVE_MACH_MACH_H}

/* Define to 1 if you have the `mallctl' function. */
#cmakedefine HAVE_MALLCTL ${HAVE_MALLCTL}

/* Define to 1 if you have the `mallinfo' function. */
#cmakedefine HAVE_MALLINFO ${HAVE_MALLINFO}

/* Define to 1 if you have the `mallinfo2' function. */
#cmakedefine HAVE_MALLINFO2 ${HAVE_MALLINFO2}

/* Define to 1 if you have the <malloc/malloc.h> header file. */
#cmakedefine HAVE_MALLOC_MALLOC_H ${HAVE_MALLOC_MALLOC_H}

/* Define to 1 if you have the `malloc_zone_statistics' function. */
#cmakedefine HAVE_MALLOC_ZONE_STATISTICS ${HAVE_MALLOC_ZONE_STATISTICS}

/* Define to 1 if you have the `posix_spawn' function. */
#cmakedefine HAVE_POSIX_SPAWN ${HAVE_POSIX_SPAWN}

/* Define to 1 if you have the `pread' function. */
#cmakedefine HAVE_PREAD ${HAVE_PREAD}

/* Define to 1 if you have the <pthread.h> header file. */
#cmakedefine HAVE_PTHREAD_H ${HAVE_PTHREAD_H}

/* Have pthread_mutex_lock */
#cmakedefine HAVE_PTHREAD_MUTEX_LOCK ${HAVE_PTHREAD_MUTEX_LOCK}

/* Have pthread_rwlock_init */
#cmakedefine HAVE_PTHREAD_RWLOCK_INIT ${HAVE_PTHREAD_RWLOCK_INIT}

/* Define to 1 if you have the `sbrk' function. */
#cmakedefine HAVE_SBRK ${HAVE_SBRK}

/* Define to 1 if you have the `setenv' function. */
#cmakedefine HAVE_SETENV ${HAVE_SETENV}

/* Define to 1 if you have the `setrlimit' function. */
#cmakedefine HAVE_SETRLIMIT ${HAVE_SETRLIMIT}

/* Define to 1 if you have the `sigaltstack' function. */
#cmakedefine HAVE_SIGALTSTACK ${HAVE_SIGALTSTACK}

/* Define to 1 if you have the <signal.h> header file. */
#cmakedefine HAVE_SIGNAL_H ${HAVE_SIGNAL_H}

/* Define to 1 if you have the `strerror_r' function. */
#cmakedefine HAVE_STRERROR_R ${HAVE_STRERROR_R}

/* Define to 1 if you have the `sysconf' function. */
#cmakedefine HAVE_SYSCONF ${HAVE_SYSCONF}

/* Define to 1 if you have the <sys/ioctl.h> header file. */
#cmakedefine HAVE_SYS_IOCTL_H ${HAVE_SYS_IOCTL_H}

/* Define to 1 if you have the <sys/mman.h> header file. */
#cmakedefine HAVE_SYS_MMAN_H ${HAVE_SYS_MMAN_H}

/* Define to 1 if you have the <sys/param.h> header file. */
#cmakedefine HAVE_SYS_PARAM_H ${HAVE_SYS_PARAM_H}

/* Define to 1 if you have the <sys/resource.h> header file. */
#cmakedefine HAVE_SYS_RESOURCE_H ${HAVE_SYS_RESOURCE_H}

/* Define to 1 if you have the <sys/stat.h> header file. */
#cmakedefine HAVE_SYS_STAT_H ${HAVE_SYS_STAT_H}

/* Define to 1 if you have the <sys/time.h> header file. */
#cmakedefine HAVE_SYS_TIME_H ${HAVE_SYS_TIME_H}

/* Define to 1 if stat struct has st_mtimespec member .*/
#cmakedefine HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC ${HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC}

/* Define to 1 if stat struct has st_mtim member. */
#cmakedefine HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC ${HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC}

/* Define to 1 if you have the <sys/types.h> header file. */
#cmakedefine HAVE_SYS_TYPES_H ${HAVE_SYS_TYPES_H}

/* Define to 1 if you have the <termios.h> header file. */
#cmakedefine HAVE_TERMIOS_H ${HAVE_TERMIOS_H}

/* Define to 1 if you have the <unistd.h> header file. */
#cmakedefine HAVE_UNISTD_H ${HAVE_UNISTD_H}

/* Define to 1 if you have the <valgrind/valgrind.h> header file. */
#cmakedefine HAVE_VALGRIND_VALGRIND_H ${HAVE_VALGRIND_VALGRIND_H}

/* Have host's _alloca */
#cmakedefine HAVE__ALLOCA ${HAVE__ALLOCA}

/* Define to 1 if you have the `_chsize_s' function. */
#cmakedefine HAVE__CHSIZE_S ${HAVE__CHSIZE_S}

/* Define to 1 if you have the `_Unwind_Backtrace' function. */
#cmakedefine HAVE__UNWIND_BACKTRACE ${HAVE__UNWIND_BACKTRACE}

/* Have host's __alloca */
#cmakedefine HAVE___ALLOCA ${HAVE___ALLOCA}

/* Have host's __ashldi3 */
#cmakedefine HAVE___ASHLDI3 ${HAVE___ASHLDI3}

/* Have host's __ashrdi3 */
#cmakedefine HAVE___ASHRDI3 ${HAVE___ASHRDI3}

/* Have host's __chkstk */
#cmakedefine HAVE___CHKSTK ${HAVE___CHKSTK}

/* Have host's __chkstk_ms */
#cmakedefine HAVE___CHKSTK_MS ${HAVE___CHKSTK_MS}

/* Have host's __cmpdi2 */
#cmakedefine HAVE___CMPDI2 ${HAVE___CMPDI2}

/* Have host's __divdi3 */
#cmakedefine HAVE___DIVDI3 ${HAVE___DIVDI3}

/* Have host's __fixdfdi */
#cmakedefine HAVE___FIXDFDI ${HAVE___FIXDFDI}

/* Have host's __fixsfdi */
#cmakedefine HAVE___FIXSFDI ${HAVE___FIXSFDI}

/* Have host's __floatdidf */
#cmakedefine HAVE___FLOATDIDF ${HAVE___FLOATDIDF}

/* Have host's __lshrdi3 */
#cmakedefine HAVE___LSHRDI3 ${HAVE___LSHRDI3}

/* Have host's __main */
#cmakedefine HAVE___MAIN ${HAVE___MAIN}

/* Have host's __moddi3 */
#cmakedefine HAVE___MODDI3 ${HAVE___MODDI3}

/* Have host's __udivdi3 */
#cmakedefine HAVE___UDIVDI3 ${HAVE___UDIVDI3}

/* Have host's __umoddi3 */
#cmakedefine HAVE___UMODDI3 ${HAVE___UMODDI3}

/* Have host's ___chkstk */
#cmakedefine HAVE____CHKSTK ${HAVE____CHKSTK}

/* Have host's ___chkstk_ms */
#cmakedefine HAVE____CHKSTK_MS ${HAVE____CHKSTK_MS}

/* Linker version detected at compile time. */
#cmakedefine HOST_LINK_VERSION "${HOST_LINK_VERSION}"

/* Define if overriding target triple is enabled */
#cmakedefine LLVM_TARGET_TRIPLE_ENV "${LLVM_TARGET_TRIPLE_ENV}"

/* Whether tools show host and target info when invoked with --version */
#cmakedefine01 LLVM_VERSION_PRINTER_SHOW_HOST_TARGET_INFO

/* Whether tools show optional build config flags when invoked with --version */
#cmakedefine01 LLVM_VERSION_PRINTER_SHOW_BUILD_CONFIG

/* Define if libxml2 is supported on this platform. */
#cmakedefine LLVM_ENABLE_LIBXML2 ${LLVM_ENABLE_LIBXML2}

/* Define to the extension used for shared libraries, say, ".so". */
#cmakedefine LTDL_SHLIB_EXT "${LTDL_SHLIB_EXT}"

/* Define to the extension used for plugin libraries, say, ".so". */
#cmakedefine LLVM_PLUGIN_EXT "${LLVM_PLUGIN_EXT}"

/* Define to the address where bug reports for this package should be sent. */
#cmakedefine PACKAGE_BUGREPORT "${PACKAGE_BUGREPORT}"

/* Define to the full name of this package. */
#cmakedefine PACKAGE_NAME "${PACKAGE_NAME}"

/* Define to the full name and version of this package. */
#cmakedefine PACKAGE_STRING "${PACKAGE_STRING}"

/* Define to the version of this package. */
#cmakedefine PACKAGE_VERSION "${PACKAGE_VERSION}"

/* Define to the vendor of this package. */
#cmakedefine PACKAGE_VENDOR "${PACKAGE_VENDOR}"

/* Define to a function implementing stricmp */
#cmakedefine stricmp ${stricmp}

/* Define to a function implementing strdup */
#cmakedefine strdup ${strdup}

/* Whether GlobalISel rule coverage is being collected */
#cmakedefine01 LLVM_GISEL_COV_ENABLED

/* Define to the default GlobalISel coverage file prefix */
#cmakedefine LLVM_GISEL_COV_PREFIX "${LLVM_GISEL_COV_PREFIX}"

/* Whether Timers signpost passes in Xcode Instruments */
#cmakedefine01 LLVM_SUPPORT_XCODE_SIGNPOSTS

#cmakedefine HAVE_PROC_PID_RUSAGE 1

#cmakedefine HAVE_BUILTIN_THREAD_POINTER ${HAVE_BUILTIN_THREAD_POINTER}

#endif
