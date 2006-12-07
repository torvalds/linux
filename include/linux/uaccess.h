#ifndef __LINUX_UACCESS_H__
#define __LINUX_UACCESS_H__

#include <asm/uaccess.h>

#ifndef ARCH_HAS_NOCACHE_UACCESS

static inline unsigned long __copy_from_user_inatomic_nocache(void *to,
				const void __user *from, unsigned long n)
{
	return __copy_from_user_inatomic(to, from, n);
}

static inline unsigned long __copy_from_user_nocache(void *to,
				const void __user *from, unsigned long n)
{
	return __copy_from_user(to, from, n);
}

#endif		/* ARCH_HAS_NOCACHE_UACCESS */

/**
 * probe_kernel_address(): safely attempt to read from a location
 * @addr: address to read from - its type is type typeof(retval)*
 * @retval: read into this variable
 *
 * Safely read from address @addr into variable @revtal.  If a kernel fault
 * happens, handle that and return -EFAULT.
 * We ensure that the __get_user() is executed in atomic context so that
 * do_page_fault() doesn't attempt to take mmap_sem.  This makes
 * probe_kernel_address() suitable for use within regions where the caller
 * already holds mmap_sem, or other locks which nest inside mmap_sem.
 */
#define probe_kernel_address(addr, retval)		\
	({						\
		long ret;				\
							\
		inc_preempt_count();			\
		ret = __get_user(retval, (__force typeof(*addr) __user *)addr);\
		dec_preempt_count();			\
		ret;					\
	})

#endif		/* __LINUX_UACCESS_H__ */
