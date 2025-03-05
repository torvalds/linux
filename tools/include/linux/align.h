/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _TOOLS_LINUX_ALIGN_H
#define _TOOLS_LINUX_ALIGN_H

#include <uapi/linux/const.h>

#define ALIGN(x, a)		__ALIGN_KERNEL((x), (a))
#define ALIGN_DOWN(x, a)	__ALIGN_KERNEL((x) - ((a) - 1), (a))
#define IS_ALIGNED(x, a)	(((x) & ((typeof(x))(a) - 1)) == 0)

#endif /* _TOOLS_LINUX_ALIGN_H */
