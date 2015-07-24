/*
 * jump label support
 *
 * Copyright (C) 2009 Jason Baron <jbaron@redhat.com>
 * Copyright (C) 2011 Peter Zijlstra <pzijlstr@redhat.com>
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

#ifdef HAVE_JUMP_LABEL

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

	if (jea->key < jeb->key)
		return -1;

	if (jea->key > jeb->key)
		return 1;

	return 0;
}

static void
jump_label_sort_entries(struct jump_entry *start, struct jump_entry *stop)
{
	unsigned long size;

	size = (((unsigned long)stop - (unsigned long)start)
					/ sizeof(struct jump_entry));
	sort(start, size, sizeof(struct jump_entry), jump_label_cmp, NULL);
}

static void jump_label_update(struct static_key *key);

void static_key_slow_inc(struct static_key *key)
{
	STATIC_KEY_CHECK_USE();
	if (atomic_inc_not_zero(&key->enabled))
		return;

	jump_label_lock();
	if (atomic_inc_return(&key->enabled) == 1)
		jump_label_update(key);
	jump_label_unlock();
}
EXPORT_SYMBOL_GPL(static_key_slow_inc);

static void __static_key_slow_dec(struct static_key *key,
		unsigned long rate_limit, struct delayed_work *work)
{
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

static void jump_label_update_timeout(struct work_struct *work)
{
	struct static_key_deferred *key =
		container_of(work, struct static_key_deferred, work.work);
	__static_key_slow_dec(&key->key, 0, NULL);
}

void static_key_slow_dec(struct static_key *key)
{
	STATIC_KEY_CHECK_USE();
	__static_key_slow_dec(key, 0, NULL);
}
EXPORT_SYMBOL_GPL(static_key_slow_dec);

void static_key_slow_dec_deferred(struct static_key_deferred *key)
{
	STATIC_KEY_CHECK_USE();
	__static_key_slow_dec(&key->key, key->timeout, &key->work);
}
EXPORT_SYMBOL_GPL(static_key_slow_dec_deferred);

void jump_label_rate_limit(struct static_key_deferred *key,
		unsigned long rl)
{
	STATIC_KEY_CHECK_USE();
	key->timeout = rl;
	INIT_DELAYED_WORK(&key->work, jump_label_update_timeout);
}
EXPORT_SYMBOL_GPL(jump_label_rate_limit);

static int addr_conflict(struct jump_entry *entry, void *start, void *end)
{
	if (entry->code <= (unsigned long)end &&
		entry->code + JUMP_LABEL_NOP_SIZE > (unsigned long)start)
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
	return (struct jump_entry *)((unsigned long)key->entries & ~JUMP_TYPE_MASK);
}

static inline bool static_key_type(struct static_key *key)
{
	return (unsigned long)key->entries & JUMP_TYPE_MASK;
}

static inline struct static_key *jump_entry_key(struct jump_entry *entry)
{
	return (struct static_key *)((unsigned long)entry->key);
}

static enum jump_label_type jump_label_type(struct jump_entry *entry)
{
	struct static_key *key = jump_entry_key(entry);
	bool enabled = static_key_enabled(key);
	bool type = static_key_type(key);

	return enabled ^ type;
}

static void __jump_label_update(struct static_key *key,
				struct jump_entry *entry,
				struct jump_entry *stop)
{
	for (; (entry < stop) && (jump_entry_key(entry) == key); entry++) {
		/*
		 * entry->code set to 0 invalidates module init text sections
		 * kernel_text_address() verifies we are not in core kernel
		 * init code, see jump_label_invalidate_module_init().
		 */
		if (entry->code && kernel_text_address(entry->code))
			arch_jump_label_transform(entry, jump_label_type(entry));
	}
}

void __init jump_label_init(void)
{
	struct jump_entry *iter_start = __start___jump_table;
	struct jump_entry *iter_stop = __stop___jump_table;
	struct static_key *key = NULL;
	struct jump_entry *iter;

	jump_label_lock();
	jump_label_sort_entries(iter_start, iter_stop);

	for (iter = iter_start; iter < iter_stop; iter++) {
		struct static_key *iterk;

		arch_jump_label_transform_static(iter, jump_label_type(iter));
		iterk = jump_entry_key(iter);
		if (iterk == key)
			continue;

		key = iterk;
		/*
		 * Set key->entries to iter, but preserve JUMP_LABEL_TRUE_BRANCH.
		 */
		*((unsigned long *)&key->entries) += (unsigned long)iter;
#ifdef CONFIG_MODULES
		key->next = NULL;
#endif
	}
	static_key_initialized = true;
	jump_label_unlock();
}

