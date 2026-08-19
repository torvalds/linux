// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

/**
 * DOC: LUO File Lifecycle Bound Global Data
 *
 * File-Lifecycle-Bound (FLB) objects provide a mechanism for managing global
 * state that is shared across multiple live-updatable files. The lifecycle of
 * this shared state is tied to the preservation of the files that depend on it.
 *
 * An FLB represents a global resource, such as the IOMMU core state, that is
 * required by multiple file descriptors (e.g., all VFIO fds).
 *
 * The preservation of the FLB's state is triggered when the *first* file
 * depending on it is preserved. The cleanup of this state (unpreserve or
 * finish) is triggered when the *last* file depending on it is unpreserved or
 * finished.
 *
 * Handler Dependency: A file handler declares its dependency on one or more
 * FLBs by registering them via liveupdate_register_flb().
 *
 * Callback Model: Each FLB is defined by a set of operations
 * (&struct liveupdate_flb_ops) that LUO invokes at key points:
 *
 *     - .preserve(): Called for the first file. Saves global state.
 *     - .unpreserve(): Called for the last file (if aborted pre-reboot).
 *     - .retrieve(): Called on-demand in the new kernel to restore the state.
 *     - .finish(): Called for the last file in the new kernel for cleanup.
 *
 * This reference-counted approach ensures that shared state is saved exactly
 * once and restored exactly once, regardless of how many files depend on it,
 * and that its lifecycle is correctly managed across the kexec transition.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cleanup.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/kexec_handover.h>
#include <linux/kho/abi/luo.h>
#include <linux/list_private.h>
#include <linux/liveupdate.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include "luo_internal.h"

#define LUO_FLB_PGCNT		1ul
#define LUO_FLB_MAX		(((LUO_FLB_PGCNT << PAGE_SHIFT) -	\
		sizeof(struct luo_flb_header_ser)) / sizeof(struct luo_flb_ser))

struct luo_flb_header {
	struct luo_flb_header_ser *header_ser;
	struct luo_flb_ser *ser;
	bool active;
};

struct luo_flb_global {
	struct luo_flb_header incoming;
	struct luo_flb_header outgoing;
	struct list_head list;
	long count;
};

static struct luo_flb_global luo_flb_global = {
	.list = LIST_HEAD_INIT(luo_flb_global.list),
};

/*
 * struct luo_flb_link - Links an FLB definition to a file handler's internal
 * list of dependencies.
 * @flb:  A pointer to the registered &struct liveupdate_flb definition.
 * @list: The list_head for linking.
 */
struct luo_flb_link {
	struct liveupdate_flb *flb;
	struct list_head list;
};

/* luo_flb_get_private - Access private field, and if needed initialize it. */
static struct luo_flb_private *luo_flb_get_private(struct liveupdate_flb *flb)
{
	struct luo_flb_private *private = &ACCESS_PRIVATE(flb, private);
	static DEFINE_SPINLOCK(luo_flb_init_lock);

	if (smp_load_acquire(&private->initialized))
		return private;

	guard(spinlock)(&luo_flb_init_lock);
	if (!private->initialized) {
		mutex_init(&private->incoming.lock);
		mutex_init(&private->outgoing.lock);
		INIT_LIST_HEAD(&private->list);
		private->users = 0;
		smp_store_release(&private->initialized, true);
	}

	return private;
}

static int luo_flb_file_preserve_one(struct liveupdate_flb *flb)
{
	struct luo_flb_private *private = luo_flb_get_private(flb);

	scoped_guard(mutex, &private->outgoing.lock) {
		if (!refcount_read(&private->outgoing.count)) {
			struct liveupdate_flb_op_args args = {0};
			int err;

			if (!try_module_get(flb->ops->owner))
				return -ENODEV;

			args.flb = flb;
			err = flb->ops->preserve(&args);
			if (err) {
				module_put(flb->ops->owner);
				return err;
			}
			private->outgoing.data = args.data;
			private->outgoing.obj = args.obj;
			refcount_set(&private->outgoing.count, 1);
		} else {
			refcount_inc(&private->outgoing.count);
		}
	}

	return 0;
}

