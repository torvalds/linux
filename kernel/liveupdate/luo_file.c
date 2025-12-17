// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

/**
 * DOC: LUO File Descriptors
 *
 * LUO provides the infrastructure to preserve specific, stateful file
 * descriptors across a kexec-based live update. The primary goal is to allow
 * workloads, such as virtual machines using vfio, memfd, or iommufd, to
 * retain access to their essential resources without interruption.
 *
 * The framework is built around a callback-based handler model and a well-
 * defined lifecycle for each preserved file.
 *
 * Handler Registration:
 * Kernel modules responsible for a specific file type (e.g., memfd, vfio)
 * register a &struct liveupdate_file_handler. This handler provides a set of
 * callbacks that LUO invokes at different stages of the update process, most
 * notably:
 *
 *   - can_preserve(): A lightweight check to determine if the handler is
 *     compatible with a given 'struct file'.
 *   - preserve(): The heavyweight operation that saves the file's state and
 *     returns an opaque u64 handle. This is typically performed while the
 *     workload is still active to minimize the downtime during the
 *     actual reboot transition.
 *   - unpreserve(): Cleans up any resources allocated by .preserve(), called
 *     if the preservation process is aborted before the reboot (i.e. session is
 *     closed).
 *   - freeze(): A final pre-reboot opportunity to prepare the state for kexec.
 *     We are already in reboot syscall, and therefore userspace cannot mutate
 *     the file anymore.
 *   - unfreeze(): Undoes the actions of .freeze(), called if the live update
 *     is aborted after the freeze phase.
 *   - retrieve(): Reconstructs the file in the new kernel from the preserved
 *     handle.
 *   - finish(): Performs final check and cleanup in the new kernel. After
 *     succesul finish call, LUO gives up ownership to this file.
 *
 * File Preservation Lifecycle happy path:
 *
 * 1. Preserve (Normal Operation): A userspace agent preserves files one by one
 *    via an ioctl. For each file, luo_preserve_file() finds a compatible
 *    handler, calls its .preserve() operation, and creates an internal &struct
 *    luo_file to track the live state.
 *
 * 2. Freeze (Pre-Reboot): Just before the kexec, luo_file_freeze() is called.
 *    It iterates through all preserved files, calls their respective .freeze()
 *    operation, and serializes their final metadata (compatible string, token,
 *    and data handle) into a contiguous memory block for KHO.
 *
 * 3. Deserialize: After kexec, luo_file_deserialize() runs when session gets
 *    deserialized (which is when /dev/liveupdate is first opened). It reads the
 *    serialized data from the KHO memory region and reconstructs the in-memory
 *    list of &struct luo_file instances for the new kernel, linking them to
 *    their corresponding handlers.
 *
 * 4. Retrieve (New Kernel - Userspace Ready): The userspace agent can now
 *    restore file descriptors by providing a token. luo_retrieve_file()
 *    searches for the matching token, calls the handler's .retrieve() op to
 *    re-create the 'struct file', and returns a new FD. Files can be
 *    retrieved in ANY order.
 *
 * 5. Finish (New Kernel - Cleanup): Once a session retrival is complete,
 *    luo_file_finish() is called. It iterates through all files, invokes their
 *    .finish() operations for final cleanup, and releases all associated kernel
 *    resources.
 *
 * File Preservation Lifecycle unhappy paths:
 *
 * 1. Abort Before Reboot: If the userspace agent aborts the live update
 *    process before calling reboot (e.g., by closing the session file
 *    descriptor), the session's release handler calls
 *    luo_file_unpreserve_files(). This invokes the .unpreserve() callback on
 *    all preserved files, ensuring all allocated resources are cleaned up and
 *    returning the system to a clean state.
 *
 * 2. Freeze Failure: During the reboot() syscall, if any handler's .freeze()
 *    op fails, the .unfreeze() op is invoked on all previously *successful*
 *    freezes to roll back their state. The reboot() syscall then returns an
 *    error to userspace, canceling the live update.
 *
 * 3. Finish Failure: In the new kernel, if a handler's .finish() op fails,
 *    the luo_file_finish() operation is aborted. LUO retains ownership of
 *    all files within that session, including those that were not yet
 *    processed. The userspace agent can attempt to call the finish operation
 *    again later. If the issue cannot be resolved, these resources will be held
 *    by LUO until the next live update cycle, at which point they will be
 *    discarded.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cleanup.h>
#include <linux/compiler.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kexec_handover.h>
#include <linux/kho/abi/luo.h>
#include <linux/liveupdate.h>
#include <linux/module.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "luo_internal.h"

static LIST_HEAD(luo_file_handler_list);

/* 2 4K pages, give space for 128 files per file_set */
#define LUO_FILE_PGCNT		2ul
#define LUO_FILE_MAX							\
	((LUO_FILE_PGCNT << PAGE_SHIFT) / sizeof(struct luo_file_ser))

