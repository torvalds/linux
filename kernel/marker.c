/*
 * Copyright (C) 2007 Mathieu Desnoyers
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
#include <linux/marker.h>
#include <linux/err.h>
#include <linux/slab.h>

extern struct marker __start___markers[];
extern struct marker __stop___markers[];

/* Set to 1 to enable marker debug output */
const int marker_debug;

/*
 * markers_mutex nests inside module_mutex. Markers mutex protects the builtin
 * and module markers and the hash table.
 */
static DEFINE_MUTEX(markers_mutex);

/*
 * Marker hash table, containing the active markers.
 * Protected by module_mutex.
 */
#define MARKER_HASH_BITS 6
#define MARKER_TABLE_SIZE (1 << MARKER_HASH_BITS)

/*
 * Note about RCU :
 * It is used to make sure every handler has finished using its private data
 * between two consecutive operation (add or remove) on a given marker.  It is
 * also used to delay the free of multiple probes array until a quiescent state
 * is reached.
 * marker entries modifications are protected by the markers_mutex.
 */
struct marker_entry {
	struct hlist_node hlist;
	char *format;
	void (*call)(const struct marker *mdata,	/* Probe wrapper */
		void *call_private, const char *fmt, ...);
	struct marker_probe_closure single;
	struct marker_probe_closure *multi;
	int refcount;	/* Number of times armed. 0 if disarmed. */
	struct rcu_head rcu;
	void *oldptr;
	unsigned char rcu_pending:1;
	unsigned char ptype:1;
	char name[0];	/* Contains name'\0'format'\0' */
};

static struct hlist_head marker_table[MARKER_TABLE_SIZE];

/**
 * __mark_empty_function - Empty probe callback
 * @probe_private: probe private data
 * @call_private: call site private data
 * @fmt: format string
 * @...: variable argument list
 *
 * Empty callback provided as a probe to the markers. By providing this to a
 * disabled marker, we make sure the  execution flow is always valid even
 * though the function pointer change and the marker enabling are two distinct
 * operations that modifies the execution flow of preemptible code.
 */
void __mark_empty_function(void *probe_private, void *call_private,
	const char *fmt, va_list *args)
{
}
EXPORT_SYMBOL_GPL(__mark_empty_function);

/*
 * marker_probe_cb Callback that prepares the variable argument list for probes.
 * @mdata: pointer of type struct marker
 * @call_private: caller site private data
 * @fmt: format string
 * @...:  Variable argument list.
 *
 * Since we do not use "typical" pointer based RCU in the 1 argument case, we
 * need to put a full smp_rmb() in this branch. This is why we do not use
 * rcu_dereference() for the pointer read.
 */
void marker_probe_cb(const struct marker *mdata, void *call_private,
	const char *fmt, ...)
{
	va_list args;
	char ptype;

	/*
	 * preempt_disable does two things : disabling preemption to make sure
	 * the teardown of the callbacks can be done correctly when they are in
	 * modules and they insure RCU read coherency.
	 */
	preempt_disable();
	ptype = mdata->ptype;
	if (likely(!ptype)) {
		marker_probe_func *func;
		/* Must read the ptype before ptr. They are not data dependant,
		 * so we put an explicit smp_rmb() here. */
		smp_rmb();
		func = mdata->single.func;
		/* Must read the ptr before private data. They are not data
		 * dependant, so we put an explicit smp_rmb() here. */
		smp_rmb();
		va_start(args, fmt);
		func(mdata->single.probe_private, call_private, fmt, &args);
		va_end(args);
	} else {
		struct marker_probe_closure *multi;
		int i;
		/*
		 * multi points to an array, therefore accessing the array
		 * depends on reading multi. However, even in this case,
		 * we must insure that the pointer is read _before_ the array
		 * data. Same as rcu_dereference, but we need a full smp_rmb()
		 * in the fast path, so put the explicit barrier here.
		 */
		smp_read_barrier_depends();
		multi = mdata->multi;
		for (i = 0; multi[i].func; i++) {
			va_start(args, fmt);
			multi[i].func(multi[i].probe_private, call_private, fmt,
				&args);
			va_end(args);
		}
	}
	preempt_enable();
}
EXPORT_SYMBOL_GPL(marker_probe_cb);

/*
 * marker_probe_cb Callback that does not prepare the variable argument list.
 * @mdata: pointer of type struct marker
 * @call_private: caller site private data
 * @fmt: format string
 * @...:  Variable argument list.
 *
 * Should be connected to markers "MARK_NOARGS".
 */
