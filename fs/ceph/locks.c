// SPDX-License-Identifier: GPL-2.0
#include <linux/ceph/ceph_debug.h>

#include <linux/file.h>
#include <linux/namei.h>
#include <linux/random.h>

#include "super.h"
#include "mds_client.h"
#include <linux/ceph/pagelist.h>

static u64 lock_secret;
static int ceph_lock_wait_for_completion(struct ceph_mds_client *mdsc,
                                         struct ceph_mds_request *req);

static inline u64 secure_addr(void *addr)
{
	u64 v = lock_secret ^ (u64)(unsigned long)addr;
	/*
	 * Set the most significant bit, so that MDS knows the 'owner'
	 * is sufficient to identify the owner of lock. (old code uses
	 * both 'owner' and 'pid')
	 */
	v |= (1ULL << 63);
	return v;
}

void __init ceph_flock_init(void)
{
	get_random_bytes(&lock_secret, sizeof(lock_secret));
}

static void ceph_fl_copy_lock(struct file_lock *dst, struct file_lock *src)
{
	struct inode *inode = file_inode(src->fl_file);
	atomic_inc(&ceph_inode(inode)->i_filelock_ref);
}

static void ceph_fl_release_lock(struct file_lock *fl)
{
	struct inode *inode = file_inode(fl->fl_file);
	struct ceph_inode_info *ci = ceph_inode(inode);
	if (atomic_dec_and_test(&ci->i_filelock_ref)) {
		/* clear error when all locks are released */
		spin_lock(&ci->i_ceph_lock);
		ci->i_ceph_flags &= ~CEPH_I_ERROR_FILELOCK;
		spin_unlock(&ci->i_ceph_lock);
	}
}

static const struct file_lock_operations ceph_fl_lock_ops = {
	.fl_copy_lock = ceph_fl_copy_lock,
	.fl_release_private = ceph_fl_release_lock,
};

/**
 * Implement fcntl and flock locking functions.
 */
static int ceph_lock_message(u8 lock_type, u16 operation, struct inode *inode,
			     int cmd, u8 wait, struct file_lock *fl)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_client(inode->i_sb)->mdsc;
	struct ceph_mds_request *req;
	int err;
	u64 length = 0;
	u64 owner;

	if (operation == CEPH_MDS_OP_SETFILELOCK) {
		/*
		 * increasing i_filelock_ref closes race window between
		 * handling request reply and adding file_lock struct to
		 * inode. Otherwise, auth caps may get trimmed in the
		 * window. Caller function will decrease the counter.
		 */
		fl->fl_ops = &ceph_fl_lock_ops;
		atomic_inc(&ceph_inode(inode)->i_filelock_ref);
	}

	if (operation != CEPH_MDS_OP_SETFILELOCK || cmd == CEPH_LOCK_UNLOCK)
		wait = 0;

	req = ceph_mdsc_create_request(mdsc, operation, USE_AUTH_MDS);
	if (IS_ERR(req))
		return PTR_ERR(req);
	req->r_inode = inode;
	ihold(inode);
	req->r_num_caps = 1;

	/* mds requires start and length rather than start and end */
	if (LLONG_MAX == fl->fl_end)
		length = 0;
	else
		length = fl->fl_end - fl->fl_start + 1;

	owner = secure_addr(fl->fl_owner);

	dout("ceph_lock_message: rule: %d, op: %d, owner: %llx, pid: %llu, "
	     "start: %llu, length: %llu, wait: %d, type: %d\n", (int)lock_type,
	     (int)operation, owner, (u64)fl->fl_pid, fl->fl_start, length,
	     wait, fl->fl_type);

	req->r_args.filelock_change.rule = lock_type;
	req->r_args.filelock_change.type = cmd;
	req->r_args.filelock_change.owner = cpu_to_le64(owner);
	req->r_args.filelock_change.pid = cpu_to_le64((u64)fl->fl_pid);
	req->r_args.filelock_change.start = cpu_to_le64(fl->fl_start);
	req->r_args.filelock_change.length = cpu_to_le64(length);
	req->r_args.filelock_change.wait = wait;

	if (wait)
		req->r_wait_for_completion = ceph_lock_wait_for_completion;

	err = ceph_mdsc_do_request(mdsc, inode, req);
	if (!err && operation == CEPH_MDS_OP_GETFILELOCK) {
		fl->fl_pid = -le64_to_cpu(req->r_reply_info.filelock_reply->pid);
		if (CEPH_LOCK_SHARED == req->r_reply_info.filelock_reply->type)
			fl->fl_type = F_RDLCK;
		else if (CEPH_LOCK_EXCL == req->r_reply_info.filelock_reply->type)
			fl->fl_type = F_WRLCK;
		else
			fl->fl_type = F_UNLCK;

		fl->fl_start = le64_to_cpu(req->r_reply_info.filelock_reply->start);
		length = le64_to_cpu(req->r_reply_info.filelock_reply->start) +
						 le64_to_cpu(req->r_reply_info.filelock_reply->length);
		if (length >= 1)
			fl->fl_end = length -1;
		else
			fl->fl_end = 0;

	}
	ceph_mdsc_put_request(req);
	dout("ceph_lock_message: rule: %d, op: %d, pid: %llu, start: %llu, "
	     "length: %llu, wait: %d, type: %d, err code %d\n", (int)lock_type,
	     (int)operation, (u64)fl->fl_pid, fl->fl_start,
	     length, wait, fl->fl_type, err);
	return err;
}

