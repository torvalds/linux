/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_JUMP_LABEL_H
#define _LINUX_JUMP_LABEL_H

/*
 * Jump label support
 *
 * Copyright (C) 2009-2012 Jason Baron <jbaron@redhat.com>
 * Copyright (C) 2011-2012 Red Hat, Inc., Peter Zijlstra
 *
 * DEPRECATED API:
 *
 * The use of 'struct static_key' directly, is now DEPRECATED. In addition
 * static_key_{true,false}() is also DEPRECATED. IE DO NOT use the following:
 *
 * struct static_key false = STATIC_KEY_INIT_FALSE;
 * struct static_key true = STATIC_KEY_INIT_TRUE;
 * static_key_true()
 * static_key_false()
 *
 * The updated API replacements are:
 *
 * DEFINE_STATIC_KEY_TRUE(key);
 * DEFINE_STATIC_KEY_FALSE(key);
 * DEFINE_STATIC_KEY_ARRAY_TRUE(keys, count);
 * DEFINE_STATIC_KEY_ARRAY_FALSE(keys, count);
 * static_branch_likely()
 * static_branch_unlikely()
 *
 * Jump labels provide an interface to generate dynamic branches using
 * self-modifying code. Assuming toolchain and architecture support, if we
 * define a "key" that is initially false via "DEFINE_STATIC_KEY_FALSE(key)",
 * an "if (static_branch_unlikely(&key))" statement is an unconditional branch
 * (which defaults to false - and the true block is placed out of line).
 * Similarly, we can define an initially true key via
 * "DEFINE_STATIC_KEY_TRUE(key)", and use it in the same
 * "if (static_branch_unlikely(&key))", in which case we will generate an
 * unconditional branch to the out-of-line true branch. Keys that are
 * initially true or false can be using in both static_branch_unlikely()
 * and static_branch_likely() statements.
 *
 * At runtime we can change the branch target by setting the key
 * to true via a call to static_branch_enable(), or false using
 * static_branch_disable(). If the direction of the branch is switched by
 * these calls then we run-time modify the branch target via a
 * no-op -> jump or jump -> no-op conversion. For example, for an
 * initially false key that is used in an "if (static_branch_unlikely(&key))"
 * statement, setting the key to true requires us to patch in a jump
 * to the out-of-line of true branch.
 *
 * In addition to static_branch_{enable,disable}, we can also reference count
 * the key or branch direction via static_branch_{inc,dec}. Thus,
 * static_branch_inc() can be thought of as a 'make more true' and
 * static_branch_dec() as a 'make more false'.
 *
 * Since this relies on modifying code, the branch modifying functions
 * must be considered absolute slow paths (machine wide synchronization etc.).
 * OTOH, since the affected branches are unconditional, their runtime overhead
 * will be absolutely minimal, esp. in the default (off) case where the total
 * effect is a single NOP of appropriate size. The on case will patch in a jump
 * to the out-of-line block.
 *
 * When the control is directly exposed to userspace, it is prudent to delay the
 * decrement to avoid high frequency code modifications which can (and do)
 * cause significant performance degradation. Struct static_key_deferred and
 * static_key_slow_dec_deferred() provide for this.
 *
 * Lacking toolchain and or architecture support, static keys fall back to a
 * simple conditional branch.
 *
 * Additional babbling in: Documentation/static-keys.txt
 */

#if defined(CC_HAVE_ASM_GOTO) && defined(CONFIG_JUMP_LABEL)
# define HAVE_JUMP_LABEL
#endif

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <linux/compiler.h>

extern bool static_key_initialized;

#define STATIC_KEY_CHECK_USE(key) WARN(!static_key_initialized,		      \
				    "%s(): static key '%pS' used before call to jump_label_init()", \
				    __func__, (key))

#ifdef HAVE_JUMP_LABEL