void marker_probe_cb_noarg(const struct marker *mdata,
	void *call_private, const char *fmt, ...)
{
	va_list args;	/* not initialized */
	char ptype;

	preempt_disable();
	ptype = mdata->ptype;
	if (likely(!ptype)) {
		marker_probe_func *func;
		/* Must read the ptype before ptr. They are not data dependant,
		 * so we put an explicit smp_rmb() here. */
		smp_rmb();
		func = mdata->single.func;
		/* Must read the ptr before private data. They are not data
		 * dependant, so we put an explicit smp_rmb() here. */
		smp_rmb();
		func(mdata->single.probe_private, call_private, fmt, &args);
	} else {
		struct marker_probe_closure *multi;
		int i;
		/*
		 * multi points to an array, therefore accessing the array
		 * depends on reading multi. However, even in this case,
		 * we must insure that the pointer is read _before_ the array
		 * data. Same as rcu_dereference, but we need a full smp_rmb()
		 * in the fast path, so put the explicit barrier here.
		 */
		smp_read_barrier_depends();
		multi = mdata->multi;
		for (i = 0; multi[i].func; i++)
			multi[i].func(multi[i].probe_private, call_private, fmt,
				&args);
	}
	preempt_enable();
}
EXPORT_SYMBOL_GPL(marker_probe_cb_noarg);

static void free_old_closure(struct rcu_head *head)
{
	struct marker_entry *entry = container_of(head,
		struct marker_entry, rcu);
	kfree(entry->oldptr);
	/* Make sure we free the data before setting the pending flag to 0 */
	smp_wmb();
	entry->rcu_pending = 0;
}

static void debug_print_probes(struct marker_entry *entry)
{
	int i;

	if (!marker_debug)
		return;

	if (!entry->ptype) {
		printk(KERN_DEBUG "Single probe : %p %p\n",
			entry->single.func,
			entry->single.probe_private);
	} else {
		for (i = 0; entry->multi[i].func; i++)
			printk(KERN_DEBUG "Multi probe %d : %p %p\n", i,
				entry->multi[i].func,
				entry->multi[i].probe_private);
	}
}

static struct marker_probe_closure *
marker_entry_add_probe(struct marker_entry *entry,
		marker_probe_func *probe, void *probe_private)
{
	int nr_probes = 0;
	struct marker_probe_closure *old, *new;

	WARN_ON(!probe);

	debug_print_probes(entry);
	old = entry->multi;
	if (!entry->ptype) {
		if (entry->single.func == probe &&
				entry->single.probe_private == probe_private)
			return ERR_PTR(-EBUSY);
		if (entry->single.func == __mark_empty_function) {
			/* 0 -> 1 probes */
			entry->single.func = probe;
			entry->single.probe_private = probe_private;
			entry->refcount = 1;
			entry->ptype = 0;
			debug_print_probes(entry);
			return NULL;
		} else {
			/* 1 -> 2 probes */
			nr_probes = 1;
			old = NULL;
		}
	} else {
		/* (N -> N+1), (N != 0, 1) probes */
		for (nr_probes = 0; old[nr_probes].func; nr_probes++)
			if (old[nr_probes].func == probe
					&& old[nr_probes].probe_private
						== probe_private)
				return ERR_PTR(-EBUSY);
	}
	/* + 2 : one for new probe, one for NULL func */
	new = kzalloc((nr_probes + 2) * sizeof(struct marker_probe_closure),
			GFP_KERNEL);
	if (new == NULL)
		return ERR_PTR(-ENOMEM);
	if (!old)
		new[0] = entry->single;
	else
		memcpy(new, old,
			nr_probes * sizeof(struct marker_probe_closure));
	new[nr_probes].func = probe;
	new[nr_probes].probe_private = probe_private;
	entry->refcount = nr_probes + 1;
	entry->multi = new;
	entry->ptype = 1;
	debug_print_probes(entry);
	return old;
}

