// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/static_call.h>
#include <linux/bug.h>
#include <linux/smp.h>
#include <linux/sort.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/processor.h>
#include <asm/sections.h>

extern struct static_call_site __start_static_call_sites[],
			       __stop_static_call_sites[];
extern struct static_call_tramp_key __start_static_call_tramp_key[],
				    __stop_static_call_tramp_key[];

int static_call_initialized;

/*
 * Must be called before early_initcall() to be effective.
 */
void static_call_force_reinit(void)
{
	if (WARN_ON_ONCE(!static_call_initialized))
		return;

	static_call_initialized++;
}

/* mutex to protect key modules/sites */
static DEFINE_MUTEX(static_call_mutex);

static void static_call_lock(void)
{
	mutex_lock(&static_call_mutex);
}

static void static_call_unlock(void)
{
	mutex_unlock(&static_call_mutex);
}

static inline void *static_call_addr(struct static_call_site *site)
{
	return (void *)((long)site->addr + (long)&site->addr);
}

static inline unsigned long __static_call_key(const struct static_call_site *site)
{
	return (long)site->key + (long)&site->key;
}

static inline struct static_call_key *static_call_key(const struct static_call_site *site)
{
	return (void *)(__static_call_key(site) & ~STATIC_CALL_SITE_FLAGS);
}

/* These assume the key is word-aligned. */
static inline bool static_call_is_init(struct static_call_site *site)
{
	return __static_call_key(site) & STATIC_CALL_SITE_INIT;
}

static inline bool static_call_is_tail(struct static_call_site *site)
{
	return __static_call_key(site) & STATIC_CALL_SITE_TAIL;
}

static inline void static_call_set_init(struct static_call_site *site)
{
	site->key = (__static_call_key(site) | STATIC_CALL_SITE_INIT) -
		    (long)&site->key;
}

static int static_call_site_cmp(const void *_a, const void *_b)
{
	const struct static_call_site *a = _a;
	const struct static_call_site *b = _b;
	const struct static_call_key *key_a = static_call_key(a);
	const struct static_call_key *key_b = static_call_key(b);

	if (key_a < key_b)
		return -1;

	if (key_a > key_b)
		return 1;

	return 0;
}

static void static_call_site_swap(void *_a, void *_b, int size)
{
	long delta = (unsigned long)_a - (unsigned long)_b;
	struct static_call_site *a = _a;
	struct static_call_site *b = _b;
	struct static_call_site tmp = *a;

	a->addr = b->addr  - delta;
	a->key  = b->key   - delta;

	b->addr = tmp.addr + delta;
	b->key  = tmp.key  + delta;
}

static inline void static_call_sort_entries(struct static_call_site *start,
					    struct static_call_site *stop)
{
	sort(start, stop - start, sizeof(struct static_call_site),
	     static_call_site_cmp, static_call_site_swap);
}

static inline bool static_call_key_has_mods(struct static_call_key *key)
{
	return !(key->type & 1);
}

static inline struct static_call_mod *static_call_key_next(struct static_call_key *key)
{
	if (!static_call_key_has_mods(key))
		return NULL;

	return key->mods;
}

static inline struct static_call_site *static_call_key_sites(struct static_call_key *key)
{
	if (static_call_key_has_mods(key))
		return NULL;

	return (struct static_call_site *)(key->type & ~1);
}

void __static_call_update(struct static_call_key *key, void *tramp, void *func)
{
	struct static_call_site *site, *stop;
	struct static_call_mod *site_mod, first;

	cpus_read_lock();
	static_call_lock();

	if (key->func == func)
		goto done;

	key->func = func;

	arch_static_call_transform(NULL, tramp, func, false);

	/*
	 * If uninitialized, we'll not update the callsites, but they still
	 * point to the trampoline and we just patched that.
	 */
	if (WARN_ON_ONCE(!static_call_initialized))
		goto done;

	first = (struct static_call_mod){
		.next = static_call_key_next(key),
		.mod = NULL,
		.sites = static_call_key_sites(key),
	};

	for (site_mod = &first; site_mod; site_mod = site_mod->next) {
		bool init = system_state < SYSTEM_RUNNING;
		struct module *mod = site_mod->mod;

		if (!site_mod->sites) {
			/*
			 * This can happen if the static call key is defined in
			 * a module which doesn't use it.
			 *
			 * It also happens in the has_mods case, where the
			 * 'first' entry has no sites associated with it.
			 */
			continue;
		}

		stop = __stop_static_call_sites;

		if (mod) {
#ifdef CONFIG_MODULES
			stop = mod->static_call_sites +
			       mod->num_static_call_sites;
			init = mod->state == MODULE_STATE_COMING;
#endif
		}

		for (site = site_mod->sites;
		     site < stop && static_call_key(site) == key; site++) {
			void *site_addr = static_call_addr(site);

			if (!init && static_call_is_init(site))
				continue;

			if (!kernel_text_address((unsigned long)site_addr)) {
				/*
				 * This skips patching built-in __exit, which
				 * is part of init_section_contains() but is
				 * not part of kernel_text_address().
				 *
				 * Skipping built-in __exit is fine since it
				 * will never be executed.
				 */
				WARN_ONCE(!static_call_is_init(site),
					  "can't patch static call site at %pS",
					  site_addr);
				continue;
			}

			arch_static_call_transform(site_addr, tramp, func,
						   static_call_is_tail(site));
		}
	}

done:
	static_call_unlock();
	cpus_read_unlock();
}
EXPORT_SYMBOL_GPL(__static_call_update);

