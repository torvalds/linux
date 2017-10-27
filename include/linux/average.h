#ifndef _LINUX_AVERAGE_H
#define _LINUX_AVERAGE_H

/*
 * Exponentially weighted moving average (EWMA)
 *
 * This implements a fixed-precision EWMA algorithm, with both the
 * precision and fall-off coefficient determined at compile-time
 * and built into the generated helper funtions.
 *
 * The first argument to the macro is the name that will be used
 * for the struct and helper functions.
 *
 * The second argument, the precision, expresses how many bits are
 * used for the fractional part of the fixed-precision values.
 *
 * The third argument, the weight reciprocal, determines how the
 * new values will be weighed vs. the old state, new values will
 * get weight 1/weight_rcp and old values 1-1/weight_rcp. Note
 * that this parameter must be a power of two for efficiency.
 */

#define DECLARE_EWMA(name, _precision, _weight_rcp)			\
	struct ewma_##name {						\
		unsigned long internal;					\
	};								\
	static inline void ewma_##name##_init(struct ewma_##name *e)	\
	{								\
		BUILD_BUG_ON(!__builtin_constant_p(_precision));	\
		BUILD_BUG_ON(!__builtin_constant_p(_weight_rcp));	\
		/*							\
		 * Even if you want to feed it just 0/1 you should have	\
		 * some bits for the non-fractional part...		\
		 */							\
		BUILD_BUG_ON((_precision) > 30);			\
		BUILD_BUG_ON_NOT_POWER_OF_2(_weight_rcp);		\
		e->internal = 0;					\
	}								\
	static inline unsigned long					\
	ewma_##name##_read(struct ewma_##name *e)			\
	{								\
		BUILD_BUG_ON(!__builtin_constant_p(_precision));	\
		BUILD_BUG_ON(!__builtin_constant_p(_weight_rcp));	\
		BUILD_BUG_ON((_precision) > 30);			\
		BUILD_BUG_ON_NOT_POWER_OF_2(_weight_rcp);		\
		return e->internal >> (_precision);			\
	}								\
	static inline void ewma_##name##_add(struct ewma_##name *e,	\
					     unsigned long val)		\
	{								\
		unsigned long internal = ACCESS_ONCE(e->internal);	\
		unsigned long weight_rcp = ilog2(_weight_rcp);		\
		unsigned long precision = _precision;			\
									\
		BUILD_BUG_ON(!__builtin_constant_p(_precision));	\
		BUILD_BUG_ON(!__builtin_constant_p(_weight_rcp));	\
		BUILD_BUG_ON((_precision) > 30);			\
		BUILD_BUG_ON_NOT_POWER_OF_2(_weight_rcp);		\
									\
		ACCESS_ONCE(e->internal) = internal ?			\
			(((internal << weight_rcp) - internal) +	\
				(val << precision)) >> weight_rcp :	\
			(val << precision);				\
	}

#endif /* _LINUX_AVERAGE_H */