static struct marker_probe_closure *
marker_entry_remove_probe(struct marker_entry *entry,
		marker_probe_func *probe, void *probe_private)
{
	int nr_probes = 0, nr_del = 0, i;
	struct marker_probe_closure *old, *new;

	old = entry->multi;

	debug_print_probes(entry);
	if (!entry->ptype) {
		/* 0 -> N is an error */
		WARN_ON(entry->single.func == __mark_empty_function);
		/* 1 -> 0 probes */
		WARN_ON(probe && entry->single.func != probe);
		WARN_ON(entry->single.probe_private != probe_private);
		entry->single.func = __mark_empty_function;
		entry->refcount = 0;
		entry->ptype = 0;
		debug_print_probes(entry);
		return NULL;
	} else {
		/* (N -> M), (N > 1, M >= 0) probes */
		for (nr_probes = 0; old[nr_probes].func; nr_probes++) {
			if ((!probe || old[nr_probes].func == probe)
					&& old[nr_probes].probe_private
						== probe_private)
				nr_del++;
		}
	}

	if (nr_probes - nr_del == 0) {
		/* N -> 0, (N > 1) */
		entry->single.func = __mark_empty_function;
		entry->refcount = 0;
		entry->ptype = 0;
	} else if (nr_probes - nr_del == 1) {
		/* N -> 1, (N > 1) */
		for (i = 0; old[i].func; i++)
			if ((probe && old[i].func != probe) ||
					old[i].probe_private != probe_private)
				entry->single = old[i];
		entry->refcount = 1;
		entry->ptype = 0;
	} else {
		int j = 0;
		/* N -> M, (N > 1, M > 1) */
		/* + 1 for NULL */
		new = kzalloc((nr_probes - nr_del + 1)
			* sizeof(struct marker_probe_closure), GFP_KERNEL);
		if (new == NULL)
			return ERR_PTR(-ENOMEM);
		for (i = 0; old[i].func; i++)
			if ((probe && old[i].func != probe) ||
					old[i].probe_private != probe_private)
				new[j++] = old[i];
		entry->refcount = nr_probes - nr_del;
		entry->ptype = 1;
		entry->multi = new;
	}
	debug_print_probes(entry);
	return old;
}

/*
 * Get marker if the marker is present in the marker hash table.
 * Must be called with markers_mutex held.
 * Returns NULL if not present.
 */
static struct marker_entry *get_marker(const char *name)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct marker_entry *e;
	u32 hash = jhash(name, strlen(name), 0);

	head = &marker_table[hash & ((1 << MARKER_HASH_BITS)-1)];
	hlist_for_each_entry(e, node, head, hlist) {
		if (!strcmp(name, e->name))
			return e;
	}
	return NULL;
}

/*
 * Add the marker to the marker hash table. Must be called with markers_mutex
 * held.
 */
static struct marker_entry *add_marker(const char *name, const char *format)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct marker_entry *e;
	size_t name_len = strlen(name) + 1;
	size_t format_len = 0;
	u32 hash = jhash(name, name_len-1, 0);

	if (format)
		format_len = strlen(format) + 1;
	head = &marker_table[hash & ((1 << MARKER_HASH_BITS)-1)];
	hlist_for_each_entry(e, node, head, hlist) {
		if (!strcmp(name, e->name)) {
			printk(KERN_NOTICE
				"Marker %s busy\n", name);
			return ERR_PTR(-EBUSY);	/* Already there */
		}
	}
	/*
	 * Using kmalloc here to allocate a variable length element. Could
	 * cause some memory fragmentation if overused.
	 */
	e = kmalloc(sizeof(struct marker_entry) + name_len + format_len,
			GFP_KERNEL);
	if (!e)
		return ERR_PTR(-ENOMEM);
	memcpy(&e->name[0], name, name_len);
	if (format) {
		e->format = &e->name[name_len];
		memcpy(e->format, format, format_len);
		if (strcmp(e->format, MARK_NOARGS) == 0)
			e->call = marker_probe_cb_noarg;
		else
			e->call = marker_probe_cb;
		trace_mark(core_marker_format, "name %s format %s",
				e->name, e->format);
	} else {
		e->format = NULL;
		e->call = marker_probe_cb;
	}
	e->single.func = __mark_empty_function;
	e->single.probe_private = NULL;
	e->multi = NULL;
	e->ptype = 0;
	e->refcount = 0;
	e->rcu_pending = 0;
	hlist_add_head(&e->hlist, head);
	return e;
}

/*
 * Remove the marker from the marker hash table. Must be called with mutex_lock
 * held.
 */
