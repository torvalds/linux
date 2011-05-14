/*
 * linux/fs/ocfs2/ioctl.c
 *
 * Copyright (C) 2006 Herbert Poetzl
 * adapted from Remy Card's ext2/ioctl.c
 */

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/compat.h>

#include <cluster/masklog.h>

#include "ocfs2.h"
#include "alloc.h"
#include "dlmglue.h"
#include "file.h"
#include "inode.h"
#include "journal.h"

#include "ocfs2_fs.h"
#include "ioctl.h"
#include "resize.h"
#include "refcounttree.h"

#include <linux/ext2_fs.h>

#define o2info_from_user(a, b)	\
		copy_from_user(&(a), (b), sizeof(a))
#define o2info_to_user(a, b)	\
		copy_to_user((typeof(a) __user *)b, &(a), sizeof(a))

/*
 * This call is void because we are already reporting an error that may
 * be -EFAULT.  The error will be returned from the ioctl(2) call.  It's
 * just a best-effort to tell userspace that this request caused the error.
 */
static inline void __o2info_set_request_error(struct ocfs2_info_request *kreq,
					struct ocfs2_info_request __user *req)
{
	kreq->ir_flags |= OCFS2_INFO_FL_ERROR;
	(void)put_user(kreq->ir_flags, (__u32 __user *)&(req->ir_flags));
}

#define o2info_set_request_error(a, b) \
		__o2info_set_request_error((struct ocfs2_info_request *)&(a), b)

static inline void __o2info_set_request_filled(struct ocfs2_info_request *req)
{
	req->ir_flags |= OCFS2_INFO_FL_FILLED;
}

#define o2info_set_request_filled(a) \
		__o2info_set_request_filled((struct ocfs2_info_request *)&(a))

static inline void __o2info_clear_request_filled(struct ocfs2_info_request *req)
{
	req->ir_flags &= ~OCFS2_INFO_FL_FILLED;
}

#define o2info_clear_request_filled(a) \
		__o2info_clear_request_filled((struct ocfs2_info_request *)&(a))

static int ocfs2_get_inode_attr(struct inode *inode, unsigned *flags)
{
	int status;

	status = ocfs2_inode_lock(inode, NULL, 0);
	if (status < 0) {
		mlog_errno(status);
		return status;
	}
	ocfs2_get_inode_flags(OCFS2_I(inode));
	*flags = OCFS2_I(inode)->ip_attr;
	ocfs2_inode_unlock(inode, 0);

	return status;
}

static int ocfs2_set_inode_attr(struct inode *inode, unsigned flags,
				unsigned mask)
{
	struct ocfs2_inode_info *ocfs2_inode = OCFS2_I(inode);
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	handle_t *handle = NULL;
	struct buffer_head *bh = NULL;
	unsigned oldflags;
	int status;

	mutex_lock(&inode->i_mutex);

	status = ocfs2_inode_lock(inode, &bh, 1);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = -EACCES;
	if (!inode_owner_or_capable(inode))
		goto bail_unlock;

	if (!S_ISDIR(inode->i_mode))
		flags &= ~OCFS2_DIRSYNC_FL;

	handle = ocfs2_start_trans(osb, OCFS2_INODE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		goto bail_unlock;
	}

	oldflags = ocfs2_inode->ip_attr;
	flags = flags & mask;
	flags |= oldflags & ~mask;

	/*
	 * The IMMUTABLE and APPEND_ONLY flags can only be changed by
	 * the relevant capability.
	 */
	status = -EPERM;
	if ((oldflags & OCFS2_IMMUTABLE_FL) || ((flags ^ oldflags) &
		(OCFS2_APPEND_FL | OCFS2_IMMUTABLE_FL))) {
		if (!capable(CAP_LINUX_IMMUTABLE))
			goto bail_unlock;
	}

	ocfs2_inode->ip_attr = flags;
	ocfs2_set_inode_flags(inode);

	status = ocfs2_mark_inode_dirty(handle, inode, bh);
	if (status < 0)
		mlog_errno(status);

	ocfs2_commit_trans(osb, handle);
bail_unlock:
	ocfs2_inode_unlock(inode, 1);
bail:
	mutex_unlock(&inode->i_mutex);

	brelse(bh);

	return status;
}

