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
 *   a live update is triggered via kexec, session metadata is serialized into
 *   a chain of linked-blocks and placed in a preserved memory region. The
 *   physical address of the first block header is stored in the centralized
 *   `struct luo_ser` structure.
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
 *
 * Locking:
 *
 * The LUO session subsystem uses a three-tier locking hierarchy to ensure thread
 * safety and prevent deadlocks during concurrent session mutations and kexec
 * serialization:
 *
 * 1. `luo_session_serialize_rwsem` (global rwsem):
 *    Protects session mutations (creation, retrieval, release, and ioctls)
 *    against the serialization process during reboot.
 *
 *    - Readers: Taken by any path modifying or accessing session state (e.g.,
 *      `luo_session_create()`, `luo_session_retrieve()`, `luo_session_release()`,
 *      and `luo_session_ioctl()`).
 *    - Writer: Taken by the serialization process (`luo_session_serialize()`)
 *      during reboot. On success, the write lock is held indefinitely to freeze
 *      the subsystem. On failure, it is released to allow recovery.
 *
 * 2. `luo_session_header->rwsem` (per-list rwsem):
 *    Synchronizes list-level operations for the incoming and outgoing session headers.
 *
 *    - Writer: Taken during list mutation operations (inserting or removing a
 *      session from the list).
 *    - Reader: Taken when traversing the list (e.g., retrieving a session by name).
 *
 * 3. `luo_session->mutex` (per-session mutex):
 *    Protects the internal state and file sets of an individual session. It is
 *    acquired during per-session operations such as preserving, retrieving,
 *    or freezing files.
 *
 * Lock Hierarchy:
 *   `luo_session_serialize_rwsem` -> `luo_session_header->rwsem` -> `luo_session->mutex`
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
#include <linux/kho_block.h>
#include <linux/kho/abi/luo.h>
#include <linux/list.h>
#include <linux/liveupdate.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <uapi/linux/liveupdate.h>
#include "luo_internal.h"

static DECLARE_RWSEM(luo_session_serialize_rwsem);
/**
 * struct luo_session_header - Header struct for managing LUO sessions.
 * @count:       The number of sessions currently tracked in the @list.
 * @list:        The head of the linked list of `struct luo_session` instances.
 * @rwsem:       A read-write semaphore providing synchronized access to the
 *               session list and other fields in this structure.
 * @block_set:   The set of serialization blocks.
 * @sessions_pa: Points to the location of sessions_pa within struct luo_ser.
 * @active:      Set to true when first initialized. If previous kernel did not
 *               send session data, active stays false for incoming.
 */
struct luo_session_header {
	long count;
	struct list_head list;
	struct rw_semaphore rwsem;
	struct kho_block_set block_set;
	u64 *sessions_pa;
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
		.block_set = KHO_BLOCK_SET_INIT(luo_session_global.incoming.block_set,
						sizeof(struct luo_session_ser)),
	},
	.outgoing = {
		.list = LIST_HEAD_INIT(luo_session_global.outgoing.list),
		.rwsem = __RWSEM_INITIALIZER(luo_session_global.outgoing.rwsem),
		.block_set = KHO_BLOCK_SET_INIT(luo_session_global.outgoing.block_set,
						sizeof(struct luo_session_ser)),
	},
};

static struct luo_session *luo_session_alloc(const char *name)
{
	struct luo_session *session = kzalloc_obj(*session);

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
	int err;

	guard(rwsem_write)(&sh->rwsem);

