/* Public domain. */

#ifndef _LINUX_LOG2_H
#define _LINUX_LOG2_H

#include <sys/types.h>
#include <sys/systm.h>

#define ilog2(x) ((sizeof(x) <= 4) ? (fls(x) - 1) : (flsl(x) - 1))

int	drm_order(unsigned long);

#define is_power_of_2(x)	(((x) != 0) && (((x) - 1) & (x)) == 0)
#define order_base_2(x)		drm_order(x)

static inline unsigned long
roundup_pow_of_two(unsigned long x)
{
	return (1UL << flsl(x - 1));
}

static inline unsigned long
rounddown_pow_of_two(unsigned long x)
{
	return (1UL << (flsl(x) - 1));
}

#endif
