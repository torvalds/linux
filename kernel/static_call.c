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

static bool static_call_initialized;

#define STATIC_CALL_INIT 1UL

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


static inline struct static_call_key *static_call_key(const struct static_call_site *site)
{
	return (struct static_call_key *)
		(((long)site->key + (long)&site->key) & ~STATIC_CALL_INIT);
}

/* These assume the key is word-aligned. */
static inline bool static_call_is_init(struct static_call_site *site)
{
	return ((long)site->key + (long)&site->key) & STATIC_CALL_INIT;
}

static inline void static_call_set_init(struct static_call_site *site)
{
	site->key = ((long)static_call_key(site) | STATIC_CALL_INIT) -
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

void __static_call_update(struct static_call_key *key, void *tramp, void *func)
{
	struct static_call_site *site, *stop;
	struct static_call_mod *site_mod;

	cpus_read_lock();
	static_call_lock();

	if (key->func == func)
		goto done;

	key->func = func;

	arch_static_call_transform(NULL, tramp, func);

	/*
	 * If uninitialized, we'll not update the callsites, but they still
	 * point to the trampoline and we just patched that.
	 */
	if (WARN_ON_ONCE(!static_call_initialized))
		goto done;

	for (site_mod = key->mods; site_mod; site_mod = site_mod->next) {
		struct module *mod = site_mod->mod;

		if (!site_mod->sites) {
			/*
			 * This can happen if the static call key is defined in
			 * a module which doesn't use it.
			 */
			continue;
		}

		stop = __stop_static_call_sites;

#ifdef CONFIG_MODULES
		if (mod) {
			stop = mod->static_call_sites +
			       mod->num_static_call_sites;
		}
#endif

		for (site = site_mod->sites;
		     site < stop && static_call_key(site) == key; site++) {
			void *site_addr = static_call_addr(site);

			if (static_call_is_init(site)) {
				/*
				 * Don't write to call sites which were in
				 * initmem and have since been freed.
				 */
				if (!mod && system_state >= SYSTEM_RUNNING)
					continue;
				if (mod && !within_module_init((unsigned long)site_addr, mod))
					continue;
			}

			if (!kernel_text_address((unsigned long)site_addr)) {
				WARN_ONCE(1, "can't patch static call site at %pS",
					  site_addr);
				continue;
			}

			arch_static_call_transform(site_addr, NULL, func);
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

			site_mod = kzalloc(sizeof(*site_mod), GFP_KERNEL);
			if (!site_mod)
				return -ENOMEM;

			site_mod->mod = mod;
			site_mod->sites = site;
			site_mod->next = key->mods;
			key->mods = site_mod;
		}

		arch_static_call_transform(site_addr, NULL, key->func);
	}

	return 0;
}

#ifdef CONFIG_MODULES

static int static_call_add_module(struct module *mod)
{
	return __static_call_init(mod, mod->static_call_sites,
				  mod->static_call_sites + mod->num_static_call_sites);
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
			WARN(1, "Failed to allocate memory for static calls");
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

#endif /* CONFIG_MODULES */

static void __init static_call_init(void)
{
	int ret;

	if (static_call_initialized)
		return;

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

	static_call_initialized = true;

#ifdef CONFIG_MODULES
	register_module_notifier(&static_call_module_nb);
#endif
}
early_initcall(static_call_init);
