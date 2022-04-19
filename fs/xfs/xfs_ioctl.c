// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_rtalloc.h"
#include "xfs_iwalk.h"
#include "xfs_itable.h"
#include "xfs_error.h"
#include "xfs_attr.h"
#include "xfs_bmap.h"
#include "xfs_bmap_util.h"
#include "xfs_fsops.h"
#include "xfs_discard.h"
#include "xfs_quota.h"
#include "xfs_export.h"
#include "xfs_trace.h"
#include "xfs_icache.h"
#include "xfs_trans.h"
#include "xfs_acl.h"
#include "xfs_btree.h"
#include <linux/fsmap.h>
#include "xfs_fsmap.h"
#include "scrub/xfs_scrub.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_health.h"
#include "xfs_reflink.h"
#include "xfs_ioctl.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"

#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/fileattr.h>

/*
 * xfs_find_handle maps from userspace xfs_fsop_handlereq structure to
 * a file or fs handle.
 *
 * XFS_IOC_PATH_TO_FSHANDLE
 *    returns fs handle for a mount point or path within that mount point
 * XFS_IOC_FD_TO_HANDLE
 *    returns full handle for a FD opened in user space
 * XFS_IOC_PATH_TO_HANDLE
 *    returns full handle for a path
 */
int
xfs_find_handle(
	unsigned int		cmd,
	xfs_fsop_handlereq_t	*hreq)
{
	int			hsize;
	xfs_handle_t		handle;
	struct inode		*inode;
	struct fd		f = {NULL};
	struct path		path;
	int			error;
	struct xfs_inode	*ip;

	if (cmd == XFS_IOC_FD_TO_HANDLE) {
		f = fdget(hreq->fd);
		if (!f.file)
			return -EBADF;
		inode = file_inode(f.file);
	} else {
		error = user_path_at(AT_FDCWD, hreq->path, 0, &path);
		if (error)
			return error;
		inode = d_inode(path.dentry);
	}
	ip = XFS_I(inode);

	/*
	 * We can only generate handles for inodes residing on a XFS filesystem,
	 * and only for regular files, directories or symbolic links.
	 */
	error = -EINVAL;
	if (inode->i_sb->s_magic != XFS_SB_MAGIC)
		goto out_put;

	error = -EBADF;
	if (!S_ISREG(inode->i_mode) &&
	    !S_ISDIR(inode->i_mode) &&
	    !S_ISLNK(inode->i_mode))
		goto out_put;


	memcpy(&handle.ha_fsid, ip->i_mount->m_fixedfsid, sizeof(xfs_fsid_t));

	if (cmd == XFS_IOC_PATH_TO_FSHANDLE) {
		/*
		 * This handle only contains an fsid, zero the rest.
		 */
		memset(&handle.ha_fid, 0, sizeof(handle.ha_fid));
		hsize = sizeof(xfs_fsid_t);
	} else {
		handle.ha_fid.fid_len = sizeof(xfs_fid_t) -
					sizeof(handle.ha_fid.fid_len);
		handle.ha_fid.fid_pad = 0;
		handle.ha_fid.fid_gen = inode->i_generation;
		handle.ha_fid.fid_ino = ip->i_ino;
		hsize = sizeof(xfs_handle_t);
	}

	error = -EFAULT;
	if (copy_to_user(hreq->ohandle, &handle, hsize) ||
	    copy_to_user(hreq->ohandlen, &hsize, sizeof(__s32)))
		goto out_put;

	error = 0;

 out_put:
	if (cmd == XFS_IOC_FD_TO_HANDLE)
		fdput(f);
	else
		path_put(&path);
	return error;
}

/*
 * No need to do permission checks on the various pathname components
 * as the handle operations are privileged.
 */
STATIC int
xfs_handle_acceptable(
	void			*context,
	struct dentry		*dentry)
{
	return 1;
}

/*
 * Convert userspace handle data into a dentry.
 */
struct dentry *
xfs_handle_to_dentry(
	struct file		*parfilp,
	void __user		*uhandle,
	u32			hlen)
{
	xfs_handle_t		handle;
	struct xfs_fid64	fid;

	/*
	 * Only allow handle opens under a directory.
	 */
	if (!S_ISDIR(file_inode(parfilp)->i_mode))
		return ERR_PTR(-ENOTDIR);

	if (hlen != sizeof(xfs_handle_t))
		return ERR_PTR(-EINVAL);
	if (copy_from_user(&handle, uhandle, hlen))
		return ERR_PTR(-EFAULT);
	if (handle.ha_fid.fid_len !=
	    sizeof(handle.ha_fid) - sizeof(handle.ha_fid.fid_len))
		return ERR_PTR(-EINVAL);

	memset(&fid, 0, sizeof(struct fid));
	fid.ino = handle.ha_fid.fid_ino;
	fid.gen = handle.ha_fid.fid_gen;

	return exportfs_decode_fh(parfilp->f_path.mnt, (struct fid *)&fid, 3,
			FILEID_INO32_GEN | XFS_FILEID_TYPE_64FLAG,
			xfs_handle_acceptable, NULL);
}

STATIC struct dentry *
xfs_handlereq_to_dentry(
	struct file		*parfilp,
	xfs_fsop_handlereq_t	*hreq)
{
	return xfs_handle_to_dentry(parfilp, hreq->ihandle, hreq->ihandlen);
}

int
xfs_open_by_handle(
	struct file		*parfilp,
	xfs_fsop_handlereq_t	*hreq)
{
	const struct cred	*cred = current_cred();
	int			error;
	int			fd;
	int			permflag;
	struct file		*filp;
	struct inode		*inode;
	struct dentry		*dentry;
	fmode_t			fmode;
	struct path		path;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	dentry = xfs_handlereq_to_dentry(parfilp, hreq);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);
	inode = d_inode(dentry);

	/* Restrict xfs_open_by_handle to directories & regular files. */
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode))) {
		error = -EPERM;
		goto out_dput;
	}

#if BITS_PER_LONG != 32
	hreq->oflags |= O_LARGEFILE;
#endif

	permflag = hreq->oflags;
	fmode = OPEN_FMODE(permflag);
	if ((!(permflag & O_APPEND) || (permflag & O_TRUNC)) &&
	    (fmode & FMODE_WRITE) && IS_APPEND(inode)) {
		error = -EPERM;
		goto out_dput;
	}

	if ((fmode & FMODE_WRITE) && IS_IMMUTABLE(inode)) {
		error = -EPERM;
		goto out_dput;
	}

	/* Can't write directories. */
	if (S_ISDIR(inode->i_mode) && (fmode & FMODE_WRITE)) {
		error = -EISDIR;
		goto out_dput;
	}

	fd = get_unused_fd_flags(0);
	if (fd < 0) {
		error = fd;
		goto out_dput;
	}

	path.mnt = parfilp->f_path.mnt;
	path.dentry = dentry;
	filp = dentry_open(&path, hreq->oflags, cred);
	dput(dentry);
	if (IS_ERR(filp)) {
		put_unused_fd(fd);
		return PTR_ERR(filp);
	}

	if (S_ISREG(inode->i_mode)) {
		filp->f_flags |= O_NOATIME;
		filp->f_mode |= FMODE_NOCMTIME;
	}

	fd_install(fd, filp);
	return fd;

 out_dput:
	dput(dentry);
	return error;
}

int
xfs_readlink_by_handle(
	struct file		*parfilp,
	xfs_fsop_handlereq_t	*hreq)
{
	struct dentry		*dentry;
	__u32			olen;
	int			error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	dentry = xfs_handlereq_to_dentry(parfilp, hreq);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	/* Restrict this handle operation to symlinks only. */
	if (!d_is_symlink(dentry)) {
		error = -EINVAL;
		goto out_dput;
	}

	if (copy_from_user(&olen, hreq->ohandlen, sizeof(__u32))) {
		error = -EFAULT;
		goto out_dput;
	}

	error = vfs_readlink(dentry, hreq->ohandle, olen);

 out_dput:
	dput(dentry);
	return error;
}

/*
 * Format an attribute and copy it out to the user's buffer.
 * Take care to check values and protect against them changing later,
 * we may be reading them directly out of a user buffer.
 */
