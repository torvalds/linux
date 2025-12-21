// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

/**
 * DOC: LUO Sessions
 *
 * LUO Sessions provide the core mechanism for grouping and managing `struct
 * file *` instances that need to be preserved across a kexec-based live
 * update. Each session acts as a named container for a set of file objects,
 * allowing a userspace agent to manage the lifecycle of resources critical to a
 * workload.
 *
 * Core Concepts:
 *
 * - Named Containers: Sessions are identified by a unique, user-provided name,
 *   which is used for both creation in the current kernel and retrieval in the
 *   next kernel.
 *
 * - Userspace Interface: Session management is driven from userspace via
 *   ioctls on /dev/liveupdate.
 *
 * - Serialization: Session metadata is preserved using the KHO framework. When
 *   a live update is triggered via kexec, an array of `struct luo_session_ser`
 *   is populated and placed in a preserved memory region. An FDT node is also
 *   created, containing the count of sessions and the physical address of this
 *   array.
 *
 * Session Lifecycle:
 *
 * 1.  Creation: A userspace agent calls `luo_session_create()` to create a
 *     new, empty session and receives a file descriptor for it.
 *
 * 2.  Serialization: When the `reboot(LINUX_REBOOT_CMD_KEXEC)` syscall is
 *     made, `luo_session_serialize()` is called. It iterates through all
 *     active sessions and writes their metadata into a memory area preserved
 *     by KHO.
 *
 * 3.  Deserialization (in new kernel): After kexec, `luo_session_deserialize()`
 *     runs, reading the serialized data and creating a list of `struct
 *     luo_session` objects representing the preserved sessions.
 *
 * 4.  Retrieval: A userspace agent in the new kernel can then call
 *     `luo_session_retrieve()` with a session name to get a new file
 *     descriptor and access the preserved state.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/anon_inodes.h>
#include <linux/cleanup.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kexec_handover.h>
#include <linux/kho/abi/luo.h>
#include <linux/libfdt.h>
#include <linux/list.h>
#include <linux/liveupdate.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/unaligned.h>
#include <uapi/linux/liveupdate.h>
#include "luo_internal.h"

/* 16 4K pages, give space for 744 sessions */
#define LUO_SESSION_PGCNT	16ul
#define LUO_SESSION_MAX		(((LUO_SESSION_PGCNT << PAGE_SHIFT) -	\
		sizeof(struct luo_session_header_ser)) /		\
		sizeof(struct luo_session_ser))

/**
 * struct luo_session_header - Header struct for managing LUO sessions.
 * @count:      The number of sessions currently tracked in the @list.
 * @list:       The head of the linked list of `struct luo_session` instances.
 * @rwsem:      A read-write semaphore providing synchronized access to the
 *              session list and other fields in this structure.
 * @header_ser: The header data of serialization array.
 * @ser:        The serialized session data (an array of
 *              `struct luo_session_ser`).
 * @active:     Set to true when first initialized. If previous kernel did not
 *              send session data, active stays false for incoming.
 */
struct luo_session_header {
	long count;
	struct list_head list;
	struct rw_semaphore rwsem;
	struct luo_session_header_ser *header_ser;
	struct luo_session_ser *ser;
	bool active;
};

/**
 * struct luo_session_global - Global container for managing LUO sessions.
 * @incoming:     The sessions passed from the previous kernel.
 * @outgoing:     The sessions that are going to be passed to the next kernel.
 */
struct luo_session_global {
	struct luo_session_header incoming;
	struct luo_session_header outgoing;
};

static struct luo_session_global luo_session_global = {
	.incoming = {
		.list = LIST_HEAD_INIT(luo_session_global.incoming.list),
		.rwsem = __RWSEM_INITIALIZER(luo_session_global.incoming.rwsem),
	},
	.outgoing = {
		.list = LIST_HEAD_INIT(luo_session_global.outgoing.list),
		.rwsem = __RWSEM_INITIALIZER(luo_session_global.outgoing.rwsem),
	},
};