static int remove_marker(const char *name)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct marker_entry *e;
	int found = 0;
	size_t len = strlen(name) + 1;
	u32 hash = jhash(name, len-1, 0);

	head = &marker_table[hash & ((1 << MARKER_HASH_BITS)-1)];
	hlist_for_each_entry(e, node, head, hlist) {
		if (!strcmp(name, e->name)) {
			found = 1;
			break;
		}
	}
	if (!found)
		return -ENOENT;
	if (e->single.func != __mark_empty_function)
		return -EBUSY;
	hlist_del(&e->hlist);
	/* Make sure the call_rcu has been executed */
	if (e->rcu_pending)
		rcu_barrier();
	kfree(e);
	return 0;
}

/*
 * Set the mark_entry format to the format found in the element.
 */
static int marker_set_format(struct marker_entry **entry, const char *format)
{
	struct marker_entry *e;
	size_t name_len = strlen((*entry)->name) + 1;
	size_t format_len = strlen(format) + 1;


	e = kmalloc(sizeof(struct marker_entry) + name_len + format_len,
			GFP_KERNEL);
	if (!e)
		return -ENOMEM;
	memcpy(&e->name[0], (*entry)->name, name_len);
	e->format = &e->name[name_len];
	memcpy(e->format, format, format_len);
	if (strcmp(e->format, MARK_NOARGS) == 0)
		e->call = marker_probe_cb_noarg;
	else
		e->call = marker_probe_cb;
	e->single = (*entry)->single;
	e->multi = (*entry)->multi;
	e->ptype = (*entry)->ptype;
	e->refcount = (*entry)->refcount;
	e->rcu_pending = 0;
	hlist_add_before(&e->hlist, &(*entry)->hlist);
	hlist_del(&(*entry)->hlist);
	/* Make sure the call_rcu has been executed */
	if ((*entry)->rcu_pending)
		rcu_barrier();
	kfree(*entry);
	*entry = e;
	trace_mark(core_marker_format, "name %s format %s",
			e->name, e->format);
	return 0;
}

/*
 * Sets the probe callback corresponding to one marker.
 */
static int set_marker(struct marker_entry **entry, struct marker *elem,
		int active)
{
	int ret;
	WARN_ON(strcmp((*entry)->name, elem->name) != 0);

	if ((*entry)->format) {
		if (strcmp((*entry)->format, elem->format) != 0) {
			printk(KERN_NOTICE
				"Format mismatch for probe %s "
				"(%s), marker (%s)\n",
				(*entry)->name,
				(*entry)->format,
				elem->format);
			return -EPERM;
		}
	} else {
		ret = marker_set_format(entry, elem->format);
		if (ret)
			return ret;
	}

	/*
	 * probe_cb setup (statically known) is done here. It is
	 * asynchronous with the rest of execution, therefore we only
	 * pass from a "safe" callback (with argument) to an "unsafe"
	 * callback (does not set arguments).
	 */
	elem->call = (*entry)->call;
	/*
	 * Sanity check :
	 * We only update the single probe private data when the ptr is
	 * set to a _non_ single probe! (0 -> 1 and N -> 1, N != 1)
	 */
	WARN_ON(elem->single.func != __mark_empty_function
		&& elem->single.probe_private
		!= (*entry)->single.probe_private &&
		!elem->ptype);
	elem->single.probe_private = (*entry)->single.probe_private;
	/*
	 * Make sure the private data is valid when we update the
	 * single probe ptr.
	 */
	smp_wmb();
	elem->single.func = (*entry)->single.func;
	/*
	 * We also make sure that the new probe callbacks array is consistent
	 * before setting a pointer to it.
	 */
	rcu_assign_pointer(elem->multi, (*entry)->multi);
	/*
	 * Update the function or multi probe array pointer before setting the
	 * ptype.
	 */
	smp_wmb();
	elem->ptype = (*entry)->ptype;
	elem->state = active;

	return 0;
}

/*
 * Disable a marker and its probe callback.
 * Note: only waiting an RCU period after setting elem->call to the empty
 * function insures that the original callback is not used anymore. This insured
 * by preempt_disable around the call site.
 */
static void disable_marker(struct marker *elem)
{
	/* leave "call" as is. It is known statically. */
	elem->state = 0;
	elem->single.func = __mark_empty_function;
	/* Update the function before setting the ptype */
	smp_wmb();
	elem->ptype = 0;	/* single probe */
	/*
	 * Leave the private data and id there, because removal is racy and
	 * should be done only after an RCU period. These are never used until
	 * the next initialization anyway.
	 */
}