int ocfs2_info_handle_blocksize(struct inode *inode,
				struct ocfs2_info_request __user *req)
{
	int status = -EFAULT;
	struct ocfs2_info_blocksize oib;

	if (o2info_from_user(oib, req))
		goto bail;

	oib.ib_blocksize = inode->i_sb->s_blocksize;

	o2info_set_request_filled(oib);

	if (o2info_to_user(oib, req))
		goto bail;

	status = 0;
bail:
	if (status)
		o2info_set_request_error(oib, req);

	return status;
}

int ocfs2_info_handle_clustersize(struct inode *inode,
				  struct ocfs2_info_request __user *req)
{
	int status = -EFAULT;
	struct ocfs2_info_clustersize oic;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	if (o2info_from_user(oic, req))
		goto bail;

	oic.ic_clustersize = osb->s_clustersize;

	o2info_set_request_filled(oic);

	if (o2info_to_user(oic, req))
		goto bail;

	status = 0;
bail:
	if (status)
		o2info_set_request_error(oic, req);

	return status;
}

int ocfs2_info_handle_maxslots(struct inode *inode,
			       struct ocfs2_info_request __user *req)
{
	int status = -EFAULT;
	struct ocfs2_info_maxslots oim;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	if (o2info_from_user(oim, req))
		goto bail;

	oim.im_max_slots = osb->max_slots;

	o2info_set_request_filled(oim);

	if (o2info_to_user(oim, req))
		goto bail;

	status = 0;
bail:
	if (status)
		o2info_set_request_error(oim, req);

	return status;
}

int ocfs2_info_handle_label(struct inode *inode,
			    struct ocfs2_info_request __user *req)
{
	int status = -EFAULT;
	struct ocfs2_info_label oil;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	if (o2info_from_user(oil, req))
		goto bail;

	memcpy(oil.il_label, osb->vol_label, OCFS2_MAX_VOL_LABEL_LEN);

	o2info_set_request_filled(oil);

	if (o2info_to_user(oil, req))
		goto bail;

	status = 0;
bail:
	if (status)
		o2info_set_request_error(oil, req);

	return status;
}

int ocfs2_info_handle_uuid(struct inode *inode,
			   struct ocfs2_info_request __user *req)
{
	int status = -EFAULT;
	struct ocfs2_info_uuid oiu;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	if (o2info_from_user(oiu, req))
		goto bail;

	memcpy(oiu.iu_uuid_str, osb->uuid_str, OCFS2_TEXT_UUID_LEN + 1);

	o2info_set_request_filled(oiu);

	if (o2info_to_user(oiu, req))
		goto bail;

	status = 0;
bail:
	if (status)
		o2info_set_request_error(oiu, req);

	return status;
}

int ocfs2_info_handle_fs_features(struct inode *inode,
				  struct ocfs2_info_request __user *req)
{
	int status = -EFAULT;
	struct ocfs2_info_fs_features oif;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	if (o2info_from_user(oif, req))
		goto bail;

	oif.if_compat_features = osb->s_feature_compat;
	oif.if_incompat_features = osb->s_feature_incompat;
	oif.if_ro_compat_features = osb->s_feature_ro_compat;

	o2info_set_request_filled(oif);

	if (o2info_to_user(oif, req))
		goto bail;

	status = 0;
bail:
	if (status)
		o2info_set_request_error(oif, req);

	return status;
}

int ocfs2_info_handle_journal_size(struct inode *inode,
				   struct ocfs2_info_request __user *req)
{
	int status = -EFAULT;
	struct ocfs2_info_journal_size oij;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	if (o2info_from_user(oij, req))
		goto bail;

	oij.ij_journal_size = osb->journal->j_inode->i_size;

	o2info_set_request_filled(oij);

	if (o2info_to_user(oij, req))
		goto bail;

	status = 0;
bail:
	if (status)
		o2info_set_request_error(oij, req);

	return status;
}

int ocfs2_info_handle_unknown(struct inode *inode,
			      struct ocfs2_info_request __user *req)
{
	int status = -EFAULT;
	struct ocfs2_info_request oir;

	if (o2info_from_user(oir, req))
		goto bail;

	o2info_clear_request_filled(oir);

	if (o2info_to_user(oir, req))
		goto bail;

	status = 0;
bail:
	if (status)
		o2info_set_request_error(oir, req);

