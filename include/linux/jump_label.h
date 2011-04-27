#ifndef _LINUX_JUMP_LABEL_H
#define _LINUX_JUMP_LABEL_H

#include <linux/types.h>
#include <linux/compiler.h>

#if defined(CC_HAVE_ASM_GOTO) && defined(CONFIG_JUMP_LABEL)

struct jump_label_key {
	atomic_t enabled;
	struct jump_entry *entries;
#ifdef CONFIG_MODULES
	struct jump_label_mod *next;
#endif
};

# include <asm/jump_label.h>
# define HAVE_JUMP_LABEL
#endif

enum jump_label_type {
	JUMP_LABEL_DISABLE = 0,
	JUMP_LABEL_ENABLE,
};

struct module;

#ifdef HAVE_JUMP_LABEL

#ifdef CONFIG_MODULES
#define JUMP_LABEL_INIT {{ 0 }, NULL, NULL}
#else
#define JUMP_LABEL_INIT {{ 0 }, NULL}
#endif

static __always_inline bool static_branch(struct jump_label_key *key)
{
	return arch_static_branch(key);
}

extern struct jump_entry __start___jump_table[];
extern struct jump_entry __stop___jump_table[];

extern void jump_label_lock(void);
extern void jump_label_unlock(void);
extern void arch_jump_label_transform(struct jump_entry *entry,
				 enum jump_label_type type);
extern void arch_jump_label_text_poke_early(jump_label_t addr);
extern int jump_label_text_reserved(void *start, void *end);
extern void jump_label_inc(struct jump_label_key *key);
extern void jump_label_dec(struct jump_label_key *key);
extern bool jump_label_enabled(struct jump_label_key *key);
extern void jump_label_apply_nops(struct module *mod);

#else

#include <asm/atomic.h>

#define JUMP_LABEL_INIT {ATOMIC_INIT(0)}

struct jump_label_key {
	atomic_t enabled;
};

static __always_inline bool static_branch(struct jump_label_key *key)
{
	if (unlikely(atomic_read(&key->enabled)))
		return true;
	return false;
}

static inline void jump_label_inc(struct jump_label_key *key)
{
	atomic_inc(&key->enabled);
}

static inline void jump_label_dec(struct jump_label_key *key)
{
	atomic_dec(&key->enabled);
}

static inline int jump_label_text_reserved(void *start, void *end)
{
	return 0;
}

static inline void jump_label_lock(void) {}
static inline void jump_label_unlock(void) {}

static inline bool jump_label_enabled(struct jump_label_key *key)
{
	return !!atomic_read(&key->enabled);
}

static inline int jump_label_apply_nops(struct module *mod)
{
	return 0;
}

#endif

#endif
