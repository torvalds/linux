#ifndef _LINUX_AVERAGE_H
#define _LINUX_AVERAGE_H

/* Exponentially weighted moving average (EWMA) */

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
