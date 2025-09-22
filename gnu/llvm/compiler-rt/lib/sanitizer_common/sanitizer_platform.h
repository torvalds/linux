//===-- sanitizer_platform.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Common platform macros.
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_PLATFORM_H
#define SANITIZER_PLATFORM_H

#if !defined(__linux__) && !defined(__FreeBSD__) && !defined(__NetBSD__) && \
    !defined(__APPLE__) && !defined(_WIN32) && !defined(__Fuchsia__) &&     \
    !(defined(__sun__) && defined(__svr4__)) && !defined(__OpenBSD__)
#  error "This operating system is not supported"
#endif

// Get __GLIBC__ on a glibc platform. Exclude Android: features.h includes C
// function declarations into a .S file which doesn't compile.
// https://crbug.com/1162741
#if __has_include(<features.h>) && !defined(__ANDROID__)
#  include <features.h>
#endif

#if defined(__linux__)
#  define SANITIZER_LINUX 1
#else
#  define SANITIZER_LINUX 0
#endif

#if defined(__GLIBC__)
#  define SANITIZER_GLIBC 1
#else
#  define SANITIZER_GLIBC 0
#endif

#if defined(__FreeBSD__)
#  define SANITIZER_FREEBSD 1
#else
#  define SANITIZER_FREEBSD 0
#endif

#if defined(__NetBSD__)
#  define SANITIZER_NETBSD 1
#else
#  define SANITIZER_NETBSD 0
#endif

#if defined(__OpenBSD__)
#  define SANITIZER_OPENBSD 1
#else
#  define SANITIZER_OPENBSD 0
#endif

#if defined(__sun__) && defined(__svr4__)
#  define SANITIZER_SOLARIS 1
#else
#  define SANITIZER_SOLARIS 0
#endif

// - SANITIZER_APPLE: all Apple code
//   - TARGET_OS_OSX: macOS
//   - SANITIZER_IOS: devices (iOS and iOS-like)
//     - SANITIZER_WATCHOS
//     - SANITIZER_TVOS
//   - SANITIZER_IOSSIM: simulators (iOS and iOS-like)
//   - SANITIZER_DRIVERKIT
#if defined(__APPLE__)
#  define SANITIZER_APPLE 1
#  include <TargetConditionals.h>
#  if TARGET_OS_OSX
#    define SANITIZER_OSX 1
#  else
#    define SANITIZER_OSX 0
#  endif
#  if TARGET_OS_IPHONE
#    define SANITIZER_IOS 1
#  else
#    define SANITIZER_IOS 0
#  endif
#  if TARGET_OS_WATCH
#    define SANITIZER_WATCHOS 1
#  else
#    define SANITIZER_WATCHOS 0
#  endif
#  if TARGET_OS_TV
#    define SANITIZER_TVOS 1
#  else
#    define SANITIZER_TVOS 0
#  endif
#  if TARGET_OS_SIMULATOR
#    define SANITIZER_IOSSIM 1
#  else
#    define SANITIZER_IOSSIM 0
#  endif
#  if defined(TARGET_OS_DRIVERKIT) && TARGET_OS_DRIVERKIT
#    define SANITIZER_DRIVERKIT 1
#  else
#    define SANITIZER_DRIVERKIT 0
#  endif
#else
#  define SANITIZER_APPLE 0
#  define SANITIZER_OSX 0
#  define SANITIZER_IOS 0
#  define SANITIZER_WATCHOS 0
#  define SANITIZER_TVOS 0
#  define SANITIZER_IOSSIM 0
#  define SANITIZER_DRIVERKIT 0
#endif

#if defined(_WIN32)
#  define SANITIZER_WINDOWS 1
#else
#  define SANITIZER_WINDOWS 0
#endif

#if defined(_WIN64)
#  define SANITIZER_WINDOWS64 1
#else
#  define SANITIZER_WINDOWS64 0
#endif

#if defined(__ANDROID__)
#  define SANITIZER_ANDROID 1
#else
#  define SANITIZER_ANDROID 0
#endif

#if defined(__Fuchsia__)
#  define SANITIZER_FUCHSIA 1
#else
#  define SANITIZER_FUCHSIA 0
#endif

// Assume linux that is not glibc or android is musl libc.
#if SANITIZER_LINUX && !SANITIZER_GLIBC && !SANITIZER_ANDROID
#  define SANITIZER_MUSL 1
#else
#  define SANITIZER_MUSL 0
#endif