/**
 * struct luo_file - Represents a single preserved file instance.
 * @fh:            Pointer to the &struct liveupdate_file_handler that manages
 *                 this type of file.
 * @file:          Pointer to the kernel's &struct file that is being preserved.
 *                 This is NULL in the new kernel until the file is successfully
 *                 retrieved.
 * @serialized_data: The opaque u64 handle to the serialized state of the file.
 *                 This handle is passed back to the handler's .freeze(),
 *                 .retrieve(), and .finish() callbacks, allowing it to track
 *                 and update its serialized state across phases.
 * @private_data:  Pointer to the private data for the file used to hold runtime
 *                 state that is not preserved. Set by the handler's .preserve()
 *                 callback, and must be freed in the handler's .unpreserve()
 *                 callback.
 * @retrieved:     A flag indicating whether a user/kernel in the new kernel has
 *                 successfully called retrieve() on this file. This prevents
 *                 multiple retrieval attempts.
 * @mutex:         A mutex that protects the fields of this specific instance
 *                 (e.g., @retrieved, @file), ensuring that operations like
 *                 retrieving or finishing a file are atomic.
 * @list:          The list_head linking this instance into its parent
 *                 file_set's list of preserved files.
 * @token:         The user-provided unique token used to identify this file.
 *
 * This structure is the core in-kernel representation of a single file being
 * managed through a live update. An instance is created by luo_preserve_file()
 * to link a 'struct file' to its corresponding handler, a user-provided token,
 * and the serialized state handle returned by the handler's .preserve()
 * operation.
 *
 * These instances are tracked in a per-file_set list. The @serialized_data
 * field, which holds a handle to the file's serialized state, may be updated
 * during the .freeze() callback before being serialized for the next kernel.
 * After reboot, these structures are recreated by luo_file_deserialize() and
 * are finally cleaned up by luo_file_finish().
 */
struct luo_file {
	struct liveupdate_file_handler *fh;
	struct file *file;
	u64 serialized_data;
	void *private_data;
	bool retrieved;
	struct mutex mutex;
	struct list_head list;
	u64 token;
};

static int luo_alloc_files_mem(struct luo_file_set *file_set)
{
	size_t size;
	void *mem;

	if (file_set->files)
		return 0;

	WARN_ON_ONCE(file_set->count);

	size = LUO_FILE_PGCNT << PAGE_SHIFT;
	mem = kho_alloc_preserve(size);
	if (IS_ERR(mem))
		return PTR_ERR(mem);

	file_set->files = mem;

	return 0;
}

static void luo_free_files_mem(struct luo_file_set *file_set)
{
	/* If file_set has files, no need to free preservation memory */
	if (file_set->count)
		return;

	if (!file_set->files)
		return;

	kho_unpreserve_free(file_set->files);
	file_set->files = NULL;
}

static bool luo_token_is_used(struct luo_file_set *file_set, u64 token)
{
	struct luo_file *iter;

	list_for_each_entry(iter, &file_set->files_list, list) {
		if (iter->token == token)
			return true;
	}

	return false;
}