static int ceph_lock_wait_for_completion(struct ceph_mds_client *mdsc,
                                         struct ceph_mds_request *req)
{
	struct ceph_mds_request *intr_req;
	struct inode *inode = req->r_inode;
	int err, lock_type;

	BUG_ON(req->r_op != CEPH_MDS_OP_SETFILELOCK);
	if (req->r_args.filelock_change.rule == CEPH_LOCK_FCNTL)
		lock_type = CEPH_LOCK_FCNTL_INTR;
	else if (req->r_args.filelock_change.rule == CEPH_LOCK_FLOCK)
		lock_type = CEPH_LOCK_FLOCK_INTR;
	else
		BUG_ON(1);
	BUG_ON(req->r_args.filelock_change.type == CEPH_LOCK_UNLOCK);

	err = wait_for_completion_interruptible(&req->r_completion);
	if (!err)
		return 0;

	dout("ceph_lock_wait_for_completion: request %llu was interrupted\n",
	     req->r_tid);

	mutex_lock(&mdsc->mutex);
	if (test_bit(CEPH_MDS_R_GOT_RESULT, &req->r_req_flags)) {
		err = 0;
	} else {
		/*
		 * ensure we aren't running concurrently with
		 * ceph_fill_trace or ceph_readdir_prepopulate, which
		 * rely on locks (dir mutex) held by our caller.
		 */
		mutex_lock(&req->r_fill_mutex);
		req->r_err = err;
		set_bit(CEPH_MDS_R_ABORTED, &req->r_req_flags);
		mutex_unlock(&req->r_fill_mutex);

		if (!req->r_session) {
			// haven't sent the request
			err = 0;
		}
	}
	mutex_unlock(&mdsc->mutex);
	if (!err)
		return 0;

	intr_req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_SETFILELOCK,
					    USE_AUTH_MDS);
	if (IS_ERR(intr_req))
		return PTR_ERR(intr_req);

	intr_req->r_inode = inode;
	ihold(inode);
	intr_req->r_num_caps = 1;

	intr_req->r_args.filelock_change = req->r_args.filelock_change;
	intr_req->r_args.filelock_change.rule = lock_type;
	intr_req->r_args.filelock_change.type = CEPH_LOCK_UNLOCK;

	err = ceph_mdsc_do_request(mdsc, inode, intr_req);
	ceph_mdsc_put_request(intr_req);

	if (err && err != -ERESTARTSYS)
		return err;

	wait_for_completion_killable(&req->r_safe_completion);
	return 0;
}

/**
 * Attempt to set an fcntl lock.
 * For now, this just goes away to the server. Later it may be more awesome.
 */