static struct luo_session *luo_session_alloc(const char *name)
{
	struct luo_session *session = kzalloc(sizeof(*session), GFP_KERNEL);

	if (!session)
		return ERR_PTR(-ENOMEM);

	strscpy(session->name, name, sizeof(session->name));
	INIT_LIST_HEAD(&session->file_set.files_list);
	luo_file_set_init(&session->file_set);
	INIT_LIST_HEAD(&session->list);
	mutex_init(&session->mutex);

	return session;
}

static void luo_session_free(struct luo_session *session)
{
	luo_file_set_destroy(&session->file_set);
	mutex_destroy(&session->mutex);
	kfree(session);
}

static int luo_session_insert(struct luo_session_header *sh,
			      struct luo_session *session)
{
	struct luo_session *it;

	guard(rwsem_write)(&sh->rwsem);

	/*
	 * For outgoing we should make sure there is room in serialization array
	 * for new session.
	 */
	if (sh == &luo_session_global.outgoing) {
		if (sh->count == LUO_SESSION_MAX)
			return -ENOMEM;
	}

	/*
	 * For small number of sessions this loop won't hurt performance
	 * but if we ever start using a lot of sessions, this might
	 * become a bottle neck during deserialization time, as it would
	 * cause O(n*n) complexity.
	 */
	list_for_each_entry(it, &sh->list, list) {
		if (!strncmp(it->name, session->name, sizeof(it->name)))
			return -EEXIST;
	}
	list_add_tail(&session->list, &sh->list);
	sh->count++;

	return 0;
}

static void luo_session_remove(struct luo_session_header *sh,
			       struct luo_session *session)
{
	guard(rwsem_write)(&sh->rwsem);
	list_del(&session->list);
	sh->count--;
}

static int luo_session_finish_one(struct luo_session *session)
{
	guard(mutex)(&session->mutex);
	return luo_file_finish(&session->file_set);
}

static void luo_session_unfreeze_one(struct luo_session *session,
				     struct luo_session_ser *ser)
{
	guard(mutex)(&session->mutex);
	luo_file_unfreeze(&session->file_set, &ser->file_set_ser);
}

static int luo_session_freeze_one(struct luo_session *session,
				  struct luo_session_ser *ser)
{
	guard(mutex)(&session->mutex);
	return luo_file_freeze(&session->file_set, &ser->file_set_ser);
}

static int luo_session_release(struct inode *inodep, struct file *filep)
{
	struct luo_session *session = filep->private_data;
	struct luo_session_header *sh;

	/* If retrieved is set, it means this session is from incoming list */
	if (session->retrieved) {
		int err = luo_session_finish_one(session);

		if (err) {
			pr_warn("Unable to finish session [%s] on release\n",
				session->name);
			return err;
		}
		sh = &luo_session_global.incoming;
	} else {
		scoped_guard(mutex, &session->mutex)
			luo_file_unpreserve_files(&session->file_set);
		sh = &luo_session_global.outgoing;
	}

	luo_session_remove(sh, session);
	luo_session_free(session);

	return 0;
}

static int luo_session_preserve_fd(struct luo_session *session,
				   struct luo_ucmd *ucmd)
{
	struct liveupdate_session_preserve_fd *argp = ucmd->cmd;
	int err;

	guard(mutex)(&session->mutex);
	err = luo_preserve_file(&session->file_set, argp->token, argp->fd);
	if (err)
		return err;

	err = luo_ucmd_respond(ucmd, sizeof(*argp));
	if (err)
		pr_warn("The file was successfully preserved, but response to user failed\n");

	return err;
}

static int luo_session_retrieve_fd(struct luo_session *session,
				   struct luo_ucmd *ucmd)
{
	struct liveupdate_session_retrieve_fd *argp = ucmd->cmd;
	struct file *file;
	int err;