/**
 * luo_preserve_file - Initiate the preservation of a file descriptor.
 * @file_set: The file_set to which the preserved file will be added.
 * @token:    A unique, user-provided identifier for the file.
 * @fd:       The file descriptor to be preserved.
 *
 * This function orchestrates the first phase of preserving a file. Upon entry,
 * it takes a reference to the 'struct file' via fget(), effectively making LUO
 * a co-owner of the file. This reference is held until the file is either
 * unpreserved or successfully finished in the next kernel, preventing the file
 * from being prematurely destroyed.
 *
 * This function orchestrates the first phase of preserving a file. It performs
 * the following steps:
 *
 * 1. Validates that the @token is not already in use within the file_set.
 * 2. Ensures the file_set's memory for files serialization is allocated
 *    (allocates if needed).
 * 3. Iterates through registered handlers, calling can_preserve() to find one
 *    compatible with the given @fd.
 * 4. Calls the handler's .preserve() operation, which saves the file's state
 *    and returns an opaque private data handle.
 * 5. Adds the new instance to the file_set's internal list.
 *
 * On success, LUO takes a reference to the 'struct file' and considers it
 * under its management until it is unpreserved or finished.
 *
 * In case of any failure, all intermediate allocations (file reference, memory
 * for the 'luo_file' struct, etc.) are cleaned up before returning an error.
 *
 * Context: Can be called from an ioctl handler during normal system operation.
 * Return: 0 on success. Returns a negative errno on failure:
 *         -EEXIST if the token is already used.
 *         -EBADF if the file descriptor is invalid.
 *         -ENOSPC if the file_set is full.
 *         -ENOENT if no compatible handler is found.
 *         -ENOMEM on memory allocation failure.
 *         Other erros might be returned by .preserve().
 */
int luo_preserve_file(struct luo_file_set *file_set, u64 token, int fd)
{
	struct liveupdate_file_op_args args = {0};
	struct liveupdate_file_handler *fh;
	struct luo_file *luo_file;
	struct file *file;
	int err;

	if (luo_token_is_used(file_set, token))
		return -EEXIST;

	if (file_set->count == LUO_FILE_MAX)
		return -ENOSPC;

	file = fget(fd);
	if (!file)
		return -EBADF;

	err = luo_alloc_files_mem(file_set);
	if (err)
		goto  err_fput;

	err = -ENOENT;
	luo_list_for_each_private(fh, &luo_file_handler_list, list) {
		if (fh->ops->can_preserve(fh, file)) {
			err = 0;
			break;
		}
	}

	/* err is still -ENOENT if no handler was found */
	if (err)
		goto err_free_files_mem;

	luo_file = kzalloc(sizeof(*luo_file), GFP_KERNEL);
	if (!luo_file) {
		err = -ENOMEM;
		goto err_free_files_mem;
	}

	luo_file->file = file;
	luo_file->fh = fh;
	luo_file->token = token;
	luo_file->retrieved = false;
	mutex_init(&luo_file->mutex);

	args.handler = fh;
	args.file = file;
	err = fh->ops->preserve(&args);
	if (err)
		goto err_kfree;

	luo_file->serialized_data = args.serialized_data;
	luo_file->private_data = args.private_data;
	list_add_tail(&luo_file->list, &file_set->files_list);
	file_set->count++;

	return 0;

err_kfree:
	kfree(luo_file);
err_free_files_mem:
	luo_free_files_mem(file_set);
err_fput:
	fput(file);

	return err;
}

/**
 * luo_file_unpreserve_files - Unpreserves all files from a file_set.
 * @file_set: The files to be cleaned up.
 *
 * This function serves as the primary cleanup path for a file_set. It is
 * invoked when the userspace agent closes the file_set's file descriptor.
 *
 * For each file, it performs the following cleanup actions:
 *   1. Calls the handler's .unpreserve() callback to allow the handler to
 *      release any resources it allocated.
 *   2. Removes the file from the file_set's internal tracking list.
 *   3. Releases the reference to the 'struct file' that was taken by
 *      luo_preserve_file() via fput(), returning ownership.
 *   4. Frees the memory associated with the internal 'struct luo_file'.
 *
 * After all individual files are unpreserved, it frees the contiguous memory
 * block that was allocated to hold their serialization data.
 */
void luo_file_unpreserve_files(struct luo_file_set *file_set)
{
	struct luo_file *luo_file;

	while (!list_empty(&file_set->files_list)) {
		struct liveupdate_file_op_args args = {0};

		luo_file = list_last_entry(&file_set->files_list,
					   struct luo_file, list);

		args.handler = luo_file->fh;
		args.file = luo_file->file;
		args.serialized_data = luo_file->serialized_data;
		args.private_data = luo_file->private_data;
		luo_file->fh->ops->unpreserve(&args);

		list_del(&luo_file->list);
		file_set->count--;

		fput(luo_file->file);
		mutex_destroy(&luo_file->mutex);
		kfree(luo_file);
	}

	luo_free_files_mem(file_set);
}

