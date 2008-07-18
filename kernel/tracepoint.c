/*
 * Copyright (C) 2008 Mathieu Desnoyers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/jhash.h>
#include <linux/list.h>
#include <linux/rcupdate.h>
#include <linux/tracepoint.h>
#include <linux/err.h>
#include <linux/slab.h>

extern struct tracepoint __start___tracepoints[];
extern struct tracepoint __stop___tracepoints[];

/* Set to 1 to enable tracepoint debug output */
static const int tracepoint_debug;

/*
 * tracepoints_mutex nests inside module_mutex. Tracepoints mutex protects the
 * builtin and module tracepoints and the hash table.
 */
static DEFINE_MUTEX(tracepoints_mutex);

/*
 * Tracepoint hash table, containing the active tracepoints.
 * Protected by tracepoints_mutex.
 */
#define TRACEPOINT_HASH_BITS 6
#define TRACEPOINT_TABLE_SIZE (1 << TRACEPOINT_HASH_BITS)

/*
 * Note about RCU :
 * It is used to to delay the free of multiple probes array until a quiescent
 * state is reached.
 * Tracepoint entries modifications are protected by the tracepoints_mutex.
 */
struct tracepoint_entry {
	struct hlist_node hlist;
	void **funcs;
	int refcount;	/* Number of times armed. 0 if disarmed. */
	struct rcu_head rcu;
	void *oldptr;
	unsigned char rcu_pending:1;
	char name[0];
};

static struct hlist_head tracepoint_table[TRACEPOINT_TABLE_SIZE];

static void free_old_closure(struct rcu_head *head)
{
	struct tracepoint_entry *entry = container_of(head,
		struct tracepoint_entry, rcu);
	kfree(entry->oldptr);
	/* Make sure we free the data before setting the pending flag to 0 */
	smp_wmb();
	entry->rcu_pending = 0;
}

static void tracepoint_entry_free_old(struct tracepoint_entry *entry, void *old)
{
	if (!old)
		return;
	entry->oldptr = old;
	entry->rcu_pending = 1;
	/* write rcu_pending before calling the RCU callback */
	smp_wmb();
#ifdef CONFIG_PREEMPT_RCU
	synchronize_sched();	/* Until we have the call_rcu_sched() */
#endif
	call_rcu(&entry->rcu, free_old_closure);
}

static void debug_print_probes(struct tracepoint_entry *entry)
{
	int i;

	if (!tracepoint_debug)
		return;

	for (i = 0; entry->funcs[i]; i++)
		printk(KERN_DEBUG "Probe %d : %p\n", i, entry->funcs[i]);
}

static void *
tracepoint_entry_add_probe(struct tracepoint_entry *entry, void *probe)
{
	int nr_probes = 0;
	void **old, **new;

	WARN_ON(!probe);

	debug_print_probes(entry);
	old = entry->funcs;
	if (old) {
		/* (N -> N+1), (N != 0, 1) probes */
		for (nr_probes = 0; old[nr_probes]; nr_probes++)
			if (old[nr_probes] == probe)
				return ERR_PTR(-EEXIST);
	}
	/* + 2 : one for new probe, one for NULL func */
	new = kzalloc((nr_probes + 2) * sizeof(void *), GFP_KERNEL);
	if (new == NULL)
		return ERR_PTR(-ENOMEM);
	if (old)
		memcpy(new, old, nr_probes * sizeof(void *));
	new[nr_probes] = probe;
	entry->refcount = nr_probes + 1;
	entry->funcs = new;
	debug_print_probes(entry);
	return old;
}

static void *
tracepoint_entry_remove_probe(struct tracepoint_entry *entry, void *probe)
{
	int nr_probes = 0, nr_del = 0, i;
	void **old, **new;

	old = entry->funcs;

	debug_print_probes(entry);
	/* (N -> M), (N > 1, M >= 0) probes */
	for (nr_probes = 0; old[nr_probes]; nr_probes++) {
		if ((!probe || old[nr_probes] == probe))
			nr_del++;
	}

	if (nr_probes - nr_del == 0) {
		/* N -> 0, (N > 1) */
		entry->funcs = NULL;
		entry->refcount = 0;
		debug_print_probes(entry);
		return old;
	} else {
		int j = 0;
		/* N -> M, (N > 1, M > 0) */
		/* + 1 for NULL */
		new = kzalloc((nr_probes - nr_del + 1)
			* sizeof(void *), GFP_KERNEL);
		if (new == NULL)
			return ERR_PTR(-ENOMEM);
		for (i = 0; old[i]; i++)
			if ((probe && old[i] != probe))
				new[j++] = old[i];
		entry->refcount = nr_probes - nr_del;
		entry->funcs = new;
	}
	debug_print_probes(entry);
	return old;
}