#define SANITIZER_POSIX                                     \
  (SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_APPLE || \
   SANITIZER_NETBSD || SANITIZER_OPENBSD || SANITIZER_SOLARIS)

#if __LP64__ || defined(_WIN64)
#  define SANITIZER_WORDSIZE 64
#else
#  define SANITIZER_WORDSIZE 32
#endif

#if SANITIZER_WORDSIZE == 64
#  define FIRST_32_SECOND_64(a, b) (b)
#else
#  define FIRST_32_SECOND_64(a, b) (a)
#endif

#if defined(__x86_64__) && !defined(_LP64)
#  define SANITIZER_X32 1
#else
#  define SANITIZER_X32 0
#endif

#if defined(__x86_64__) || defined(_M_X64)
#  define SANITIZER_X64 1
#else
#  define SANITIZER_X64 0
#endif

#if defined(__i386__) || defined(_M_IX86)
#  define SANITIZER_I386 1
#else
#  define SANITIZER_I386 0
#endif

#if defined(__mips__)
#  define SANITIZER_MIPS 1
#  if defined(__mips64) && _MIPS_SIM == _ABI64
#    define SANITIZER_MIPS32 0
#    define SANITIZER_MIPS64 1
#  else
#    define SANITIZER_MIPS32 1
#    define SANITIZER_MIPS64 0
#  endif
#else
#  define SANITIZER_MIPS 0
#  define SANITIZER_MIPS32 0
#  define SANITIZER_MIPS64 0
#endif

#if defined(__s390__)
#  define SANITIZER_S390 1
#  if defined(__s390x__)
#    define SANITIZER_S390_31 0
#    define SANITIZER_S390_64 1
#  else
#    define SANITIZER_S390_31 1
#    define SANITIZER_S390_64 0
#  endif
#else
#  define SANITIZER_S390 0
#  define SANITIZER_S390_31 0
#  define SANITIZER_S390_64 0
#endif

#if defined(__sparc__)
#  define SANITIZER_SPARC 1
#  if defined(__arch64__)
#    define SANITIZER_SPARC32 0
#    define SANITIZER_SPARC64 1
#  else
#    define SANITIZER_SPARC32 1
#    define SANITIZER_SPARC64 0
#  endif
#else
#  define SANITIZER_SPARC 0
#  define SANITIZER_SPARC32 0
#  define SANITIZER_SPARC64 0
#endif

#if defined(__powerpc__)
#  define SANITIZER_PPC 1
#  if defined(__powerpc64__)
#    define SANITIZER_PPC32 0
#    define SANITIZER_PPC64 1
// 64-bit PPC has two ABIs (v1 and v2).  The old powerpc64 target is
// big-endian, and uses v1 ABI (known for its function descriptors),
// while the new powerpc64le target is little-endian and uses v2.
// In theory, you could convince gcc to compile for their evil twins
// (eg. big-endian v2), but you won't find such combinations in the wild
// (it'd require bootstrapping a whole system, which would be quite painful
// - there's no target triple for that).  LLVM doesn't support them either.
#    if _CALL_ELF == 2
#      define SANITIZER_PPC64V1 0
#      define SANITIZER_PPC64V2 1
#    else
#      define SANITIZER_PPC64V1 1
#      define SANITIZER_PPC64V2 0
#    endif
#  else
#    define SANITIZER_PPC32 1
#    define SANITIZER_PPC64 0
#    define SANITIZER_PPC64V1 0
#    define SANITIZER_PPC64V2 0
#  endif
#else
#  define SANITIZER_PPC 0
#  define SANITIZER_PPC32 0
#  define SANITIZER_PPC64 0
#  define SANITIZER_PPC64V1 0
#  define SANITIZER_PPC64V2 0
#endif

#if defined(__arm__) || defined(_M_ARM)
#  define SANITIZER_ARM 1
#else
#  define SANITIZER_ARM 0
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#  define SANITIZER_ARM64 1
#else
#  define SANITIZER_ARM64 0
#endif

#if SANITIZER_WINDOWS64 && SANITIZER_ARM64
#  define SANITIZER_WINDOWS_ARM64 1
#  define SANITIZER_WINDOWS_x64 0
#elif SANITIZER_WINDOWS64 && !SANITIZER_ARM64
#  define SANITIZER_WINDOWS_ARM64 0
#  define SANITIZER_WINDOWS_x64 1
#else
#  define SANITIZER_WINDOWS_ARM64 0
#  define SANITIZER_WINDOWS_x64 0
#endif

#if SANITIZER_SOLARIS && SANITIZER_WORDSIZE == 32
#  define SANITIZER_SOLARIS32 1
#else
#  define SANITIZER_SOLARIS32 0
#endif