static int __static_call_init(struct module *mod,
			      struct static_call_site *start,
			      struct static_call_site *stop)
{
	struct static_call_site *site;
	struct static_call_key *key, *prev_key = NULL;
	struct static_call_mod *site_mod;

	if (start == stop)
		return 0;

	static_call_sort_entries(start, stop);

	for (site = start; site < stop; site++) {
		void *site_addr = static_call_addr(site);

		if ((mod && within_module_init((unsigned long)site_addr, mod)) ||
		    (!mod && init_section_contains(site_addr, 1)))
			static_call_set_init(site);

		key = static_call_key(site);
		if (key != prev_key) {
			prev_key = key;

			/*
			 * For vmlinux (!mod) avoid the allocation by storing
			 * the sites pointer in the key itself. Also see
			 * __static_call_update()'s @first.
			 *
			 * This allows architectures (eg. x86) to call
			 * static_call_init() before memory allocation works.
			 */
			if (!mod) {
				key->sites = site;
				key->type |= 1;
				goto do_transform;
			}

			site_mod = kzalloc(sizeof(*site_mod), GFP_KERNEL);
			if (!site_mod)
				return -ENOMEM;

			/*
			 * When the key has a direct sites pointer, extract
			 * that into an explicit struct static_call_mod, so we
			 * can have a list of modules.
			 */
			if (static_call_key_sites(key)) {
				site_mod->mod = NULL;
				site_mod->next = NULL;
				site_mod->sites = static_call_key_sites(key);

				key->mods = site_mod;

				site_mod = kzalloc(sizeof(*site_mod), GFP_KERNEL);
				if (!site_mod)
					return -ENOMEM;
			}

			site_mod->mod = mod;
			site_mod->sites = site;
			site_mod->next = static_call_key_next(key);
			key->mods = site_mod;
		}

do_transform:
		arch_static_call_transform(site_addr, NULL, key->func,
				static_call_is_tail(site));
	}

	return 0;
}

static int addr_conflict(struct static_call_site *site, void *start, void *end)
{
	unsigned long addr = (unsigned long)static_call_addr(site);

	if (addr <= (unsigned long)end &&
	    addr + CALL_INSN_SIZE > (unsigned long)start)
		return 1;

	return 0;
}

static int __static_call_text_reserved(struct static_call_site *iter_start,
				       struct static_call_site *iter_stop,
				       void *start, void *end, bool init)
{
	struct static_call_site *iter = iter_start;

	while (iter < iter_stop) {
		if (init || !static_call_is_init(iter)) {
			if (addr_conflict(iter, start, end))
				return 1;
		}
		iter++;
	}

	return 0;
}

#ifdef CONFIG_MODULES

static int __static_call_mod_text_reserved(void *start, void *end)
{
	struct module *mod;
	int ret;

	scoped_guard(rcu) {
		mod = __module_text_address((unsigned long)start);
		WARN_ON_ONCE(__module_text_address((unsigned long)end) != mod);
		if (!try_module_get(mod))
			mod = NULL;
	}
	if (!mod)
		return 0;

	ret = __static_call_text_reserved(mod->static_call_sites,
			mod->static_call_sites + mod->num_static_call_sites,
			start, end, mod->state == MODULE_STATE_COMING);

	module_put(mod);

	return ret;
}

static unsigned long tramp_key_lookup(unsigned long addr)
{
	struct static_call_tramp_key *start = __start_static_call_tramp_key;
	struct static_call_tramp_key *stop = __stop_static_call_tramp_key;
	struct static_call_tramp_key *tramp_key;

	for (tramp_key = start; tramp_key != stop; tramp_key++) {
		unsigned long tramp;

		tramp = (long)tramp_key->tramp + (long)&tramp_key->tramp;
		if (tramp == addr)
			return (long)tramp_key->key + (long)&tramp_key->key;
	}

	return 0;
}