/*
 * Get tracepoint if the tracepoint is present in the tracepoint hash table.
 * Must be called with tracepoints_mutex held.
 * Returns NULL if not present.
 */
static struct tracepoint_entry *get_tracepoint(const char *name)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct tracepoint_entry *e;
	u32 hash = jhash(name, strlen(name), 0);

	head = &tracepoint_table[hash & ((1 << TRACEPOINT_HASH_BITS)-1)];
	hlist_for_each_entry(e, node, head, hlist) {
		if (!strcmp(name, e->name))
			return e;
	}
	return NULL;
}

/*
 * Add the tracepoint to the tracepoint hash table. Must be called with
 * tracepoints_mutex held.
 */
static struct tracepoint_entry *add_tracepoint(const char *name)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct tracepoint_entry *e;
	size_t name_len = strlen(name) + 1;
	u32 hash = jhash(name, name_len-1, 0);

	head = &tracepoint_table[hash & ((1 << TRACEPOINT_HASH_BITS)-1)];
	hlist_for_each_entry(e, node, head, hlist) {
		if (!strcmp(name, e->name)) {
			printk(KERN_NOTICE
				"tracepoint %s busy\n", name);
			return ERR_PTR(-EEXIST);	/* Already there */
		}
	}
	/*
	 * Using kmalloc here to allocate a variable length element. Could
	 * cause some memory fragmentation if overused.
	 */
	e = kmalloc(sizeof(struct tracepoint_entry) + name_len, GFP_KERNEL);
	if (!e)
		return ERR_PTR(-ENOMEM);
	memcpy(&e->name[0], name, name_len);
	e->funcs = NULL;
	e->refcount = 0;
	e->rcu_pending = 0;
	hlist_add_head(&e->hlist, head);
	return e;
}

/*
 * Remove the tracepoint from the tracepoint hash table. Must be called with
 * mutex_lock held.
 */
static int remove_tracepoint(const char *name)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct tracepoint_entry *e;
	int found = 0;
	size_t len = strlen(name) + 1;
	u32 hash = jhash(name, len-1, 0);

	head = &tracepoint_table[hash & ((1 << TRACEPOINT_HASH_BITS)-1)];
	hlist_for_each_entry(e, node, head, hlist) {
		if (!strcmp(name, e->name)) {
			found = 1;
			break;
		}
	}
	if (!found)
		return -ENOENT;
	if (e->refcount)
		return -EBUSY;
	hlist_del(&e->hlist);
	/* Make sure the call_rcu has been executed */
	if (e->rcu_pending)
		rcu_barrier();
	kfree(e);
	return 0;
}

/*
 * Sets the probe callback corresponding to one tracepoint.
 */
static void set_tracepoint(struct tracepoint_entry **entry,
	struct tracepoint *elem, int active)
{
	WARN_ON(strcmp((*entry)->name, elem->name) != 0);

	/*
	 * rcu_assign_pointer has a smp_wmb() which makes sure that the new
	 * probe callbacks array is consistent before setting a pointer to it.
	 * This array is referenced by __DO_TRACE from
	 * include/linux/tracepoints.h. A matching smp_read_barrier_depends()
	 * is used.
	 */
	rcu_assign_pointer(elem->funcs, (*entry)->funcs);
	elem->state = active;
}

/*
 * Disable a tracepoint and its probe callback.
 * Note: only waiting an RCU period after setting elem->call to the empty
 * function insures that the original callback is not used anymore. This insured
 * by preempt_disable around the call site.
 */
static void disable_tracepoint(struct tracepoint *elem)
{
	elem->state = 0;
}

/**
 * tracepoint_update_probe_range - Update a probe range
 * @begin: beginning of the range
 * @end: end of the range
 *
 * Updates the probe callback corresponding to a range of tracepoints.
 */
void tracepoint_update_probe_range(struct tracepoint *begin,
	struct tracepoint *end)
{
	struct tracepoint *iter;
	struct tracepoint_entry *mark_entry;

	mutex_lock(&tracepoints_mutex);
	for (iter = begin; iter < end; iter++) {
		mark_entry = get_tracepoint(iter->name);
		if (mark_entry) {
			set_tracepoint(&mark_entry, iter,
					!!mark_entry->refcount);
		} else {
			disable_tracepoint(iter);
		}
	}
	mutex_unlock(&tracepoints_mutex);
}

/*
 * Update probes, removing the faulty probes.
 */
static void tracepoint_update_probes(void)
{
	/* Core kernel tracepoints */
	tracepoint_update_probe_range(__start___tracepoints,
		__stop___tracepoints);
	/* tracepoints in modules. */
	module_update_tracepoints();
}