static void luo_flb_file_unpreserve_one(struct liveupdate_flb *flb)
{
	struct luo_flb_private *private = luo_flb_get_private(flb);

	scoped_guard(mutex, &private->outgoing.lock) {
		if (refcount_dec_and_test(&private->outgoing.count)) {
			struct liveupdate_flb_op_args args = {0};

			args.flb = flb;
			args.data = private->outgoing.data;
			args.obj = private->outgoing.obj;

			if (flb->ops->unpreserve)
				flb->ops->unpreserve(&args);

			private->outgoing.data = 0;
			private->outgoing.obj = NULL;
			module_put(flb->ops->owner);
		}
	}
}

static int luo_flb_retrieve_one(struct liveupdate_flb *flb)
{
	struct luo_flb_private *private = luo_flb_get_private(flb);
	struct luo_flb_header *fh = &luo_flb_global.incoming;
	struct liveupdate_flb_op_args args = {0};
	bool found = false;
	int err;

	lockdep_assert_held(&private->incoming.lock);

	if (private->incoming.finished)
		return -ENODATA;

	if (private->incoming.retrieved)
		return 0;

	if (!fh->active)
		return -ENODATA;

	for (int i = 0; i < fh->header_ser->count; i++) {
		if (!strcmp(fh->ser[i].name, flb->compatible)) {
			private->incoming.data = fh->ser[i].data;
			refcount_set(&private->incoming.count, fh->ser[i].count);
			found = true;
			break;
		}
	}

	if (!found)
		return -ENOENT;

	if (!try_module_get(flb->ops->owner))
		return -ENODEV;

	args.flb = flb;
	args.data = private->incoming.data;

	err = flb->ops->retrieve(&args);
	if (err) {
		module_put(flb->ops->owner);
		return err;
	}

	private->incoming.obj = args.obj;
	private->incoming.retrieved = true;

	return 0;
}

void liveupdate_flb_put_incoming(struct liveupdate_flb *flb)
{
	struct luo_flb_private *private = luo_flb_get_private(flb);
	struct liveupdate_flb_op_args args = {0};

	scoped_guard(mutex, &private->incoming.lock) {
		if (!refcount_dec_and_test(&private->incoming.count))
			return;

		if (!private->incoming.retrieved) {
			int err = luo_flb_retrieve_one(flb);

			if (WARN_ON(err))
				return;
		}

		args.flb = flb;
		args.obj = private->incoming.obj;
		flb->ops->finish(&args);

		private->incoming.data = 0;
		private->incoming.obj = NULL;
		private->incoming.finished = true;
		module_put(flb->ops->owner);
	}
}

/**
 * luo_flb_file_preserve - Notifies FLBs that a file is about to be preserved.
 * @fh: The file handler for the preserved file.
 *
 * This function iterates through all FLBs associated with the given file
 * handler. It increments the reference count for each FLB. If the count becomes
 * 1, it triggers the FLB's .preserve() callback to save the global state.
 *
 * This operation is atomic. If any FLB's .preserve() op fails, it will roll
 * back by calling .unpreserve() on any FLBs that were successfully preserved
 * during this call.
 *
 * Context: Called from luo_preserve_file()
 * Return: 0 on success, or a negative errno on failure.
 */
int luo_flb_file_preserve(struct liveupdate_file_handler *fh)
{
	struct list_head *flb_list = &ACCESS_PRIVATE(fh, flb_list);
	struct luo_flb_link *iter;
	int err = 0;

	down_read(&luo_register_rwlock);
	list_for_each_entry(iter, flb_list, list) {
		err = luo_flb_file_preserve_one(iter->flb);
		if (err)
			goto exit_err;
	}
	up_read(&luo_register_rwlock);

	return 0;

exit_err:
	list_for_each_entry_continue_reverse(iter, flb_list, list)
		luo_flb_file_unpreserve_one(iter->flb);
	up_read(&luo_register_rwlock);

	return err;
}

