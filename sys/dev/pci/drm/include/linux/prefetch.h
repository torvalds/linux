/* Public domain. */

#ifndef _LINUX_PREFETCH_H
#define _LINUX_PREFETCH_H

#define prefetchw(x)	__builtin_prefetch(x,1)

#endif