/**
 * tracepoint_probe_register -  Connect a probe to a tracepoint
 * @name: tracepoint name
 * @probe: probe handler
 *
 * Returns 0 if ok, error value on error.
 * The probe address must at least be aligned on the architecture pointer size.
 */
int tracepoint_probe_register(const char *name, void *probe)
{
	struct tracepoint_entry *entry;
	int ret = 0;
	void *old;

	mutex_lock(&tracepoints_mutex);
	entry = get_tracepoint(name);
	if (!entry) {
		entry = add_tracepoint(name);
		if (IS_ERR(entry)) {
			ret = PTR_ERR(entry);
			goto end;
		}
	}
	/*
	 * If we detect that a call_rcu is pending for this tracepoint,
	 * make sure it's executed now.
	 */
	if (entry->rcu_pending)
		rcu_barrier();
	old = tracepoint_entry_add_probe(entry, probe);
	if (IS_ERR(old)) {
		ret = PTR_ERR(old);
		goto end;
	}
	mutex_unlock(&tracepoints_mutex);
	tracepoint_update_probes();		/* may update entry */
	mutex_lock(&tracepoints_mutex);
	entry = get_tracepoint(name);
	WARN_ON(!entry);
	tracepoint_entry_free_old(entry, old);
end:
	mutex_unlock(&tracepoints_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(tracepoint_probe_register);

/**
 * tracepoint_probe_unregister -  Disconnect a probe from a tracepoint
 * @name: tracepoint name
 * @probe: probe function pointer
 *
 * We do not need to call a synchronize_sched to make sure the probes have
 * finished running before doing a module unload, because the module unload
 * itself uses stop_machine(), which insures that every preempt disabled section
 * have finished.
 */
int tracepoint_probe_unregister(const char *name, void *probe)
{
	struct tracepoint_entry *entry;
	void *old;
	int ret = -ENOENT;

	mutex_lock(&tracepoints_mutex);
	entry = get_tracepoint(name);
	if (!entry)
		goto end;
	if (entry->rcu_pending)
		rcu_barrier();
	old = tracepoint_entry_remove_probe(entry, probe);
	mutex_unlock(&tracepoints_mutex);
	tracepoint_update_probes();		/* may update entry */
	mutex_lock(&tracepoints_mutex);
	entry = get_tracepoint(name);
	if (!entry)
		goto end;
	tracepoint_entry_free_old(entry, old);
	remove_tracepoint(name);	/* Ignore busy error message */
	ret = 0;
end:
	mutex_unlock(&tracepoints_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(tracepoint_probe_unregister);

/**
 * tracepoint_get_iter_range - Get a next tracepoint iterator given a range.
 * @tracepoint: current tracepoints (in), next tracepoint (out)
 * @begin: beginning of the range
 * @end: end of the range
 *
 * Returns whether a next tracepoint has been found (1) or not (0).
 * Will return the first tracepoint in the range if the input tracepoint is
 * NULL.
 */
int tracepoint_get_iter_range(struct tracepoint **tracepoint,
	struct tracepoint *begin, struct tracepoint *end)
{
	if (!*tracepoint && begin != end) {
		*tracepoint = begin;
		return 1;
	}
	if (*tracepoint >= begin && *tracepoint < end)
		return 1;
	return 0;
}
EXPORT_SYMBOL_GPL(tracepoint_get_iter_range);

static void tracepoint_get_iter(struct tracepoint_iter *iter)
{
	int found = 0;

	/* Core kernel tracepoints */
	if (!iter->module) {
		found = tracepoint_get_iter_range(&iter->tracepoint,
				__start___tracepoints, __stop___tracepoints);
		if (found)
			goto end;
	}
	/* tracepoints in modules. */
	found = module_get_iter_tracepoints(iter);
end:
	if (!found)
		tracepoint_iter_reset(iter);
}

void tracepoint_iter_start(struct tracepoint_iter *iter)
{
	tracepoint_get_iter(iter);
}
EXPORT_SYMBOL_GPL(tracepoint_iter_start);

void tracepoint_iter_next(struct tracepoint_iter *iter)
{
	iter->tracepoint++;
	/*
	 * iter->tracepoint may be invalid because we blindly incremented it.
	 * Make sure it is valid by marshalling on the tracepoints, getting the
	 * tracepoints from following modules if necessary.
	 */
	tracepoint_get_iter(iter);
}
EXPORT_SYMBOL_GPL(tracepoint_iter_next);

void tracepoint_iter_stop(struct tracepoint_iter *iter)
{
}
EXPORT_SYMBOL_GPL(tracepoint_iter_stop);

void tracepoint_iter_reset(struct tracepoint_iter *iter)
{
	iter->module = NULL;
	iter->tracepoint = NULL;
}
EXPORT_SYMBOL_GPL(tracepoint_iter_reset);