	argp->fd = get_unused_fd_flags(O_CLOEXEC);
	if (argp->fd < 0)
		return argp->fd;

	guard(mutex)(&session->mutex);
	err = luo_retrieve_file(&session->file_set, argp->token, &file);
	if (err < 0)
		goto  err_put_fd;

	err = luo_ucmd_respond(ucmd, sizeof(*argp));
	if (err)
		goto err_put_file;

	fd_install(argp->fd, file);

	return 0;

err_put_file:
	fput(file);
err_put_fd:
	put_unused_fd(argp->fd);

	return err;
}

static int luo_session_finish(struct luo_session *session,
			      struct luo_ucmd *ucmd)
{
	struct liveupdate_session_finish *argp = ucmd->cmd;
	int err = luo_session_finish_one(session);

	if (err)
		return err;

	return luo_ucmd_respond(ucmd, sizeof(*argp));
}

union ucmd_buffer {
	struct liveupdate_session_finish finish;
	struct liveupdate_session_preserve_fd preserve;
	struct liveupdate_session_retrieve_fd retrieve;
};

struct luo_ioctl_op {
	unsigned int size;
	unsigned int min_size;
	unsigned int ioctl_num;
	int (*execute)(struct luo_session *session, struct luo_ucmd *ucmd);
};

#define IOCTL_OP(_ioctl, _fn, _struct, _last)                                  \
	[_IOC_NR(_ioctl) - LIVEUPDATE_CMD_SESSION_BASE] = {                    \
		.size = sizeof(_struct) +                                      \
			BUILD_BUG_ON_ZERO(sizeof(union ucmd_buffer) <          \
					  sizeof(_struct)),                    \
		.min_size = offsetofend(_struct, _last),                       \
		.ioctl_num = _ioctl,                                           \
		.execute = _fn,                                                \
	}

static const struct luo_ioctl_op luo_session_ioctl_ops[] = {
	IOCTL_OP(LIVEUPDATE_SESSION_FINISH, luo_session_finish,
		 struct liveupdate_session_finish, reserved),
	IOCTL_OP(LIVEUPDATE_SESSION_PRESERVE_FD, luo_session_preserve_fd,
		 struct liveupdate_session_preserve_fd, token),
	IOCTL_OP(LIVEUPDATE_SESSION_RETRIEVE_FD, luo_session_retrieve_fd,
		 struct liveupdate_session_retrieve_fd, token),
};

static long luo_session_ioctl(struct file *filep, unsigned int cmd,
			      unsigned long arg)
{
	struct luo_session *session = filep->private_data;
	const struct luo_ioctl_op *op;
	struct luo_ucmd ucmd = {};
	union ucmd_buffer buf;
	unsigned int nr;
	int ret;

	nr = _IOC_NR(cmd);
	if (nr < LIVEUPDATE_CMD_SESSION_BASE || (nr - LIVEUPDATE_CMD_SESSION_BASE) >=
	    ARRAY_SIZE(luo_session_ioctl_ops)) {
		return -EINVAL;
	}

	ucmd.ubuffer = (void __user *)arg;
	ret = get_user(ucmd.user_size, (u32 __user *)ucmd.ubuffer);
	if (ret)
		return ret;

	op = &luo_session_ioctl_ops[nr - LIVEUPDATE_CMD_SESSION_BASE];
	if (op->ioctl_num != cmd)
		return -ENOIOCTLCMD;
	if (ucmd.user_size < op->min_size)
		return -EINVAL;

	ucmd.cmd = &buf;
	ret = copy_struct_from_user(ucmd.cmd, op->size, ucmd.ubuffer,
				    ucmd.user_size);
	if (ret)
		return ret;

	return op->execute(session, &ucmd);
}

static const struct file_operations luo_session_fops = {
	.owner = THIS_MODULE,
	.release = luo_session_release,
	.unlocked_ioctl = luo_session_ioctl,
};

