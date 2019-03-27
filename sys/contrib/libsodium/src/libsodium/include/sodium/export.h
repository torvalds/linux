
#ifndef sodium_export_H
#define sodium_export_H

#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#if !defined(__clang__) && !defined(__GNUC__)
# ifdef __attribute__
#  undef __attribute__
# endif
# define __attribute__(a)
#endif

#ifdef SODIUM_STATIC
# define SODIUM_EXPORT
# define SODIUM_EXPORT_WEAK
#else
# if defined(_MSC_VER)
#  ifdef SODIUM_DLL_EXPORT
#   define SODIUM_EXPORT __declspec(dllexport)
#  else
#   define SODIUM_EXPORT __declspec(dllimport)
#  endif
# else
#  if defined(__SUNPRO_C)
#   ifndef __GNU_C__
#    define SODIUM_EXPORT __attribute__ (visibility(__global))
#   else
#    define SODIUM_EXPORT __attribute__ __global
#   endif
#  elif defined(_MSG_VER)
#   define SODIUM_EXPORT extern __declspec(dllexport)
#  else
#   define SODIUM_EXPORT __attribute__ ((visibility ("default")))
#  endif
# endif
# if defined(__ELF__) && !defined(SODIUM_DISABLE_WEAK_FUNCTIONS)
#  define SODIUM_EXPORT_WEAK SODIUM_EXPORT __attribute__((weak))
# else
#  define SODIUM_EXPORT_WEAK SODIUM_EXPORT
# endif
#endif

#ifndef CRYPTO_ALIGN
# if defined(__INTEL_COMPILER) || defined(_MSC_VER)
#  define CRYPTO_ALIGN(x) __declspec(align(x))
# else
#  define CRYPTO_ALIGN(x) __attribute__ ((aligned(x)))
# endif
#endif

#define SODIUM_MIN(A, B) ((A) < (B) ? (A) : (B))
#define SODIUM_SIZE_MAX SODIUM_MIN(UINT64_MAX, SIZE_MAX)

#endif