static int static_call_add_module(struct module *mod)
{
	struct static_call_site *start = mod->static_call_sites;
	struct static_call_site *stop = start + mod->num_static_call_sites;
	struct static_call_site *site;

	for (site = start; site != stop; site++) {
		unsigned long s_key = __static_call_key(site);
		unsigned long addr = s_key & ~STATIC_CALL_SITE_FLAGS;
		unsigned long key;

		/*
		 * Is the key is exported, 'addr' points to the key, which
		 * means modules are allowed to call static_call_update() on
		 * it.
		 *
		 * Otherwise, the key isn't exported, and 'addr' points to the
		 * trampoline so we need to lookup the key.
		 *
		 * We go through this dance to prevent crazy modules from
		 * abusing sensitive static calls.
		 */
		if (!kernel_text_address(addr))
			continue;

		key = tramp_key_lookup(addr);
		if (!key) {
			pr_warn("Failed to fixup __raw_static_call() usage at: %ps\n",
				static_call_addr(site));
			return -EINVAL;
		}

		key |= s_key & STATIC_CALL_SITE_FLAGS;
		site->key = key - (long)&site->key;
	}

	return __static_call_init(mod, start, stop);
}

static void static_call_del_module(struct module *mod)
{
	struct static_call_site *start = mod->static_call_sites;
	struct static_call_site *stop = mod->static_call_sites +
					mod->num_static_call_sites;
	struct static_call_key *key, *prev_key = NULL;
	struct static_call_mod *site_mod, **prev;
	struct static_call_site *site;

	for (site = start; site < stop; site++) {
		key = static_call_key(site);

		/*
		 * If the key was not updated due to a memory allocation
		 * failure in __static_call_init() then treating key::sites
		 * as key::mods in the code below would cause random memory
		 * access and #GP. In that case all subsequent sites have
		 * not been touched either, so stop iterating.
		 */
		if (!static_call_key_has_mods(key))
			break;

		if (key == prev_key)
			continue;

		prev_key = key;

		for (prev = &key->mods, site_mod = key->mods;
		     site_mod && site_mod->mod != mod;
		     prev = &site_mod->next, site_mod = site_mod->next)
			;

		if (!site_mod)
			continue;

		*prev = site_mod->next;
		kfree(site_mod);
	}
}

static int static_call_module_notify(struct notifier_block *nb,
				     unsigned long val, void *data)
{
	struct module *mod = data;
	int ret = 0;

	cpus_read_lock();
	static_call_lock();

	switch (val) {
	case MODULE_STATE_COMING:
		ret = static_call_add_module(mod);
		if (ret) {
			pr_warn("Failed to allocate memory for static calls\n");
			static_call_del_module(mod);
		}
		break;
	case MODULE_STATE_GOING:
		static_call_del_module(mod);
		break;
	}

	static_call_unlock();
	cpus_read_unlock();

	return notifier_from_errno(ret);
}

static struct notifier_block static_call_module_nb = {
	.notifier_call = static_call_module_notify,
};

#else

static inline int __static_call_mod_text_reserved(void *start, void *end)
{
	return 0;
}

#endif /* CONFIG_MODULES */

int static_call_text_reserved(void *start, void *end)
{
	bool init = system_state < SYSTEM_RUNNING;
	int ret = __static_call_text_reserved(__start_static_call_sites,
			__stop_static_call_sites, start, end, init);

	if (ret)
		return ret;

	return __static_call_mod_text_reserved(start, end);
}

int __init static_call_init(void)
{
	int ret;

	/* See static_call_force_reinit(). */
	if (static_call_initialized == 1)
		return 0;

	cpus_read_lock();
	static_call_lock();
	ret = __static_call_init(NULL, __start_static_call_sites,
				 __stop_static_call_sites);
	static_call_unlock();
	cpus_read_unlock();

	if (ret) {
		pr_err("Failed to allocate memory for static_call!\n");
		BUG();
	}

#ifdef CONFIG_MODULES
	if (!static_call_initialized)
		register_module_notifier(&static_call_module_nb);
#endif

	static_call_initialized = 1;
	return 0;
}
early_initcall(static_call_init);

#ifdef CONFIG_STATIC_CALL_SELFTEST

static int func_a(int x)
{
	return x+1;
}

static int func_b(int x)
{
	return x+2;
}

DEFINE_STATIC_CALL(sc_selftest, func_a);

static struct static_call_data {
      int (*func)(int);
      int val;
      int expect;
} static_call_data [] __initdata = {
      { NULL,   2, 3 },
      { func_b, 2, 4 },
      { func_a, 2, 3 }
};

static int __init test_static_call_init(void)
{
      int i;

      for (i = 0; i < ARRAY_SIZE(static_call_data); i++ ) {
	      struct static_call_data *scd = &static_call_data[i];

              if (scd->func)
                      static_call_update(sc_selftest, scd->func);

              WARN_ON(static_call(sc_selftest)(scd->val) != scd->expect);
      }

      return 0;
}
early_initcall(test_static_call_init);

#endif /* CONFIG_STATIC_CALL_SELFTEST */
