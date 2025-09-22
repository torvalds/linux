/* Public domain. */

#ifndef _LINUX_KERNEL_H
#define _LINUX_KERNEL_H

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/stdarg.h>
#include <sys/malloc.h>

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/bitops.h>
#include <linux/log2.h>
#include <linux/linkage.h>
#include <linux/printk.h>
#include <linux/typecheck.h>
#include <linux/container_of.h>
#include <linux/stddef.h>
#include <linux/align.h>
#include <linux/math.h>
#include <linux/limits.h>
#include <asm/byteorder.h>
#include <linux/wordpart.h>

#define swap(a, b) \
	do { __typeof(a) __tmp = (a); (a) = (b); (b) = __tmp; } while(0)

#define ARRAY_SIZE nitems

#define scnprintf(str, size, fmt, arg...) snprintf(str, size, fmt, ## arg)

#define min_t(t, a, b) ({ \
	t __min_a = (a); \
	t __min_b = (b); \
	__min_a < __min_b ? __min_a : __min_b; })

#define max_t(t, a, b) ({ \
	t __max_a = (a); \
	t __max_b = (b); \
	__max_a > __max_b ? __max_a : __max_b; })

#define MIN_T(t, a, b) min_t(t, a, b)
#define MAX_T(t, a, b) max_t(t, a, b)

#define clamp_t(t, x, a, b) min_t(t, max_t(t, x, a), b)
#define clamp(x, a, b) clamp_t(__typeof(x), x, a, b)
#define clamp_val(x, a, b) clamp_t(__typeof(x), x, a, b)

#define min(a, b) MIN(a, b)
#define max(a, b) MAX(a, b)
#define min3(x, y, z) MIN(x, MIN(y, z))
#define max3(x, y, z) MAX(x, MAX(y, z))

#define min_not_zero(a, b) (a == 0) ? b : ((b == 0) ? a : min(a, b))

static inline char *
kvasprintf(int flags, const char *fmt, va_list ap)
{
	char *buf;
	size_t len;
	va_list vl;

	va_copy(vl, ap);
	len = vsnprintf(NULL, 0, fmt, vl);
	va_end(vl);

	buf = malloc(len + 1, M_DRM, flags);
	if (buf) {
		vsnprintf(buf, len + 1, fmt, ap);
	}

	return buf;
}

static inline char *
kasprintf(int flags, const char *fmt, ...)
{
	char *buf;
	va_list ap;

	va_start(ap, fmt);
	buf = kvasprintf(flags, fmt, ap);
	va_end(ap);

	return buf;
}

static inline int
vscnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	int nc;

	nc = vsnprintf(buf, size, fmt, ap);
	if (nc > (size - 1))
		return (size - 1);
	else
		return nc;
}

#define might_sleep()		assertwaitok()
#define might_sleep_if(x)	do {	\
	if (x)				\
		assertwaitok();		\
} while (0)
#define might_fault()

#define add_taint(x, y)
#define TAINT_MACHINE_CHECK	0
#define TAINT_WARN		1
#define LOCKDEP_STILL_OK	0

#define u64_to_user_ptr(x)	((void *)(uintptr_t)(x))

#define _RET_IP_		__builtin_return_address(0)
#define _THIS_IP_		0

#define STUB() do { printf("%s: stub\n", __func__); } while(0)

#define PTR_IF(c, p)		((c) ? (p) : NULL)

#endif
