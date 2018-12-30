/*
 * jump label support
 *
 * Copyright (C) 2009 Jason Baron <jbaron@redhat.com>
 * Copyright (C) 2011 Peter Zijlstra
 *
 */
#include <linux/memory.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/err.h>
#include <linux/static_key.h>
#include <linux/jump_label_ratelimit.h>
#include <linux/bug.h>
#include <linux/cpu.h>
#include <asm/sections.h>

/* mutex to protect coming/going of the the jump_label table */
static DEFINE_MUTEX(jump_label_mutex);

void jump_label_lock(void)
{
	mutex_lock(&jump_label_mutex);
}

void jump_label_unlock(void)
{
	mutex_unlock(&jump_label_mutex);
}

static int jump_label_cmp(const void *a, const void *b)
{
	const struct jump_entry *jea = a;
	const struct jump_entry *jeb = b;

	if (jump_entry_key(jea) < jump_entry_key(jeb))
		return -1;

	if (jump_entry_key(jea) > jump_entry_key(jeb))
		return 1;

	return 0;
}

static void jump_label_swap(void *a, void *b, int size)
{
	long delta = (unsigned long)a - (unsigned long)b;
	struct jump_entry *jea = a;
	struct jump_entry *jeb = b;
	struct jump_entry tmp = *jea;

	jea->code	= jeb->code - delta;
	jea->target	= jeb->target - delta;
	jea->key	= jeb->key - delta;

	jeb->code	= tmp.code + delta;
	jeb->target	= tmp.target + delta;
	jeb->key	= tmp.key + delta;
}

static void
jump_label_sort_entries(struct jump_entry *start, struct jump_entry *stop)
{
	unsigned long size;
	void *swapfn = NULL;

	if (IS_ENABLED(CONFIG_HAVE_ARCH_JUMP_LABEL_RELATIVE))
		swapfn = jump_label_swap;

	size = (((unsigned long)stop - (unsigned long)start)
					/ sizeof(struct jump_entry));
	sort(start, size, sizeof(struct jump_entry), jump_label_cmp, swapfn);
}

static void jump_label_update(struct static_key *key);

/*
 * There are similar definitions for the !CONFIG_JUMP_LABEL case in jump_label.h.
 * The use of 'atomic_read()' requires atomic.h and its problematic for some
 * kernel headers such as kernel.h and others. Since static_key_count() is not
 * used in the branch statements as it is for the !CONFIG_JUMP_LABEL case its ok
 * to have it be a function here. Similarly, for 'static_key_enable()' and
 * 'static_key_disable()', which require bug.h. This should allow jump_label.h
 * to be included from most/all places for CONFIG_JUMP_LABEL.
 */
int static_key_count(struct static_key *key)
{
	/*
	 * -1 means the first static_key_slow_inc() is in progress.
	 *  static_key_enabled() must return true, so return 1 here.
	 */
	int n = atomic_read(&key->enabled);

	return n >= 0 ? n : 1;
}
EXPORT_SYMBOL_GPL(static_key_count);

void static_key_slow_inc_cpuslocked(struct static_key *key)
{
	int v, v1;

	STATIC_KEY_CHECK_USE(key);
	lockdep_assert_cpus_held();

	/*
	 * Careful if we get concurrent static_key_slow_inc() calls;
	 * later calls must wait for the first one to _finish_ the
	 * jump_label_update() process.  At the same time, however,
	 * the jump_label_update() call below wants to see
	 * static_key_enabled(&key) for jumps to be updated properly.
	 *
	 * So give a special meaning to negative key->enabled: it sends
	 * static_key_slow_inc() down the slow path, and it is non-zero
	 * so it counts as "enabled" in jump_label_update().  Note that
	 * atomic_inc_unless_negative() checks >= 0, so roll our own.
	 */
	for (v = atomic_read(&key->enabled); v > 0; v = v1) {
		v1 = atomic_cmpxchg(&key->enabled, v, v + 1);
		if (likely(v1 == v))
			return;
	}

	jump_label_lock();
	if (atomic_read(&key->enabled) == 0) {
		atomic_set(&key->enabled, -1);
		jump_label_update(key);
		/*
		 * Ensure that if the above cmpxchg loop observes our positive
		 * value, it must also observe all the text changes.
		 */
		atomic_set_release(&key->enabled, 1);
	} else {
		atomic_inc(&key->enabled);
	}
	jump_label_unlock();
}