int ceph_lock(struct file *file, int cmd, struct file_lock *fl)
{
	struct inode *inode = file_inode(file);
	struct ceph_inode_info *ci = ceph_inode(inode);
	int err = 0;
	u16 op = CEPH_MDS_OP_SETFILELOCK;
	u8 wait = 0;
	u8 lock_cmd;

	if (!(fl->fl_flags & FL_POSIX))
		return -ENOLCK;
	/* No mandatory locks */
	if (__mandatory_lock(file->f_mapping->host) && fl->fl_type != F_UNLCK)
		return -ENOLCK;

	dout("ceph_lock, fl_owner: %p\n", fl->fl_owner);

	/* set wait bit as appropriate, then make command as Ceph expects it*/
	if (IS_GETLK(cmd))
		op = CEPH_MDS_OP_GETFILELOCK;
	else if (IS_SETLKW(cmd))
		wait = 1;

	spin_lock(&ci->i_ceph_lock);
	if (ci->i_ceph_flags & CEPH_I_ERROR_FILELOCK) {
		err = -EIO;
	}
	spin_unlock(&ci->i_ceph_lock);
	if (err < 0) {
		if (op == CEPH_MDS_OP_SETFILELOCK && F_UNLCK == fl->fl_type)
			posix_lock_file(file, fl, NULL);
		return err;
	}

	if (F_RDLCK == fl->fl_type)
		lock_cmd = CEPH_LOCK_SHARED;
	else if (F_WRLCK == fl->fl_type)
		lock_cmd = CEPH_LOCK_EXCL;
	else
		lock_cmd = CEPH_LOCK_UNLOCK;

	err = ceph_lock_message(CEPH_LOCK_FCNTL, op, inode, lock_cmd, wait, fl);
	if (!err) {
		if (op == CEPH_MDS_OP_SETFILELOCK) {
			dout("mds locked, locking locally\n");
			err = posix_lock_file(file, fl, NULL);
			if (err) {
				/* undo! This should only happen if
				 * the kernel detects local
				 * deadlock. */
				ceph_lock_message(CEPH_LOCK_FCNTL, op, inode,
						  CEPH_LOCK_UNLOCK, 0, fl);
				dout("got %d on posix_lock_file, undid lock\n",
				     err);
			}
		}
	}
	return err;
}

int ceph_flock(struct file *file, int cmd, struct file_lock *fl)
{
	struct inode *inode = file_inode(file);
	struct ceph_inode_info *ci = ceph_inode(inode);
	int err = 0;
	u8 wait = 0;
	u8 lock_cmd;

	if (!(fl->fl_flags & FL_FLOCK))
		return -ENOLCK;
	/* No mandatory locks */
	if (fl->fl_type & LOCK_MAND)
		return -EOPNOTSUPP;

	dout("ceph_flock, fl_file: %p\n", fl->fl_file);

	spin_lock(&ci->i_ceph_lock);
	if (ci->i_ceph_flags & CEPH_I_ERROR_FILELOCK) {
		err = -EIO;
	}
	spin_unlock(&ci->i_ceph_lock);
	if (err < 0) {
		if (F_UNLCK == fl->fl_type)
			locks_lock_file_wait(file, fl);
		return err;
	}

	if (IS_SETLKW(cmd))
		wait = 1;

	if (F_RDLCK == fl->fl_type)
		lock_cmd = CEPH_LOCK_SHARED;
	else if (F_WRLCK == fl->fl_type)
		lock_cmd = CEPH_LOCK_EXCL;
	else
		lock_cmd = CEPH_LOCK_UNLOCK;

	err = ceph_lock_message(CEPH_LOCK_FLOCK, CEPH_MDS_OP_SETFILELOCK,
				inode, lock_cmd, wait, fl);
	if (!err) {
		err = locks_lock_file_wait(file, fl);
		if (err) {
			ceph_lock_message(CEPH_LOCK_FLOCK,
					  CEPH_MDS_OP_SETFILELOCK,
					  inode, CEPH_LOCK_UNLOCK, 0, fl);
			dout("got %d on locks_lock_file_wait, undid lock\n", err);
		}
	}
	return err;
}

/*
 * Fills in the passed counter variables, so you can prepare pagelist metadata
 * before calling ceph_encode_locks.
 */
