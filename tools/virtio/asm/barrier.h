/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#if defined(__i386__) || defined(__x86_64__)
#define barrier() asm volatile("" ::: "memory")
#define virt_mb() __sync_synchronize()
#define virt_rmb() barrier()
#define virt_wmb() barrier()
/* Atomic store should be enough, but gcc generates worse code in that case. */
#define virt_store_mb(var, value)  do { \
	typeof(var) virt_store_mb_value = (value); \
	__atomic_exchange(&(var), &virt_store_mb_value, &virt_store_mb_value, \
			  __ATOMIC_SEQ_CST); \
	barrier(); \
} while (0);
/* Weak barriers should be used. If not - it's a bug */
# define mb() abort()
# define dma_rmb() abort()
# define dma_wmb() abort()
#elif defined(__aarch64__)
#define dmb(opt) asm volatile("dmb " #opt : : : "memory")
#define virt_mb() __sync_synchronize()
#define virt_rmb() dmb(ishld)
#define virt_wmb() dmb(ishst)
#define virt_store_mb(var, value)  do { WRITE_ONCE(var, value); dmb(ish); } while (0)
/* Weak barriers should be used. If not - it's a bug */
# define mb() abort()
# define dma_rmb() abort()
# define dma_wmb() abort()
#else
#error Please fill in barrier macros
#endif