void static_key_slow_inc(struct static_key *key)
{
	cpus_read_lock();
	static_key_slow_inc_cpuslocked(key);
	cpus_read_unlock();
}
EXPORT_SYMBOL_GPL(static_key_slow_inc);

void static_key_enable_cpuslocked(struct static_key *key)
{
	STATIC_KEY_CHECK_USE(key);
	lockdep_assert_cpus_held();

	if (atomic_read(&key->enabled) > 0) {
		WARN_ON_ONCE(atomic_read(&key->enabled) != 1);
		return;
	}

	jump_label_lock();
	if (atomic_read(&key->enabled) == 0) {
		atomic_set(&key->enabled, -1);
		jump_label_update(key);
		/*
		 * See static_key_slow_inc().
		 */
		atomic_set_release(&key->enabled, 1);
	}
	jump_label_unlock();
}
EXPORT_SYMBOL_GPL(static_key_enable_cpuslocked);

void static_key_enable(struct static_key *key)
{
	cpus_read_lock();
	static_key_enable_cpuslocked(key);
	cpus_read_unlock();
}
EXPORT_SYMBOL_GPL(static_key_enable);

void static_key_disable_cpuslocked(struct static_key *key)
{
	STATIC_KEY_CHECK_USE(key);
	lockdep_assert_cpus_held();

	if (atomic_read(&key->enabled) != 1) {
		WARN_ON_ONCE(atomic_read(&key->enabled) != 0);
		return;
	}

	jump_label_lock();
	if (atomic_cmpxchg(&key->enabled, 1, 0))
		jump_label_update(key);
	jump_label_unlock();
}
EXPORT_SYMBOL_GPL(static_key_disable_cpuslocked);

void static_key_disable(struct static_key *key)
{
	cpus_read_lock();
	static_key_disable_cpuslocked(key);
	cpus_read_unlock();
}
EXPORT_SYMBOL_GPL(static_key_disable);

static void __static_key_slow_dec_cpuslocked(struct static_key *key,
					   unsigned long rate_limit,
					   struct delayed_work *work)
{
	lockdep_assert_cpus_held();

	/*
	 * The negative count check is valid even when a negative
	 * key->enabled is in use by static_key_slow_inc(); a
	 * __static_key_slow_dec() before the first static_key_slow_inc()
	 * returns is unbalanced, because all other static_key_slow_inc()
	 * instances block while the update is in progress.
	 */
	if (!atomic_dec_and_mutex_lock(&key->enabled, &jump_label_mutex)) {
		WARN(atomic_read(&key->enabled) < 0,
		     "jump label: negative count!\n");
		return;
	}

	if (rate_limit) {
		atomic_inc(&key->enabled);
		schedule_delayed_work(work, rate_limit);
	} else {
		jump_label_update(key);
	}
	jump_label_unlock();
}

static void __static_key_slow_dec(struct static_key *key,
				  unsigned long rate_limit,
				  struct delayed_work *work)
{
	cpus_read_lock();
	__static_key_slow_dec_cpuslocked(key, rate_limit, work);
	cpus_read_unlock();
}

static void jump_label_update_timeout(struct work_struct *work)
{
	struct static_key_deferred *key =
		container_of(work, struct static_key_deferred, work.work);
	__static_key_slow_dec(&key->key, 0, NULL);
}

void static_key_slow_dec(struct static_key *key)
{
	STATIC_KEY_CHECK_USE(key);
	__static_key_slow_dec(key, 0, NULL);
}
EXPORT_SYMBOL_GPL(static_key_slow_dec);