static void
xfs_ioc_attr_put_listent(
	struct xfs_attr_list_context *context,
	int			flags,
	unsigned char		*name,
	int			namelen,
	int			valuelen)
{
	struct xfs_attrlist	*alist = context->buffer;
	struct xfs_attrlist_ent	*aep;
	int			arraytop;

	ASSERT(!context->seen_enough);
	ASSERT(context->count >= 0);
	ASSERT(context->count < (ATTR_MAX_VALUELEN/8));
	ASSERT(context->firstu >= sizeof(*alist));
	ASSERT(context->firstu <= context->bufsize);

	/*
	 * Only list entries in the right namespace.
	 */
	if (context->attr_filter != (flags & XFS_ATTR_NSP_ONDISK_MASK))
		return;

	arraytop = sizeof(*alist) +
			context->count * sizeof(alist->al_offset[0]);

	/* decrement by the actual bytes used by the attr */
	context->firstu -= round_up(offsetof(struct xfs_attrlist_ent, a_name) +
			namelen + 1, sizeof(uint32_t));
	if (context->firstu < arraytop) {
		trace_xfs_attr_list_full(context);
		alist->al_more = 1;
		context->seen_enough = 1;
		return;
	}

	aep = context->buffer + context->firstu;
	aep->a_valuelen = valuelen;
	memcpy(aep->a_name, name, namelen);
	aep->a_name[namelen] = 0;
	alist->al_offset[context->count++] = context->firstu;
	alist->al_count = context->count;
	trace_xfs_attr_list_add(context);
}

static unsigned int
xfs_attr_filter(
	u32			ioc_flags)
{
	if (ioc_flags & XFS_IOC_ATTR_ROOT)
		return XFS_ATTR_ROOT;
	if (ioc_flags & XFS_IOC_ATTR_SECURE)
		return XFS_ATTR_SECURE;
	return 0;
}

static unsigned int
xfs_attr_flags(
	u32			ioc_flags)
{
	if (ioc_flags & XFS_IOC_ATTR_CREATE)
		return XATTR_CREATE;
	if (ioc_flags & XFS_IOC_ATTR_REPLACE)
		return XATTR_REPLACE;
	return 0;
}

int
xfs_ioc_attr_list(
	struct xfs_inode		*dp,
	void __user			*ubuf,
	size_t				bufsize,
	int				flags,
	struct xfs_attrlist_cursor __user *ucursor)
{
	struct xfs_attr_list_context	context = { };
	struct xfs_attrlist		*alist;
	void				*buffer;
	int				error;

	if (bufsize < sizeof(struct xfs_attrlist) ||
	    bufsize > XFS_XATTR_LIST_MAX)
		return -EINVAL;

	/*
	 * Reject flags, only allow namespaces.
	 */
	if (flags & ~(XFS_IOC_ATTR_ROOT | XFS_IOC_ATTR_SECURE))
		return -EINVAL;
	if (flags == (XFS_IOC_ATTR_ROOT | XFS_IOC_ATTR_SECURE))
		return -EINVAL;

	/*
	 * Validate the cursor.
	 */
	if (copy_from_user(&context.cursor, ucursor, sizeof(context.cursor)))
		return -EFAULT;
	if (context.cursor.pad1 || context.cursor.pad2)
		return -EINVAL;
	if (!context.cursor.initted &&
	    (context.cursor.hashval || context.cursor.blkno ||
	     context.cursor.offset))
		return -EINVAL;

	buffer = kvzalloc(bufsize, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	/*
	 * Initialize the output buffer.
	 */
	context.dp = dp;
	context.resynch = 1;
	context.attr_filter = xfs_attr_filter(flags);
	context.buffer = buffer;
	context.bufsize = round_down(bufsize, sizeof(uint32_t));
	context.firstu = context.bufsize;
	context.put_listent = xfs_ioc_attr_put_listent;

	alist = context.buffer;
	alist->al_count = 0;
	alist->al_more = 0;
	alist->al_offset[0] = context.bufsize;

	error = xfs_attr_list(&context);
	if (error)
		goto out_free;

	if (copy_to_user(ubuf, buffer, bufsize) ||
	    copy_to_user(ucursor, &context.cursor, sizeof(context.cursor)))
		error = -EFAULT;
out_free:
	kmem_free(buffer);
	return error;
}

STATIC int
xfs_attrlist_by_handle(
	struct file		*parfilp,
	struct xfs_fsop_attrlist_handlereq __user *p)
{
	struct xfs_fsop_attrlist_handlereq al_hreq;
	struct dentry		*dentry;
	int			error = -ENOMEM;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (copy_from_user(&al_hreq, p, sizeof(al_hreq)))
		return -EFAULT;

