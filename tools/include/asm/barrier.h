/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/compiler.h>
#if defined(__i386__) || defined(__x86_64__)
#include "../../arch/x86/include/asm/barrier.h"
#elif defined(__arm__)
#include "../../arch/arm/include/asm/barrier.h"
#elif defined(__aarch64__)
#include "../../arch/arm64/include/asm/barrier.h"
#elif defined(__powerpc__)
#include "../../arch/powerpc/include/asm/barrier.h"
#elif defined(__s390__)
#include "../../arch/s390/include/asm/barrier.h"
#elif defined(__sh__)
#include "../../arch/sh/include/asm/barrier.h"
#elif defined(__sparc__)
#include "../../arch/sparc/include/asm/barrier.h"
#elif defined(__tile__)
#include "../../arch/tile/include/asm/barrier.h"
#elif defined(__alpha__)
#include "../../arch/alpha/include/asm/barrier.h"
#elif defined(__mips__)
#include "../../arch/mips/include/asm/barrier.h"
#elif defined(__ia64__)
#include "../../arch/ia64/include/asm/barrier.h"
#elif defined(__xtensa__)
#include "../../arch/xtensa/include/asm/barrier.h"
#else
#include <asm-generic/barrier.h>
#endif

/*
 * Generic fallback smp_*() definitions for archs that haven't
 * been updated yet.
 */

#ifndef smp_rmb
# define smp_rmb()	rmb()
#endif

#ifndef smp_wmb
# define smp_wmb()	wmb()
#endif

#ifndef smp_mb
# define smp_mb()	mb()
#endif

#ifndef smp_store_release
# define smp_store_release(p, v)		\
do {						\
	smp_mb();				\
	WRITE_ONCE(*p, v);			\
} while (0)
#endif

#ifndef smp_load_acquire
# define smp_load_acquire(p)			\
({						\
	typeof(*p) ___p1 = READ_ONCE(*p);	\
	smp_mb();				\
	___p1;					\
})
#endif