void static_key_slow_dec_cpuslocked(struct static_key *key)
{
	STATIC_KEY_CHECK_USE(key);
	__static_key_slow_dec_cpuslocked(key, 0, NULL);
}

void static_key_slow_dec_deferred(struct static_key_deferred *key)
{
	STATIC_KEY_CHECK_USE(key);
	__static_key_slow_dec(&key->key, key->timeout, &key->work);
}
EXPORT_SYMBOL_GPL(static_key_slow_dec_deferred);

void static_key_deferred_flush(struct static_key_deferred *key)
{
	STATIC_KEY_CHECK_USE(key);
	flush_delayed_work(&key->work);
}
EXPORT_SYMBOL_GPL(static_key_deferred_flush);

void jump_label_rate_limit(struct static_key_deferred *key,
		unsigned long rl)
{
	STATIC_KEY_CHECK_USE(key);
	key->timeout = rl;
	INIT_DELAYED_WORK(&key->work, jump_label_update_timeout);
}
EXPORT_SYMBOL_GPL(jump_label_rate_limit);

static int addr_conflict(struct jump_entry *entry, void *start, void *end)
{
	if (jump_entry_code(entry) <= (unsigned long)end &&
	    jump_entry_code(entry) + JUMP_LABEL_NOP_SIZE > (unsigned long)start)
		return 1;

	return 0;
}

static int __jump_label_text_reserved(struct jump_entry *iter_start,
		struct jump_entry *iter_stop, void *start, void *end)
{
	struct jump_entry *iter;

	iter = iter_start;
	while (iter < iter_stop) {
		if (addr_conflict(iter, start, end))
			return 1;
		iter++;
	}

	return 0;
}

/*
 * Update code which is definitely not currently executing.
 * Architectures which need heavyweight synchronization to modify
 * running code can override this to make the non-live update case
 * cheaper.
 */
void __weak __init_or_module arch_jump_label_transform_static(struct jump_entry *entry,
					    enum jump_label_type type)
{
	arch_jump_label_transform(entry, type);
}

static inline struct jump_entry *static_key_entries(struct static_key *key)
{
	WARN_ON_ONCE(key->type & JUMP_TYPE_LINKED);
	return (struct jump_entry *)(key->type & ~JUMP_TYPE_MASK);
}

static inline bool static_key_type(struct static_key *key)
{
	return key->type & JUMP_TYPE_TRUE;
}

static inline bool static_key_linked(struct static_key *key)
{
	return key->type & JUMP_TYPE_LINKED;
}

static inline void static_key_clear_linked(struct static_key *key)
{
	key->type &= ~JUMP_TYPE_LINKED;
}

static inline void static_key_set_linked(struct static_key *key)
{
	key->type |= JUMP_TYPE_LINKED;
}

/***
 * A 'struct static_key' uses a union such that it either points directly
 * to a table of 'struct jump_entry' or to a linked list of modules which in
 * turn point to 'struct jump_entry' tables.
 *
 * The two lower bits of the pointer are used to keep track of which pointer
 * type is in use and to store the initial branch direction, we use an access
 * function which preserves these bits.
 */
static void static_key_set_entries(struct static_key *key,
				   struct jump_entry *entries)
{
	unsigned long type;

	WARN_ON_ONCE((unsigned long)entries & JUMP_TYPE_MASK);
	type = key->type & JUMP_TYPE_MASK;
	key->entries = entries;
	key->type |= type;
}

static enum jump_label_type jump_label_type(struct jump_entry *entry)
{
	struct static_key *key = jump_entry_key(entry);
	bool enabled = static_key_enabled(key);
	bool branch = jump_entry_is_branch(entry);

	/* See the comment in linux/jump_label.h */
	return enabled ^ branch;
}