	dentry = xfs_handlereq_to_dentry(parfilp, &al_hreq.hreq);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	error = xfs_ioc_attr_list(XFS_I(d_inode(dentry)), al_hreq.buffer,
				  al_hreq.buflen, al_hreq.flags, &p->pos);
	dput(dentry);
	return error;
}

static int
xfs_attrmulti_attr_get(
	struct inode		*inode,
	unsigned char		*name,
	unsigned char		__user *ubuf,
	uint32_t		*len,
	uint32_t		flags)
{
	struct xfs_da_args	args = {
		.dp		= XFS_I(inode),
		.attr_filter	= xfs_attr_filter(flags),
		.attr_flags	= xfs_attr_flags(flags),
		.name		= name,
		.namelen	= strlen(name),
		.valuelen	= *len,
	};
	int			error;

	if (*len > XFS_XATTR_SIZE_MAX)
		return -EINVAL;

	error = xfs_attr_get(&args);
	if (error)
		goto out_kfree;

	*len = args.valuelen;
	if (copy_to_user(ubuf, args.value, args.valuelen))
		error = -EFAULT;

out_kfree:
	kmem_free(args.value);
	return error;
}

static int
xfs_attrmulti_attr_set(
	struct inode		*inode,
	unsigned char		*name,
	const unsigned char	__user *ubuf,
	uint32_t		len,
	uint32_t		flags)
{
	struct xfs_da_args	args = {
		.dp		= XFS_I(inode),
		.attr_filter	= xfs_attr_filter(flags),
		.attr_flags	= xfs_attr_flags(flags),
		.name		= name,
		.namelen	= strlen(name),
	};
	int			error;

	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		return -EPERM;

	if (ubuf) {
		if (len > XFS_XATTR_SIZE_MAX)
			return -EINVAL;
		args.value = memdup_user(ubuf, len);
		if (IS_ERR(args.value))
			return PTR_ERR(args.value);
		args.valuelen = len;
	}

	error = xfs_attr_set(&args);
	if (!error && (flags & XFS_IOC_ATTR_ROOT))
		xfs_forget_acl(inode, name);
	kfree(args.value);
	return error;
}

int
xfs_ioc_attrmulti_one(
	struct file		*parfilp,
	struct inode		*inode,
	uint32_t		opcode,
	void __user		*uname,
	void __user		*value,
	uint32_t		*len,
	uint32_t		flags)
{
	unsigned char		*name;
	int			error;

	if ((flags & XFS_IOC_ATTR_ROOT) && (flags & XFS_IOC_ATTR_SECURE))
		return -EINVAL;

	name = strndup_user(uname, MAXNAMELEN);
	if (IS_ERR(name))
		return PTR_ERR(name);

	switch (opcode) {
	case ATTR_OP_GET:
		error = xfs_attrmulti_attr_get(inode, name, value, len, flags);
		break;
	case ATTR_OP_REMOVE:
		value = NULL;
		*len = 0;
		fallthrough;
	case ATTR_OP_SET:
		error = mnt_want_write_file(parfilp);
		if (error)
			break;
		error = xfs_attrmulti_attr_set(inode, name, value, *len, flags);
		mnt_drop_write_file(parfilp);
		break;
	default:
		error = -EINVAL;
		break;
	}

	kfree(name);
	return error;
}

STATIC int
xfs_attrmulti_by_handle(
	struct file		*parfilp,
	void			__user *arg)
{
	int			error;
	xfs_attr_multiop_t	*ops;
	xfs_fsop_attrmulti_handlereq_t am_hreq;
	struct dentry		*dentry;
	unsigned int		i, size;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (copy_from_user(&am_hreq, arg, sizeof(xfs_fsop_attrmulti_handlereq_t)))
		return -EFAULT;

	/* overflow check */
	if (am_hreq.opcount >= INT_MAX / sizeof(xfs_attr_multiop_t))
		return -E2BIG;

	dentry = xfs_handlereq_to_dentry(parfilp, &am_hreq.hreq);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	error = -E2BIG;
	size = am_hreq.opcount * sizeof(xfs_attr_multiop_t);
	if (!size || size > 16 * PAGE_SIZE)
		goto out_dput;

	ops = memdup_user(am_hreq.ops, size);
	if (IS_ERR(ops)) {
		error = PTR_ERR(ops);
		goto out_dput;
	}

	error = 0;
	for (i = 0; i < am_hreq.opcount; i++) {
		ops[i].am_error = xfs_ioc_attrmulti_one(parfilp,
				d_inode(dentry), ops[i].am_opcode,
				ops[i].am_attrname, ops[i].am_attrvalue,
				&ops[i].am_length, ops[i].am_flags);
	}

	if (copy_to_user(am_hreq.ops, ops, size))
		error = -EFAULT;

	kfree(ops);
 out_dput:
	dput(dentry);
	return error;
}

/* Return 0 on success or positive error */
int
xfs_fsbulkstat_one_fmt(
	struct xfs_ibulk		*breq,
	const struct xfs_bulkstat	*bstat)
{
	struct xfs_bstat		bs1;

	xfs_bulkstat_to_bstat(breq->mp, &bs1, bstat);
	if (copy_to_user(breq->ubuffer, &bs1, sizeof(bs1)))
		return -EFAULT;
	return xfs_ibulk_advance(breq, sizeof(struct xfs_bstat));
}

int
xfs_fsinumbers_fmt(
	struct xfs_ibulk		*breq,
	const struct xfs_inumbers	*igrp)
{
	struct xfs_inogrp		ig1;

	xfs_inumbers_to_inogrp(&ig1, igrp);
	if (copy_to_user(breq->ubuffer, &ig1, sizeof(struct xfs_inogrp)))
		return -EFAULT;
	return xfs_ibulk_advance(breq, sizeof(struct xfs_inogrp));
}

STATIC int
xfs_ioc_fsbulkstat(
	struct file		*file,
	unsigned int		cmd,
	void			__user *arg)
{
	struct xfs_mount	*mp = XFS_I(file_inode(file))->i_mount;
	struct xfs_fsop_bulkreq	bulkreq;
	struct xfs_ibulk	breq = {
		.mp		= mp,
		.mnt_userns	= file_mnt_user_ns(file),
		.ocount		= 0,
	};
	xfs_ino_t		lastino;
	int			error;

	/* done = 1 if there are more stats to get and if bulkstat */
	/* should be called again (unused here, but used in dmapi) */

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (xfs_is_shutdown(mp))
		return -EIO;

	if (copy_from_user(&bulkreq, arg, sizeof(struct xfs_fsop_bulkreq)))
		return -EFAULT;

	if (copy_from_user(&lastino, bulkreq.lastip, sizeof(__s64)))
		return -EFAULT;

	if (bulkreq.icount <= 0)
		return -EINVAL;

	if (bulkreq.ubuffer == NULL)
		return -EINVAL;

	breq.ubuffer = bulkreq.ubuffer;
	breq.icount = bulkreq.icount;

	/*
	 * FSBULKSTAT_SINGLE expects that *lastip contains the inode number
	 * that we want to stat.  However, FSINUMBERS and FSBULKSTAT expect
	 * that *lastip contains either zero or the number of the last inode to
	 * be examined by the previous call and return results starting with
	 * the next inode after that.  The new bulk request back end functions
	 * take the inode to start with, so we have to compute the startino
	 * parameter from lastino to maintain correct function.  lastino == 0
	 * is a special case because it has traditionally meant "first inode
	 * in filesystem".
	 */
	if (cmd == XFS_IOC_FSINUMBERS) {
		breq.startino = lastino ? lastino + 1 : 0;
		error = xfs_inumbers(&breq, xfs_fsinumbers_fmt);
		lastino = breq.startino - 1;
	} else if (cmd == XFS_IOC_FSBULKSTAT_SINGLE) {
		breq.startino = lastino;
		breq.icount = 1;
		error = xfs_bulkstat_one(&breq, xfs_fsbulkstat_one_fmt);
	} else {	/* XFS_IOC_FSBULKSTAT */
		breq.startino = lastino ? lastino + 1 : 0;
		error = xfs_bulkstat(&breq, xfs_fsbulkstat_one_fmt);
		lastino = breq.startino - 1;
	}

	if (error)
		return error;

	if (bulkreq.lastip != NULL &&
	    copy_to_user(bulkreq.lastip, &lastino, sizeof(xfs_ino_t)))
		return -EFAULT;

	if (bulkreq.ocount != NULL &&
	    copy_to_user(bulkreq.ocount, &breq.ocount, sizeof(__s32)))
		return -EFAULT;

	return 0;
}

/* Return 0 on success or positive error */
static int
xfs_bulkstat_fmt(
	struct xfs_ibulk		*breq,
	const struct xfs_bulkstat	*bstat)
{
	if (copy_to_user(breq->ubuffer, bstat, sizeof(struct xfs_bulkstat)))
		return -EFAULT;
	return xfs_ibulk_advance(breq, sizeof(struct xfs_bulkstat));
}

/*
 * Check the incoming bulk request @hdr from userspace and initialize the
 * internal @breq bulk request appropriately.  Returns 0 if the bulk request
 * should proceed; -ECANCELED if there's nothing to do; or the usual
 * negative error code.
 */
static int
xfs_bulk_ireq_setup(
	struct xfs_mount	*mp,
	struct xfs_bulk_ireq	*hdr,
	struct xfs_ibulk	*breq,
	void __user		*ubuffer)
{
	if (hdr->icount == 0 ||
	    (hdr->flags & ~XFS_BULK_IREQ_FLAGS_ALL) ||
	    memchr_inv(hdr->reserved, 0, sizeof(hdr->reserved)))
		return -EINVAL;

	breq->startino = hdr->ino;
	breq->ubuffer = ubuffer;
	breq->icount = hdr->icount;
	breq->ocount = 0;
	breq->flags = 0;

	/*
	 * The @ino parameter is a special value, so we must look it up here.
	 * We're not allowed to have IREQ_AGNO, and we only return one inode
	 * worth of data.
	 */
	if (hdr->flags & XFS_BULK_IREQ_SPECIAL) {
		if (hdr->flags & XFS_BULK_IREQ_AGNO)
			return -EINVAL;

		switch (hdr->ino) {
		case XFS_BULK_IREQ_SPECIAL_ROOT:
			hdr->ino = mp->m_sb.sb_rootino;
			break;
		default:
			return -EINVAL;
		}
		breq->icount = 1;
	}

	/*
	 * The IREQ_AGNO flag means that we only want results from a given AG.
	 * If @hdr->ino is zero, we start iterating in that AG.  If @hdr->ino is
	 * beyond the specified AG then we return no results.
	 */
	if (hdr->flags & XFS_BULK_IREQ_AGNO) {
		if (hdr->agno >= mp->m_sb.sb_agcount)
			return -EINVAL;

		if (breq->startino == 0)
			breq->startino = XFS_AGINO_TO_INO(mp, hdr->agno, 0);
		else if (XFS_INO_TO_AGNO(mp, breq->startino) < hdr->agno)
			return -EINVAL;

		breq->flags |= XFS_IBULK_SAME_AG;

		/* Asking for an inode past the end of the AG?  We're done! */
		if (XFS_INO_TO_AGNO(mp, breq->startino) > hdr->agno)
			return -ECANCELED;
	} else if (hdr->agno)
		return -EINVAL;

	/* Asking for an inode past the end of the FS?  We're done! */
	if (XFS_INO_TO_AGNO(mp, breq->startino) >= mp->m_sb.sb_agcount)
		return -ECANCELED;

	return 0;
}

/*
 * Update the userspace bulk request @hdr to reflect the end state of the
 * internal bulk request @breq.
 */
static void
xfs_bulk_ireq_teardown(
	struct xfs_bulk_ireq	*hdr,
	struct xfs_ibulk	*breq)
{
	hdr->ino = breq->startino;
	hdr->ocount = breq->ocount;
}

/* Handle the v5 bulkstat ioctl. */
STATIC int
xfs_ioc_bulkstat(
	struct file			*file,
	unsigned int			cmd,
	struct xfs_bulkstat_req __user	*arg)
{
	struct xfs_mount		*mp = XFS_I(file_inode(file))->i_mount;
	struct xfs_bulk_ireq		hdr;
	struct xfs_ibulk		breq = {
		.mp			= mp,
		.mnt_userns		= file_mnt_user_ns(file),
	};
	int				error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (xfs_is_shutdown(mp))
		return -EIO;

	if (copy_from_user(&hdr, &arg->hdr, sizeof(hdr)))
		return -EFAULT;

	error = xfs_bulk_ireq_setup(mp, &hdr, &breq, arg->bulkstat);
	if (error == -ECANCELED)
		goto out_teardown;
	if (error < 0)
		return error;

