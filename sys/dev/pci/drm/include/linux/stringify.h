/* Public domain. */

#ifndef _LINUX_STRINGIFY_H
#define _LINUX_STRINGIFY_H

#define ___stringify(x...)	#x
#define __stringify(x...)	___stringify(x)

#endif