static void __jump_label_update(struct static_key *key,
				struct jump_entry *entry,
				struct jump_entry *stop,
				bool init)
{
	for (; (entry < stop) && (jump_entry_key(entry) == key); entry++) {
		/*
		 * An entry->code of 0 indicates an entry which has been
		 * disabled because it was in an init text area.
		 */
		if (init || !jump_entry_is_init(entry)) {
			if (kernel_text_address(jump_entry_code(entry)))
				arch_jump_label_transform(entry, jump_label_type(entry));
			else
				WARN_ONCE(1, "can't patch jump_label at %pS",
					  (void *)jump_entry_code(entry));
		}
	}
}

void __init jump_label_init(void)
{
	struct jump_entry *iter_start = __start___jump_table;
	struct jump_entry *iter_stop = __stop___jump_table;
	struct static_key *key = NULL;
	struct jump_entry *iter;

	/*
	 * Since we are initializing the static_key.enabled field with
	 * with the 'raw' int values (to avoid pulling in atomic.h) in
	 * jump_label.h, let's make sure that is safe. There are only two
	 * cases to check since we initialize to 0 or 1.
	 */
	BUILD_BUG_ON((int)ATOMIC_INIT(0) != 0);
	BUILD_BUG_ON((int)ATOMIC_INIT(1) != 1);

	if (static_key_initialized)
		return;

	cpus_read_lock();
	jump_label_lock();
	jump_label_sort_entries(iter_start, iter_stop);

	for (iter = iter_start; iter < iter_stop; iter++) {
		struct static_key *iterk;

		/* rewrite NOPs */
		if (jump_label_type(iter) == JUMP_LABEL_NOP)
			arch_jump_label_transform_static(iter, JUMP_LABEL_NOP);

		if (init_section_contains((void *)jump_entry_code(iter), 1))
			jump_entry_set_init(iter);

		iterk = jump_entry_key(iter);
		if (iterk == key)
			continue;

		key = iterk;
		static_key_set_entries(key, iter);
	}
	static_key_initialized = true;
	jump_label_unlock();
	cpus_read_unlock();
}

#ifdef CONFIG_MODULES

static enum jump_label_type jump_label_init_type(struct jump_entry *entry)
{
	struct static_key *key = jump_entry_key(entry);
	bool type = static_key_type(key);
	bool branch = jump_entry_is_branch(entry);

	/* See the comment in linux/jump_label.h */
	return type ^ branch;
}

struct static_key_mod {
	struct static_key_mod *next;
	struct jump_entry *entries;
	struct module *mod;
};

static inline struct static_key_mod *static_key_mod(struct static_key *key)
{
	WARN_ON_ONCE(!static_key_linked(key));
	return (struct static_key_mod *)(key->type & ~JUMP_TYPE_MASK);
}

/***
 * key->type and key->next are the same via union.
 * This sets key->next and preserves the type bits.
 *
 * See additional comments above static_key_set_entries().
 */
static void static_key_set_mod(struct static_key *key,
			       struct static_key_mod *mod)
{
	unsigned long type;

	WARN_ON_ONCE((unsigned long)mod & JUMP_TYPE_MASK);
	type = key->type & JUMP_TYPE_MASK;
	key->next = mod;
	key->type |= type;
}

static int __jump_label_mod_text_reserved(void *start, void *end)
{
	struct module *mod;

	preempt_disable();
	mod = __module_text_address((unsigned long)start);
	WARN_ON_ONCE(__module_text_address((unsigned long)end) != mod);
	preempt_enable();

	if (!mod)
		return 0;


	return __jump_label_text_reserved(mod->jump_entries,
				mod->jump_entries + mod->num_jump_entries,
				start, end);
}

static void __jump_label_mod_update(struct static_key *key)
{
	struct static_key_mod *mod;

	for (mod = static_key_mod(key); mod; mod = mod->next) {
		struct jump_entry *stop;
		struct module *m;

		/*
		 * NULL if the static_key is defined in a module
		 * that does not use it
		 */
		if (!mod->entries)
			continue;

		m = mod->mod;
		if (!m)
			stop = __stop___jump_table;
		else
			stop = m->jump_entries + m->num_jump_entries;
		__jump_label_update(key, mod->entries, stop,
				    m && m->state == MODULE_STATE_COMING);
	}
}