/**
 * marker_update_probe_range - Update a probe range
 * @begin: beginning of the range
 * @end: end of the range
 *
 * Updates the probe callback corresponding to a range of markers.
 */
void marker_update_probe_range(struct marker *begin,
	struct marker *end)
{
	struct marker *iter;
	struct marker_entry *mark_entry;

	mutex_lock(&markers_mutex);
	for (iter = begin; iter < end; iter++) {
		mark_entry = get_marker(iter->name);
		if (mark_entry) {
			set_marker(&mark_entry, iter,
					!!mark_entry->refcount);
			/*
			 * ignore error, continue
			 */
		} else {
			disable_marker(iter);
		}
	}
	mutex_unlock(&markers_mutex);
}

/*
 * Update probes, removing the faulty probes.
 *
 * Internal callback only changed before the first probe is connected to it.
 * Single probe private data can only be changed on 0 -> 1 and 2 -> 1
 * transitions.  All other transitions will leave the old private data valid.
 * This makes the non-atomicity of the callback/private data updates valid.
 *
 * "special case" updates :
 * 0 -> 1 callback
 * 1 -> 0 callback
 * 1 -> 2 callbacks
 * 2 -> 1 callbacks
 * Other updates all behave the same, just like the 2 -> 3 or 3 -> 2 updates.
 * Site effect : marker_set_format may delete the marker entry (creating a
 * replacement).
 */
static void marker_update_probes(void)
{
	/* Core kernel markers */
	marker_update_probe_range(__start___markers, __stop___markers);
	/* Markers in modules. */
	module_update_markers();
}

/**
 * marker_probe_register -  Connect a probe to a marker
 * @name: marker name
 * @format: format string
 * @probe: probe handler
 * @probe_private: probe private data
 *
 * private data must be a valid allocated memory address, or NULL.
 * Returns 0 if ok, error value on error.
 * The probe address must at least be aligned on the architecture pointer size.
 */
