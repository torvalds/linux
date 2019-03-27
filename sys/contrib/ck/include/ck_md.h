/*
 * Copyright 2018 Samy Al Bahra.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: head/sys/contrib/ck/include/ck_md.h 329388 2018-02-16 17:50:06Z cog
net $
 */

/*
 * This header file is meant for use of Concurrency Kit in the FreeBSD kernel.
 */

#ifndef CK_MD_H
#define CK_MD_H

#include <sys/param.h>

#ifndef _KERNEL
#error This header file is meant for the FreeBSD kernel.
#endif /* _KERNEL */

#ifndef CK_MD_CACHELINE
/*
 * FreeBSD's CACHE_LINE macro is a compile-time maximum cache-line size for an
 * architecture, defined to be 128 bytes by default on x86*. Even in presence
 * of adjacent sector prefetch, this doesn't make sense from a modeling
 * perspective.
 */
#if defined(__amd64__) || defined(__i386__)
#define CK_MD_CACHELINE (64)
#else
#define CK_MD_CACHELINE	(CACHE_LINE_SIZE)
#endif /* !__amd64__ && !__i386__ */
#endif /* CK_MD_CACHELINE */

#ifndef CK_MD_PAGESIZE
#define CK_MD_PAGESIZE (PAGE_SIZE)
#endif

/*
 * Once FreeBSD has a mechanism to detect RTM, this can be enabled and RTM
 * facilities can be called. These facilities refer to TSX.
 */
#ifndef CK_MD_RTM_DISABLE
#define CK_MD_RTM_DISABLE
#endif /* CK_MD_RTM_DISABLE */

/*
 * Do not enable pointer-packing-related (VMA) optimizations in kernel-space.
 */
#ifndef CK_MD_POINTER_PACK_DISABLE
#define CK_MD_POINTER_PACK_DISABLE
#endif /* CK_MD_POINTER_PACK_DISABLE */

/*
 * The following would be used for pointer-packing tricks, disabled for the
 * kernel.
 */
#ifndef CK_MD_VMA_BITS_UNKNOWN
#define CK_MD_VMA_BITS_UNKNOWN
#endif /* CK_MD_VMA_BITS_UNKNOWN */

/*
 * Do not enable double operations in kernel-space.
 */
#ifndef CK_PR_DISABLE_DOUBLE
#define CK_PR_DISABLE_DOUBLE
#endif /* CK_PR_DISABLE_DOUBLE */

/*
 * If building for a uni-processor target, then enable the uniprocessor
 * feature flag. This, among other things, will remove the lock prefix.
 */
#ifndef SMP
#define CK_MD_UMP
#endif /* SMP */

/*
 * Disable the use of compiler builtin functions.
 */
#define CK_MD_CC_BUILTIN_DISABLE 1

/*
 * CK expects those, which are normally defined by the build system.
 */
#if defined(__i386__) && !defined(__x86__)
#define __x86__
/*
 * If x86 becomes more relevant, we may want to consider importing in
 * __mbk() to avoid potential issues around false sharing.
 */
#define CK_MD_TSO
#define CK_MD_SSE_DISABLE 1
#elif defined(__amd64__)
#define CK_MD_TSO
#elif defined(__sparc64__) && !defined(__sparcv9__)
#define __sparcv9__
#define CK_MD_TSO
#elif defined(__powerpc64__) && !defined(__ppc64__)
#define __ppc64__
#elif defined(__powerpc__) && !defined(__ppc__)
#define __ppc__
#endif

/* If no memory model has been defined, assume RMO. */
#if !defined(CK_MD_RMO) && !defined(CK_MD_TSO) && !defined(CK_MD_PSO)
#define CK_MD_RMO
#endif

#define CK_VERSION "0.7.0"
#define CK_GIT_SHA "db5db44"

#endif /* CK_MD_H */
