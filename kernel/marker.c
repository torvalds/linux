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

extern struct marker __start___markers[];
extern struct marker __stop___markers[];

/*
 * markers_mutex nests inside module_mutex. Markers mutex protects the builtin
 * and module markers, the hash table and deferred_sync.
 */
static DEFINE_MUTEX(markers_mutex);

/*
 * Marker deferred synchronization.
 * Upon marker probe_unregister, we delay call to synchronize_sched() to
 * accelerate mass unregistration (only when there is no more reference to a
 * given module do we call synchronize_sched()). However, we need to make sure
 * every critical region has ended before we re-arm a marker that has been
 * unregistered and then registered back with a different probe data.
 */
static int deferred_sync;

/*
 * Marker hash table, containing the active markers.
 * Protected by module_mutex.
 */
#define MARKER_HASH_BITS 6
#define MARKER_TABLE_SIZE (1 << MARKER_HASH_BITS)

struct marker_entry {
	struct hlist_node hlist;
	char *format;
	marker_probe_func *probe;
	void *private;
	int refcount;	/* Number of times armed. 0 if disarmed. */
	char name[0];	/* Contains name'\0'format'\0' */
};

static struct hlist_head marker_table[MARKER_TABLE_SIZE];

/**
 * __mark_empty_function - Empty probe callback
 * @mdata: pointer of type const struct marker
 * @fmt: format string
 * @...: variable argument list
 *
 * Empty callback provided as a probe to the markers. By providing this to a
 * disabled marker, we make sure the  execution flow is always valid even
 * though the function pointer change and the marker enabling are two distinct
 * operations that modifies the execution flow of preemptible code.
 */
void __mark_empty_function(const struct marker *mdata, void *private,
	const char *fmt, ...)
{
}
EXPORT_SYMBOL_GPL(__mark_empty_function);

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
static int add_marker(const char *name, const char *format,
	marker_probe_func *probe, void *private)
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
				"Marker %s busy, probe %p already installed\n",
				name, e->probe);
			return -EBUSY;	/* Already there */
		}
	}
	/*
	 * Using kmalloc here to allocate a variable length element. Could
	 * cause some memory fragmentation if overused.
	 */
	e = kmalloc(sizeof(struct marker_entry) + name_len + format_len,
			GFP_KERNEL);
	if (!e)
		return -ENOMEM;
	memcpy(&e->name[0], name, name_len);
	if (format) {
		e->format = &e->name[name_len];
		memcpy(e->format, format, format_len);
		trace_mark(core_marker_format, "name %s format %s",
				e->name, e->format);
	} else
		e->format = NULL;
	e->probe = probe;
	e->private = private;
	e->refcount = 0;
	hlist_add_head(&e->hlist, head);
	return 0;
}

/*
 * Remove the marker from the marker hash table. Must be called with mutex_lock
 * held.
 */
static void *remove_marker(const char *name)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct marker_entry *e;
	int found = 0;
	size_t len = strlen(name) + 1;
	void *private = NULL;
	u32 hash = jhash(name, len-1, 0);

	head = &marker_table[hash & ((1 << MARKER_HASH_BITS)-1)];
	hlist_for_each_entry(e, node, head, hlist) {
		if (!strcmp(name, e->name)) {
			found = 1;
			break;
		}
	}
	if (found) {
		private = e->private;
		hlist_del(&e->hlist);
		kfree(e);
	}
	return private;
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
	e->probe = (*entry)->probe;
	e->private = (*entry)->private;
	e->refcount = (*entry)->refcount;
	hlist_add_before(&e->hlist, &(*entry)->hlist);
	hlist_del(&(*entry)->hlist);
	kfree(*entry);
	*entry = e;
	trace_mark(core_marker_format, "name %s format %s",
			e->name, e->format);
	return 0;
}

/*
 * Sets the probe callback corresponding to one marker.
 */