#if defined(__riscv) && (__riscv_xlen == 64)
#  define SANITIZER_RISCV64 1
#else
#  define SANITIZER_RISCV64 0
#endif

#if defined(__loongarch_lp64)
#  define SANITIZER_LOONGARCH64 1
#else
#  define SANITIZER_LOONGARCH64 0
#endif

// By default we allow to use SizeClassAllocator64 on 64-bit platform.
// But in some cases SizeClassAllocator64 does not work well and we need to
// fallback to SizeClassAllocator32.
// For such platforms build this code with -DSANITIZER_CAN_USE_ALLOCATOR64=0 or
// change the definition of SANITIZER_CAN_USE_ALLOCATOR64 here.
#ifndef SANITIZER_CAN_USE_ALLOCATOR64
#  if (SANITIZER_RISCV64 && !SANITIZER_FUCHSIA && !SANITIZER_LINUX) || \
      SANITIZER_IOS || SANITIZER_DRIVERKIT
#    define SANITIZER_CAN_USE_ALLOCATOR64 0
#  elif defined(__mips64) || defined(__hexagon__)
#    define SANITIZER_CAN_USE_ALLOCATOR64 0
#  else
#    define SANITIZER_CAN_USE_ALLOCATOR64 (SANITIZER_WORDSIZE == 64)
#  endif
#endif

// The range of addresses which can be returned my mmap.
// FIXME: this value should be different on different platforms.  Larger values
// will still work but will consume more memory for TwoLevelByteMap.
#if defined(__mips__)
#  if SANITIZER_GO && defined(__mips64)
#    define SANITIZER_MMAP_RANGE_SIZE FIRST_32_SECOND_64(1ULL << 32, 1ULL << 47)
#  else
#    define SANITIZER_MMAP_RANGE_SIZE FIRST_32_SECOND_64(1ULL << 32, 1ULL << 40)
#  endif
#elif SANITIZER_RISCV64
// FIXME: Rather than hardcoding the VMA here, we should rely on
// GetMaxUserVirtualAddress(). This will require some refactoring though since
// many places either hardcode some value or SANITIZER_MMAP_RANGE_SIZE is
// assumed to be some constant integer.
#  if SANITIZER_FUCHSIA
#    define SANITIZER_MMAP_RANGE_SIZE (1ULL << 38)
#  else
#    define SANITIZER_MMAP_RANGE_SIZE FIRST_32_SECOND_64(1ULL << 32, 1ULL << 56)
#  endif
#elif defined(__aarch64__)
#  if SANITIZER_APPLE
#    if SANITIZER_OSX || SANITIZER_IOSSIM
#      define SANITIZER_MMAP_RANGE_SIZE \
        FIRST_32_SECOND_64(1ULL << 32, 1ULL << 47)
#    else
// Darwin iOS/ARM64 has a 36-bit VMA, 64GiB VM
#      define SANITIZER_MMAP_RANGE_SIZE \
        FIRST_32_SECOND_64(1ULL << 32, 1ULL << 36)
#    endif
#  else
#    define SANITIZER_MMAP_RANGE_SIZE FIRST_32_SECOND_64(1ULL << 32, 1ULL << 48)
#  endif
#elif defined(__sparc__)
#  define SANITIZER_MMAP_RANGE_SIZE FIRST_32_SECOND_64(1ULL << 32, 1ULL << 52)
#else
#  define SANITIZER_MMAP_RANGE_SIZE FIRST_32_SECOND_64(1ULL << 32, 1ULL << 47)
#endif

// Whether the addresses are sign-extended from the VMA range to the word.
// The SPARC64 Linux port implements this to split the VMA space into two
// non-contiguous halves with a huge hole in the middle.
#if defined(__sparc__) && SANITIZER_WORDSIZE == 64
#  define SANITIZER_SIGN_EXTENDED_ADDRESSES 1
#else
#  define SANITIZER_SIGN_EXTENDED_ADDRESSES 0
#endif

// udi16 syscalls can only be used when the following conditions are
// met:
// * target is one of arm32, x86-32, sparc32, sh or m68k
// * libc version is libc5, glibc-2.0, glibc-2.1 or glibc-2.2 to 2.15
//   built against > linux-2.2 kernel headers
// Since we don't want to include libc headers here, we check the
// target only.
#if defined(__arm__) || SANITIZER_X32 || defined(__sparc__)
#  define SANITIZER_USES_UID16_SYSCALLS 1
#else
#  define SANITIZER_USES_UID16_SYSCALLS 0
#endif