/***
 * apply_jump_label_nops - patch module jump labels with arch_get_jump_label_nop()
 * @mod: module to patch
 *
 * Allow for run-time selection of the optimal nops. Before the module
 * loads patch these with arch_get_jump_label_nop(), which is specified by
 * the arch specific jump label code.
 */
void jump_label_apply_nops(struct module *mod)
{
	struct jump_entry *iter_start = mod->jump_entries;
	struct jump_entry *iter_stop = iter_start + mod->num_jump_entries;
	struct jump_entry *iter;

	/* if the module doesn't have jump label entries, just return */
	if (iter_start == iter_stop)
		return;

	for (iter = iter_start; iter < iter_stop; iter++) {
		/* Only write NOPs for arch_branch_static(). */
		if (jump_label_init_type(iter) == JUMP_LABEL_NOP)
			arch_jump_label_transform_static(iter, JUMP_LABEL_NOP);
	}
}

static int jump_label_add_module(struct module *mod)
{
	struct jump_entry *iter_start = mod->jump_entries;
	struct jump_entry *iter_stop = iter_start + mod->num_jump_entries;
	struct jump_entry *iter;
	struct static_key *key = NULL;
	struct static_key_mod *jlm, *jlm2;

	/* if the module doesn't have jump label entries, just return */
	if (iter_start == iter_stop)
		return 0;

	jump_label_sort_entries(iter_start, iter_stop);

	for (iter = iter_start; iter < iter_stop; iter++) {
		struct static_key *iterk;

		if (within_module_init(jump_entry_code(iter), mod))
			jump_entry_set_init(iter);

		iterk = jump_entry_key(iter);
		if (iterk == key)
			continue;

		key = iterk;
		if (within_module((unsigned long)key, mod)) {
			static_key_set_entries(key, iter);
			continue;
		}
		jlm = kzalloc(sizeof(struct static_key_mod), GFP_KERNEL);
		if (!jlm)
			return -ENOMEM;
		if (!static_key_linked(key)) {
			jlm2 = kzalloc(sizeof(struct static_key_mod),
				       GFP_KERNEL);
			if (!jlm2) {
				kfree(jlm);
				return -ENOMEM;
			}
			preempt_disable();
			jlm2->mod = __module_address((unsigned long)key);
			preempt_enable();
			jlm2->entries = static_key_entries(key);
			jlm2->next = NULL;
			static_key_set_mod(key, jlm2);
			static_key_set_linked(key);
		}
		jlm->mod = mod;
		jlm->entries = iter;
		jlm->next = static_key_mod(key);
		static_key_set_mod(key, jlm);
		static_key_set_linked(key);

		/* Only update if we've changed from our initial state */
		if (jump_label_type(iter) != jump_label_init_type(iter))
			__jump_label_update(key, iter, iter_stop, true);
	}

	return 0;
}

static void jump_label_del_module(struct module *mod)
{
	struct jump_entry *iter_start = mod->jump_entries;
	struct jump_entry *iter_stop = iter_start + mod->num_jump_entries;
	struct jump_entry *iter;
	struct static_key *key = NULL;
	struct static_key_mod *jlm, **prev;

	for (iter = iter_start; iter < iter_stop; iter++) {
		if (jump_entry_key(iter) == key)
			continue;

		key = jump_entry_key(iter);

		if (within_module((unsigned long)key, mod))
			continue;

		/* No memory during module load */
		if (WARN_ON(!static_key_linked(key)))
			continue;

		prev = &key->next;
		jlm = static_key_mod(key);

		while (jlm && jlm->mod != mod) {
			prev = &jlm->next;
			jlm = jlm->next;
		}

		/* No memory during module load */
		if (WARN_ON(!jlm))
			continue;

		if (prev == &key->next)
			static_key_set_mod(key, jlm->next);
		else
			*prev = jlm->next;

		kfree(jlm);

		jlm = static_key_mod(key);
		/* if only one etry is left, fold it back into the static_key */
		if (jlm->next == NULL) {
			static_key_set_entries(key, jlm->entries);
			static_key_clear_linked(key);
			kfree(jlm);
		}
	}
}

