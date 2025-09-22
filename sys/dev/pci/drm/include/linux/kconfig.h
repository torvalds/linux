/* Public domain. */

#ifndef _LINUX_KCONFIG_H
#define _LINUX_KCONFIG_H

#include <sys/endian.h>

#include <generated/autoconf.h>

#define __NEWARG1			__newarg,
#define __is_defined(x)			__is_defined2(x)
#define __is_defined2(x)		__is_defined3(__NEWARG##x)
#define __is_defined3(x)		__is_defined4(x 1, 0)
#define __is_defined4(a, b, ...)	b

#define IS_ENABLED(x)		__is_defined(x)
#define IS_REACHABLE(x)		__is_defined(x)
#define IS_BUILTIN(x)		__is_defined(x)
#define IS_MODULE(x)		0

#if BYTE_ORDER == BIG_ENDIAN
#define __BIG_ENDIAN
#else
#define __LITTLE_ENDIAN
#endif

#endif