static int luo_file_freeze_one(struct luo_file_set *file_set,
			       struct luo_file *luo_file)
{
	int err = 0;

	guard(mutex)(&luo_file->mutex);

	if (luo_file->fh->ops->freeze) {
		struct liveupdate_file_op_args args = {0};

		args.handler = luo_file->fh;
		args.file = luo_file->file;
		args.serialized_data = luo_file->serialized_data;
		args.private_data = luo_file->private_data;

		err = luo_file->fh->ops->freeze(&args);
		if (!err)
			luo_file->serialized_data = args.serialized_data;
	}

	return err;
}

static void luo_file_unfreeze_one(struct luo_file_set *file_set,
				  struct luo_file *luo_file)
{
	guard(mutex)(&luo_file->mutex);

	if (luo_file->fh->ops->unfreeze) {
		struct liveupdate_file_op_args args = {0};

		args.handler = luo_file->fh;
		args.file = luo_file->file;
		args.serialized_data = luo_file->serialized_data;
		args.private_data = luo_file->private_data;

		luo_file->fh->ops->unfreeze(&args);
	}

	luo_file->serialized_data = 0;
}

static void __luo_file_unfreeze(struct luo_file_set *file_set,
				struct luo_file *failed_entry)
{
	struct list_head *files_list = &file_set->files_list;
	struct luo_file *luo_file;

	list_for_each_entry(luo_file, files_list, list) {
		if (luo_file == failed_entry)
			break;

		luo_file_unfreeze_one(file_set, luo_file);
	}

	memset(file_set->files, 0, LUO_FILE_PGCNT << PAGE_SHIFT);
}

/**
 * luo_file_freeze - Freezes all preserved files and serializes their metadata.
 * @file_set:     The file_set whose files are to be frozen.
 * @file_set_ser: Where to put the serialized file_set.
 *
 * This function is called from the reboot() syscall path, just before the
 * kernel transitions to the new image via kexec. Its purpose is to perform the
 * final preparation and serialization of all preserved files in the file_set.
 *
 * It iterates through each preserved file in FIFO order (the order of
 * preservation) and performs two main actions:
 *
 * 1. Freezes the File: It calls the handler's .freeze() callback for each
 *    file. This gives the handler a final opportunity to quiesce the device or
 *    prepare its state for the upcoming reboot. The handler may update its
 *    private data handle during this step.
 *
 * 2. Serializes Metadata: After a successful freeze, it copies the final file
 *    metadata—the handler's compatible string, the user token, and the final
 *    private data handle—into the pre-allocated contiguous memory buffer
 *    (file_set->files) that will be handed over to the next kernel via KHO.
 *
 * Error Handling (Rollback):
 * This function is atomic. If any handler's .freeze() operation fails, the
 * entire live update is aborted. The __luo_file_unfreeze() helper is
 * immediately called to invoke the .unfreeze() op on all files that were
 * successfully frozen before the point of failure, rolling them back to a
 * running state. The function then returns an error, causing the reboot()
 * syscall to fail.
 *
 * Context: Called only from the liveupdate_reboot() path.
 * Return: 0 on success, or a negative errno on failure.
 */
int luo_file_freeze(struct luo_file_set *file_set,
		    struct luo_file_set_ser *file_set_ser)
{
	struct luo_file_ser *file_ser = file_set->files;
	struct luo_file *luo_file;
	int err;
	int i;

	if (!file_set->count)
		return 0;

	if (WARN_ON(!file_ser))
		return -EINVAL;

	i = 0;
	list_for_each_entry(luo_file, &file_set->files_list, list) {
		err = luo_file_freeze_one(file_set, luo_file);
		if (err < 0) {
			pr_warn("Freeze failed for token[%#0llx] handler[%s] err[%pe]\n",
				luo_file->token, luo_file->fh->compatible,
				ERR_PTR(err));
			goto err_unfreeze;
		}

		strscpy(file_ser[i].compatible, luo_file->fh->compatible,
			sizeof(file_ser[i].compatible));
		file_ser[i].data = luo_file->serialized_data;
		file_ser[i].token = luo_file->token;
		i++;
	}

	file_set_ser->count = file_set->count;
	if (file_set->files)
		file_set_ser->files = virt_to_phys(file_set->files);

	return 0;

err_unfreeze:
	__luo_file_unfreeze(file_set, luo_file);

