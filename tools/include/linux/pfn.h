/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_LINUX_PFN_H_
#define _TOOLS_LINUX_PFN_H_

#include <linux/mm.h>

#define PFN_UP(x)	(((x) + PAGE_SIZE - 1) >> PAGE_SHIFT)
#define PFN_DOWN(x)	((x) >> PAGE_SHIFT)
#define PFN_PHYS(x)	((phys_addr_t)(x) << PAGE_SHIFT)
#define PHYS_PFN(x)	((unsigned long)((x) >> PAGE_SHIFT))
#endif