/* Create a "struct file" for session */
static int luo_session_getfile(struct luo_session *session, struct file **filep)
{
	char name_buf[128];
	struct file *file;

	lockdep_assert_held(&session->mutex);
	snprintf(name_buf, sizeof(name_buf), "[luo_session] %s", session->name);
	file = anon_inode_getfile(name_buf, &luo_session_fops, session, O_RDWR);
	if (IS_ERR(file))
		return PTR_ERR(file);

	*filep = file;

	return 0;
}

int luo_session_create(const char *name, struct file **filep)
{
	struct luo_session *session;
	int err;

	session = luo_session_alloc(name);
	if (IS_ERR(session))
		return PTR_ERR(session);

	err = luo_session_insert(&luo_session_global.outgoing, session);
	if (err)
		goto err_free;

	scoped_guard(mutex, &session->mutex)
		err = luo_session_getfile(session, filep);
	if (err)
		goto err_remove;

	return 0;

err_remove:
	luo_session_remove(&luo_session_global.outgoing, session);
err_free:
	luo_session_free(session);

	return err;
}

int luo_session_retrieve(const char *name, struct file **filep)
{
	struct luo_session_header *sh = &luo_session_global.incoming;
	struct luo_session *session = NULL;
	struct luo_session *it;
	int err;

	scoped_guard(rwsem_read, &sh->rwsem) {
		list_for_each_entry(it, &sh->list, list) {
			if (!strncmp(it->name, name, sizeof(it->name))) {
				session = it;
				break;
			}
		}
	}

	if (!session)
		return -ENOENT;

	guard(mutex)(&session->mutex);
	if (session->retrieved)
		return -EINVAL;

	err = luo_session_getfile(session, filep);
	if (!err)
		session->retrieved = true;

	return err;
}

int __init luo_session_setup_outgoing(void *fdt_out)
{
	struct luo_session_header_ser *header_ser;
	u64 header_ser_pa;
	int err;

	header_ser = kho_alloc_preserve(LUO_SESSION_PGCNT << PAGE_SHIFT);
	if (IS_ERR(header_ser))
		return PTR_ERR(header_ser);
	header_ser_pa = virt_to_phys(header_ser);

	err = fdt_begin_node(fdt_out, LUO_FDT_SESSION_NODE_NAME);
	err |= fdt_property_string(fdt_out, "compatible",
				   LUO_FDT_SESSION_COMPATIBLE);
	err |= fdt_property(fdt_out, LUO_FDT_SESSION_HEADER, &header_ser_pa,
			    sizeof(header_ser_pa));
	err |= fdt_end_node(fdt_out);

	if (err)
		goto err_unpreserve;

	luo_session_global.outgoing.header_ser = header_ser;
	luo_session_global.outgoing.ser = (void *)(header_ser + 1);
	luo_session_global.outgoing.active = true;

	return 0;

err_unpreserve:
	kho_unpreserve_free(header_ser);
	return err;
}

int __init luo_session_setup_incoming(void *fdt_in)
{
	struct luo_session_header_ser *header_ser;
	int err, header_size, offset;
	u64 header_ser_pa;
	const void *ptr;

	offset = fdt_subnode_offset(fdt_in, 0, LUO_FDT_SESSION_NODE_NAME);
	if (offset < 0) {
		pr_err("Unable to get session node: [%s]\n",
		       LUO_FDT_SESSION_NODE_NAME);
		return -EINVAL;
	}

	err = fdt_node_check_compatible(fdt_in, offset,
					LUO_FDT_SESSION_COMPATIBLE);
	if (err) {
		pr_err("Session node incompatible [%s]\n",
		       LUO_FDT_SESSION_COMPATIBLE);
		return -EINVAL;
	}

	header_size = 0;
	ptr = fdt_getprop(fdt_in, offset, LUO_FDT_SESSION_HEADER, &header_size);
	if (!ptr || header_size != sizeof(u64)) {
		pr_err("Unable to get session header '%s' [%d]\n",
		       LUO_FDT_SESSION_HEADER, header_size);
		return -EINVAL;
	}

	header_ser_pa = get_unaligned((u64 *)ptr);
	header_ser = phys_to_virt(header_ser_pa);

	luo_session_global.incoming.header_ser = header_ser;
	luo_session_global.incoming.ser = (void *)(header_ser + 1);
	luo_session_global.incoming.active = true;

	return 0;
}

