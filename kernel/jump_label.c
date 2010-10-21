/*
 * jump label support
 *
 * Copyright (C) 2009 Jason Baron <jbaron@redhat.com>
 *
 */
#include <linux/jump_label.h>
#include <linux/memory.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/jhash.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/err.h>

#ifdef HAVE_JUMP_LABEL

#define JUMP_LABEL_HASH_BITS 6
#define JUMP_LABEL_TABLE_SIZE (1 << JUMP_LABEL_HASH_BITS)
static struct hlist_head jump_label_table[JUMP_LABEL_TABLE_SIZE];

/* mutex to protect coming/going of the the jump_label table */
static DEFINE_MUTEX(jump_label_mutex);

struct jump_label_entry {
	struct hlist_node hlist;
	struct jump_entry *table;
	int nr_entries;
	/* hang modules off here */
	struct hlist_head modules;
	unsigned long key;
};

struct jump_label_module_entry {
	struct hlist_node hlist;
	struct jump_entry *table;
	int nr_entries;
	struct module *mod;
};

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
sort_jump_label_entries(struct jump_entry *start, struct jump_entry *stop)
{
	unsigned long size;

	size = (((unsigned long)stop - (unsigned long)start)
					/ sizeof(struct jump_entry));
	sort(start, size, sizeof(struct jump_entry), jump_label_cmp, NULL);
}

static struct jump_label_entry *get_jump_label_entry(jump_label_t key)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct jump_label_entry *e;
	u32 hash = jhash((void *)&key, sizeof(jump_label_t), 0);

	head = &jump_label_table[hash & (JUMP_LABEL_TABLE_SIZE - 1)];
	hlist_for_each_entry(e, node, head, hlist) {
		if (key == e->key)
			return e;
	}
	return NULL;
}

static struct jump_label_entry *
add_jump_label_entry(jump_label_t key, int nr_entries, struct jump_entry *table)
{
	struct hlist_head *head;
	struct jump_label_entry *e;
	u32 hash;

	e = get_jump_label_entry(key);
	if (e)
		return ERR_PTR(-EEXIST);

	e = kmalloc(sizeof(struct jump_label_entry), GFP_KERNEL);
	if (!e)
		return ERR_PTR(-ENOMEM);

	hash = jhash((void *)&key, sizeof(jump_label_t), 0);
	head = &jump_label_table[hash & (JUMP_LABEL_TABLE_SIZE - 1)];
	e->key = key;
	e->table = table;
	e->nr_entries = nr_entries;
	INIT_HLIST_HEAD(&(e->modules));
	hlist_add_head(&e->hlist, head);
	return e;
}

static int
build_jump_label_hashtable(struct jump_entry *start, struct jump_entry *stop)
{
	struct jump_entry *iter, *iter_begin;
	struct jump_label_entry *entry;
	int count;

	sort_jump_label_entries(start, stop);
	iter = start;
	while (iter < stop) {
		entry = get_jump_label_entry(iter->key);
		if (!entry) {
			iter_begin = iter;
			count = 0;
			while ((iter < stop) &&
				(iter->key == iter_begin->key)) {
				iter++;
				count++;
			}
			entry = add_jump_label_entry(iter_begin->key,
							count, iter_begin);
			if (IS_ERR(entry))
				return PTR_ERR(entry);
		 } else {
			WARN_ONCE(1, KERN_ERR "build_jump_hashtable: unexpected entry!\n");
			return -1;
		}
	}
	return 0;
}

/***
 * jump_label_update - update jump label text
 * @key -  key value associated with a a jump label
 * @type - enum set to JUMP_LABEL_ENABLE or JUMP_LABEL_DISABLE
 *
 * Will enable/disable the jump for jump label @key, depending on the
 * value of @type.
 *
 */

void jump_label_update(unsigned long key, enum jump_label_type type)
{
	struct jump_entry *iter;
	struct jump_label_entry *entry;
	struct hlist_node *module_node;
	struct jump_label_module_entry *e_module;
	int count;

	mutex_lock(&jump_label_mutex);
	entry = get_jump_label_entry((jump_label_t)key);
	if (entry) {
		count = entry->nr_entries;
		iter = entry->table;
		while (count--) {
			if (kernel_text_address(iter->code))
				arch_jump_label_transform(iter, type);
			iter++;
		}
		/* eanble/disable jump labels in modules */
		hlist_for_each_entry(e_module, module_node, &(entry->modules),
							hlist) {
			count = e_module->nr_entries;
			iter = e_module->table;
			while (count--) {
				if (kernel_text_address(iter->code))
					arch_jump_label_transform(iter, type);
				iter++;
			}
		}
	}
	mutex_unlock(&jump_label_mutex);
}

static int addr_conflict(struct jump_entry *entry, void *start, void *end)
{
	if (entry->code <= (unsigned long)end &&
		entry->code + JUMP_LABEL_NOP_SIZE > (unsigned long)start)
		return 1;

	return 0;
}

#ifdef CONFIG_MODULES

static int module_conflict(void *start, void *end)
{
	struct hlist_head *head;
	struct hlist_node *node, *node_next, *module_node, *module_node_next;
	struct jump_label_entry *e;
	struct jump_label_module_entry *e_module;
	struct jump_entry *iter;
	int i, count;
	int conflict = 0;

	for (i = 0; i < JUMP_LABEL_TABLE_SIZE; i++) {
		head = &jump_label_table[i];
		hlist_for_each_entry_safe(e, node, node_next, head, hlist) {
			hlist_for_each_entry_safe(e_module, module_node,
							module_node_next,
							&(e->modules), hlist) {
				count = e_module->nr_entries;
				iter = e_module->table;
				while (count--) {
					if (addr_conflict(iter, start, end)) {
						conflict = 1;
						goto out;
					}
					iter++;
				}
			}
		}
	}
out:
	return conflict;
}