struct static_key {
	atomic_t enabled;
/*
 * Note:
 *   To make anonymous unions work with old compilers, the static
 *   initialization of them requires brackets. This creates a dependency
 *   on the order of the struct with the initializers. If any fields
 *   are added, STATIC_KEY_INIT_TRUE and STATIC_KEY_INIT_FALSE may need
 *   to be modified.
 *
 * bit 0 => 1 if key is initially true
 *	    0 if initially false
 * bit 1 => 1 if points to struct static_key_mod
 *	    0 if points to struct jump_entry
 */
	union {
		unsigned long type;
		struct jump_entry *entries;
		struct static_key_mod *next;
	};
};

#else
struct static_key {
	atomic_t enabled;
};
#endif	/* HAVE_JUMP_LABEL */
#endif /* __ASSEMBLY__ */

#ifdef HAVE_JUMP_LABEL
#include <asm/jump_label.h>
#endif

#ifndef __ASSEMBLY__

enum jump_label_type {
	JUMP_LABEL_NOP = 0,
	JUMP_LABEL_JMP,
};

struct module;

#ifdef HAVE_JUMP_LABEL

#define JUMP_TYPE_FALSE		0UL
#define JUMP_TYPE_TRUE		1UL
#define JUMP_TYPE_LINKED	2UL
#define JUMP_TYPE_MASK		3UL

static __always_inline bool static_key_false(struct static_key *key)
{
	return arch_static_branch(key, false);
}

static __always_inline bool static_key_true(struct static_key *key)
{
	return !arch_static_branch(key, true);
}

extern struct jump_entry __start___jump_table[];
extern struct jump_entry __stop___jump_table[];

extern void jump_label_init(void);
extern void jump_label_invalidate_init(void);
extern void jump_label_lock(void);
extern void jump_label_unlock(void);
extern void arch_jump_label_transform(struct jump_entry *entry,
				      enum jump_label_type type);
extern void arch_jump_label_transform_static(struct jump_entry *entry,
					     enum jump_label_type type);
extern int jump_label_text_reserved(void *start, void *end);
extern void static_key_slow_inc(struct static_key *key);
extern void static_key_slow_dec(struct static_key *key);
extern void static_key_slow_inc_cpuslocked(struct static_key *key);
extern void static_key_slow_dec_cpuslocked(struct static_key *key);
extern void jump_label_apply_nops(struct module *mod);
extern int static_key_count(struct static_key *key);
extern void static_key_enable(struct static_key *key);
extern void static_key_disable(struct static_key *key);
extern void static_key_enable_cpuslocked(struct static_key *key);
extern void static_key_disable_cpuslocked(struct static_key *key);

/*
 * We should be using ATOMIC_INIT() for initializing .enabled, but
 * the inclusion of atomic.h is problematic for inclusion of jump_label.h
 * in 'low-level' headers. Thus, we are initializing .enabled with a
 * raw value, but have added a BUILD_BUG_ON() to catch any issues in
 * jump_label_init() see: kernel/jump_label.c.
 */
#define STATIC_KEY_INIT_TRUE					\
	{ .enabled = { 1 },					\
	  { .entries = (void *)JUMP_TYPE_TRUE } }
#define STATIC_KEY_INIT_FALSE					\
	{ .enabled = { 0 },					\
	  { .entries = (void *)JUMP_TYPE_FALSE } }

#else  /* !HAVE_JUMP_LABEL */

#include <linux/atomic.h>
#include <linux/bug.h>

static inline int static_key_count(struct static_key *key)
{
	return atomic_read(&key->enabled);
}

static __always_inline void jump_label_init(void)
{
	static_key_initialized = true;
}

static inline void jump_label_invalidate_init(void) {}

static __always_inline bool static_key_false(struct static_key *key)
{
	if (unlikely(static_key_count(key) > 0))
		return true;
	return false;
}

static __always_inline bool static_key_true(struct static_key *key)
{
	if (likely(static_key_count(key) > 0))
		return true;
	return false;
}