	return err;
}

/**
 * luo_file_unfreeze - Unfreezes all files in a file_set and clear serialization
 * @file_set:     The file_set whose files are to be unfrozen.
 * @file_set_ser: Serialized file_set.
 *
 * This function rolls back the state of all files in a file_set after the
 * freeze phase has begun but must be aborted. It is the counterpart to
 * luo_file_freeze().
 *
 * It invokes the __luo_file_unfreeze() helper with a NULL argument, which
 * signals the helper to iterate through all files in the file_set and call
 * their respective .unfreeze() handler callbacks.
 *
 * Context: This is called when the live update is aborted during
 *          the reboot() syscall, after luo_file_freeze() has been called.
 */
void luo_file_unfreeze(struct luo_file_set *file_set,
		       struct luo_file_set_ser *file_set_ser)
{
	if (!file_set->count)
		return;

	__luo_file_unfreeze(file_set, NULL);
	memset(file_set_ser, 0, sizeof(*file_set_ser));
}

/**
 * luo_retrieve_file - Restores a preserved file from a file_set by its token.
 * @file_set: The file_set from which to retrieve the file.
 * @token:    The unique token identifying the file to be restored.
 * @filep:    Output parameter; on success, this is populated with a pointer
 *            to the newly retrieved 'struct file'.
 *
 * This function is the primary mechanism for recreating a file in the new
 * kernel after a live update. It searches the file_set's list of deserialized
 * files for an entry matching the provided @token.
 *
 * The operation is idempotent: if a file has already been successfully
 * retrieved, this function will simply return a pointer to the existing
 * 'struct file' and report success without re-executing the retrieve
 * operation. This is handled by checking the 'retrieved' flag under a lock.
 *
 * File retrieval can happen in any order; it is not bound by the order of
 * preservation.
 *
 * Context: Can be called from an ioctl or other in-kernel code in the new
 *          kernel.
 * Return: 0 on success. Returns a negative errno on failure:
 *         -ENOENT if no file with the matching token is found.
 *         Any error code returned by the handler's .retrieve() op.
 */
int luo_retrieve_file(struct luo_file_set *file_set, u64 token,
		      struct file **filep)
{
	struct liveupdate_file_op_args args = {0};
	struct luo_file *luo_file;
	bool found = false;
	int err;

	if (list_empty(&file_set->files_list))
		return -ENOENT;

	list_for_each_entry(luo_file, &file_set->files_list, list) {
		if (luo_file->token == token) {
			found = true;
			break;
		}
	}

	if (!found)
		return -ENOENT;

	guard(mutex)(&luo_file->mutex);
	if (luo_file->retrieved) {
		/*
		 * Someone is asking for this file again, so get a reference
		 * for them.
		 */
		get_file(luo_file->file);
		*filep = luo_file->file;
		return 0;
	}

	args.handler = luo_file->fh;
	args.serialized_data = luo_file->serialized_data;
	err = luo_file->fh->ops->retrieve(&args);
	if (!err) {
		luo_file->file = args.file;

		/* Get reference so we can keep this file in LUO until finish */
		get_file(luo_file->file);
		*filep = luo_file->file;
		luo_file->retrieved = true;
	}

	return err;
}

static int luo_file_can_finish_one(struct luo_file_set *file_set,
				   struct luo_file *luo_file)
{
	bool can_finish = true;

	guard(mutex)(&luo_file->mutex);

	if (luo_file->fh->ops->can_finish) {
		struct liveupdate_file_op_args args = {0};

		args.handler = luo_file->fh;
		args.file = luo_file->file;
		args.serialized_data = luo_file->serialized_data;
		args.retrieved = luo_file->retrieved;
		can_finish = luo_file->fh->ops->can_finish(&args);
	}

	return can_finish ? 0 : -EBUSY;
}

static void luo_file_finish_one(struct luo_file_set *file_set,
				struct luo_file *luo_file)
{
	struct liveupdate_file_op_args args = {0};

	guard(mutex)(&luo_file->mutex);

	args.handler = luo_file->fh;
	args.file = luo_file->file;
	args.serialized_data = luo_file->serialized_data;
	args.retrieved = luo_file->retrieved;

	luo_file->fh->ops->finish(&args);
}