#if defined(__mips__)
#  define SANITIZER_POINTER_FORMAT_LENGTH FIRST_32_SECOND_64(8, 10)
#else
#  define SANITIZER_POINTER_FORMAT_LENGTH FIRST_32_SECOND_64(8, 12)
#endif

/// \macro MSC_PREREQ
/// \brief Is the compiler MSVC of at least the specified version?
/// The common \param version values to check for are:
///  * 1800: Microsoft Visual Studio 2013 / 12.0
///  * 1900: Microsoft Visual Studio 2015 / 14.0
#ifdef _MSC_VER
#  define MSC_PREREQ(version) (_MSC_VER >= (version))
#else
#  define MSC_PREREQ(version) 0
#endif

#if SANITIZER_APPLE && defined(__x86_64__)
#  define SANITIZER_NON_UNIQUE_TYPEINFO 0
#else
#  define SANITIZER_NON_UNIQUE_TYPEINFO 1
#endif

// On linux, some architectures had an ABI transition from 64-bit long double
// (ie. same as double) to 128-bit long double.  On those, glibc symbols
// involving long doubles come in two versions, and we need to pass the
// correct one to dlvsym when intercepting them.
#if SANITIZER_LINUX && (SANITIZER_S390 || SANITIZER_PPC32 || SANITIZER_PPC64V1)
#  define SANITIZER_NLDBL_VERSION "GLIBC_2.4"
#endif

#if SANITIZER_GO == 0
#  define SANITIZER_GO 0
#endif

// On PowerPC and ARM Thumb, calling pthread_exit() causes LSan to detect leaks.
// pthread_exit() performs unwinding that leads to dlopen'ing libgcc_s.so.
// dlopen mallocs "libgcc_s.so" string which confuses LSan, it fails to realize
// that this allocation happens in dynamic linker and should be ignored.
#if SANITIZER_PPC || defined(__thumb__)
#  define SANITIZER_SUPPRESS_LEAK_ON_PTHREAD_EXIT 1
#else
#  define SANITIZER_SUPPRESS_LEAK_ON_PTHREAD_EXIT 0
#endif

#if SANITIZER_FREEBSD || SANITIZER_APPLE || SANITIZER_NETBSD || SANITIZER_SOLARIS
#  define SANITIZER_MADVISE_DONTNEED MADV_FREE
#else
#  define SANITIZER_MADVISE_DONTNEED MADV_DONTNEED
#endif

// Older gcc have issues aligning to a constexpr, and require an integer.
// See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=56859 among others.
#if defined(__powerpc__) || defined(__powerpc64__)
#  define SANITIZER_CACHE_LINE_SIZE 128
#else
#  define SANITIZER_CACHE_LINE_SIZE 64
#endif

// Enable offline markup symbolizer for Fuchsia.
#if SANITIZER_FUCHSIA
#  define SANITIZER_SYMBOLIZER_MARKUP 1
#else
#  define SANITIZER_SYMBOLIZER_MARKUP 0
#endif

// Enable ability to support sanitizer initialization that is
// compatible with the sanitizer library being loaded via
// `dlopen()`.
#if SANITIZER_APPLE
#  define SANITIZER_SUPPORTS_INIT_FOR_DLOPEN 1
#else
#  define SANITIZER_SUPPORTS_INIT_FOR_DLOPEN 0
#endif

// SANITIZER_SUPPORTS_THREADLOCAL
// 1 - THREADLOCAL macro is supported by target
// 0 - THREADLOCAL macro is not supported by target
#ifndef __has_feature
// TODO: Support other compilers here
#  define SANITIZER_SUPPORTS_THREADLOCAL 1
#else
#  if __has_feature(tls)
#    define SANITIZER_SUPPORTS_THREADLOCAL 1
#  else
#    define SANITIZER_SUPPORTS_THREADLOCAL 0
#  endif
#endif

#if defined(__thumb__) && defined(__linux__)
// Workaround for
// https://lab.llvm.org/buildbot/#/builders/clang-thumbv7-full-2stage
// or
// https://lab.llvm.org/staging/#/builders/clang-thumbv7-full-2stage
// It fails *rss_limit_mb_test* without meaningful errors.
#  define SANITIZER_START_BACKGROUND_THREAD_IN_ASAN_INTERNAL 1
#else
#  define SANITIZER_START_BACKGROUND_THREAD_IN_ASAN_INTERNAL 0
#endif

#endif  // SANITIZER_PLATFORM_H