	/*
	 * For outgoing we should make sure there is room in serialization array
	 * for new session.
	 */
	if (sh == &luo_session_global.outgoing) {
		err = kho_block_set_grow(&sh->block_set, sh->count + 1);
		if (err)
			return err;
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
	if (sh == &luo_session_global.outgoing)
		kho_block_set_shrink(&sh->block_set, sh->count);
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

	guard(rwsem_read)(&luo_session_serialize_rwsem);
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

	mutex_lock(&session->mutex);
	err = luo_retrieve_file(&session->file_set, argp->token, &file);
	mutex_unlock(&session->mutex);
	if (err < 0)
		goto err_put_fd;

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

static int luo_session_get_name(struct luo_session *session,
				struct luo_ucmd *ucmd)
{
	struct liveupdate_session_get_name *argp = ucmd->cmd;

	if (argp->reserved != 0)
		return -EINVAL;

	strscpy((char *)argp->name, session->name, sizeof(argp->name));

	return luo_ucmd_respond(ucmd, sizeof(*argp));
}

union ucmd_buffer {
	struct liveupdate_session_finish finish;
	struct liveupdate_session_preserve_fd preserve;
	struct liveupdate_session_retrieve_fd retrieve;
	struct liveupdate_session_get_name get_name;
};

/* Type of sessions the ioctl applies to. */
enum luo_ioctl_type {
	LUO_IOCTL_INCOMING,
	LUO_IOCTL_OUTGOING,
	LUO_IOCTL_ALL,
};

struct luo_ioctl_op {
	unsigned int size;
	unsigned int min_size;
	unsigned int ioctl_num;
	enum luo_ioctl_type type;
	int (*execute)(struct luo_session *session, struct luo_ucmd *ucmd);
};

#define IOCTL_OP(_ioctl, _fn, _struct, _last, _type)                           \
	[_IOC_NR(_ioctl) - LIVEUPDATE_CMD_SESSION_BASE] = {                    \
		.size = sizeof(_struct) +                                      \
			BUILD_BUG_ON_ZERO(sizeof(union ucmd_buffer) <          \
					  sizeof(_struct)),                    \
		.min_size = offsetofend(_struct, _last),                       \
		.ioctl_num = _ioctl,                                           \
		.type = _type,                                                 \
		.execute = _fn,                                                \
	}

static const struct luo_ioctl_op luo_session_ioctl_ops[] = {
	IOCTL_OP(LIVEUPDATE_SESSION_FINISH, luo_session_finish,
		 struct liveupdate_session_finish, reserved, LUO_IOCTL_INCOMING),
	IOCTL_OP(LIVEUPDATE_SESSION_PRESERVE_FD, luo_session_preserve_fd,
		 struct liveupdate_session_preserve_fd, token, LUO_IOCTL_OUTGOING),
	IOCTL_OP(LIVEUPDATE_SESSION_RETRIEVE_FD, luo_session_retrieve_fd,
		 struct liveupdate_session_retrieve_fd, token, LUO_IOCTL_INCOMING),
	IOCTL_OP(LIVEUPDATE_SESSION_GET_NAME, luo_session_get_name,
		 struct liveupdate_session_retrieve_fd, token, LUO_IOCTL_ALL),
};

static bool luo_ioctl_type_valid(struct luo_session *session,
				 const struct luo_ioctl_op *op)
{
	switch (op->type) {
	case LUO_IOCTL_INCOMING:
		/* Retrieved is only set on incoming sessions */
		return session->retrieved;
	case LUO_IOCTL_OUTGOING:
		return !session->retrieved;
	case LUO_IOCTL_ALL:
		return true;
	}

	/* Catch-all. */
	return false;
}

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
	if (!luo_ioctl_type_valid(session, op))
		return -EINVAL;
	if (ucmd.user_size < op->min_size)
		return -EINVAL;

	ucmd.cmd = &buf;
	ret = copy_struct_from_user(ucmd.cmd, op->size, ucmd.ubuffer,
				    ucmd.user_size);
	if (ret)
		return ret;

	guard(rwsem_read)(&luo_session_serialize_rwsem);
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
	size_t len = strnlen(name, LIVEUPDATE_SESSION_NAME_LENGTH);
	struct luo_session *session;
	int err;

	if (len == 0 || len > LIVEUPDATE_SESSION_NAME_LENGTH - 1)
		return -EINVAL;

	session = luo_session_alloc(name);
	if (IS_ERR(session))
		return PTR_ERR(session);

	down_read(&luo_session_serialize_rwsem);
	err = luo_session_insert(&luo_session_global.outgoing, session);
	if (err)
		goto err_free;

	mutex_lock(&session->mutex);
	err = luo_session_getfile(session, filep);
	mutex_unlock(&session->mutex);
	if (err)
		goto err_remove;
	up_read(&luo_session_serialize_rwsem);

	return 0;

err_remove:
	luo_session_remove(&luo_session_global.outgoing, session);
err_free:
	luo_session_free(session);
	up_read(&luo_session_serialize_rwsem);

	return err;
}

int luo_session_retrieve(const char *name, struct file **filep)
{
	struct luo_session_header *sh = &luo_session_global.incoming;
	struct luo_session *session = NULL;
	struct luo_session *it;
	int err;

	guard(rwsem_read)(&luo_session_serialize_rwsem);
	guard(rwsem_read)(&sh->rwsem);
	list_for_each_entry(it, &sh->list, list) {
		if (!strncmp(it->name, name, sizeof(it->name))) {
			session = it;
			break;
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

void __init luo_session_setup_outgoing(u64 *sessions_pa)
{
	luo_session_global.outgoing.sessions_pa = sessions_pa;
	luo_session_global.outgoing.active = true;
}

int __init luo_session_setup_incoming(u64 sessions_pa)
{
	struct luo_session_header *sh = &luo_session_global.incoming;
	int err;

	if (!sessions_pa)
		return 0;

	err = kho_block_set_restore(&sh->block_set, sessions_pa);
	if (err)
		return err;

	sh->active = true;
	return 0;
}

static int luo_session_deserialize_one(struct luo_session_header *sh,
				       struct luo_session_ser *ser)
{
	struct luo_session *session;
	int err;

	session = luo_session_alloc(ser->name);
	if (IS_ERR(session)) {
		pr_warn("Failed to allocate session [%.*s] during deserialization %pe\n",
			(int)sizeof(ser->name), ser->name, session);
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
		err = luo_file_deserialize(&session->file_set,
					   &ser->file_set_ser);
	}
	if (err) {
		pr_warn("Failed to deserialize files for session [%s] %pe\n",
			session->name, ERR_PTR(err));
		return err;
	}

	return 0;
}

int luo_session_deserialize(void)
{
	struct luo_session_header *sh = &luo_session_global.incoming;
	static bool is_deserialized;
	struct luo_session_ser *ser;
	struct kho_block_set_it it;
	static int saved_err;
	int err;

	/* If has been deserialized, always return the same error code */
	if (is_deserialized)
		return saved_err;

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
	kho_block_set_it_init(&it, &sh->block_set);
	while ((ser = kho_block_set_it_read_entry(&it))) {
		err = luo_session_deserialize_one(sh, ser);
		if (err)
			goto save_err;
	}

	kho_block_set_destroy(&sh->block_set);

	return 0;

save_err:
	kho_block_set_destroy(&sh->block_set);
	saved_err = err;
	return err;
}

int luo_session_serialize(void)
{
	struct luo_session_header *sh = &luo_session_global.outgoing;
	struct luo_session *session;
	struct kho_block_set_it it;
	int err;

	down_write(&luo_session_serialize_rwsem);
	down_write(&sh->rwsem);
	*sh->sessions_pa = 0;

	kho_block_set_it_init(&it, &sh->block_set);

	list_for_each_entry(session, &sh->list, list) {
		struct luo_session_ser *ser = kho_block_set_it_reserve_entry(&it);

		/* This should not fail normally as blocks were pre-allocated */
		if (WARN_ON_ONCE(!ser)) {
			err = -ENOSPC;
			goto err_undo;
		}

		err = luo_session_freeze_one(session, ser);
		if (err) {
			kho_block_set_it_prev(&it);
			goto err_undo;
		}

		strscpy(ser->name, session->name, sizeof(ser->name));
	}

	if (sh->count > 0)
		*sh->sessions_pa = kho_block_set_head_pa(&sh->block_set);
	up_write(&sh->rwsem);

	return 0;

err_undo:
	list_for_each_entry_continue_reverse(session, &sh->list, list) {
		struct luo_session_ser *ser = kho_block_set_it_prev(&it);

		luo_session_unfreeze_one(session, ser);
		memset(ser->name, 0, sizeof(ser->name));
	}
	up_write(&sh->rwsem);
	up_write(&luo_session_serialize_rwsem);

	return err;
}
