/* Public domain. */

#ifndef _LINUX_LIMITS_H
#define _LINUX_LIMITS_H

#include <sys/stdint.h>

#define S8_MAX		INT8_MAX
#define S16_MAX		INT16_MAX
#define S32_MAX		INT32_MAX
#define S64_MAX		INT64_MAX

#define U8_MAX		UINT8_MAX
#define U16_MAX		UINT16_MAX
#define U32_MAX		UINT32_MAX
#define U64_C(x)	UINT64_C(x)
#define U64_MAX		UINT64_MAX

#endif