/**
 * luo_flb_file_unpreserve - Notifies FLBs that a dependent file was unpreserved.
 * @fh: The file handler for the unpreserved file.
 *
 * This function iterates through all FLBs associated with the given file
 * handler, in reverse order of registration. It decrements the reference count
 * for each FLB. If the count becomes 0, it triggers the FLB's .unpreserve()
 * callback to clean up the global state.
 *
 * Context: Called when a preserved file is being cleaned up before reboot
 *          (e.g., from luo_file_unpreserve_files()).
 */
void luo_flb_file_unpreserve(struct liveupdate_file_handler *fh)
{
	struct list_head *flb_list = &ACCESS_PRIVATE(fh, flb_list);
	struct luo_flb_link *iter;

	guard(rwsem_read)(&luo_register_rwlock);
	list_for_each_entry_reverse(iter, flb_list, list)
		luo_flb_file_unpreserve_one(iter->flb);
}

/**
 * luo_flb_file_finish - Notifies FLBs that a dependent file has been finished.
 * @fh: The file handler for the finished file.
 *
 * This function iterates through all FLBs associated with the given file
 * handler, in reverse order of registration. It decrements the incoming
 * reference count for each FLB. If the count becomes 0, it triggers the FLB's
 * .finish() callback for final cleanup in the new kernel.
 *
 * Context: Called from luo_file_finish() for each file being finished.
 */
void luo_flb_file_finish(struct liveupdate_file_handler *fh)
{
	struct list_head *flb_list = &ACCESS_PRIVATE(fh, flb_list);
	struct luo_flb_link *iter;

	guard(rwsem_read)(&luo_register_rwlock);
	list_for_each_entry_reverse(iter, flb_list, list)
		liveupdate_flb_put_incoming(iter->flb);
}

static void luo_flb_unregister_one(struct liveupdate_file_handler *fh,
				   struct liveupdate_flb *flb)
{
	struct luo_flb_private *private = luo_flb_get_private(flb);
	struct list_head *flb_list = &ACCESS_PRIVATE(fh, flb_list);
	struct luo_flb_link *iter;
	bool found = false;

	/* Find and remove the link from the file handler's list */
	list_for_each_entry(iter, flb_list, list) {
		if (iter->flb == flb) {
			list_del(&iter->list);
			kfree(iter);
			found = true;
			break;
		}
	}

	if (!found) {
		pr_warn("Failed to unregister FLB '%s': not found in file handler '%s'\n",
			flb->compatible, fh->compatible);
		return;
	}

	private->users--;

	/*
	 * If this is the last file-handler with which we are registred, remove
	 * from the global list.
	 */
	if (!private->users) {
		list_del_init(&private->list);
		luo_flb_global.count--;
	}
}

/**
 * luo_flb_unregister_all - Unregister all FLBs associated with a file handler.
 * @fh: The file handler whose FLBs should be unregistered.
 *
 * This function iterates through the list of FLBs associated with the given
 * file handler and unregisters them all one by one.
 */
void luo_flb_unregister_all(struct liveupdate_file_handler *fh)
{
	struct list_head *flb_list = &ACCESS_PRIVATE(fh, flb_list);
	struct luo_flb_link *iter, *tmp;

	if (!liveupdate_enabled())
		return;

	lockdep_assert_held_write(&luo_register_rwlock);
	list_for_each_entry_safe(iter, tmp, flb_list, list)
		luo_flb_unregister_one(fh, iter->flb);
}