	error = xfs_bulkstat(&breq, xfs_bulkstat_fmt);
	if (error)
		return error;

out_teardown:
	xfs_bulk_ireq_teardown(&hdr, &breq);
	if (copy_to_user(&arg->hdr, &hdr, sizeof(hdr)))
		return -EFAULT;

	return 0;
}

STATIC int
xfs_inumbers_fmt(
	struct xfs_ibulk		*breq,
	const struct xfs_inumbers	*igrp)
{
	if (copy_to_user(breq->ubuffer, igrp, sizeof(struct xfs_inumbers)))
		return -EFAULT;
	return xfs_ibulk_advance(breq, sizeof(struct xfs_inumbers));
}

/* Handle the v5 inumbers ioctl. */
STATIC int
xfs_ioc_inumbers(
	struct xfs_mount		*mp,
	unsigned int			cmd,
	struct xfs_inumbers_req __user	*arg)
{
	struct xfs_bulk_ireq		hdr;
	struct xfs_ibulk		breq = {
		.mp			= mp,
	};
	int				error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (xfs_is_shutdown(mp))
		return -EIO;

	if (copy_from_user(&hdr, &arg->hdr, sizeof(hdr)))
		return -EFAULT;

	error = xfs_bulk_ireq_setup(mp, &hdr, &breq, arg->inumbers);
	if (error == -ECANCELED)
		goto out_teardown;
	if (error < 0)
		return error;

	error = xfs_inumbers(&breq, xfs_inumbers_fmt);
	if (error)
		return error;

out_teardown:
	xfs_bulk_ireq_teardown(&hdr, &breq);
	if (copy_to_user(&arg->hdr, &hdr, sizeof(hdr)))
		return -EFAULT;

	return 0;
}

STATIC int
xfs_ioc_fsgeometry(
	struct xfs_mount	*mp,
	void			__user *arg,
	int			struct_version)
{
	struct xfs_fsop_geom	fsgeo;
	size_t			len;

	xfs_fs_geometry(mp, &fsgeo, struct_version);

	if (struct_version <= 3)
		len = sizeof(struct xfs_fsop_geom_v1);
	else if (struct_version == 4)
		len = sizeof(struct xfs_fsop_geom_v4);
	else {
		xfs_fsop_geom_health(mp, &fsgeo);
		len = sizeof(fsgeo);
	}

	if (copy_to_user(arg, &fsgeo, len))
		return -EFAULT;
	return 0;
}

STATIC int
xfs_ioc_ag_geometry(
	struct xfs_mount	*mp,
	void			__user *arg)
{
	struct xfs_ag_geometry	ageo;
	int			error;

	if (copy_from_user(&ageo, arg, sizeof(ageo)))
		return -EFAULT;
	if (ageo.ag_flags)
		return -EINVAL;
	if (memchr_inv(&ageo.ag_reserved, 0, sizeof(ageo.ag_reserved)))
		return -EINVAL;

	error = xfs_ag_get_geometry(mp, ageo.ag_number, &ageo);
	if (error)
		return error;

	if (copy_to_user(arg, &ageo, sizeof(ageo)))
		return -EFAULT;
	return 0;
}

/*
 * Linux extended inode flags interface.
 */

static void
xfs_fill_fsxattr(
	struct xfs_inode	*ip,
	int			whichfork,
	struct fileattr		*fa)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_ifork	*ifp = XFS_IFORK_PTR(ip, whichfork);

	fileattr_fill_xflags(fa, xfs_ip2xflags(ip));

	if (ip->i_diflags & XFS_DIFLAG_EXTSIZE) {
		fa->fsx_extsize = XFS_FSB_TO_B(mp, ip->i_extsize);
	} else if (ip->i_diflags & XFS_DIFLAG_EXTSZINHERIT) {
		/*
		 * Don't let a misaligned extent size hint on a directory
		 * escape to userspace if it won't pass the setattr checks
		 * later.
		 */
		if ((ip->i_diflags & XFS_DIFLAG_RTINHERIT) &&
		    ip->i_extsize % mp->m_sb.sb_rextsize > 0) {
			fa->fsx_xflags &= ~(FS_XFLAG_EXTSIZE |
					    FS_XFLAG_EXTSZINHERIT);
			fa->fsx_extsize = 0;
		} else {
			fa->fsx_extsize = XFS_FSB_TO_B(mp, ip->i_extsize);
		}
	}

	if (ip->i_diflags2 & XFS_DIFLAG2_COWEXTSIZE)
		fa->fsx_cowextsize = XFS_FSB_TO_B(mp, ip->i_cowextsize);
	fa->fsx_projid = ip->i_projid;
	if (ifp && !xfs_need_iread_extents(ifp))
		fa->fsx_nextents = xfs_iext_count(ifp);
	else
		fa->fsx_nextents = xfs_ifork_nextents(ifp);
}

STATIC int
xfs_ioc_fsgetxattra(
	xfs_inode_t		*ip,
	void			__user *arg)
{
	struct fileattr		fa;

	xfs_ilock(ip, XFS_ILOCK_SHARED);
	xfs_fill_fsxattr(ip, XFS_ATTR_FORK, &fa);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	return copy_fsxattr_to_user(&fa, arg);
}

int
xfs_fileattr_get(
	struct dentry		*dentry,
	struct fileattr		*fa)
{
	struct xfs_inode	*ip = XFS_I(d_inode(dentry));

	if (d_is_special(dentry))
		return -ENOTTY;

	xfs_ilock(ip, XFS_ILOCK_SHARED);
	xfs_fill_fsxattr(ip, XFS_DATA_FORK, fa);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	return 0;
}

STATIC uint16_t
xfs_flags2diflags(
	struct xfs_inode	*ip,
	unsigned int		xflags)
{
	/* can't set PREALLOC this way, just preserve it */
	uint16_t		di_flags =
		(ip->i_diflags & XFS_DIFLAG_PREALLOC);

	if (xflags & FS_XFLAG_IMMUTABLE)
		di_flags |= XFS_DIFLAG_IMMUTABLE;
	if (xflags & FS_XFLAG_APPEND)
		di_flags |= XFS_DIFLAG_APPEND;
	if (xflags & FS_XFLAG_SYNC)
		di_flags |= XFS_DIFLAG_SYNC;
	if (xflags & FS_XFLAG_NOATIME)
		di_flags |= XFS_DIFLAG_NOATIME;
	if (xflags & FS_XFLAG_NODUMP)
		di_flags |= XFS_DIFLAG_NODUMP;
	if (xflags & FS_XFLAG_NODEFRAG)
		di_flags |= XFS_DIFLAG_NODEFRAG;
	if (xflags & FS_XFLAG_FILESTREAM)
		di_flags |= XFS_DIFLAG_FILESTREAM;
	if (S_ISDIR(VFS_I(ip)->i_mode)) {
		if (xflags & FS_XFLAG_RTINHERIT)
			di_flags |= XFS_DIFLAG_RTINHERIT;
		if (xflags & FS_XFLAG_NOSYMLINKS)
			di_flags |= XFS_DIFLAG_NOSYMLINKS;
		if (xflags & FS_XFLAG_EXTSZINHERIT)
			di_flags |= XFS_DIFLAG_EXTSZINHERIT;
		if (xflags & FS_XFLAG_PROJINHERIT)
			di_flags |= XFS_DIFLAG_PROJINHERIT;
	} else if (S_ISREG(VFS_I(ip)->i_mode)) {
		if (xflags & FS_XFLAG_REALTIME)
			di_flags |= XFS_DIFLAG_REALTIME;
		if (xflags & FS_XFLAG_EXTSIZE)
			di_flags |= XFS_DIFLAG_EXTSIZE;
	}

	return di_flags;
}

STATIC uint64_t
xfs_flags2diflags2(
	struct xfs_inode	*ip,
	unsigned int		xflags)
{
	uint64_t		di_flags2 =
		(ip->i_diflags2 & (XFS_DIFLAG2_REFLINK |
				   XFS_DIFLAG2_BIGTIME));

	if (xflags & FS_XFLAG_DAX)
		di_flags2 |= XFS_DIFLAG2_DAX;
	if (xflags & FS_XFLAG_COWEXTSIZE)
		di_flags2 |= XFS_DIFLAG2_COWEXTSIZE;

	return di_flags2;
}

static int
xfs_ioctl_setattr_xflags(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip,
	struct fileattr		*fa)
{
	struct xfs_mount	*mp = ip->i_mount;
	uint64_t		i_flags2;