static inline void static_key_slow_inc(struct static_key *key)
{
	STATIC_KEY_CHECK_USE(key);
	atomic_inc(&key->enabled);
}

static inline void static_key_slow_dec(struct static_key *key)
{
	STATIC_KEY_CHECK_USE(key);
	atomic_dec(&key->enabled);
}

#define static_key_slow_inc_cpuslocked(key) static_key_slow_inc(key)
#define static_key_slow_dec_cpuslocked(key) static_key_slow_dec(key)

static inline int jump_label_text_reserved(void *start, void *end)
{
	return 0;
}

static inline void jump_label_lock(void) {}
static inline void jump_label_unlock(void) {}

static inline int jump_label_apply_nops(struct module *mod)
{
	return 0;
}

static inline void static_key_enable(struct static_key *key)
{
	STATIC_KEY_CHECK_USE(key);

	if (atomic_read(&key->enabled) != 0) {
		WARN_ON_ONCE(atomic_read(&key->enabled) != 1);
		return;
	}
	atomic_set(&key->enabled, 1);
}

static inline void static_key_disable(struct static_key *key)
{
	STATIC_KEY_CHECK_USE(key);

	if (atomic_read(&key->enabled) != 1) {
		WARN_ON_ONCE(atomic_read(&key->enabled) != 0);
		return;
	}
	atomic_set(&key->enabled, 0);
}

#define static_key_enable_cpuslocked(k)		static_key_enable((k))
#define static_key_disable_cpuslocked(k)	static_key_disable((k))

#define STATIC_KEY_INIT_TRUE	{ .enabled = ATOMIC_INIT(1) }
#define STATIC_KEY_INIT_FALSE	{ .enabled = ATOMIC_INIT(0) }

#endif	/* HAVE_JUMP_LABEL */

#define STATIC_KEY_INIT STATIC_KEY_INIT_FALSE
#define jump_label_enabled static_key_enabled

/* -------------------------------------------------------------------------- */

/*
 * Two type wrappers around static_key, such that we can use compile time
 * type differentiation to emit the right code.
 *
 * All the below code is macros in order to play type games.
 */

struct static_key_true {
	struct static_key key;
};

struct static_key_false {
	struct static_key key;
};

#define STATIC_KEY_TRUE_INIT  (struct static_key_true) { .key = STATIC_KEY_INIT_TRUE,  }
#define STATIC_KEY_FALSE_INIT (struct static_key_false){ .key = STATIC_KEY_INIT_FALSE, }

#define DEFINE_STATIC_KEY_TRUE(name)	\
	struct static_key_true name = STATIC_KEY_TRUE_INIT

#define DECLARE_STATIC_KEY_TRUE(name)	\
	extern struct static_key_true name

#define DEFINE_STATIC_KEY_FALSE(name)	\
	struct static_key_false name = STATIC_KEY_FALSE_INIT

#define DECLARE_STATIC_KEY_FALSE(name)	\
	extern struct static_key_false name

#define DEFINE_STATIC_KEY_ARRAY_TRUE(name, count)		\
	struct static_key_true name[count] = {			\
		[0 ... (count) - 1] = STATIC_KEY_TRUE_INIT,	\
	}

#define DEFINE_STATIC_KEY_ARRAY_FALSE(name, count)		\
	struct static_key_false name[count] = {			\
		[0 ... (count) - 1] = STATIC_KEY_FALSE_INIT,	\
	}

extern bool ____wrong_branch_error(void);

#define static_key_enabled(x)							\
({										\
	if (!__builtin_types_compatible_p(typeof(*x), struct static_key) &&	\
	    !__builtin_types_compatible_p(typeof(*x), struct static_key_true) &&\
	    !__builtin_types_compatible_p(typeof(*x), struct static_key_false))	\
		____wrong_branch_error();					\
	static_key_count((struct static_key *)x) > 0;				\
})

#ifdef HAVE_JUMP_LABEL

