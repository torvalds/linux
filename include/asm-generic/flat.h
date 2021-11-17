/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_GENERIC_FLAT_H
#define _ASM_GENERIC_FLAT_H

#include <linux/uaccess.h>

static inline int flat_get_addr_from_rp(u32 __user *rp, u32 relval, u32 flags,
		u32 *addr)
{
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	return copy_from_user(addr, rp, 4) ? -EFAULT : 0;
#else
	return get_user(*addr, rp);
#endif
}

static inline int flat_put_addr_at_rp(u32 __user *rp, u32 addr, u32 rel)
{
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	return copy_to_user(rp, &addr, 4) ? -EFAULT : 0;
#else
	return put_user(addr, rp);
#endif
}

#endif /* _ASM_GENERIC_FLAT_H */