static int
jump_label_module_notify(struct notifier_block *self, unsigned long val,
			 void *data)
{
	struct module *mod = data;
	int ret = 0;

	cpus_read_lock();
	jump_label_lock();

	switch (val) {
	case MODULE_STATE_COMING:
		ret = jump_label_add_module(mod);
		if (ret) {
			WARN(1, "Failed to allocate memory: jump_label may not work properly.\n");
			jump_label_del_module(mod);
		}
		break;
	case MODULE_STATE_GOING:
		jump_label_del_module(mod);
		break;
	}

	jump_label_unlock();
	cpus_read_unlock();

	return notifier_from_errno(ret);
}

static struct notifier_block jump_label_module_nb = {
	.notifier_call = jump_label_module_notify,
	.priority = 1, /* higher than tracepoints */
};

static __init int jump_label_init_module(void)
{
	return register_module_notifier(&jump_label_module_nb);
}
early_initcall(jump_label_init_module);

#endif /* CONFIG_MODULES */

/***
 * jump_label_text_reserved - check if addr range is reserved
 * @start: start text addr
 * @end: end text addr
 *
 * checks if the text addr located between @start and @end
 * overlaps with any of the jump label patch addresses. Code
 * that wants to modify kernel text should first verify that
 * it does not overlap with any of the jump label addresses.
 * Caller must hold jump_label_mutex.
 *
 * returns 1 if there is an overlap, 0 otherwise
 */
int jump_label_text_reserved(void *start, void *end)
{
	int ret = __jump_label_text_reserved(__start___jump_table,
			__stop___jump_table, start, end);

	if (ret)
		return ret;

#ifdef CONFIG_MODULES
	ret = __jump_label_mod_text_reserved(start, end);
#endif
	return ret;
}

static void jump_label_update(struct static_key *key)
{
	struct jump_entry *stop = __stop___jump_table;
	struct jump_entry *entry;
#ifdef CONFIG_MODULES
	struct module *mod;

	if (static_key_linked(key)) {
		__jump_label_mod_update(key);
		return;
	}

	preempt_disable();
	mod = __module_address((unsigned long)key);
	if (mod)
		stop = mod->jump_entries + mod->num_jump_entries;
	preempt_enable();
#endif
	entry = static_key_entries(key);
	/* if there are no users, entry can be NULL */
	if (entry)
		__jump_label_update(key, entry, stop,
				    system_state < SYSTEM_RUNNING);
}

#ifdef CONFIG_STATIC_KEYS_SELFTEST
static DEFINE_STATIC_KEY_TRUE(sk_true);
static DEFINE_STATIC_KEY_FALSE(sk_false);

static __init int jump_label_test(void)
{
	int i;

	for (i = 0; i < 2; i++) {
		WARN_ON(static_key_enabled(&sk_true.key) != true);
		WARN_ON(static_key_enabled(&sk_false.key) != false);

		WARN_ON(!static_branch_likely(&sk_true));
		WARN_ON(!static_branch_unlikely(&sk_true));
		WARN_ON(static_branch_likely(&sk_false));
		WARN_ON(static_branch_unlikely(&sk_false));

		static_branch_disable(&sk_true);
		static_branch_enable(&sk_false);

		WARN_ON(static_key_enabled(&sk_true.key) == true);
		WARN_ON(static_key_enabled(&sk_false.key) == false);

		WARN_ON(static_branch_likely(&sk_true));
		WARN_ON(static_branch_unlikely(&sk_true));
		WARN_ON(!static_branch_likely(&sk_false));
		WARN_ON(!static_branch_unlikely(&sk_false));

		static_branch_enable(&sk_true);
		static_branch_disable(&sk_false);
	}

	return 0;
}
early_initcall(jump_label_test);
#endif /* STATIC_KEYS_SELFTEST */