/**
 * liveupdate_register_flb - Associate an FLB with a file handler and register it globally.
 * @fh:   The file handler that will now depend on the FLB.
 * @flb:  The File-Lifecycle-Bound object to associate.
 *
 * Establishes a dependency, informing the LUO core that whenever a file of
 * type @fh is preserved, the state of @flb must also be managed.
 *
 * On the first registration of a given @flb object, it is added to a global
 * registry. This function checks for duplicate registrations, both for a
 * specific handler and globally, and ensures the total number of unique
 * FLBs does not exceed the system limit.
 *
 * Context: Typically called from a subsystem's module init function after
 *          both the handler and the FLB have been defined and initialized.
 * Return: 0 on success. Returns a negative errno on failure:
 *         -EINVAL if arguments are NULL or not initialized.
 *         -ENOMEM on memory allocation failure.
 *         -EEXIST if this FLB is already registered with this handler.
 *         -ENOSPC if the maximum number of global FLBs has been reached.
 *         -EOPNOTSUPP if live update is disabled or not configured.
 */
int liveupdate_register_flb(struct liveupdate_file_handler *fh,
			    struct liveupdate_flb *flb)
{
	struct luo_flb_private *private = luo_flb_get_private(flb);
	struct list_head *flb_list = &ACCESS_PRIVATE(fh, flb_list);
	struct luo_flb_link *link __free(kfree) = NULL;
	struct liveupdate_flb *gflb;
	struct luo_flb_link *iter;

	if (!liveupdate_enabled())
		return -EOPNOTSUPP;

	if (WARN_ON(!flb->ops->preserve || !flb->ops->unpreserve ||
		    !flb->ops->retrieve || !flb->ops->finish)) {
		return -EINVAL;
	}

	/*
	 * File handler must already be registered, as it initializes the
	 * flb_list
	 */
	if (WARN_ON(list_empty(&ACCESS_PRIVATE(fh, list))))
		return -EINVAL;

	link = kzalloc_obj(*link);
	if (!link)
		return -ENOMEM;

	guard(rwsem_write)(&luo_register_rwlock);

	/* Check that this FLB is not already linked to this file handler */
	list_for_each_entry(iter, flb_list, list) {
		if (iter->flb == flb)
			return -EEXIST;
	}

	/*
	 * If this FLB is not linked to global list it's the first time the FLB
	 * is registered
	 */
	if (!private->users) {
		if (WARN_ON(!list_empty(&private->list)))
			return -EINVAL;

		if (luo_flb_global.count == LUO_FLB_MAX)
			return -ENOSPC;

		/* Check that compatible string is unique in global list */
		list_private_for_each_entry(gflb, &luo_flb_global.list, private.list) {
			if (!strcmp(gflb->compatible, flb->compatible))
				return -EEXIST;
		}

		list_add_tail(&private->list, &luo_flb_global.list);
		luo_flb_global.count++;
	}

	/* Finally, link the FLB to the file handler */
	private->users++;
	link->flb = flb;
	list_add_tail(&no_free_ptr(link)->list, flb_list);

	return 0;
}

/**
 * liveupdate_unregister_flb - Remove an FLB dependency from a file handler.
 * @fh:   The file handler that is currently depending on the FLB.
 * @flb:  The File-Lifecycle-Bound object to remove.
 *
 * Removes the association between the specified file handler and the FLB
 * previously established by liveupdate_register_flb().
 *
 * This function manages the global lifecycle of the FLB. It decrements the
 * FLB's usage count. If this was the last file handler referencing this FLB,
 * the FLB is removed from the global registry and the reference to its
 * owner module (acquired during registration) is released.
 *
 * Context: It is typically called from a subsystem's module exit function.
 */
void liveupdate_unregister_flb(struct liveupdate_file_handler *fh,
			       struct liveupdate_flb *flb)
{
	if (!liveupdate_enabled())
		return;

	guard(rwsem_write)(&luo_register_rwlock);

	luo_flb_unregister_one(fh, flb);
}

/**
 * liveupdate_flb_get_incoming - Retrieve the incoming FLB object.
 * @flb:  The FLB definition.
 * @objp: Output parameter; will be populated with the live shared object.
 *
 * Returns a pointer to its shared live object for the incoming (post-reboot)
 * path.
 *
 * If this is the first time the object is requested in the new kernel, this
 * function will trigger the FLB's .retrieve() callback to reconstruct the
 * object from its preserved state. Subsequent calls will return the same
 * cached object.
 *
 * Return: 0 on success, or a negative errno on failure. -ENODATA means no
 * incoming FLB data, -ENOENT means specific flb not found in the incoming
 * data, -ENODEV if the FLB's module is unloading, and -EOPNOTSUPP when
 * live update is disabled or not configured.
 */
