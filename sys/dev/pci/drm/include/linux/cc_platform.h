/* Public domain. */

#ifndef _LINUX_CC_PLATFORM_H
#define _LINUX_CC_PLATFORM_H

#include <linux/types.h>

#define CC_ATTR_GUEST_MEM_ENCRYPT	0

static inline bool
cc_platform_has(int x)
{
	return false;
}

#endif
