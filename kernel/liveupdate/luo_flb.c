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
#include <linux/libfdt.h>
#include <linux/list_private.h>
#include <linux/liveupdate.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/unaligned.h>
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

	if (!private->initialized) {
		mutex_init(&private->incoming.lock);
		mutex_init(&private->outgoing.lock);
		INIT_LIST_HEAD(&private->list);
		private->users = 0;
		private->initialized = true;
	}

	return private;
}

static int luo_flb_file_preserve_one(struct liveupdate_flb *flb)
{
	struct luo_flb_private *private = luo_flb_get_private(flb);

	scoped_guard(mutex, &private->outgoing.lock) {
		if (!private->outgoing.count) {
			struct liveupdate_flb_op_args args = {0};
			int err;

			args.flb = flb;
			err = flb->ops->preserve(&args);
			if (err)
				return err;
			private->outgoing.data = args.data;
			private->outgoing.obj = args.obj;
		}
		private->outgoing.count++;
	}

	return 0;
}

static void luo_flb_file_unpreserve_one(struct liveupdate_flb *flb)
{
	struct luo_flb_private *private = luo_flb_get_private(flb);

	scoped_guard(mutex, &private->outgoing.lock) {
		private->outgoing.count--;
		if (!private->outgoing.count) {
			struct liveupdate_flb_op_args args = {0};

			args.flb = flb;
			args.data = private->outgoing.data;
			args.obj = private->outgoing.obj;

			if (flb->ops->unpreserve)
				flb->ops->unpreserve(&args);

			private->outgoing.data = 0;
			private->outgoing.obj = NULL;
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

	guard(mutex)(&private->incoming.lock);

	if (private->incoming.finished)
		return -ENODATA;

	if (private->incoming.retrieved)
		return 0;

	if (!fh->active)
		return -ENODATA;

	for (int i = 0; i < fh->header_ser->count; i++) {
		if (!strcmp(fh->ser[i].name, flb->compatible)) {
			private->incoming.data = fh->ser[i].data;
			private->incoming.count = fh->ser[i].count;
			found = true;
			break;
		}
	}

	if (!found)
		return -ENOENT;

	args.flb = flb;
	args.data = private->incoming.data;

	err = flb->ops->retrieve(&args);
	if (err)
		return err;

	private->incoming.obj = args.obj;
	private->incoming.retrieved = true;

	return 0;
}

static void luo_flb_file_finish_one(struct liveupdate_flb *flb)
{
	struct luo_flb_private *private = luo_flb_get_private(flb);
	u64 count;

	scoped_guard(mutex, &private->incoming.lock)
		count = --private->incoming.count;

	if (!count) {
		struct liveupdate_flb_op_args args = {0};

		if (!private->incoming.retrieved) {
			int err = luo_flb_retrieve_one(flb);

			if (WARN_ON(err))
				return;
		}

		scoped_guard(mutex, &private->incoming.lock) {
			args.flb = flb;
			args.obj = private->incoming.obj;
			flb->ops->finish(&args);

			private->incoming.data = 0;
			private->incoming.obj = NULL;
			private->incoming.finished = true;
		}
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

	list_for_each_entry(iter, flb_list, list) {
		err = luo_flb_file_preserve_one(iter->flb);
		if (err)
			goto exit_err;
	}

	return 0;

exit_err:
	list_for_each_entry_continue_reverse(iter, flb_list, list)
		luo_flb_file_unpreserve_one(iter->flb);

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

	list_for_each_entry_reverse(iter, flb_list, list)
		luo_flb_file_finish_one(iter->flb);
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
	int err;

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

	link = kzalloc(sizeof(*link), GFP_KERNEL);
	if (!link)
		return -ENOMEM;

	/*
	 * Ensure the system is quiescent (no active sessions).
	 * This acts as a global lock for registration: no other thread can
	 * be in this section, and no sessions can be creating/using FDs.
	 */
	if (!luo_session_quiesce())
		return -EBUSY;

	/* Check that this FLB is not already linked to this file handler */
	err = -EEXIST;
	list_for_each_entry(iter, flb_list, list) {
		if (iter->flb == flb)
			goto err_resume;
	}

	/*
	 * If this FLB is not linked to global list it's the first time the FLB
	 * is registered
	 */
	if (!private->users) {
		if (WARN_ON(!list_empty(&private->list))) {
			err = -EINVAL;
			goto err_resume;
		}

		if (luo_flb_global.count == LUO_FLB_MAX) {
			err = -ENOSPC;
			goto err_resume;
		}

		/* Check that compatible string is unique in global list */
		list_private_for_each_entry(gflb, &luo_flb_global.list, private.list) {
			if (!strcmp(gflb->compatible, flb->compatible))
				goto err_resume;
		}

		if (!try_module_get(flb->ops->owner)) {
			err = -EAGAIN;
			goto err_resume;
		}

		list_add_tail(&private->list, &luo_flb_global.list);
		luo_flb_global.count++;
	}

	/* Finally, link the FLB to the file handler */
	private->users++;
	link->flb = flb;
	list_add_tail(&no_free_ptr(link)->list, flb_list);
	luo_session_resume();

	return 0;

err_resume:
	luo_session_resume();
	return err;
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
 * Context: This function ensures the session is quiesced (no active FDs
 *          being created) during the update. It is typically called from a
 *          subsystem's module exit function.
 * Return: 0 on success.
 *         -EOPNOTSUPP if live update is disabled.
 *         -EBUSY if the live update session is active and cannot be quiesced.
 *         -ENOENT if the FLB was not found in the file handler's list.
 */
int liveupdate_unregister_flb(struct liveupdate_file_handler *fh,
			      struct liveupdate_flb *flb)
{
	struct luo_flb_private *private = luo_flb_get_private(flb);
	struct list_head *flb_list = &ACCESS_PRIVATE(fh, flb_list);
	struct luo_flb_link *iter;
	int err = -ENOENT;

	if (!liveupdate_enabled())
		return -EOPNOTSUPP;

	/*
	 * Ensure the system is quiescent (no active sessions).
	 * This acts as a global lock for unregistration.
	 */
	if (!luo_session_quiesce())
		return -EBUSY;

	/* Find and remove the link from the file handler's list */
	list_for_each_entry(iter, flb_list, list) {
		if (iter->flb == flb) {
			list_del(&iter->list);
			kfree(iter);
			err = 0;
			break;
		}
	}

	if (err)
		goto err_resume;

	private->users--;
	/*
	 * If this is the last file-handler with which we are registred, remove
	 * from the global list, and relese module reference.
	 */
	if (!private->users) {
		list_del_init(&private->list);
		luo_flb_global.count--;
		module_put(flb->ops->owner);
	}

	luo_session_resume();

	return 0;

err_resume:
	luo_session_resume();
	return err;
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
 * data, and -EOPNOTSUPP when live update is disabled or not configured.
 */
int liveupdate_flb_get_incoming(struct liveupdate_flb *flb, void **objp)
{
	struct luo_flb_private *private = luo_flb_get_private(flb);

	if (!liveupdate_enabled())
		return -EOPNOTSUPP;

	if (!private->incoming.obj) {
		int err = luo_flb_retrieve_one(flb);

		if (err)
			return err;
	}

	guard(mutex)(&private->incoming.lock);
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

int __init luo_flb_setup_outgoing(void *fdt_out)
{
	struct luo_flb_header_ser *header_ser;
	u64 header_ser_pa;
	int err;

	header_ser = kho_alloc_preserve(LUO_FLB_PGCNT << PAGE_SHIFT);
	if (IS_ERR(header_ser))
		return PTR_ERR(header_ser);

	header_ser_pa = virt_to_phys(header_ser);

	err = fdt_begin_node(fdt_out, LUO_FDT_FLB_NODE_NAME);
	err |= fdt_property_string(fdt_out, "compatible",
				   LUO_FDT_FLB_COMPATIBLE);
	err |= fdt_property(fdt_out, LUO_FDT_FLB_HEADER, &header_ser_pa,
			    sizeof(header_ser_pa));
	err |= fdt_end_node(fdt_out);

	if (err)
		goto err_unpreserve;

	header_ser->pgcnt = LUO_FLB_PGCNT;
	luo_flb_global.outgoing.header_ser = header_ser;
	luo_flb_global.outgoing.ser = (void *)(header_ser + 1);
	luo_flb_global.outgoing.active = true;

	return 0;

err_unpreserve:
	kho_unpreserve_free(header_ser);

	return err;
}

int __init luo_flb_setup_incoming(void *fdt_in)
{
	struct luo_flb_header_ser *header_ser;
	int err, header_size, offset;
	const void *ptr;
	u64 header_ser_pa;

	offset = fdt_subnode_offset(fdt_in, 0, LUO_FDT_FLB_NODE_NAME);
	if (offset < 0) {
		pr_err("Unable to get FLB node [%s]\n", LUO_FDT_FLB_NODE_NAME);

		return -ENOENT;
	}

	err = fdt_node_check_compatible(fdt_in, offset,
					LUO_FDT_FLB_COMPATIBLE);
	if (err) {
		pr_err("FLB node is incompatible with '%s' [%d]\n",
		       LUO_FDT_FLB_COMPATIBLE, err);

		return -EINVAL;
	}

	header_size = 0;
	ptr = fdt_getprop(fdt_in, offset, LUO_FDT_FLB_HEADER, &header_size);
	if (!ptr || header_size != sizeof(u64)) {
		pr_err("Unable to get FLB header property '%s' [%d]\n",
		       LUO_FDT_FLB_HEADER, header_size);

		return -EINVAL;
	}

	header_ser_pa = get_unaligned((u64 *)ptr);
	header_ser = phys_to_virt(header_ser_pa);

	luo_flb_global.incoming.header_ser = header_ser;
	luo_flb_global.incoming.ser = (void *)(header_ser + 1);
	luo_flb_global.incoming.active = true;

	return 0;
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

	list_private_for_each_entry(gflb, &luo_flb_global.list, private.list) {
		struct luo_flb_private *private = luo_flb_get_private(gflb);

		if (private->outgoing.count > 0) {
			strscpy(fh->ser[i].name, gflb->compatible,
				sizeof(fh->ser[i].name));
			fh->ser[i].data = private->outgoing.data;
			fh->ser[i].count = private->outgoing.count;
			i++;
		}
	}

	fh->header_ser->count = i;
}