int liveupdate_flb_get_incoming(struct liveupdate_flb *flb, void **objp)
{
	struct luo_flb_private *private = luo_flb_get_private(flb);

	if (!liveupdate_enabled())
		return -EOPNOTSUPP;

	guard(mutex)(&private->incoming.lock);

	if (!private->incoming.obj) {
		int err = luo_flb_retrieve_one(flb);

		if (err)
			return err;
	}

	refcount_inc(&private->incoming.count);
	*objp = private->incoming.obj;

	return 0;
}

/**
 * liveupdate_flb_get_outgoing - Retrieve the outgoing FLB object.
 * @flb:  The FLB definition.
 * @objp: Output parameter; will be populated with the live shared object.
 *
 * Returns a pointer to its shared live object for the outgoing (pre-reboot)
 * path.
 *
 * This function assumes the object has already been created by the FLB's
 * .preserve() callback, which is triggered when the first dependent file
 * is preserved.
 *
 * Return: 0 on success, or a negative errno on failure.
 */
int liveupdate_flb_get_outgoing(struct liveupdate_flb *flb, void **objp)
{
	struct luo_flb_private *private = luo_flb_get_private(flb);

	if (!liveupdate_enabled())
		return -EOPNOTSUPP;

	guard(mutex)(&private->outgoing.lock);
	*objp = private->outgoing.obj;

	return 0;
}

int __init luo_flb_setup_outgoing(u64 *flbs_pa)
{
	struct luo_flb_header_ser *header_ser;

	header_ser = kho_alloc_preserve(LUO_FLB_PGCNT << PAGE_SHIFT);
	if (IS_ERR(header_ser))
		return PTR_ERR(header_ser);

	*flbs_pa = virt_to_phys(header_ser);

	header_ser->pgcnt = LUO_FLB_PGCNT;
	luo_flb_global.outgoing.header_ser = header_ser;
	luo_flb_global.outgoing.ser = (void *)(header_ser + 1);
	luo_flb_global.outgoing.active = true;

	return 0;
}

void __init luo_flb_setup_incoming(u64 flbs_pa)
{
	struct luo_flb_header_ser *header_ser;

	if (!flbs_pa)
		return;

	header_ser = phys_to_virt(flbs_pa);
	luo_flb_global.incoming.header_ser = header_ser;
	luo_flb_global.incoming.ser = (void *)(header_ser + 1);
	luo_flb_global.incoming.active = true;
}

/**
 * luo_flb_serialize - Serializes all active FLB objects for KHO.
 *
 * This function is called from the reboot path. It iterates through all
 * registered File-Lifecycle-Bound (FLB) objects. For each FLB that has been
 * preserved (i.e., its reference count is greater than zero), it writes its
 * metadata into the memory region designated for Kexec Handover.
 *
 * The serialized data includes the FLB's compatibility string, its opaque
 * data handle, and the final reference count. This allows the new kernel to
 * find the appropriate handler and reconstruct the FLB's state.
 *
 * Context: Called from liveupdate_reboot() just before kho_finalize().
 */
void luo_flb_serialize(void)
{
	struct luo_flb_header *fh = &luo_flb_global.outgoing;
	struct liveupdate_flb *gflb;
	int i = 0;

	guard(rwsem_read)(&luo_register_rwlock);
	list_private_for_each_entry(gflb, &luo_flb_global.list, private.list) {
		struct luo_flb_private *private = luo_flb_get_private(gflb);
		long count = refcount_read(&private->outgoing.count);

		if (count > 0) {
			strscpy(fh->ser[i].name, gflb->compatible,
				sizeof(fh->ser[i].name));
			fh->ser[i].data = private->outgoing.data;
			fh->ser[i].count = count;
			i++;
		}
	}

	fh->header_ser->count = i;
}