#endif

/***
 * jump_label_text_reserved - check if addr range is reserved
 * @start: start text addr
 * @end: end text addr
 *
 * checks if the text addr located between @start and @end
 * overlaps with any of the jump label patch addresses. Code
 * that wants to modify kernel text should first verify that
 * it does not overlap with any of the jump label addresses.
 *
 * returns 1 if there is an overlap, 0 otherwise
 */
int jump_label_text_reserved(void *start, void *end)
{
	struct jump_entry *iter;
	struct jump_entry *iter_start = __start___jump_table;
	struct jump_entry *iter_stop = __start___jump_table;
	int conflict = 0;

	mutex_lock(&jump_label_mutex);
	iter = iter_start;
	while (iter < iter_stop) {
		if (addr_conflict(iter, start, end)) {
			conflict = 1;
			goto out;
		}
		iter++;
	}

	/* now check modules */
#ifdef CONFIG_MODULES
	conflict = module_conflict(start, end);
#endif
out:
	mutex_unlock(&jump_label_mutex);
	return conflict;
}

static __init int init_jump_label(void)
{
	int ret;
	struct jump_entry *iter_start = __start___jump_table;
	struct jump_entry *iter_stop = __stop___jump_table;
	struct jump_entry *iter;

	mutex_lock(&jump_label_mutex);
	ret = build_jump_label_hashtable(__start___jump_table,
					 __stop___jump_table);
	iter = iter_start;
	while (iter < iter_stop) {
		arch_jump_label_text_poke_early(iter->code);
		iter++;
	}
	mutex_unlock(&jump_label_mutex);
	return ret;
}
early_initcall(init_jump_label);

#ifdef CONFIG_MODULES

static struct jump_label_module_entry *
add_jump_label_module_entry(struct jump_label_entry *entry,
			    struct jump_entry *iter_begin,
			    int count, struct module *mod)
{
	struct jump_label_module_entry *e;

	e = kmalloc(sizeof(struct jump_label_module_entry), GFP_KERNEL);
	if (!e)
		return ERR_PTR(-ENOMEM);
	e->mod = mod;
	e->nr_entries = count;
	e->table = iter_begin;
	hlist_add_head(&e->hlist, &entry->modules);
	return e;
}

static int add_jump_label_module(struct module *mod)
{
	struct jump_entry *iter, *iter_begin;
	struct jump_label_entry *entry;
	struct jump_label_module_entry *module_entry;
	int count;

	/* if the module doesn't have jump label entries, just return */
	if (!mod->num_jump_entries)
		return 0;

	sort_jump_label_entries(mod->jump_entries,
				mod->jump_entries + mod->num_jump_entries);
	iter = mod->jump_entries;
	while (iter < mod->jump_entries + mod->num_jump_entries) {
		entry = get_jump_label_entry(iter->key);
		iter_begin = iter;
		count = 0;
		while ((iter < mod->jump_entries + mod->num_jump_entries) &&
			(iter->key == iter_begin->key)) {
				iter++;
				count++;
		}
		if (!entry) {
			entry = add_jump_label_entry(iter_begin->key, 0, NULL);
			if (IS_ERR(entry))
				return PTR_ERR(entry);
		}
		module_entry = add_jump_label_module_entry(entry, iter_begin,
							   count, mod);
		if (IS_ERR(module_entry))
			return PTR_ERR(module_entry);
	}
	return 0;
}

static void remove_jump_label_module(struct module *mod)
{
	struct hlist_head *head;
	struct hlist_node *node, *node_next, *module_node, *module_node_next;
	struct jump_label_entry *e;
	struct jump_label_module_entry *e_module;
	int i;

	/* if the module doesn't have jump label entries, just return */
	if (!mod->num_jump_entries)
		return;

	for (i = 0; i < JUMP_LABEL_TABLE_SIZE; i++) {
		head = &jump_label_table[i];
		hlist_for_each_entry_safe(e, node, node_next, head, hlist) {
			hlist_for_each_entry_safe(e_module, module_node,
						  module_node_next,
						  &(e->modules), hlist) {
				if (e_module->mod == mod) {
					hlist_del(&e_module->hlist);
					kfree(e_module);
				}
			}
			if (hlist_empty(&e->modules) && (e->nr_entries == 0)) {
				hlist_del(&e->hlist);
				kfree(e);
			}
		}
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
		mutex_lock(&jump_label_mutex);
		ret = add_jump_label_module(mod);
		if (ret)
			remove_jump_label_module(mod);
		mutex_unlock(&jump_label_mutex);
		break;
	case MODULE_STATE_GOING:
		mutex_lock(&jump_label_mutex);
		remove_jump_label_module(mod);
		mutex_unlock(&jump_label_mutex);
		break;
	}
	return ret;
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
	struct jump_entry *iter;

	/* if the module doesn't have jump label entries, just return */
	if (!mod->num_jump_entries)
		return;

	iter = mod->jump_entries;
	while (iter < mod->jump_entries + mod->num_jump_entries) {
		arch_jump_label_text_poke_early(iter->code);
		iter++;
	}
}

struct notifier_block jump_label_module_nb = {
	.notifier_call = jump_label_module_notify,
	.priority = 0,
};

static __init int init_jump_label_module(void)
{
	return register_module_notifier(&jump_label_module_nb);
}
early_initcall(init_jump_label_module);

#endif /* CONFIG_MODULES */

#endif
