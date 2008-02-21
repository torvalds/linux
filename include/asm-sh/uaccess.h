#ifndef __ASM_SH_UACCESS_H
#define __ASM_SH_UACCESS_H

#ifdef CONFIG_SUPERH32
# include "uaccess_32.h"
#else
# include "uaccess_64.h"
#endif

static inline unsigned long
copy_from_user(void *to, const void __user *from, unsigned long n)
{
	unsigned long __copy_from = (unsigned long) from;
	__kernel_size_t __copy_size = (__kernel_size_t) n;

	if (__copy_size && __access_ok(__copy_from, __copy_size))
		return __copy_user(to, from, __copy_size);

	return __copy_size;
}

static inline unsigned long
copy_to_user(void __user *to, const void *from, unsigned long n)
{
	unsigned long __copy_to = (unsigned long) to;
	__kernel_size_t __copy_size = (__kernel_size_t) n;

	if (__copy_size && __access_ok(__copy_to, __copy_size))
		return __copy_user(to, from, __copy_size);

	return __copy_size;
}

#endif /* __ASM_SH_UACCESS_H */
