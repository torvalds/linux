/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* bits.h: Macros for dealing with bitmasks.  */

#ifndef _UAPI_LINUX_BITS_H
#define _UAPI_LINUX_BITS_H

#define __GENMASK(h, l) \
        (((~_UL(0)) - (_UL(1) << (l)) + 1) & \
         (~_UL(0) >> (__BITS_PER_LONG - 1 - (h))))

#define __GENMASK_ULL(h, l) \
        (((~_ULL(0)) - (_ULL(1) << (l)) + 1) & \
         (~_ULL(0) >> (__BITS_PER_LONG_LONG - 1 - (h))))

#define __GENMASK_U128(h, l) \
	((_BIT128((h)) << 1) - (_BIT128(l)))

#endif /* _UAPI_LINUX_BITS_H */
