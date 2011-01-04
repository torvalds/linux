#ifndef _LINUX_JUMP_LABEL_H
#define _LINUX_JUMP_LABEL_H

#if defined(CC_HAVE_ASM_GOTO) && defined(CONFIG_JUMP_LABEL)
# include <asm/jump_label.h>
# define HAVE_JUMP_LABEL
#endif

enum jump_label_type {
	JUMP_LABEL_ENABLE,
	JUMP_LABEL_DISABLE
};

struct module;

#ifdef HAVE_JUMP_LABEL

extern struct jump_entry __start___jump_table[];
extern struct jump_entry __stop___jump_table[];

extern void jump_label_lock(void);
extern void jump_label_unlock(void);
extern void arch_jump_label_transform(struct jump_entry *entry,
				 enum jump_label_type type);
extern void arch_jump_label_text_poke_early(jump_label_t addr);
extern void jump_label_update(unsigned long key, enum jump_label_type type);
extern void jump_label_apply_nops(struct module *mod);
extern int jump_label_text_reserved(void *start, void *end);

#define jump_label_enable(key) \
	jump_label_update((unsigned long)key, JUMP_LABEL_ENABLE);

#define jump_label_disable(key) \
	jump_label_update((unsigned long)key, JUMP_LABEL_DISABLE);

#else

#define JUMP_LABEL(key, label)			\
do {						\
	if (unlikely(*key))			\
		goto label;			\
} while (0)

#define jump_label_enable(cond_var)	\
do {					\
       *(cond_var) = 1;			\
} while (0)

#define jump_label_disable(cond_var)	\
do {					\
       *(cond_var) = 0;			\
} while (0)

static inline int jump_label_apply_nops(struct module *mod)
{
	return 0;
}

static inline int jump_label_text_reserved(void *start, void *end)
{
	return 0;
}

static inline void jump_label_lock(void) {}
static inline void jump_label_unlock(void) {}

#endif

#define COND_STMT(key, stmt)					\
do {								\
	__label__ jl_enabled;					\
	JUMP_LABEL(key, jl_enabled);				\
	if (0) {						\
jl_enabled:							\
		stmt;						\
	}							\
} while (0)

#endif