/*
 * Combine the right initial value (type) with the right branch order
 * to generate the desired result.
 *
 *
 * type\branch|	likely (1)	      |	unlikely (0)
 * -----------+-----------------------+------------------
 *            |                       |
 *  true (1)  |	   ...		      |	   ...
 *            |    NOP		      |	   JMP L
 *            |    <br-stmts>	      |	1: ...
 *            |	L: ...		      |
 *            |			      |
 *            |			      |	L: <br-stmts>
 *            |			      |	   jmp 1b
 *            |                       |
 * -----------+-----------------------+------------------
 *            |                       |
 *  false (0) |	   ...		      |	   ...
 *            |    JMP L	      |	   NOP
 *            |    <br-stmts>	      |	1: ...
 *            |	L: ...		      |
 *            |			      |
 *            |			      |	L: <br-stmts>
 *            |			      |	   jmp 1b
 *            |                       |
 * -----------+-----------------------+------------------
 *
 * The initial value is encoded in the LSB of static_key::entries,
 * type: 0 = false, 1 = true.
 *
 * The branch type is encoded in the LSB of jump_entry::key,
 * branch: 0 = unlikely, 1 = likely.
 *
 * This gives the following logic table:
 *
 *	enabled	type	branch	  instuction
 * -----------------------------+-----------
 *	0	0	0	| NOP
 *	0	0	1	| JMP
 *	0	1	0	| NOP
 *	0	1	1	| JMP
 *
 *	1	0	0	| JMP
 *	1	0	1	| NOP
 *	1	1	0	| JMP
 *	1	1	1	| NOP
 *
 * Which gives the following functions:
 *
 *   dynamic: instruction = enabled ^ branch
 *   static:  instruction = type ^ branch
 *
 * See jump_label_type() / jump_label_init_type().
 */

#define static_branch_likely(x)							\
({										\
	bool branch;								\
	if (__builtin_types_compatible_p(typeof(*x), struct static_key_true))	\
		branch = !arch_static_branch(&(x)->key, true);			\
	else if (__builtin_types_compatible_p(typeof(*x), struct static_key_false)) \
		branch = !arch_static_branch_jump(&(x)->key, true);		\
	else									\
		branch = ____wrong_branch_error();				\
	likely(branch);								\
})

#define static_branch_unlikely(x)						\
({										\
	bool branch;								\
	if (__builtin_types_compatible_p(typeof(*x), struct static_key_true))	\
		branch = arch_static_branch_jump(&(x)->key, false);		\
	else if (__builtin_types_compatible_p(typeof(*x), struct static_key_false)) \
		branch = arch_static_branch(&(x)->key, false);			\
	else									\
		branch = ____wrong_branch_error();				\
	unlikely(branch);							\
})

#else /* !HAVE_JUMP_LABEL */

#define static_branch_likely(x)		likely(static_key_enabled(&(x)->key))
#define static_branch_unlikely(x)	unlikely(static_key_enabled(&(x)->key))

#endif /* HAVE_JUMP_LABEL */

/*
 * Advanced usage; refcount, branch is enabled when: count != 0
 */

#define static_branch_inc(x)		static_key_slow_inc(&(x)->key)
#define static_branch_dec(x)		static_key_slow_dec(&(x)->key)
#define static_branch_inc_cpuslocked(x)	static_key_slow_inc_cpuslocked(&(x)->key)
#define static_branch_dec_cpuslocked(x)	static_key_slow_dec_cpuslocked(&(x)->key)

/*
 * Normal usage; boolean enable/disable.
 */

#define static_branch_enable(x)			static_key_enable(&(x)->key)
#define static_branch_disable(x)		static_key_disable(&(x)->key)
#define static_branch_enable_cpuslocked(x)	static_key_enable_cpuslocked(&(x)->key)
#define static_branch_disable_cpuslocked(x)	static_key_disable_cpuslocked(&(x)->key)

#endif /* __ASSEMBLY__ */

#endif	/* _LINUX_JUMP_LABEL_H */
