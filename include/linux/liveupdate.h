/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */
#ifndef _LINUX_LIVEUPDATE_H
#define _LINUX_LIVEUPDATE_H

#include <linux/bug.h>
#include <linux/compiler.h>
#include <linux/kho/abi/luo.h>
#include <linux/list.h>
#include <linux/types.h>
#include <uapi/linux/liveupdate.h>

struct liveupdate_file_handler;
struct file;

/**
 * struct liveupdate_file_op_args - Arguments for file operation callbacks.
 * @handler:          The file handler being called.
 * @retrieved:        The retrieve status for the 'can_finish / finish'
 *                    operation.
 * @file:             The file object. For retrieve: [OUT] The callback sets
 *                    this to the new file. For other ops: [IN] The caller sets
 *                    this to the file being operated on.
 * @serialized_data:  The opaque u64 handle, preserve/prepare/freeze may update
 *                    this field.
 * @private_data:     Private data for the file used to hold runtime state that
 *                    is not preserved. Set by the handler's .preserve()
 *                    callback, and must be freed in the handler's
 *                    .unpreserve() callback.
 *
 * This structure bundles all parameters for the file operation callbacks.
 * The 'data' and 'file' fields are used for both input and output.
 */
struct liveupdate_file_op_args {
	struct liveupdate_file_handler *handler;
	bool retrieved;
	struct file *file;
	u64 serialized_data;
	void *private_data;
};

/**
 * struct liveupdate_file_ops - Callbacks for live-updatable files.
 * @can_preserve: Required. Lightweight check to see if this handler is
 *                compatible with the given file.
 * @preserve:     Required. Performs state-saving for the file.
 * @unpreserve:   Required. Cleans up any resources allocated by @preserve.
 * @freeze:       Optional. Final actions just before kernel transition.
 * @unfreeze:     Optional. Undo freeze operations.
 * @retrieve:     Required. Restores the file in the new kernel.
 * @can_finish:   Optional. Check if this FD can finish, i.e. all restoration
 *                pre-requirements for this FD are satisfied. Called prior to
 *                finish, in order to do successful finish calls for all
 *                resources in the session.
 * @finish:       Required. Final cleanup in the new kernel.
 * @owner:        Module reference
 *
 * All operations (except can_preserve) receive a pointer to a
 * 'struct liveupdate_file_op_args' containing the necessary context.
 */
struct liveupdate_file_ops {
	bool (*can_preserve)(struct liveupdate_file_handler *handler,
			     struct file *file);
	int (*preserve)(struct liveupdate_file_op_args *args);
	void (*unpreserve)(struct liveupdate_file_op_args *args);
	int (*freeze)(struct liveupdate_file_op_args *args);
	void (*unfreeze)(struct liveupdate_file_op_args *args);
	int (*retrieve)(struct liveupdate_file_op_args *args);
	bool (*can_finish)(struct liveupdate_file_op_args *args);
	void (*finish)(struct liveupdate_file_op_args *args);
	struct module *owner;
};

/**
 * struct liveupdate_file_handler - Represents a handler for a live-updatable file type.
 * @ops:                Callback functions
 * @compatible:         The compatibility string (e.g., "memfd-v1", "vfiofd-v1")
 *                      that uniquely identifies the file type this handler
 *                      supports. This is matched against the compatible string
 *                      associated with individual &struct file instances.
 *
 * Modules that want to support live update for specific file types should
 * register an instance of this structure. LUO uses this registration to
 * determine if a given file can be preserved and to find the appropriate
 * operations to manage its state across the update.
 */
struct liveupdate_file_handler {
	const struct liveupdate_file_ops *ops;
	const char compatible[LIVEUPDATE_HNDL_COMPAT_LENGTH];

	/* private: */

	/*
	 * Used for linking this handler instance into a global list of
	 * registered file handlers.
	 */
	struct list_head __private list;
};

#ifdef CONFIG_LIVEUPDATE

/* Return true if live update orchestrator is enabled */
bool liveupdate_enabled(void);

/* Called during kexec to tell LUO that entered into reboot */
int liveupdate_reboot(void);

int liveupdate_register_file_handler(struct liveupdate_file_handler *fh);
int liveupdate_unregister_file_handler(struct liveupdate_file_handler *fh);

#else /* CONFIG_LIVEUPDATE */

static inline bool liveupdate_enabled(void)
{
	return false;
}

static inline int liveupdate_reboot(void)
{
	return 0;
}

static inline int liveupdate_register_file_handler(struct liveupdate_file_handler *fh)
{
	return -EOPNOTSUPP;
}

static inline int liveupdate_unregister_file_handler(struct liveupdate_file_handler *fh)
{
	return -EOPNOTSUPP;
}

#endif /* CONFIG_LIVEUPDATE */
#endif /* _LINUX_LIVEUPDATE_H */
