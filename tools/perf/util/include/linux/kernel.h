#ifndef PERF_LINUX_KERNEL_H_
#define PERF_LINUX_KERNEL_H_

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))

#define PERF_ALIGN(x, a)	__PERF_ALIGN_MASK(x, (typeof(x))(a)-1)
#define __PERF_ALIGN_MASK(x, mask)	(((x)+(mask))&~(mask))

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

#define BUILD_BUG_ON_ZERO(e) (sizeof(struct { int:-!!(e); }))

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

#ifndef roundup
#define roundup(x, y) (                                \
{                                                      \
	const typeof(y) __y = y;		       \
	(((x) + (__y - 1)) / __y) * __y;	       \
}                                                      \
)
#endif

#ifndef BUG_ON
#ifdef NDEBUG
#define BUG_ON(cond) do { if (cond) {} } while (0)
#else
#define BUG_ON(cond) assert(!(cond))
#endif
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

/*
 * This looks more complex than it should be. But we need to
 * get the type for the ~ right in round_down (it needs to be
 * as wide as the result!), and we want to evaluate the macro
 * arguments just once each.
 */
#define __round_mask(x, y) ((__typeof__(x))((y)-1))
#define round_up(x, y) ((((x)-1) | __round_mask(x, y))+1)
#define round_down(x, y) ((x) & ~__round_mask(x, y))

#endif
