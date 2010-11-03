#ifndef _LINUX_JUMP_LABEL_REF_H
#define _LINUX_JUMP_LABEL_REF_H

#include <linux/jump_label.h>
#include <asm/atomic.h>

#ifdef HAVE_JUMP_LABEL

static inline void jump_label_inc(atomic_t *key)
{
	if (atomic_add_return(1, key) == 1)
		jump_label_enable(key);
}

static inline void jump_label_dec(atomic_t *key)
{
	if (atomic_dec_and_test(key))
		jump_label_disable(key);
}

#else /* !HAVE_JUMP_LABEL */

static inline void jump_label_inc(atomic_t *key)
{
	atomic_inc(key);
}

static inline void jump_label_dec(atomic_t *key)
{
	atomic_dec(key);
}

#undef JUMP_LABEL
#define JUMP_LABEL(key, label)						\
do {									\
	if (unlikely(__builtin_choose_expr(				\
	      __builtin_types_compatible_p(typeof(key), atomic_t *),	\
	      atomic_read((atomic_t *)(key)), *(key))))			\
		goto label;						\
} while (0)

#endif /* HAVE_JUMP_LABEL */

#endif /* _LINUX_JUMP_LABEL_REF_H */