#ifdef CONFIG_MODULES

struct static_key_mod {
	struct static_key_mod *next;
	struct jump_entry *entries;
	struct module *mod;
};

static int __jump_label_mod_text_reserved(void *start, void *end)
{
	struct module *mod;

	mod = __module_text_address((unsigned long)start);
	if (!mod)
		return 0;

	WARN_ON_ONCE(__module_text_address((unsigned long)end) != mod);

	return __jump_label_text_reserved(mod->jump_entries,
				mod->jump_entries + mod->num_jump_entries,
				start, end);
}

static void __jump_label_mod_update(struct static_key *key)
{
	struct static_key_mod *mod;

	for (mod = key->next; mod; mod = mod->next) {
		struct module *m = mod->mod;

		__jump_label_update(key, mod->entries,
				    m->jump_entries + m->num_jump_entries);
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

	for (iter = iter_start; iter < iter_stop; iter++)
		arch_jump_label_transform_static(iter, JUMP_LABEL_NOP);
}

static int jump_label_add_module(struct module *mod)
{
	struct jump_entry *iter_start = mod->jump_entries;
	struct jump_entry *iter_stop = iter_start + mod->num_jump_entries;
	struct jump_entry *iter;
	struct static_key *key = NULL;
	struct static_key_mod *jlm;

	/* if the module doesn't have jump label entries, just return */
	if (iter_start == iter_stop)
		return 0;

	jump_label_sort_entries(iter_start, iter_stop);

	for (iter = iter_start; iter < iter_stop; iter++) {
		struct static_key *iterk;

		iterk = jump_entry_key(iter);
		if (iterk == key)
			continue;

		key = iterk;
		if (within_module(iter->key, mod)) {
			/*
			 * Set key->entries to iter, but preserve JUMP_LABEL_TRUE_BRANCH.
			 */
			*((unsigned long *)&key->entries) += (unsigned long)iter;
			key->next = NULL;
			continue;
		}
		jlm = kzalloc(sizeof(struct static_key_mod), GFP_KERNEL);
		if (!jlm)
			return -ENOMEM;
		jlm->mod = mod;
		jlm->entries = iter;
		jlm->next = key->next;
		key->next = jlm;

		if (jump_label_type(iter) == JUMP_LABEL_JMP)
			__jump_label_update(key, iter, iter_stop);
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

		if (within_module(iter->key, mod))
			continue;

		prev = &key->next;
		jlm = key->next;

		while (jlm && jlm->mod != mod) {
			prev = &jlm->next;
			jlm = jlm->next;
		}

		if (jlm) {
			*prev = jlm->next;
			kfree(jlm);
		}
	}
}

static void jump_label_invalidate_module_init(struct module *mod)
{
	struct jump_entry *iter_start = mod->jump_entries;
	struct jump_entry *iter_stop = iter_start + mod->num_jump_entries;
	struct jump_entry *iter;

	for (iter = iter_start; iter < iter_stop; iter++) {
		if (within_module_init(iter->code, mod))
			iter->code = 0;
	}
}

static int
jump_label_module_notify(struct notifier_block *self, unsigned long val,
			 void *data)
{
	struct module *mod = data;
	int ret = 0;

	switch (val) {
	case MODULE_STATE_COMING:
		jump_label_lock();
		ret = jump_label_add_module(mod);
		if (ret)
			jump_label_del_module(mod);
		jump_label_unlock();
		break;
	case MODULE_STATE_GOING:
		jump_label_lock();
		jump_label_del_module(mod);
		jump_label_unlock();
		break;
	case MODULE_STATE_LIVE:
		jump_label_lock();
		jump_label_invalidate_module_init(mod);
		jump_label_unlock();
		break;
	}

	return notifier_from_errno(ret);
}

struct notifier_block jump_label_module_nb = {
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
	struct jump_entry *entry = static_key_entries(key);
#ifdef CONFIG_MODULES
	struct module *mod;

	__jump_label_mod_update(key);

	preempt_disable();
	mod = __module_address((unsigned long)key);
	if (mod)
		stop = mod->jump_entries + mod->num_jump_entries;
	preempt_enable();
#endif
	/* if there are no users, entry can be NULL */
	if (entry)
		__jump_label_update(key, entry, stop);
}

#endif
