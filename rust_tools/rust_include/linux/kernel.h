/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TOOLS_LINUX_KERNEL_H
#define __TOOLS_LINUX_KERNEL_H

#include <stdarg.h>
#include <stddef.h>
#include <assert.h>
#include <linux/build_bug.h>
#include <linux/compiler.h>
#include <linux/math.h>
#include <linux/panic.h>
#include <endian.h>
#include <byteswap.h>
#include <linux/container_of.h>

#ifndef UINT_MAX
#define UINT_MAX	(~0U)
#endif

#define _RET_IP_		((unsigned long)__builtin_return_address(0))

#define PERF_ALIGN(x, a)	__PERF_ALIGN_MASK(x, (typeof(x))(a)-1)
#define __PERF_ALIGN_MASK(x, mask)	(((x)+(mask))&~(mask))

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#ifndef max
#define max(x, y) ({				\
	typeof(x) _max1 = (x);			\
	typeof(y) _max2 = (y);			\
	(void) (&_max1 == &_max2);		\
	_max1 > _max2 ? _max1 : _max2; })
#endif

#ifndef min
#define min(x, y) ({				\
	typeof(x) _min1 = (x);			\
	typeof(y) _min2 = (y);			\
	(void) (&_min1 == &_min2);		\
	_min1 < _min2 ? _min1 : _min2; })
#endif

#define max_t(type, x, y)	max((type)x, (type)y)
#define min_t(type, x, y)	min((type)x, (type)y)
#define clamp(val, lo, hi)	min((typeof(val))max(val, lo), hi)

#ifndef BUG_ON
#ifdef NDEBUG
#define BUG_ON(cond) do { if (cond) {} } while (0)
#else
#define BUG_ON(cond) assert(!(cond))
#endif
#endif
#define BUG()	BUG_ON(1)

#if __BYTE_ORDER == __BIG_ENDIAN
#define cpu_to_le16 bswap_16
#define cpu_to_le32 bswap_32
#define cpu_to_le64 bswap_64
#define le16_to_cpu bswap_16
#define le32_to_cpu bswap_32
#define le64_to_cpu bswap_64
#define cpu_to_be16
#define cpu_to_be32
#define cpu_to_be64
#define be16_to_cpu
#define be32_to_cpu
#define be64_to_cpu
#else
#define cpu_to_le16
#define cpu_to_le32
#define cpu_to_le64
#define le16_to_cpu
#define le32_to_cpu
#define le64_to_cpu
#define cpu_to_be16 bswap_16
#define cpu_to_be32 bswap_32
#define cpu_to_be64 bswap_64
#define be16_to_cpu bswap_16
#define be32_to_cpu bswap_32
#define be64_to_cpu bswap_64
#endif

int vscnprintf(char *buf, size_t size, const char *fmt, va_list args);
int scnprintf(char * buf, size_t size, const char * fmt, ...);
int scnprintf_pad(char * buf, size_t size, const char * fmt, ...);

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))
#endif

#define current_gfp_context(k) 0
#define synchronize_rcu()

#endif