int luo_session_deserialize(void)
{
	struct luo_session_header *sh = &luo_session_global.incoming;
	static bool is_deserialized;
	static int err;

	/* If has been deserialized, always return the same error code */
	if (is_deserialized)
		return err;

	is_deserialized = true;
	if (!sh->active)
		return 0;

	/*
	 * Note on error handling:
	 *
	 * If deserialization fails (e.g., allocation failure or corrupt data),
	 * we intentionally skip cleanup of sessions that were already restored.
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
	for (int i = 0; i < sh->header_ser->count; i++) {
		struct luo_session *session;

		session = luo_session_alloc(sh->ser[i].name);
		if (IS_ERR(session)) {
			pr_warn("Failed to allocate session [%s] during deserialization %pe\n",
				sh->ser[i].name, session);
			return PTR_ERR(session);
		}

		err = luo_session_insert(sh, session);
		if (err) {
			pr_warn("Failed to insert session [%s] %pe\n",
				session->name, ERR_PTR(err));
			luo_session_free(session);
			return err;
		}

		scoped_guard(mutex, &session->mutex) {
			luo_file_deserialize(&session->file_set,
					     &sh->ser[i].file_set_ser);
		}
	}

	kho_restore_free(sh->header_ser);
	sh->header_ser = NULL;
	sh->ser = NULL;

	return 0;
}

int luo_session_serialize(void)
{
	struct luo_session_header *sh = &luo_session_global.outgoing;
	struct luo_session *session;
	int i = 0;
	int err;

	guard(rwsem_write)(&sh->rwsem);
	list_for_each_entry(session, &sh->list, list) {
		err = luo_session_freeze_one(session, &sh->ser[i]);
		if (err)
			goto err_undo;

		strscpy(sh->ser[i].name, session->name,
			sizeof(sh->ser[i].name));
		i++;
	}
	sh->header_ser->count = sh->count;

	return 0;

err_undo:
	list_for_each_entry_continue_reverse(session, &sh->list, list) {
		i--;
		luo_session_unfreeze_one(session, &sh->ser[i]);
		memset(sh->ser[i].name, 0, sizeof(sh->ser[i].name));
	}

	return err;
}

/**
 * luo_session_quiesce - Ensure no active sessions exist and lock session lists.
 *
 * Acquires exclusive write locks on both incoming and outgoing session lists.
 * It then validates no sessions exist in either list.
 *
 * This mechanism is used during file handler un/registration to ensure that no
 * sessions are currently using the handler, and no new sessions can be created
 * while un/registration is in progress.
 *
 * This prevents registering new handlers while sessions are active or
 * while deserialization is in progress.
 *
 * Return:
 * true  - System is quiescent (0 sessions) and locked.
 * false - Active sessions exist. The locks are released internally.
 */
bool luo_session_quiesce(void)
{
	down_write(&luo_session_global.incoming.rwsem);
	down_write(&luo_session_global.outgoing.rwsem);

	if (luo_session_global.incoming.count ||
	    luo_session_global.outgoing.count) {
		up_write(&luo_session_global.outgoing.rwsem);
		up_write(&luo_session_global.incoming.rwsem);
		return false;
	}

	return true;
}

/**
 * luo_session_resume - Unlock session lists and resume normal activity.
 *
 * Releases the exclusive locks acquired by a successful call to
 * luo_session_quiesce().
 */
void luo_session_resume(void)
{
	up_write(&luo_session_global.outgoing.rwsem);
	up_write(&luo_session_global.incoming.rwsem);
}
