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
#include <linux/mutex.h>
#include <linux/types.h>
#include <uapi/linux/liveupdate.h>

struct liveupdate_file_handler;
struct liveupdate_flb;
struct liveupdate_session;
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
	/* A list of FLB dependencies. */
	struct list_head __private flb_list;
};

/**
 * struct liveupdate_flb_op_args - Arguments for FLB operation callbacks.
 * @flb:       The global FLB instance for which this call is performed.
 * @data:      For .preserve():    [OUT] The callback sets this field.
 *             For .unpreserve():  [IN]  The handle from .preserve().
 *             For .retrieve():    [IN]  The handle from .preserve().
 * @obj:       For .preserve():    [OUT] Sets this to the live object.
 *             For .retrieve():    [OUT] Sets this to the live object.
 *             For .finish():      [IN]  The live object from .retrieve().
 *
 * This structure bundles all parameters for the FLB operation callbacks.
 */
struct liveupdate_flb_op_args {
	struct liveupdate_flb *flb;
	u64 data;
	void *obj;
};

/**
 * struct liveupdate_flb_ops - Callbacks for global File-Lifecycle-Bound data.
 * @preserve:        Called when the first file using this FLB is preserved.
 *                   The callback must save its state and return a single,
 *                   self-contained u64 handle by setting the 'argp->data'
 *                   field and 'argp->obj'.
 * @unpreserve:      Called when the last file using this FLB is unpreserved
 *                   (aborted before reboot). Receives the handle via
 *                   'argp->data' and live object via 'argp->obj'.
 * @retrieve:        Called on-demand in the new kernel, the first time a
 *                   component requests access to the shared object. It receives
 *                   the preserved handle via 'argp->data' and must reconstruct
 *                   the live object, returning it by setting the 'argp->obj'
 *                   field.
 * @finish:          Called in the new kernel when the last file using this FLB
 *                   is finished. Receives the live object via 'argp->obj' for
 *                   cleanup.
 * @owner:           Module reference
 *
 * Operations that manage global shared data with file bound lifecycle,
 * triggered by the first file that uses it and concluded by the last file that
 * uses it, across all sessions.
 */
struct liveupdate_flb_ops {
	int (*preserve)(struct liveupdate_flb_op_args *argp);
	void (*unpreserve)(struct liveupdate_flb_op_args *argp);
	int (*retrieve)(struct liveupdate_flb_op_args *argp);
	void (*finish)(struct liveupdate_flb_op_args *argp);
	struct module *owner;
};

/*
 * struct luo_flb_private_state - Private FLB state structures.
 * @count:     The number of preserved files currently depending on this FLB.
 *             This is used to trigger the preserve/unpreserve/finish ops on the
 *             first/last file.
 * @data:      The opaque u64 handle returned by .preserve() or passed to
 *             .retrieve().
 * @obj:       The live kernel object returned by .preserve() or .retrieve().
 * @lock:      A mutex that protects all fields within this structure, providing
 *             the synchronization service for the FLB's ops.
 * @finished:  True once the FLB's finish() callback has run.
 * @retrieved: True once the FLB's retrieve() callback has run.
 */
struct luo_flb_private_state {
	long count;
	u64 data;
	void *obj;
	struct mutex lock;
	bool finished;
	bool retrieved;
};

/*
 * struct luo_flb_private - Keep separate incoming and outgoing states.
 * @list:        A global list of registered FLBs.
 * @outgoing:    The runtime state for the pre-reboot
 *               (preserve/unpreserve) lifecycle.
 * @incoming:    The runtime state for the post-reboot (retrieve/finish)
 *               lifecycle.
 * @users:       With how many File-Handlers this FLB is registered.
 * @initialized: true when private fields have been initialized.
 */
struct luo_flb_private {
	struct list_head list;
	struct luo_flb_private_state outgoing;
	struct luo_flb_private_state incoming;
	int users;
	bool initialized;
};

/**
 * struct liveupdate_flb - A global definition for a shared data object.
 * @ops:         Callback functions
 * @compatible:  The compatibility string (e.g., "iommu-core-v1"
 *               that uniquely identifies the FLB type this handler
 *               supports. This is matched against the compatible string
 *               associated with individual &struct liveupdate_flb
 *               instances.
 *
 * This struct is the "template" that a driver registers to define a shared,
 * file-lifecycle-bound object. The actual runtime state (the live object,
 * refcount, etc.) is managed privately by the LUO core.
 */
struct liveupdate_flb {
	const struct liveupdate_flb_ops *ops;
	const char compatible[LIVEUPDATE_FLB_COMPAT_LENGTH];

	/* private: */
	struct luo_flb_private __private private;
};

#ifdef CONFIG_LIVEUPDATE

/* Return true if live update orchestrator is enabled */
bool liveupdate_enabled(void);

/* Called during kexec to tell LUO that entered into reboot */
int liveupdate_reboot(void);

int liveupdate_register_file_handler(struct liveupdate_file_handler *fh);
int liveupdate_unregister_file_handler(struct liveupdate_file_handler *fh);

int liveupdate_register_flb(struct liveupdate_file_handler *fh,
			    struct liveupdate_flb *flb);
int liveupdate_unregister_flb(struct liveupdate_file_handler *fh,
			      struct liveupdate_flb *flb);

int liveupdate_flb_get_incoming(struct liveupdate_flb *flb, void **objp);
int liveupdate_flb_get_outgoing(struct liveupdate_flb *flb, void **objp);

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

static inline int liveupdate_register_flb(struct liveupdate_file_handler *fh,
					  struct liveupdate_flb *flb)
{
	return -EOPNOTSUPP;
}

static inline int liveupdate_unregister_flb(struct liveupdate_file_handler *fh,
					    struct liveupdate_flb *flb)
{
	return -EOPNOTSUPP;
}

static inline int liveupdate_flb_get_incoming(struct liveupdate_flb *flb,
					      void **objp)
{
	return -EOPNOTSUPP;
}

static inline int liveupdate_flb_get_outgoing(struct liveupdate_flb *flb,
					      void **objp)
{
	return -EOPNOTSUPP;
}

#endif /* CONFIG_LIVEUPDATE */
#endif /* _LINUX_LIVEUPDATE_H */
