/* Public domain. */

#ifndef _LINUX_AVERAGE_H
#define _LINUX_AVERAGE_H

#include <sys/types.h>
#include <lib/libkern/libkern.h>

#define DECLARE_EWMA(name, precision, recip)			\
struct ewma_##name {						\
	u_long value;						\
};								\
								\
static inline void						\
ewma_##name##_init(struct ewma_##name *p)			\
{								\
	p->value = 0;						\
}								\
								\
static inline void						\
ewma_##name##_add(struct ewma_##name *p, u_long value)		\
{								\
	u_long shift = fls(recip) - 1;				\
								\
	if (p->value == 0)					\
		p->value = (value << (precision));		\
	else							\
		p->value = ((((p->value << shift) - p->value) +	\
		    (value << (precision))) >> shift);		\
}								\
								\
static inline u_long						\
ewma_##name##_read(struct ewma_##name *p)			\
{								\
	return (p->value >> (precision));			\
}

#endif