int marker_probe_register(const char *name, const char *format,
			marker_probe_func *probe, void *probe_private)
{
	struct marker_entry *entry;
	int ret = 0;
	struct marker_probe_closure *old;

	mutex_lock(&markers_mutex);
	entry = get_marker(name);
	if (!entry) {
		entry = add_marker(name, format);
		if (IS_ERR(entry)) {
			ret = PTR_ERR(entry);
			goto end;
		}
	}
	/*
	 * If we detect that a call_rcu is pending for this marker,
	 * make sure it's executed now.
	 */
	if (entry->rcu_pending)
		rcu_barrier();
	old = marker_entry_add_probe(entry, probe, probe_private);
	if (IS_ERR(old)) {
		ret = PTR_ERR(old);
		goto end;
	}
	mutex_unlock(&markers_mutex);
	marker_update_probes();		/* may update entry */
	mutex_lock(&markers_mutex);
	entry = get_marker(name);
	WARN_ON(!entry);
	entry->oldptr = old;
	entry->rcu_pending = 1;
	/* write rcu_pending before calling the RCU callback */
	smp_wmb();
#ifdef CONFIG_PREEMPT_RCU
	synchronize_sched();	/* Until we have the call_rcu_sched() */
#endif
	call_rcu(&entry->rcu, free_old_closure);
end:
	mutex_unlock(&markers_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(marker_probe_register);

/**
 * marker_probe_unregister -  Disconnect a probe from a marker
 * @name: marker name
 * @probe: probe function pointer
 * @probe_private: probe private data
 *
 * Returns the private data given to marker_probe_register, or an ERR_PTR().
 * We do not need to call a synchronize_sched to make sure the probes have
 * finished running before doing a module unload, because the module unload
 * itself uses stop_machine(), which insures that every preempt disabled section
 * have finished.
 */
int marker_probe_unregister(const char *name,
	marker_probe_func *probe, void *probe_private)
{
	struct marker_entry *entry;
	struct marker_probe_closure *old;
	int ret = -ENOENT;

	mutex_lock(&markers_mutex);
	entry = get_marker(name);
	if (!entry)
		goto end;
	if (entry->rcu_pending)
		rcu_barrier();
	old = marker_entry_remove_probe(entry, probe, probe_private);
	mutex_unlock(&markers_mutex);
	marker_update_probes();		/* may update entry */
	mutex_lock(&markers_mutex);
	entry = get_marker(name);
	if (!entry)
		goto end;
	entry->oldptr = old;
	entry->rcu_pending = 1;
	/* write rcu_pending before calling the RCU callback */
	smp_wmb();
#ifdef CONFIG_PREEMPT_RCU
	synchronize_sched();	/* Until we have the call_rcu_sched() */
#endif
	call_rcu(&entry->rcu, free_old_closure);
	remove_marker(name);	/* Ignore busy error message */
	ret = 0;
end:
	mutex_unlock(&markers_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(marker_probe_unregister);

static struct marker_entry *
get_marker_from_private_data(marker_probe_func *probe, void *probe_private)
{
	struct marker_entry *entry;
	unsigned int i;
	struct hlist_head *head;
	struct hlist_node *node;

	for (i = 0; i < MARKER_TABLE_SIZE; i++) {
		head = &marker_table[i];
		hlist_for_each_entry(entry, node, head, hlist) {
			if (!entry->ptype) {
				if (entry->single.func == probe
						&& entry->single.probe_private
						== probe_private)
					return entry;
			} else {
				struct marker_probe_closure *closure;
				closure = entry->multi;
				for (i = 0; closure[i].func; i++) {
					if (closure[i].func == probe &&
							closure[i].probe_private
							== probe_private)
						return entry;
				}
			}
		}
	}
	return NULL;
}

/**
 * marker_probe_unregister_private_data -  Disconnect a probe from a marker
 * @probe: probe function
 * @probe_private: probe private data
 *
 * Unregister a probe by providing the registered private data.
 * Only removes the first marker found in hash table.
 * Return 0 on success or error value.
 * We do not need to call a synchronize_sched to make sure the probes have
 * finished running before doing a module unload, because the module unload
 * itself uses stop_machine(), which insures that every preempt disabled section
 * have finished.
 */
int marker_probe_unregister_private_data(marker_probe_func *probe,
		void *probe_private)
{
	struct marker_entry *entry;
	int ret = 0;
	struct marker_probe_closure *old;

	mutex_lock(&markers_mutex);
	entry = get_marker_from_private_data(probe, probe_private);
	if (!entry) {
		ret = -ENOENT;
		goto end;
	}
	if (entry->rcu_pending)
		rcu_barrier();
	old = marker_entry_remove_probe(entry, NULL, probe_private);
	mutex_unlock(&markers_mutex);
	marker_update_probes();		/* may update entry */
	mutex_lock(&markers_mutex);
	entry = get_marker_from_private_data(probe, probe_private);
	WARN_ON(!entry);
	entry->oldptr = old;
	entry->rcu_pending = 1;
	/* write rcu_pending before calling the RCU callback */
	smp_wmb();
#ifdef CONFIG_PREEMPT_RCU
	synchronize_sched();	/* Until we have the call_rcu_sched() */
#endif
	call_rcu(&entry->rcu, free_old_closure);
	remove_marker(entry->name);	/* Ignore busy error message */
end:
	mutex_unlock(&markers_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(marker_probe_unregister_private_data);

/**
 * marker_get_private_data - Get a marker's probe private data
 * @name: marker name
 * @probe: probe to match
 * @num: get the nth matching probe's private data
 *
 * Returns the nth private data pointer (starting from 0) matching, or an
 * ERR_PTR.
 * Returns the private data pointer, or an ERR_PTR.
 * The private data pointer should _only_ be dereferenced if the caller is the
 * owner of the data, or its content could vanish. This is mostly used to
 * confirm that a caller is the owner of a registered probe.
 */
void *marker_get_private_data(const char *name, marker_probe_func *probe,
		int num)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct marker_entry *e;
	size_t name_len = strlen(name) + 1;
	u32 hash = jhash(name, name_len-1, 0);
	int i;

	head = &marker_table[hash & ((1 << MARKER_HASH_BITS)-1)];
	hlist_for_each_entry(e, node, head, hlist) {
		if (!strcmp(name, e->name)) {
			if (!e->ptype) {
				if (num == 0 && e->single.func == probe)
					return e->single.probe_private;
				else
					break;
			} else {
				struct marker_probe_closure *closure;
				int match = 0;
				closure = e->multi;
				for (i = 0; closure[i].func; i++) {
					if (closure[i].func != probe)
						continue;
					if (match++ == num)
						return closure[i].probe_private;
				}
			}
		}
	}
	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL_GPL(marker_get_private_data);