static int set_marker(struct marker_entry **entry, struct marker *elem)
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
	elem->call = (*entry)->probe;
	elem->private = (*entry)->private;
	elem->state = 1;
	return 0;
}

/*
 * Disable a marker and its probe callback.
 * Note: only after a synchronize_sched() issued after setting elem->call to the
 * empty function insures that the original callback is not used anymore. This
 * insured by preemption disabling around the call site.
 */
static void disable_marker(struct marker *elem)
{
	elem->state = 0;
	elem->call = __mark_empty_function;
	/*
	 * Leave the private data and id there, because removal is racy and
	 * should be done only after a synchronize_sched(). These are never used
	 * until the next initialization anyway.
	 */
}

/**
 * marker_update_probe_range - Update a probe range
 * @begin: beginning of the range
 * @end: end of the range
 * @probe_module: module address of the probe being updated
 * @refcount: number of references left to the given probe_module (out)
 *
 * Updates the probe callback corresponding to a range of markers.
 */
void marker_update_probe_range(struct marker *begin,
	struct marker *end, struct module *probe_module,
	int *refcount)
{
	struct marker *iter;
	struct marker_entry *mark_entry;

	mutex_lock(&markers_mutex);
	for (iter = begin; iter < end; iter++) {
		mark_entry = get_marker(iter->name);
		if (mark_entry && mark_entry->refcount) {
			set_marker(&mark_entry, iter);
			/*
			 * ignore error, continue
			 */
			if (probe_module)
				if (probe_module ==
			__module_text_address((unsigned long)mark_entry->probe))
					(*refcount)++;
		} else {
			disable_marker(iter);
		}
	}
	mutex_unlock(&markers_mutex);
}

/*
 * Update probes, removing the faulty probes.
 * Issues a synchronize_sched() when no reference to the module passed
 * as parameter is found in the probes so the probe module can be
 * safely unloaded from now on.
 */
static void marker_update_probes(struct module *probe_module)
{
	int refcount = 0;

	/* Core kernel markers */
	marker_update_probe_range(__start___markers,
			__stop___markers, probe_module, &refcount);
	/* Markers in modules. */
	module_update_markers(probe_module, &refcount);
	if (probe_module && refcount == 0) {
		synchronize_sched();
		deferred_sync = 0;
	}
}

/**
 * marker_probe_register -  Connect a probe to a marker
 * @name: marker name
 * @format: format string
 * @probe: probe handler
 * @private: probe private data
 *
 * private data must be a valid allocated memory address, or NULL.
 * Returns 0 if ok, error value on error.
 */