	/* Can't change realtime flag if any extents are allocated. */
	if ((ip->i_df.if_nextents || ip->i_delayed_blks) &&
	    XFS_IS_REALTIME_INODE(ip) != (fa->fsx_xflags & FS_XFLAG_REALTIME))
		return -EINVAL;

	/* If realtime flag is set then must have realtime device */
	if (fa->fsx_xflags & FS_XFLAG_REALTIME) {
		if (mp->m_sb.sb_rblocks == 0 || mp->m_sb.sb_rextsize == 0 ||
		    (ip->i_extsize % mp->m_sb.sb_rextsize))
			return -EINVAL;
	}

	/* Clear reflink if we are actually able to set the rt flag. */
	if ((fa->fsx_xflags & FS_XFLAG_REALTIME) && xfs_is_reflink_inode(ip))
		ip->i_diflags2 &= ~XFS_DIFLAG2_REFLINK;

	/* Don't allow us to set DAX mode for a reflinked file for now. */
	if ((fa->fsx_xflags & FS_XFLAG_DAX) && xfs_is_reflink_inode(ip))
		return -EINVAL;

	/* diflags2 only valid for v3 inodes. */
	i_flags2 = xfs_flags2diflags2(ip, fa->fsx_xflags);
	if (i_flags2 && !xfs_has_v3inodes(mp))
		return -EINVAL;

	ip->i_diflags = xfs_flags2diflags(ip, fa->fsx_xflags);
	ip->i_diflags2 = i_flags2;

	xfs_diflags_to_iflags(ip, false);
	xfs_trans_ichgtime(tp, ip, XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	XFS_STATS_INC(mp, xs_ig_attrchg);
	return 0;
}

static void
xfs_ioctl_setattr_prepare_dax(
	struct xfs_inode	*ip,
	struct fileattr		*fa)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct inode            *inode = VFS_I(ip);

	if (S_ISDIR(inode->i_mode))
		return;

	if (xfs_has_dax_always(mp) || xfs_has_dax_never(mp))
		return;

	if (((fa->fsx_xflags & FS_XFLAG_DAX) &&
	    !(ip->i_diflags2 & XFS_DIFLAG2_DAX)) ||
	    (!(fa->fsx_xflags & FS_XFLAG_DAX) &&
	     (ip->i_diflags2 & XFS_DIFLAG2_DAX)))
		d_mark_dontcache(inode);
}

/*
 * Set up the transaction structure for the setattr operation, checking that we
 * have permission to do so. On success, return a clean transaction and the
 * inode locked exclusively ready for further operation specific checks. On
 * failure, return an error without modifying or locking the inode.
 */
static struct xfs_trans *
xfs_ioctl_setattr_get_trans(
	struct xfs_inode	*ip,
	struct xfs_dquot	*pdqp)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	int			error = -EROFS;

	if (xfs_is_readonly(mp))
		goto out_error;
	error = -EIO;
	if (xfs_is_shutdown(mp))
		goto out_error;

	error = xfs_trans_alloc_ichange(ip, NULL, NULL, pdqp,
			has_capability_noaudit(current, CAP_FOWNER), &tp);
	if (error)
		goto out_error;

	if (xfs_has_wsync(mp))
		xfs_trans_set_sync(tp);

	return tp;

out_error:
	return ERR_PTR(error);
}

/*
 * Validate a proposed extent size hint.  For regular files, the hint can only
 * be changed if no extents are allocated.
 */
static int
xfs_ioctl_setattr_check_extsize(
	struct xfs_inode	*ip,
	struct fileattr		*fa)
{
	struct xfs_mount	*mp = ip->i_mount;
	xfs_failaddr_t		failaddr;
	uint16_t		new_diflags;

	if (!fa->fsx_valid)
		return 0;

	if (S_ISREG(VFS_I(ip)->i_mode) && ip->i_df.if_nextents &&
	    XFS_FSB_TO_B(mp, ip->i_extsize) != fa->fsx_extsize)
		return -EINVAL;

	if (fa->fsx_extsize & mp->m_blockmask)
		return -EINVAL;

	new_diflags = xfs_flags2diflags(ip, fa->fsx_xflags);

	/*
	 * Inode verifiers do not check that the extent size hint is an integer
	 * multiple of the rt extent size on a directory with both rtinherit
	 * and extszinherit flags set.  Don't let sysadmins misconfigure
	 * directories.
	 */
	if ((new_diflags & XFS_DIFLAG_RTINHERIT) &&
	    (new_diflags & XFS_DIFLAG_EXTSZINHERIT)) {
		unsigned int	rtextsize_bytes;

		rtextsize_bytes = XFS_FSB_TO_B(mp, mp->m_sb.sb_rextsize);
		if (fa->fsx_extsize % rtextsize_bytes)
			return -EINVAL;
	}

	failaddr = xfs_inode_validate_extsize(ip->i_mount,
			XFS_B_TO_FSB(mp, fa->fsx_extsize),
			VFS_I(ip)->i_mode, new_diflags);
	return failaddr != NULL ? -EINVAL : 0;
}

static int
xfs_ioctl_setattr_check_cowextsize(
	struct xfs_inode	*ip,
	struct fileattr		*fa)
{
	struct xfs_mount	*mp = ip->i_mount;
	xfs_failaddr_t		failaddr;
	uint64_t		new_diflags2;
	uint16_t		new_diflags;

	if (!fa->fsx_valid)
		return 0;

	if (fa->fsx_cowextsize & mp->m_blockmask)
		return -EINVAL;

	new_diflags = xfs_flags2diflags(ip, fa->fsx_xflags);
	new_diflags2 = xfs_flags2diflags2(ip, fa->fsx_xflags);

	failaddr = xfs_inode_validate_cowextsize(ip->i_mount,
			XFS_B_TO_FSB(mp, fa->fsx_cowextsize),
			VFS_I(ip)->i_mode, new_diflags, new_diflags2);
	return failaddr != NULL ? -EINVAL : 0;
}

static int
xfs_ioctl_setattr_check_projid(
	struct xfs_inode	*ip,
	struct fileattr		*fa)
{
	if (!fa->fsx_valid)
		return 0;

	/* Disallow 32bit project ids if 32bit IDs are not enabled. */
	if (fa->fsx_projid > (uint16_t)-1 &&
	    !xfs_has_projid32(ip->i_mount))
		return -EINVAL;
	return 0;
}

int
xfs_fileattr_set(
	struct user_namespace	*mnt_userns,
	struct dentry		*dentry,
	struct fileattr		*fa)
{
	struct xfs_inode	*ip = XFS_I(d_inode(dentry));
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	struct xfs_dquot	*pdqp = NULL;
	struct xfs_dquot	*olddquot = NULL;
	int			error;

	trace_xfs_ioctl_setattr(ip);

	if (d_is_special(dentry))
		return -ENOTTY;

	if (!fa->fsx_valid) {
		if (fa->flags & ~(FS_IMMUTABLE_FL | FS_APPEND_FL |
				  FS_NOATIME_FL | FS_NODUMP_FL |
				  FS_SYNC_FL | FS_DAX_FL | FS_PROJINHERIT_FL))
			return -EOPNOTSUPP;
	}

	error = xfs_ioctl_setattr_check_projid(ip, fa);
	if (error)
		return error;

	/*
	 * If disk quotas is on, we make sure that the dquots do exist on disk,
	 * before we start any other transactions. Trying to do this later
	 * is messy. We don't care to take a readlock to look at the ids
	 * in inode here, because we can't hold it across the trans_reserve.
	 * If the IDs do change before we take the ilock, we're covered
	 * because the i_*dquot fields will get updated anyway.
	 */
	if (fa->fsx_valid && XFS_IS_QUOTA_ON(mp)) {
		error = xfs_qm_vop_dqalloc(ip, VFS_I(ip)->i_uid,
				VFS_I(ip)->i_gid, fa->fsx_projid,
				XFS_QMOPT_PQUOTA, NULL, NULL, &pdqp);
		if (error)
			return error;
	}

	xfs_ioctl_setattr_prepare_dax(ip, fa);

	tp = xfs_ioctl_setattr_get_trans(ip, pdqp);
	if (IS_ERR(tp)) {
		error = PTR_ERR(tp);
		goto error_free_dquots;
	}

	error = xfs_ioctl_setattr_check_extsize(ip, fa);
	if (error)
		goto error_trans_cancel;

	error = xfs_ioctl_setattr_check_cowextsize(ip, fa);
	if (error)
		goto error_trans_cancel;

