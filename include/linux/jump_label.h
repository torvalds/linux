#ifndef _LINUX_JUMP_LABEL_H
#define _LINUX_JUMP_LABEL_H

/*
 * Jump label support
 *
 * Copyright (C) 2009-2012 Jason Baron <jbaron@redhat.com>
 * Copyright (C) 2011-2012 Peter Zijlstra <pzijlstr@redhat.com>
 *
 * Jump labels provide an interface to generate dynamic branches using
 * self-modifying code. Assuming toolchain and architecture support the result
 * of a "if (static_branch(&key))" statement is a unconditional branch (which
 * defaults to false - and the true block is placed out of line).
 *
 * However at runtime we can change the 'static' branch target using
 * jump_label_{inc,dec}(). These function as a 'reference' count on the key
 * object and for as long as there are references all branches referring to
 * that particular key will point to the (out of line) true block.
 *
 * Since this relies on modifying code the jump_label_{inc,dec}() functions
 * must be considered absolute slow paths (machine wide synchronization etc.).
 * OTOH, since the affected branches are unconditional their runtime overhead
 * will be absolutely minimal, esp. in the default (off) case where the total
 * effect is a single NOP of appropriate size. The on case will patch in a jump
 * to the out-of-line block.
 *
 * When the control is directly exposed to userspace it is prudent to delay the
 * decrement to avoid high frequency code modifications which can (and do)
 * cause significant performance degradation. Struct jump_label_key_deferred and
 * jump_label_dec_deferred() provide for this.
 *
 * Lacking toolchain and or architecture support, it falls back to a simple
 * conditional branch.
 */

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/workqueue.h>

#if defined(CC_HAVE_ASM_GOTO) && defined(CONFIG_JUMP_LABEL)

struct jump_label_key {
	atomic_t enabled;
	struct jump_entry *entries;
#ifdef CONFIG_MODULES
	struct jump_label_mod *next;
#endif
};

struct jump_label_key_deferred {
	struct jump_label_key key;
	unsigned long timeout;
	struct delayed_work work;
};

# include <asm/jump_label.h>
# define HAVE_JUMP_LABEL
#endif	/* CC_HAVE_ASM_GOTO && CONFIG_JUMP_LABEL */

enum jump_label_type {
	JUMP_LABEL_DISABLE = 0,
	JUMP_LABEL_ENABLE,
};

struct module;

#ifdef HAVE_JUMP_LABEL

#ifdef CONFIG_MODULES
#define JUMP_LABEL_INIT {ATOMIC_INIT(0), NULL, NULL}
#else
#define JUMP_LABEL_INIT {ATOMIC_INIT(0), NULL}
#endif

static __always_inline bool static_branch(struct jump_label_key *key)
{
	return arch_static_branch(key);
}

extern struct jump_entry __start___jump_table[];
extern struct jump_entry __stop___jump_table[];

extern void jump_label_init(void);
extern void jump_label_lock(void);
extern void jump_label_unlock(void);
extern void arch_jump_label_transform(struct jump_entry *entry,
				      enum jump_label_type type);
extern void arch_jump_label_transform_static(struct jump_entry *entry,
					     enum jump_label_type type);
extern int jump_label_text_reserved(void *start, void *end);
extern void jump_label_inc(struct jump_label_key *key);
extern void jump_label_dec(struct jump_label_key *key);
extern void jump_label_dec_deferred(struct jump_label_key_deferred *key);
extern bool jump_label_enabled(struct jump_label_key *key);
extern void jump_label_apply_nops(struct module *mod);
extern void jump_label_rate_limit(struct jump_label_key_deferred *key,
		unsigned long rl);

#else  /* !HAVE_JUMP_LABEL */

#include <linux/atomic.h>

#define JUMP_LABEL_INIT {ATOMIC_INIT(0)}

struct jump_label_key {
	atomic_t enabled;
};

static __always_inline void jump_label_init(void)
{
}

struct jump_label_key_deferred {
	struct jump_label_key  key;
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

static inline void jump_label_dec_deferred(struct jump_label_key_deferred *key)
{
	jump_label_dec(&key->key);
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

static inline void jump_label_rate_limit(struct jump_label_key_deferred *key,
		unsigned long rl)
{
}
#endif	/* HAVE_JUMP_LABEL */

#define jump_label_key_enabled	((struct jump_label_key){ .enabled = ATOMIC_INIT(1), })
#define jump_label_key_disabled	((struct jump_label_key){ .enabled = ATOMIC_INIT(0), })

#endif	/* _LINUX_JUMP_LABEL_H */