	return status;
}

/*
 * Validate and distinguish OCFS2_IOC_INFO requests.
 *
 * - validate the magic number.
 * - distinguish different requests.
 * - validate size of different requests.
 */
int ocfs2_info_handle_request(struct inode *inode,
			      struct ocfs2_info_request __user *req)
{
	int status = -EFAULT;
	struct ocfs2_info_request oir;

	if (o2info_from_user(oir, req))
		goto bail;

	status = -EINVAL;
	if (oir.ir_magic != OCFS2_INFO_MAGIC)
		goto bail;

	switch (oir.ir_code) {
	case OCFS2_INFO_BLOCKSIZE:
		if (oir.ir_size == sizeof(struct ocfs2_info_blocksize))
			status = ocfs2_info_handle_blocksize(inode, req);
		break;
	case OCFS2_INFO_CLUSTERSIZE:
		if (oir.ir_size == sizeof(struct ocfs2_info_clustersize))
			status = ocfs2_info_handle_clustersize(inode, req);
		break;
	case OCFS2_INFO_MAXSLOTS:
		if (oir.ir_size == sizeof(struct ocfs2_info_maxslots))
			status = ocfs2_info_handle_maxslots(inode, req);
		break;
	case OCFS2_INFO_LABEL:
		if (oir.ir_size == sizeof(struct ocfs2_info_label))
			status = ocfs2_info_handle_label(inode, req);
		break;
	case OCFS2_INFO_UUID:
		if (oir.ir_size == sizeof(struct ocfs2_info_uuid))
			status = ocfs2_info_handle_uuid(inode, req);
		break;
	case OCFS2_INFO_FS_FEATURES:
		if (oir.ir_size == sizeof(struct ocfs2_info_fs_features))
			status = ocfs2_info_handle_fs_features(inode, req);
		break;
	case OCFS2_INFO_JOURNAL_SIZE:
		if (oir.ir_size == sizeof(struct ocfs2_info_journal_size))
			status = ocfs2_info_handle_journal_size(inode, req);
		break;
	default:
		status = ocfs2_info_handle_unknown(inode, req);
		break;
	}

bail:
	return status;
}

int ocfs2_get_request_ptr(struct ocfs2_info *info, int idx,
			  u64 *req_addr, int compat_flag)
{
	int status = -EFAULT;
	u64 __user *bp = NULL;

	if (compat_flag) {
#ifdef CONFIG_COMPAT
		/*
		 * pointer bp stores the base address of a pointers array,
		 * which collects all addresses of separate request.
		 */
		bp = (u64 __user *)(unsigned long)compat_ptr(info->oi_requests);
#else
		BUG();
#endif
	} else
		bp = (u64 __user *)(unsigned long)(info->oi_requests);

	if (o2info_from_user(*req_addr, bp + idx))
		goto bail;

	status = 0;
bail:
	return status;
}

/*
 * OCFS2_IOC_INFO handles an array of requests passed from userspace.
 *
 * ocfs2_info_handle() recevies a large info aggregation, grab and
 * validate the request count from header, then break it into small
 * pieces, later specific handlers can handle them one by one.
 *
 * Idea here is to make each separate request small enough to ensure
 * a better backward&forward compatibility, since a small piece of
 * request will be less likely to be broken if disk layout get changed.
 */
int ocfs2_info_handle(struct inode *inode, struct ocfs2_info *info,
		      int compat_flag)
{
	int i, status = 0;
	u64 req_addr;
	struct ocfs2_info_request __user *reqp;

	if ((info->oi_count > OCFS2_INFO_MAX_REQUEST) ||
	    (!info->oi_requests)) {
		status = -EINVAL;
		goto bail;
	}

	for (i = 0; i < info->oi_count; i++) {

		status = ocfs2_get_request_ptr(info, i, &req_addr, compat_flag);
		if (status)
			break;

		reqp = (struct ocfs2_info_request *)(unsigned long)req_addr;
		if (!reqp) {
			status = -EINVAL;
			goto bail;
		}

		status = ocfs2_info_handle_request(inode, reqp);
		if (status)
			break;
	}

bail:
	return status;
}