	error = xfs_ioctl_setattr_xflags(tp, ip, fa);
	if (error)
		goto error_trans_cancel;

	if (!fa->fsx_valid)
		goto skip_xattr;
	/*
	 * Change file ownership.  Must be the owner or privileged.  CAP_FSETID
	 * overrides the following restrictions:
	 *
	 * The set-user-ID and set-group-ID bits of a file will be cleared upon
	 * successful return from chown()
	 */

	if ((VFS_I(ip)->i_mode & (S_ISUID|S_ISGID)) &&
	    !capable_wrt_inode_uidgid(mnt_userns, VFS_I(ip), CAP_FSETID))
		VFS_I(ip)->i_mode &= ~(S_ISUID|S_ISGID);

	/* Change the ownerships and register project quota modifications */
	if (ip->i_projid != fa->fsx_projid) {
		if (XFS_IS_PQUOTA_ON(mp)) {
			olddquot = xfs_qm_vop_chown(tp, ip,
						&ip->i_pdquot, pdqp);
		}
		ip->i_projid = fa->fsx_projid;
	}

	/*
	 * Only set the extent size hint if we've already determined that the
	 * extent size hint should be set on the inode. If no extent size flags
	 * are set on the inode then unconditionally clear the extent size hint.
	 */
	if (ip->i_diflags & (XFS_DIFLAG_EXTSIZE | XFS_DIFLAG_EXTSZINHERIT))
		ip->i_extsize = XFS_B_TO_FSB(mp, fa->fsx_extsize);
	else
		ip->i_extsize = 0;

	if (xfs_has_v3inodes(mp)) {
		if (ip->i_diflags2 & XFS_DIFLAG2_COWEXTSIZE)
			ip->i_cowextsize = XFS_B_TO_FSB(mp, fa->fsx_cowextsize);
		else
			ip->i_cowextsize = 0;
	}

skip_xattr:
	error = xfs_trans_commit(tp);

	/*
	 * Release any dquot(s) the inode had kept before chown.
	 */
	xfs_qm_dqrele(olddquot);
	xfs_qm_dqrele(pdqp);

	return error;

error_trans_cancel:
	xfs_trans_cancel(tp);
error_free_dquots:
	xfs_qm_dqrele(pdqp);
	return error;
}

static bool
xfs_getbmap_format(
	struct kgetbmap		*p,
	struct getbmapx __user	*u,
	size_t			recsize)
{
	if (put_user(p->bmv_offset, &u->bmv_offset) ||
	    put_user(p->bmv_block, &u->bmv_block) ||
	    put_user(p->bmv_length, &u->bmv_length) ||
	    put_user(0, &u->bmv_count) ||
	    put_user(0, &u->bmv_entries))
		return false;
	if (recsize < sizeof(struct getbmapx))
		return true;
	if (put_user(0, &u->bmv_iflags) ||
	    put_user(p->bmv_oflags, &u->bmv_oflags) ||
	    put_user(0, &u->bmv_unused1) ||
	    put_user(0, &u->bmv_unused2))
		return false;
	return true;
}

STATIC int
xfs_ioc_getbmap(
	struct file		*file,
	unsigned int		cmd,
	void			__user *arg)
{
	struct getbmapx		bmx = { 0 };
	struct kgetbmap		*buf;
	size_t			recsize;
	int			error, i;

	switch (cmd) {
	case XFS_IOC_GETBMAPA:
		bmx.bmv_iflags = BMV_IF_ATTRFORK;
		fallthrough;
	case XFS_IOC_GETBMAP:
		/* struct getbmap is a strict subset of struct getbmapx. */
		recsize = sizeof(struct getbmap);
		break;
	case XFS_IOC_GETBMAPX:
		recsize = sizeof(struct getbmapx);
		break;
	default:
		return -EINVAL;
	}

	if (copy_from_user(&bmx, arg, recsize))
		return -EFAULT;

	if (bmx.bmv_count < 2)
		return -EINVAL;
	if (bmx.bmv_count >= INT_MAX / recsize)
		return -ENOMEM;

	buf = kvcalloc(bmx.bmv_count, sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	error = xfs_getbmap(XFS_I(file_inode(file)), &bmx, buf);
	if (error)
		goto out_free_buf;

	error = -EFAULT;
	if (copy_to_user(arg, &bmx, recsize))
		goto out_free_buf;
	arg += recsize;

	for (i = 0; i < bmx.bmv_entries; i++) {
		if (!xfs_getbmap_format(buf + i, arg, recsize))
			goto out_free_buf;
		arg += recsize;
	}

	error = 0;
out_free_buf:
	kmem_free(buf);
	return error;
}

STATIC int
xfs_ioc_getfsmap(
	struct xfs_inode	*ip,
	struct fsmap_head	__user *arg)
{
	struct xfs_fsmap_head	xhead = {0};
	struct fsmap_head	head;
	struct fsmap		*recs;
	unsigned int		count;
	__u32			last_flags = 0;
	bool			done = false;
	int			error;

	if (copy_from_user(&head, arg, sizeof(struct fsmap_head)))
		return -EFAULT;
	if (memchr_inv(head.fmh_reserved, 0, sizeof(head.fmh_reserved)) ||
	    memchr_inv(head.fmh_keys[0].fmr_reserved, 0,
		       sizeof(head.fmh_keys[0].fmr_reserved)) ||
	    memchr_inv(head.fmh_keys[1].fmr_reserved, 0,
		       sizeof(head.fmh_keys[1].fmr_reserved)))
		return -EINVAL;

	/*
	 * Use an internal memory buffer so that we don't have to copy fsmap
	 * data to userspace while holding locks.  Start by trying to allocate
	 * up to 128k for the buffer, but fall back to a single page if needed.
	 */
	count = min_t(unsigned int, head.fmh_count,
			131072 / sizeof(struct fsmap));
	recs = kvcalloc(count, sizeof(struct fsmap), GFP_KERNEL);
	if (!recs) {
		count = min_t(unsigned int, head.fmh_count,
				PAGE_SIZE / sizeof(struct fsmap));
		recs = kvcalloc(count, sizeof(struct fsmap), GFP_KERNEL);
		if (!recs)
			return -ENOMEM;
	}

	xhead.fmh_iflags = head.fmh_iflags;
	xfs_fsmap_to_internal(&xhead.fmh_keys[0], &head.fmh_keys[0]);
	xfs_fsmap_to_internal(&xhead.fmh_keys[1], &head.fmh_keys[1]);

	trace_xfs_getfsmap_low_key(ip->i_mount, &xhead.fmh_keys[0]);
	trace_xfs_getfsmap_high_key(ip->i_mount, &xhead.fmh_keys[1]);

	head.fmh_entries = 0;
	do {
		struct fsmap __user	*user_recs;
		struct fsmap		*last_rec;

		user_recs = &arg->fmh_recs[head.fmh_entries];
		xhead.fmh_entries = 0;
		xhead.fmh_count = min_t(unsigned int, count,
					head.fmh_count - head.fmh_entries);

		/* Run query, record how many entries we got. */
		error = xfs_getfsmap(ip->i_mount, &xhead, recs);
		switch (error) {
		case 0:
			/*
			 * There are no more records in the result set.  Copy
			 * whatever we got to userspace and break out.
			 */
			done = true;
			break;
		case -ECANCELED:
			/*
			 * The internal memory buffer is full.  Copy whatever
			 * records we got to userspace and go again if we have
			 * not yet filled the userspace buffer.
			 */
			error = 0;
			break;
		default:
			goto out_free;
		}
		head.fmh_entries += xhead.fmh_entries;
		head.fmh_oflags = xhead.fmh_oflags;

		/*
		 * If the caller wanted a record count or there aren't any
		 * new records to return, we're done.
		 */
		if (head.fmh_count == 0 || xhead.fmh_entries == 0)
			break;

		/* Copy all the records we got out to userspace. */
		if (copy_to_user(user_recs, recs,
				 xhead.fmh_entries * sizeof(struct fsmap))) {
			error = -EFAULT;
			goto out_free;
		}

		/* Remember the last record flags we copied to userspace. */
		last_rec = &recs[xhead.fmh_entries - 1];
		last_flags = last_rec->fmr_flags;

		/* Set up the low key for the next iteration. */
		xfs_fsmap_to_internal(&xhead.fmh_keys[0], last_rec);
		trace_xfs_getfsmap_low_key(ip->i_mount, &xhead.fmh_keys[0]);
	} while (!done && head.fmh_entries < head.fmh_count);

	/*
	 * If there are no more records in the query result set and we're not
	 * in counting mode, mark the last record returned with the LAST flag.
	 */
	if (done && head.fmh_count > 0 && head.fmh_entries > 0) {
		struct fsmap __user	*user_rec;

		last_flags |= FMR_OF_LAST;
		user_rec = &arg->fmh_recs[head.fmh_entries - 1];

		if (copy_to_user(&user_rec->fmr_flags, &last_flags,
					sizeof(last_flags))) {
			error = -EFAULT;
			goto out_free;
		}
	}

	/* copy back header */
	if (copy_to_user(arg, &head, sizeof(struct fsmap_head))) {
		error = -EFAULT;
		goto out_free;
	}

out_free:
	kmem_free(recs);
	return error;
}

STATIC int
xfs_ioc_scrub_metadata(
	struct file			*file,
	void				__user *arg)
{
	struct xfs_scrub_metadata	scrub;
	int				error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&scrub, arg, sizeof(scrub)))
		return -EFAULT;

	error = xfs_scrub_metadata(file, &scrub);
	if (error)
		return error;

	if (copy_to_user(arg, &scrub, sizeof(scrub)))
		return -EFAULT;

	return 0;
}