int marker_probe_register(const char *name, const char *format,
			marker_probe_func *probe, void *private)
{
	struct marker_entry *entry;
	int ret = 0;

	mutex_lock(&markers_mutex);
	entry = get_marker(name);
	if (entry && entry->refcount) {
		ret = -EBUSY;
		goto end;
	}
	if (deferred_sync) {
		synchronize_sched();
		deferred_sync = 0;
	}
	ret = add_marker(name, format, probe, private);
	if (ret)
		goto end;
	mutex_unlock(&markers_mutex);
	marker_update_probes(NULL);
	return ret;
end:
	mutex_unlock(&markers_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(marker_probe_register);

/**
 * marker_probe_unregister -  Disconnect a probe from a marker
 * @name: marker name
 *
 * Returns the private data given to marker_probe_register, or an ERR_PTR().
 */
void *marker_probe_unregister(const char *name)
{
	struct module *probe_module;
	struct marker_entry *entry;
	void *private;

	mutex_lock(&markers_mutex);
	entry = get_marker(name);
	if (!entry) {
		private = ERR_PTR(-ENOENT);
		goto end;
	}
	entry->refcount = 0;
	/* In what module is the probe handler ? */
	probe_module = __module_text_address((unsigned long)entry->probe);
	private = remove_marker(name);
	deferred_sync = 1;
	mutex_unlock(&markers_mutex);
	marker_update_probes(probe_module);
	return private;
end:
	mutex_unlock(&markers_mutex);
	return private;
}
EXPORT_SYMBOL_GPL(marker_probe_unregister);

/**
 * marker_probe_unregister_private_data -  Disconnect a probe from a marker
 * @private: probe private data
 *
 * Unregister a marker by providing the registered private data.
 * Returns the private data given to marker_probe_register, or an ERR_PTR().
 */
void *marker_probe_unregister_private_data(void *private)
{
	struct module *probe_module;
	struct hlist_head *head;
	struct hlist_node *node;
	struct marker_entry *entry;
	int found = 0;
	unsigned int i;

	mutex_lock(&markers_mutex);
	for (i = 0; i < MARKER_TABLE_SIZE; i++) {
		head = &marker_table[i];
		hlist_for_each_entry(entry, node, head, hlist) {
			if (entry->private == private) {
				found = 1;
				goto iter_end;
			}
		}
	}
iter_end:
	if (!found) {
		private = ERR_PTR(-ENOENT);
		goto end;
	}
	entry->refcount = 0;
	/* In what module is the probe handler ? */
	probe_module = __module_text_address((unsigned long)entry->probe);
	private = remove_marker(entry->name);
	deferred_sync = 1;
	mutex_unlock(&markers_mutex);
	marker_update_probes(probe_module);
	return private;
end:
	mutex_unlock(&markers_mutex);
	return private;
}
EXPORT_SYMBOL_GPL(marker_probe_unregister_private_data);

/**
 * marker_arm - Arm a marker
 * @name: marker name
 *
 * Activate a marker. It keeps a reference count of the number of
 * arming/disarming done.
 * Returns 0 if ok, error value on error.
 */
int marker_arm(const char *name)
{
	struct marker_entry *entry;
	int ret = 0;

	mutex_lock(&markers_mutex);
	entry = get_marker(name);
	if (!entry) {
		ret = -ENOENT;
		goto end;
	}
	/*
	 * Only need to update probes when refcount passes from 0 to 1.
	 */
	if (entry->refcount++)
		goto end;
end:
	mutex_unlock(&markers_mutex);
	marker_update_probes(NULL);
	return ret;
}
EXPORT_SYMBOL_GPL(marker_arm);

/**
 * marker_disarm - Disarm a marker
 * @name: marker name
 *
 * Disarm a marker. It keeps a reference count of the number of arming/disarming
 * done.
 * Returns 0 if ok, error value on error.
 */
int marker_disarm(const char *name)
{
	struct marker_entry *entry;
	int ret = 0;

	mutex_lock(&markers_mutex);
	entry = get_marker(name);
	if (!entry) {
		ret = -ENOENT;
		goto end;
	}
	/*
	 * Only permit decrement refcount if higher than 0.
	 * Do probe update only on 1 -> 0 transition.
	 */
	if (entry->refcount) {
		if (--entry->refcount)
			goto end;
	} else {
		ret = -EPERM;
		goto end;
	}
end:
	mutex_unlock(&markers_mutex);
	marker_update_probes(NULL);
	return ret;
}
EXPORT_SYMBOL_GPL(marker_disarm);

/**
 * marker_get_private_data - Get a marker's probe private data
 * @name: marker name
 *
 * Returns the private data pointer, or an ERR_PTR.
 * The private data pointer should _only_ be dereferenced if the caller is the
 * owner of the data, or its content could vanish. This is mostly used to
 * confirm that a caller is the owner of a registered probe.
 */
void *marker_get_private_data(const char *name)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct marker_entry *e;
	size_t name_len = strlen(name) + 1;
	u32 hash = jhash(name, name_len-1, 0);
	int found = 0;

	head = &marker_table[hash & ((1 << MARKER_HASH_BITS)-1)];
	hlist_for_each_entry(e, node, head, hlist) {
		if (!strcmp(name, e->name)) {
			found = 1;
			return e->private;
		}
	}
	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL_GPL(marker_get_private_data);