long ocfs2_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	unsigned int flags;
	int new_clusters;
	int status;
	struct ocfs2_space_resv sr;
	struct ocfs2_new_group_input input;
	struct reflink_arguments args;
	const char *old_path, *new_path;
	bool preserve;
	struct ocfs2_info info;

	switch (cmd) {
	case OCFS2_IOC_GETFLAGS:
		status = ocfs2_get_inode_attr(inode, &flags);
		if (status < 0)
			return status;

		flags &= OCFS2_FL_VISIBLE;
		return put_user(flags, (int __user *) arg);
	case OCFS2_IOC_SETFLAGS:
		if (get_user(flags, (int __user *) arg))
			return -EFAULT;

		status = mnt_want_write(filp->f_path.mnt);
		if (status)
			return status;
		status = ocfs2_set_inode_attr(inode, flags,
			OCFS2_FL_MODIFIABLE);
		mnt_drop_write(filp->f_path.mnt);
		return status;
	case OCFS2_IOC_RESVSP:
	case OCFS2_IOC_RESVSP64:
	case OCFS2_IOC_UNRESVSP:
	case OCFS2_IOC_UNRESVSP64:
		if (copy_from_user(&sr, (int __user *) arg, sizeof(sr)))
			return -EFAULT;

		return ocfs2_change_file_space(filp, cmd, &sr);
	case OCFS2_IOC_GROUP_EXTEND:
		if (!capable(CAP_SYS_RESOURCE))
			return -EPERM;

		if (get_user(new_clusters, (int __user *)arg))
			return -EFAULT;

		return ocfs2_group_extend(inode, new_clusters);
	case OCFS2_IOC_GROUP_ADD:
	case OCFS2_IOC_GROUP_ADD64:
		if (!capable(CAP_SYS_RESOURCE))
			return -EPERM;

		if (copy_from_user(&input, (int __user *) arg, sizeof(input)))
			return -EFAULT;

		return ocfs2_group_add(inode, &input);
	case OCFS2_IOC_REFLINK:
		if (copy_from_user(&args, (struct reflink_arguments *)arg,
				   sizeof(args)))
			return -EFAULT;
		old_path = (const char *)(unsigned long)args.old_path;
		new_path = (const char *)(unsigned long)args.new_path;
		preserve = (args.preserve != 0);

		return ocfs2_reflink_ioctl(inode, old_path, new_path, preserve);
	case OCFS2_IOC_INFO:
		if (copy_from_user(&info, (struct ocfs2_info __user *)arg,
				   sizeof(struct ocfs2_info)))
			return -EFAULT;

		return ocfs2_info_handle(inode, &info, 0);
	default:
		return -ENOTTY;
	}
}

#ifdef CONFIG_COMPAT
long ocfs2_compat_ioctl(struct file *file, unsigned cmd, unsigned long arg)
{
	bool preserve;
	struct reflink_arguments args;
	struct inode *inode = file->f_path.dentry->d_inode;
	struct ocfs2_info info;

	switch (cmd) {
	case OCFS2_IOC32_GETFLAGS:
		cmd = OCFS2_IOC_GETFLAGS;
		break;
	case OCFS2_IOC32_SETFLAGS:
		cmd = OCFS2_IOC_SETFLAGS;
		break;
	case OCFS2_IOC_RESVSP:
	case OCFS2_IOC_RESVSP64:
	case OCFS2_IOC_UNRESVSP:
	case OCFS2_IOC_UNRESVSP64:
	case OCFS2_IOC_GROUP_EXTEND:
	case OCFS2_IOC_GROUP_ADD:
	case OCFS2_IOC_GROUP_ADD64:
		break;
	case OCFS2_IOC_REFLINK:
		if (copy_from_user(&args, (struct reflink_arguments *)arg,
				   sizeof(args)))
			return -EFAULT;
		preserve = (args.preserve != 0);

		return ocfs2_reflink_ioctl(inode, compat_ptr(args.old_path),
					   compat_ptr(args.new_path), preserve);
	case OCFS2_IOC_INFO:
		if (copy_from_user(&info, (struct ocfs2_info __user *)arg,
				   sizeof(struct ocfs2_info)))
			return -EFAULT;

		return ocfs2_info_handle(inode, &info, 1);
	default:
		return -ENOIOCTLCMD;
	}

	return ocfs2_ioctl(file, cmd, arg);
}
#endif
