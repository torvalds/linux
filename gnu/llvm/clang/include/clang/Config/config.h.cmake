/* This generated file is for internal use. Do not include it from headers. */

#ifdef CLANG_CONFIG_H
#error config.h can only be included once
#else
#define CLANG_CONFIG_H

/* Bug report URL. */
#define BUG_REPORT_URL "${BUG_REPORT_URL}"

/* Default to -fPIE and -pie on Linux. */
#cmakedefine01 CLANG_DEFAULT_PIE_ON_LINUX

/* Default linker to use. */
#define CLANG_DEFAULT_LINKER "${CLANG_DEFAULT_LINKER}"

/* Default C++ stdlib to use. */
#define CLANG_DEFAULT_CXX_STDLIB "${CLANG_DEFAULT_CXX_STDLIB}"

/* Default runtime library to use. */
#define CLANG_DEFAULT_RTLIB "${CLANG_DEFAULT_RTLIB}"

/* Default unwind library to use. */
#define CLANG_DEFAULT_UNWINDLIB "${CLANG_DEFAULT_UNWINDLIB}"

/* Default objcopy to use */
#define CLANG_DEFAULT_OBJCOPY "${CLANG_DEFAULT_OBJCOPY}"

/* Default OpenMP runtime used by -fopenmp. */
#define CLANG_DEFAULT_OPENMP_RUNTIME "${CLANG_DEFAULT_OPENMP_RUNTIME}"

/* Default architecture for SystemZ. */
#define CLANG_SYSTEMZ_DEFAULT_ARCH "${CLANG_SYSTEMZ_DEFAULT_ARCH}"

/* Multilib basename for libdir. */
#define CLANG_INSTALL_LIBDIR_BASENAME "${CLANG_INSTALL_LIBDIR_BASENAME}"

/* Relative directory for resource files */
#define CLANG_RESOURCE_DIR "${CLANG_RESOURCE_DIR}"

/* Directories clang will search for headers */
#define C_INCLUDE_DIRS "${C_INCLUDE_DIRS}"

/* Directories clang will search for configuration files */
#cmakedefine CLANG_CONFIG_FILE_SYSTEM_DIR "${CLANG_CONFIG_FILE_SYSTEM_DIR}"
#cmakedefine CLANG_CONFIG_FILE_USER_DIR "${CLANG_CONFIG_FILE_USER_DIR}"

/* Default <path> to all compiler invocations for --sysroot=<path>. */
#define DEFAULT_SYSROOT "${DEFAULT_SYSROOT}"

/* Directory where gcc is installed. */
#define GCC_INSTALL_PREFIX "${GCC_INSTALL_PREFIX}"

/* Define if we have libxml2 */
#cmakedefine CLANG_HAVE_LIBXML ${CLANG_HAVE_LIBXML}

/* Define if we have sys/resource.h (rlimits) */
#cmakedefine CLANG_HAVE_RLIMITS ${CLANG_HAVE_RLIMITS}

/* Define if we have dlfcn.h */
#cmakedefine CLANG_HAVE_DLFCN_H ${CLANG_HAVE_DLFCN_H}

/* Define if dladdr() is available on this platform. */
#cmakedefine CLANG_HAVE_DLADDR ${CLANG_HAVE_DLADDR}

/* Linker version detected at compile time. */
#cmakedefine HOST_LINK_VERSION "${HOST_LINK_VERSION}"

/* pass --build-id to ld */
#cmakedefine ENABLE_LINKER_BUILD_ID

/* enable x86 relax relocations by default */
#cmakedefine01 ENABLE_X86_RELAX_RELOCATIONS

/* Enable IEEE binary128 as default long double format on PowerPC Linux. */
#cmakedefine01 PPC_LINUX_DEFAULT_IEEELONGDOUBLE

/* Enable each functionality of modules */
#cmakedefine01 CLANG_ENABLE_ARCMT
#cmakedefine01 CLANG_ENABLE_OBJC_REWRITER
#cmakedefine01 CLANG_ENABLE_STATIC_ANALYZER

/* Spawn a new process clang.exe for the CC1 tool invocation, when necessary */
#cmakedefine01 CLANG_SPAWN_CC1

/* Whether CIR is built into Clang */
#cmakedefine01 CLANG_ENABLE_CIR

#endif
