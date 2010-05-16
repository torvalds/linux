#ifndef PERF_LINUX_KERNEL_H_
#define PERF_LINUX_KERNEL_H_

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))

#define ALIGN(x,a)		__ALIGN_MASK(x,(typeof(x))(a)-1)
#define __ALIGN_MASK(x,mask)	(((x)+(mask))&~(mask))

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#ifndef container_of
/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({			\
	const typeof(((type *)0)->member) * __mptr = (ptr);	\
	(type *)((char *)__mptr - offsetof(type, member)); })
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

#ifndef BUG_ON
#define BUG_ON(cond) assert(!(cond))
#endif

/*
 * Both need more care to handle endianness
 * (Don't use bitmap_copy_le() for now)
 */
#define cpu_to_le64(x)	(x)
#define cpu_to_le32(x)	(x)

static inline int
vscnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
	int i;
	ssize_t ssize = size;

	i = vsnprintf(buf, size, fmt, args);

	return (i >= ssize) ? (ssize - 1) : i;
}

static inline int scnprintf(char * buf, size_t size, const char * fmt, ...)
{
	va_list args;
	ssize_t ssize = size;
	int i;

	va_start(args, fmt);
	i = vsnprintf(buf, size, fmt, args);
	va_end(args);

	return (i >= ssize) ? (ssize - 1) : i;
}

static inline unsigned long
simple_strtoul(const char *nptr, char **endptr, int base)
{
	return strtoul(nptr, endptr, base);
}

#ifndef pr_fmt
#define pr_fmt(fmt) fmt
#endif

#define pr_err(fmt, ...) \
	do { fprintf(stderr, pr_fmt(fmt), ##__VA_ARGS__); } while (0)
#define pr_warning(fmt, ...) \
	do { fprintf(stderr, pr_fmt(fmt), ##__VA_ARGS__); } while (0)
#define pr_info(fmt, ...) \
	do { fprintf(stderr, pr_fmt(fmt), ##__VA_ARGS__); } while (0)
#define pr_debug(fmt, ...) \
	eprintf(1, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_debugN(n, fmt, ...) \
	eprintf(n, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_debug2(fmt, ...) pr_debugN(2, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_debug3(fmt, ...) pr_debugN(3, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_debug4(fmt, ...) pr_debugN(4, pr_fmt(fmt), ##__VA_ARGS__)

#endif