int
xfs_ioc_swapext(
	xfs_swapext_t	*sxp)
{
	xfs_inode_t     *ip, *tip;
	struct fd	f, tmp;
	int		error = 0;

	/* Pull information for the target fd */
	f = fdget((int)sxp->sx_fdtarget);
	if (!f.file) {
		error = -EINVAL;
		goto out;
	}

	if (!(f.file->f_mode & FMODE_WRITE) ||
	    !(f.file->f_mode & FMODE_READ) ||
	    (f.file->f_flags & O_APPEND)) {
		error = -EBADF;
		goto out_put_file;
	}

	tmp = fdget((int)sxp->sx_fdtmp);
	if (!tmp.file) {
		error = -EINVAL;
		goto out_put_file;
	}

	if (!(tmp.file->f_mode & FMODE_WRITE) ||
	    !(tmp.file->f_mode & FMODE_READ) ||
	    (tmp.file->f_flags & O_APPEND)) {
		error = -EBADF;
		goto out_put_tmp_file;
	}

	if (IS_SWAPFILE(file_inode(f.file)) ||
	    IS_SWAPFILE(file_inode(tmp.file))) {
		error = -EINVAL;
		goto out_put_tmp_file;
	}

	/*
	 * We need to ensure that the fds passed in point to XFS inodes
	 * before we cast and access them as XFS structures as we have no
	 * control over what the user passes us here.
	 */
	if (f.file->f_op != &xfs_file_operations ||
	    tmp.file->f_op != &xfs_file_operations) {
		error = -EINVAL;
		goto out_put_tmp_file;
	}

	ip = XFS_I(file_inode(f.file));
	tip = XFS_I(file_inode(tmp.file));

	if (ip->i_mount != tip->i_mount) {
		error = -EINVAL;
		goto out_put_tmp_file;
	}

	if (ip->i_ino == tip->i_ino) {
		error = -EINVAL;
		goto out_put_tmp_file;
	}

	if (xfs_is_shutdown(ip->i_mount)) {
		error = -EIO;
		goto out_put_tmp_file;
	}

	error = xfs_swap_extents(ip, tip, sxp);

 out_put_tmp_file:
	fdput(tmp);
 out_put_file:
	fdput(f);
 out:
	return error;
}

static int
xfs_ioc_getlabel(
	struct xfs_mount	*mp,
	char			__user *user_label)
{
	struct xfs_sb		*sbp = &mp->m_sb;
	char			label[XFSLABEL_MAX + 1];

	/* Paranoia */
	BUILD_BUG_ON(sizeof(sbp->sb_fname) > FSLABEL_MAX);

	/* 1 larger than sb_fname, so this ensures a trailing NUL char */
	memset(label, 0, sizeof(label));
	spin_lock(&mp->m_sb_lock);
	strncpy(label, sbp->sb_fname, XFSLABEL_MAX);
	spin_unlock(&mp->m_sb_lock);

	if (copy_to_user(user_label, label, sizeof(label)))
		return -EFAULT;
	return 0;
}

static int
xfs_ioc_setlabel(
	struct file		*filp,
	struct xfs_mount	*mp,
	char			__user *newlabel)
{
	struct xfs_sb		*sbp = &mp->m_sb;
	char			label[XFSLABEL_MAX + 1];
	size_t			len;
	int			error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	/*
	 * The generic ioctl allows up to FSLABEL_MAX chars, but XFS is much
	 * smaller, at 12 bytes.  We copy one more to be sure we find the
	 * (required) NULL character to test the incoming label length.
	 * NB: The on disk label doesn't need to be null terminated.
	 */
	if (copy_from_user(label, newlabel, XFSLABEL_MAX + 1))
		return -EFAULT;
	len = strnlen(label, XFSLABEL_MAX + 1);
	if (len > sizeof(sbp->sb_fname))
		return -EINVAL;

	error = mnt_want_write_file(filp);
	if (error)
		return error;

	spin_lock(&mp->m_sb_lock);
	memset(sbp->sb_fname, 0, sizeof(sbp->sb_fname));
	memcpy(sbp->sb_fname, label, len);
	spin_unlock(&mp->m_sb_lock);

	/*
	 * Now we do several things to satisfy userspace.
	 * In addition to normal logging of the primary superblock, we also
	 * immediately write these changes to sector zero for the primary, then
	 * update all backup supers (as xfs_db does for a label change), then
	 * invalidate the block device page cache.  This is so that any prior
	 * buffered reads from userspace (i.e. from blkid) are invalidated,
	 * and userspace will see the newly-written label.
	 */
	error = xfs_sync_sb_buf(mp);
	if (error)
		goto out;
	/*
	 * growfs also updates backup supers so lock against that.
	 */
	mutex_lock(&mp->m_growlock);
	error = xfs_update_secondary_sbs(mp);
	mutex_unlock(&mp->m_growlock);

	invalidate_bdev(mp->m_ddev_targp->bt_bdev);

out:
	mnt_drop_write_file(filp);
	return error;
}

static inline int
xfs_fs_eofblocks_from_user(
	struct xfs_fs_eofblocks		*src,
	struct xfs_icwalk		*dst)
{
	if (src->eof_version != XFS_EOFBLOCKS_VERSION)
		return -EINVAL;

	if (src->eof_flags & ~XFS_EOF_FLAGS_VALID)
		return -EINVAL;

	if (memchr_inv(&src->pad32, 0, sizeof(src->pad32)) ||
	    memchr_inv(src->pad64, 0, sizeof(src->pad64)))
		return -EINVAL;

	dst->icw_flags = 0;
	if (src->eof_flags & XFS_EOF_FLAGS_SYNC)
		dst->icw_flags |= XFS_ICWALK_FLAG_SYNC;
	if (src->eof_flags & XFS_EOF_FLAGS_UID)
		dst->icw_flags |= XFS_ICWALK_FLAG_UID;
	if (src->eof_flags & XFS_EOF_FLAGS_GID)
		dst->icw_flags |= XFS_ICWALK_FLAG_GID;
	if (src->eof_flags & XFS_EOF_FLAGS_PRID)
		dst->icw_flags |= XFS_ICWALK_FLAG_PRID;
	if (src->eof_flags & XFS_EOF_FLAGS_MINFILESIZE)
		dst->icw_flags |= XFS_ICWALK_FLAG_MINFILESIZE;

	dst->icw_prid = src->eof_prid;
	dst->icw_min_file_size = src->eof_min_file_size;

	dst->icw_uid = INVALID_UID;
	if (src->eof_flags & XFS_EOF_FLAGS_UID) {
		dst->icw_uid = make_kuid(current_user_ns(), src->eof_uid);
		if (!uid_valid(dst->icw_uid))
			return -EINVAL;
	}

	dst->icw_gid = INVALID_GID;
	if (src->eof_flags & XFS_EOF_FLAGS_GID) {
		dst->icw_gid = make_kgid(current_user_ns(), src->eof_gid);
		if (!gid_valid(dst->icw_gid))
			return -EINVAL;
	}
	return 0;
}

/*
 * These long-unused ioctls were removed from the official ioctl API in 5.17,
 * but retain these definitions so that we can log warnings about them.
 */
#define XFS_IOC_ALLOCSP		_IOW ('X', 10, struct xfs_flock64)
#define XFS_IOC_FREESP		_IOW ('X', 11, struct xfs_flock64)
#define XFS_IOC_ALLOCSP64	_IOW ('X', 36, struct xfs_flock64)
#define XFS_IOC_FREESP64	_IOW ('X', 37, struct xfs_flock64)

