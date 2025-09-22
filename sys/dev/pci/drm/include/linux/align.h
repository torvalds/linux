/* Public domain. */

#ifndef _LINUX_ALIGN_H
#define _LINUX_ALIGN_H

#include <sys/param.h>

#define roundup2(x, y) (((x) + ((y) - 1)) & (~((__typeof(x))(y) - 1)))
#define rounddown2(x, y) ((x) & ~((__typeof(x))(y) - 1))

#undef ALIGN
#define ALIGN(x, y) roundup2((x), (y))

#define IS_ALIGNED(x, y)	(((x) & ((y) - 1)) == 0)
#define PTR_ALIGN(x, y)		((__typeof(x))roundup2((unsigned long)(x), (y)))
#define ALIGN_DOWN(x, y)	((__typeof(x))rounddown2((unsigned long)(x), (y)))

#endif
