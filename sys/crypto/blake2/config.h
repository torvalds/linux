/* This file is in the public domain */
/* $FreeBSD$ */
#pragma once

#include <sys/endian.h>

#if _BYTE_ORDER == _LITTLE_ENDIAN
#define NATIVE_LITTLE_ENDIAN 1
#else
/* #undef NATIVE_LITTLE_ENDIAN */
#endif

#if defined(__ARM_FEATURE_UNALIGNED) \
    || defined(__i386__) || defined(__x86_64__) \
    || defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_8__) \
/* #undef HAVE_ALIGNED_ACCESS_REQUIRED */
#else
#define HAVE_ALIGNED_ACCESS_REQUIRED 1
#endif

#define HAVE_EXPLICIT_BZERO 1