/*
 * Note: some of the ioctl's return positive numbers as a
 * byte count indicating success, such as readlink_by_handle.
 * So we don't "sign flip" like most other routines.  This means
 * true errors need to be returned as a negative value.
 */
long
xfs_file_ioctl(
	struct file		*filp,
	unsigned int		cmd,
	unsigned long		p)
{
	struct inode		*inode = file_inode(filp);
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	void			__user *arg = (void __user *)p;
	int			error;

	trace_xfs_file_ioctl(ip);

	switch (cmd) {
	case FITRIM:
		return xfs_ioc_trim(mp, arg);
	case FS_IOC_GETFSLABEL:
		return xfs_ioc_getlabel(mp, arg);
	case FS_IOC_SETFSLABEL:
		return xfs_ioc_setlabel(filp, mp, arg);
	case XFS_IOC_ALLOCSP:
	case XFS_IOC_FREESP:
	case XFS_IOC_ALLOCSP64:
	case XFS_IOC_FREESP64:
		xfs_warn_once(mp,
	"%s should use fallocate; XFS_IOC_{ALLOC,FREE}SP ioctl unsupported",
				current->comm);
		return -ENOTTY;
	case XFS_IOC_DIOINFO: {
		struct xfs_buftarg	*target = xfs_inode_buftarg(ip);
		struct dioattr		da;

		da.d_mem =  da.d_miniosz = target->bt_logical_sectorsize;
		da.d_maxiosz = INT_MAX & ~(da.d_miniosz - 1);

		if (copy_to_user(arg, &da, sizeof(da)))
			return -EFAULT;
		return 0;
	}

	case XFS_IOC_FSBULKSTAT_SINGLE:
	case XFS_IOC_FSBULKSTAT:
	case XFS_IOC_FSINUMBERS:
		return xfs_ioc_fsbulkstat(filp, cmd, arg);

	case XFS_IOC_BULKSTAT:
		return xfs_ioc_bulkstat(filp, cmd, arg);
	case XFS_IOC_INUMBERS:
		return xfs_ioc_inumbers(mp, cmd, arg);

	case XFS_IOC_FSGEOMETRY_V1:
		return xfs_ioc_fsgeometry(mp, arg, 3);
	case XFS_IOC_FSGEOMETRY_V4:
		return xfs_ioc_fsgeometry(mp, arg, 4);
	case XFS_IOC_FSGEOMETRY:
		return xfs_ioc_fsgeometry(mp, arg, 5);

	case XFS_IOC_AG_GEOMETRY:
		return xfs_ioc_ag_geometry(mp, arg);

	case XFS_IOC_GETVERSION:
		return put_user(inode->i_generation, (int __user *)arg);

	case XFS_IOC_FSGETXATTRA:
		return xfs_ioc_fsgetxattra(ip, arg);

	case XFS_IOC_GETBMAP:
	case XFS_IOC_GETBMAPA:
	case XFS_IOC_GETBMAPX:
		return xfs_ioc_getbmap(filp, cmd, arg);

	case FS_IOC_GETFSMAP:
		return xfs_ioc_getfsmap(ip, arg);

	case XFS_IOC_SCRUB_METADATA:
		return xfs_ioc_scrub_metadata(filp, arg);

	case XFS_IOC_FD_TO_HANDLE:
	case XFS_IOC_PATH_TO_HANDLE:
	case XFS_IOC_PATH_TO_FSHANDLE: {
		xfs_fsop_handlereq_t	hreq;

		if (copy_from_user(&hreq, arg, sizeof(hreq)))
			return -EFAULT;
		return xfs_find_handle(cmd, &hreq);
	}
	case XFS_IOC_OPEN_BY_HANDLE: {
		xfs_fsop_handlereq_t	hreq;

		if (copy_from_user(&hreq, arg, sizeof(xfs_fsop_handlereq_t)))
			return -EFAULT;
		return xfs_open_by_handle(filp, &hreq);
	}

	case XFS_IOC_READLINK_BY_HANDLE: {
		xfs_fsop_handlereq_t	hreq;

		if (copy_from_user(&hreq, arg, sizeof(xfs_fsop_handlereq_t)))
			return -EFAULT;
		return xfs_readlink_by_handle(filp, &hreq);
	}
	case XFS_IOC_ATTRLIST_BY_HANDLE:
		return xfs_attrlist_by_handle(filp, arg);

	case XFS_IOC_ATTRMULTI_BY_HANDLE:
		return xfs_attrmulti_by_handle(filp, arg);

	case XFS_IOC_SWAPEXT: {
		struct xfs_swapext	sxp;

		if (copy_from_user(&sxp, arg, sizeof(xfs_swapext_t)))
			return -EFAULT;
		error = mnt_want_write_file(filp);
		if (error)
			return error;
		error = xfs_ioc_swapext(&sxp);
		mnt_drop_write_file(filp);
		return error;
	}

	case XFS_IOC_FSCOUNTS: {
		xfs_fsop_counts_t out;

		xfs_fs_counts(mp, &out);

		if (copy_to_user(arg, &out, sizeof(out)))
			return -EFAULT;
		return 0;
	}

	case XFS_IOC_SET_RESBLKS: {
		xfs_fsop_resblks_t inout;
		uint64_t	   in;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (xfs_is_readonly(mp))
			return -EROFS;

		if (copy_from_user(&inout, arg, sizeof(inout)))
			return -EFAULT;

		error = mnt_want_write_file(filp);
		if (error)
			return error;

		/* input parameter is passed in resblks field of structure */
		in = inout.resblks;
		error = xfs_reserve_blocks(mp, &in, &inout);
		mnt_drop_write_file(filp);
		if (error)
			return error;

		if (copy_to_user(arg, &inout, sizeof(inout)))
			return -EFAULT;
		return 0;
	}

	case XFS_IOC_GET_RESBLKS: {
		xfs_fsop_resblks_t out;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		error = xfs_reserve_blocks(mp, NULL, &out);
		if (error)
			return error;

		if (copy_to_user(arg, &out, sizeof(out)))
			return -EFAULT;

		return 0;
	}

	case XFS_IOC_FSGROWFSDATA: {
		struct xfs_growfs_data in;

		if (copy_from_user(&in, arg, sizeof(in)))
			return -EFAULT;

		error = mnt_want_write_file(filp);
		if (error)
			return error;
		error = xfs_growfs_data(mp, &in);
		mnt_drop_write_file(filp);
		return error;
	}

	case XFS_IOC_FSGROWFSLOG: {
		struct xfs_growfs_log in;

		if (copy_from_user(&in, arg, sizeof(in)))
			return -EFAULT;

		error = mnt_want_write_file(filp);
		if (error)
			return error;
		error = xfs_growfs_log(mp, &in);
		mnt_drop_write_file(filp);
		return error;
	}

	case XFS_IOC_FSGROWFSRT: {
		xfs_growfs_rt_t in;

		if (copy_from_user(&in, arg, sizeof(in)))
			return -EFAULT;

		error = mnt_want_write_file(filp);
		if (error)
			return error;
		error = xfs_growfs_rt(mp, &in);
		mnt_drop_write_file(filp);
		return error;
	}

	case XFS_IOC_GOINGDOWN: {
		uint32_t in;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (get_user(in, (uint32_t __user *)arg))
			return -EFAULT;

		return xfs_fs_goingdown(mp, in);
	}

	case XFS_IOC_ERROR_INJECTION: {
		xfs_error_injection_t in;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (copy_from_user(&in, arg, sizeof(in)))
			return -EFAULT;

		return xfs_errortag_add(mp, in.errtag);
	}

	case XFS_IOC_ERROR_CLEARALL:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		return xfs_errortag_clearall(mp);

	case XFS_IOC_FREE_EOFBLOCKS: {
		struct xfs_fs_eofblocks	eofb;
		struct xfs_icwalk	icw;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (xfs_is_readonly(mp))
			return -EROFS;

		if (copy_from_user(&eofb, arg, sizeof(eofb)))
			return -EFAULT;

		error = xfs_fs_eofblocks_from_user(&eofb, &icw);
		if (error)
			return error;

		trace_xfs_ioc_free_eofblocks(mp, &icw, _RET_IP_);

		sb_start_write(mp->m_super);
		error = xfs_blockgc_free_space(mp, &icw);
		sb_end_write(mp->m_super);
		return error;
	}

	default:
		return -ENOTTY;
	}
}