void ceph_count_locks(struct inode *inode, int *fcntl_count, int *flock_count)
{
	struct file_lock *lock;
	struct file_lock_context *ctx;

	*fcntl_count = 0;
	*flock_count = 0;

	ctx = inode->i_flctx;
	if (ctx) {
		spin_lock(&ctx->flc_lock);
		list_for_each_entry(lock, &ctx->flc_posix, fl_list)
			++(*fcntl_count);
		list_for_each_entry(lock, &ctx->flc_flock, fl_list)
			++(*flock_count);
		spin_unlock(&ctx->flc_lock);
	}
	dout("counted %d flock locks and %d fcntl locks\n",
	     *flock_count, *fcntl_count);
}

/*
 * Given a pointer to a lock, convert it to a ceph filelock
 */
static int lock_to_ceph_filelock(struct file_lock *lock,
				 struct ceph_filelock *cephlock)
{
	int err = 0;
	cephlock->start = cpu_to_le64(lock->fl_start);
	cephlock->length = cpu_to_le64(lock->fl_end - lock->fl_start + 1);
	cephlock->client = cpu_to_le64(0);
	cephlock->pid = cpu_to_le64((u64)lock->fl_pid);
	cephlock->owner = cpu_to_le64(secure_addr(lock->fl_owner));

	switch (lock->fl_type) {
	case F_RDLCK:
		cephlock->type = CEPH_LOCK_SHARED;
		break;
	case F_WRLCK:
		cephlock->type = CEPH_LOCK_EXCL;
		break;
	case F_UNLCK:
		cephlock->type = CEPH_LOCK_UNLOCK;
		break;
	default:
		dout("Have unknown lock type %d\n", lock->fl_type);
		err = -EINVAL;
	}

	return err;
}

/**
 * Encode the flock and fcntl locks for the given inode into the ceph_filelock
 * array. Must be called with inode->i_lock already held.
 * If we encounter more of a specific lock type than expected, return -ENOSPC.
 */
int ceph_encode_locks_to_buffer(struct inode *inode,
				struct ceph_filelock *flocks,
				int num_fcntl_locks, int num_flock_locks)
{
	struct file_lock *lock;
	struct file_lock_context *ctx = inode->i_flctx;
	int err = 0;
	int seen_fcntl = 0;
	int seen_flock = 0;
	int l = 0;

	dout("encoding %d flock and %d fcntl locks\n", num_flock_locks,
	     num_fcntl_locks);

	if (!ctx)
		return 0;

	spin_lock(&ctx->flc_lock);
	list_for_each_entry(lock, &ctx->flc_posix, fl_list) {
		++seen_fcntl;
		if (seen_fcntl > num_fcntl_locks) {
			err = -ENOSPC;
			goto fail;
		}
		err = lock_to_ceph_filelock(lock, &flocks[l]);
		if (err)
			goto fail;
		++l;
	}
	list_for_each_entry(lock, &ctx->flc_flock, fl_list) {
		++seen_flock;
		if (seen_flock > num_flock_locks) {
			err = -ENOSPC;
			goto fail;
		}
		err = lock_to_ceph_filelock(lock, &flocks[l]);
		if (err)
			goto fail;
		++l;
	}
fail:
	spin_unlock(&ctx->flc_lock);
	return err;
}

/**
 * Copy the encoded flock and fcntl locks into the pagelist.
 * Format is: #fcntl locks, sequential fcntl locks, #flock locks,
 * sequential flock locks.
 * Returns zero on success.
 */
int ceph_locks_to_pagelist(struct ceph_filelock *flocks,
			   struct ceph_pagelist *pagelist,
			   int num_fcntl_locks, int num_flock_locks)
{
	int err = 0;
	__le32 nlocks;

	nlocks = cpu_to_le32(num_fcntl_locks);
	err = ceph_pagelist_append(pagelist, &nlocks, sizeof(nlocks));
	if (err)
		goto out_fail;

	if (num_fcntl_locks > 0) {
		err = ceph_pagelist_append(pagelist, flocks,
					   num_fcntl_locks * sizeof(*flocks));
		if (err)
			goto out_fail;
	}

	nlocks = cpu_to_le32(num_flock_locks);
	err = ceph_pagelist_append(pagelist, &nlocks, sizeof(nlocks));
	if (err)
		goto out_fail;

	if (num_flock_locks > 0) {
		err = ceph_pagelist_append(pagelist, &flocks[num_fcntl_locks],
					   num_flock_locks * sizeof(*flocks));
	}
out_fail:
	return err;
}