/**
 * luo_file_finish - Completes the lifecycle for all files in a file_set.
 * @file_set: The file_set to be finalized.
 *
 * This function orchestrates the final teardown of a live update file_set in
 * the new kernel. It should be called after all necessary files have been
 * retrieved and the userspace agent is ready to release the preserved state.
 *
 * The function iterates through all tracked files. For each file, it performs
 * the following sequence of cleanup actions:
 *
 * 1. If file is not yet retrieved, retrieves it, and calls can_finish() on
 *    every file in the file_set. If all can_finish return true, continue to
 *    finish.
 * 2. Calls the handler's .finish() callback (via luo_file_finish_one) to
 *    allow for final resource cleanup within the handler.
 * 3. Releases LUO's ownership reference on the 'struct file' via fput(). This
 *    is the counterpart to the get_file() call in luo_retrieve_file().
 * 4. Removes the 'struct luo_file' from the file_set's internal list.
 * 5. Frees the memory for the 'struct luo_file' instance itself.
 *
 * After successfully finishing all individual files, it frees the
 * contiguous memory block that was used to transfer the serialized metadata
 * from the previous kernel.
 *
 * Error Handling (Atomic Failure):
 * This operation is atomic. If any handler's .can_finish() op fails, the entire
 * function aborts immediately and returns an error.
 *
 * Context: Can be called from an ioctl handler in the new kernel.
 * Return: 0 on success, or a negative errno on failure.
 */
int luo_file_finish(struct luo_file_set *file_set)
{
	struct list_head *files_list = &file_set->files_list;
	struct luo_file *luo_file;
	int err;

	if (!file_set->count)
		return 0;

	list_for_each_entry(luo_file, files_list, list) {
		err = luo_file_can_finish_one(file_set, luo_file);
		if (err)
			return err;
	}

	while (!list_empty(&file_set->files_list)) {
		luo_file = list_last_entry(&file_set->files_list,
					   struct luo_file, list);

		luo_file_finish_one(file_set, luo_file);

		if (luo_file->file)
			fput(luo_file->file);
		list_del(&luo_file->list);
		file_set->count--;
		mutex_destroy(&luo_file->mutex);
		kfree(luo_file);
	}

	if (file_set->files) {
		kho_restore_free(file_set->files);
		file_set->files = NULL;
	}

	return 0;
}

/**
 * luo_file_deserialize - Reconstructs the list of preserved files in the new kernel.
 * @file_set:     The incoming file_set to fill with deserialized data.
 * @file_set_ser: Serialized KHO file_set data from the previous kernel.
 *
 * This function is called during the early boot process of the new kernel. It
 * takes the raw, contiguous memory block of 'struct luo_file_ser' entries,
 * provided by the previous kernel, and transforms it back into a live,
 * in-memory linked list of 'struct luo_file' instances.
 *
 * For each serialized entry, it performs the following steps:
 *   1. Reads the 'compatible' string.
 *   2. Searches the global list of registered file handlers for one that
 *      matches the compatible string.
 *   3. Allocates a new 'struct luo_file'.
 *   4. Populates the new structure with the deserialized data (token, private
 *      data handle) and links it to the found handler. The 'file' pointer is
 *      initialized to NULL, as the file has not been retrieved yet.
 *   5. Adds the new 'struct luo_file' to the file_set's files_list.
 *
 * This prepares the file_set for userspace, which can later call
 * luo_retrieve_file() to restore the actual file descriptors.
 *
 * Context: Called from session deserialization.
 */
int luo_file_deserialize(struct luo_file_set *file_set,
			 struct luo_file_set_ser *file_set_ser)
{
	struct luo_file_ser *file_ser;
	u64 i;

	if (!file_set_ser->files) {
		WARN_ON(file_set_ser->count);
		return 0;
	}

	file_set->count = file_set_ser->count;
	file_set->files = phys_to_virt(file_set_ser->files);

