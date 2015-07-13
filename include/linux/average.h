#ifndef _LINUX_AVERAGE_H
#define _LINUX_AVERAGE_H

/* Exponentially weighted moving average (EWMA) */

/* For more documentation see lib/average.c */

struct ewma {
	unsigned long internal;
	unsigned long factor;
	unsigned long weight;
};

extern void ewma_init(struct ewma *avg, unsigned long factor,
		      unsigned long weight);

extern struct ewma *ewma_add(struct ewma *avg, unsigned long val);

/**
 * ewma_read() - Get average value
 * @avg: Average structure
 *
 * Returns the average value held in @avg.
 */
static inline unsigned long ewma_read(const struct ewma *avg)
{
	return avg->internal >> avg->factor;
}

#define DECLARE_EWMA(name, _factor, _weight)				\
	struct ewma_##name {						\
		unsigned long internal;					\
	};								\
	static inline void ewma_##name##_init(struct ewma_##name *e)	\
	{								\
		BUILD_BUG_ON(!__builtin_constant_p(_factor));		\
		BUILD_BUG_ON(!__builtin_constant_p(_weight));		\
		BUILD_BUG_ON_NOT_POWER_OF_2(_factor);			\
		BUILD_BUG_ON_NOT_POWER_OF_2(_weight);			\
		e->internal = 0;					\
	}								\
	static inline unsigned long					\
	ewma_##name##_read(struct ewma_##name *e)			\
	{								\
		BUILD_BUG_ON(!__builtin_constant_p(_factor));		\
		BUILD_BUG_ON(!__builtin_constant_p(_weight));		\
		BUILD_BUG_ON_NOT_POWER_OF_2(_factor);			\
		BUILD_BUG_ON_NOT_POWER_OF_2(_weight);			\
		return e->internal >> ilog2(_factor);			\
	}								\
	static inline void ewma_##name##_add(struct ewma_##name *e,	\
					     unsigned long val)		\
	{								\
		unsigned long internal = ACCESS_ONCE(e->internal);	\
		unsigned long weight = ilog2(_weight);			\
		unsigned long factor = ilog2(_factor);			\
									\
		BUILD_BUG_ON(!__builtin_constant_p(_factor));		\
		BUILD_BUG_ON(!__builtin_constant_p(_weight));		\
		BUILD_BUG_ON_NOT_POWER_OF_2(_factor);			\
		BUILD_BUG_ON_NOT_POWER_OF_2(_weight);			\
									\
		ACCESS_ONCE(e->internal) = internal ?			\
			(((internal << weight) - internal) +		\
				(val << factor)) >> weight :		\
			(val << factor);				\
	}

#endif /* _LINUX_AVERAGE_H */
