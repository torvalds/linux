/* Public domain. */

#ifndef _LINUX_BUILD_BUG_H
#define _LINUX_BUILD_BUG_H

#include <linux/compiler.h>

#define BUILD_BUG()
#define BUILD_BUG_ON(x) CTASSERT(!(x))
#define BUILD_BUG_ON_NOT_POWER_OF_2(x)	0
#define BUILD_BUG_ON_MSG(x, y)		do { } while (0)
#define BUILD_BUG_ON_INVALID(x)		((void)0)
#define BUILD_BUG_ON_ZERO(x)		0

#define static_assert(x, ...)

#endif