	/*
	 * Note on error handling:
	 *
	 * If deserialization fails (e.g., allocation failure or corrupt data),
	 * we intentionally skip cleanup of files that were already restored.
	 *
	 * A partial failure leaves the preserved state inconsistent.
	 * Implementing a safe "undo" to unwind complex dependencies (sessions,
	 * files, hardware state) is error-prone and provides little value, as
	 * the system is effectively in a broken state.
	 *
	 * We treat these resources as leaked. The expected recovery path is for
	 * userspace to detect the failure and trigger a reboot, which will
	 * reliably reset devices and reclaim memory.
	 */
	file_ser = file_set->files;
	for (i = 0; i < file_set->count; i++) {
		struct liveupdate_file_handler *fh;
		bool handler_found = false;
		struct luo_file *luo_file;

		luo_list_for_each_private(fh, &luo_file_handler_list, list) {
			if (!strcmp(fh->compatible, file_ser[i].compatible)) {
				handler_found = true;
				break;
			}
		}

		if (!handler_found) {
			pr_warn("No registered handler for compatible '%s'\n",
				file_ser[i].compatible);
			return -ENOENT;
		}

		luo_file = kzalloc(sizeof(*luo_file), GFP_KERNEL);
		if (!luo_file)
			return -ENOMEM;

		luo_file->fh = fh;
		luo_file->file = NULL;
		luo_file->serialized_data = file_ser[i].data;
		luo_file->token = file_ser[i].token;
		luo_file->retrieved = false;
		mutex_init(&luo_file->mutex);
		list_add_tail(&luo_file->list, &file_set->files_list);
	}

	return 0;
}

void luo_file_set_init(struct luo_file_set *file_set)
{
	INIT_LIST_HEAD(&file_set->files_list);
}

void luo_file_set_destroy(struct luo_file_set *file_set)
{
	WARN_ON(file_set->count);
	WARN_ON(!list_empty(&file_set->files_list));
}

/**
 * liveupdate_register_file_handler - Register a file handler with LUO.
 * @fh: Pointer to a caller-allocated &struct liveupdate_file_handler.
 * The caller must initialize this structure, including a unique
 * 'compatible' string and a valid 'fh' callbacks. This function adds the
 * handler to the global list of supported file handlers.
 *
 * Context: Typically called during module initialization for file types that
 * support live update preservation.
 *
 * Return: 0 on success. Negative errno on failure.
 */
int liveupdate_register_file_handler(struct liveupdate_file_handler *fh)
{
	struct liveupdate_file_handler *fh_iter;
	int err;

	if (!liveupdate_enabled())
		return -EOPNOTSUPP;

	/* Sanity check that all required callbacks are set */
	if (!fh->ops->preserve || !fh->ops->unpreserve || !fh->ops->retrieve ||
	    !fh->ops->finish || !fh->ops->can_preserve) {
		return -EINVAL;
	}

	/*
	 * Ensure the system is quiescent (no active sessions).
	 * This prevents registering new handlers while sessions are active or
	 * while deserialization is in progress.
	 */
	if (!luo_session_quiesce())
		return -EBUSY;

	/* Check for duplicate compatible strings */
	luo_list_for_each_private(fh_iter, &luo_file_handler_list, list) {
		if (!strcmp(fh_iter->compatible, fh->compatible)) {
			pr_err("File handler registration failed: Compatible string '%s' already registered.\n",
			       fh->compatible);
			err = -EEXIST;
			goto err_resume;
		}
	}

	/* Pin the module implementing the handler */
	if (!try_module_get(fh->ops->owner)) {
		err = -EAGAIN;
		goto err_resume;
	}

	INIT_LIST_HEAD(&ACCESS_PRIVATE(fh, list));
	list_add_tail(&ACCESS_PRIVATE(fh, list), &luo_file_handler_list);
	luo_session_resume();

	return 0;

err_resume:
	luo_session_resume();
	return err;
}

/**
 * liveupdate_unregister_file_handler - Unregister a liveupdate file handler
 * @fh: The file handler to unregister
 *
 * Unregisters the file handler from the liveupdate core. This function
 * reverses the operations of liveupdate_register_file_handler().
 *
 * It ensures safe removal by checking that:
 * No live update session is currently in progress.
 *
 * If the unregistration fails, the internal test state is reverted.
 *
 * Return: 0 Success. -EOPNOTSUPP when live update is not enabled. -EBUSY A live
 * update is in progress, can't quiesce live update.
 */
int liveupdate_unregister_file_handler(struct liveupdate_file_handler *fh)
{
	if (!liveupdate_enabled())
		return -EOPNOTSUPP;

	if (!luo_session_quiesce())
		return -EBUSY;

	list_del(&ACCESS_PRIVATE(fh, list));
	module_put(fh->ops->owner);
	luo_session_resume();

	return 0;
}
