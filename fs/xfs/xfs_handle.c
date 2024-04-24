// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * Copyright (c) 2022-2024 Oracle.
 * All rights reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_shared.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_inode.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_trans.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_attr.h"
#include "xfs_ioctl.h"
#include "xfs_parent.h"
#include "xfs_da_btree.h"
#include "xfs_handle.h"
#include "xfs_health.h"
#include "xfs_icache.h"
#include "xfs_export.h"
#include "xfs_xattr.h"
#include "xfs_acl.h"

#include <linux/namei.h>

static inline size_t
xfs_filehandle_fid_len(void)
{
	struct xfs_handle	*handle = NULL;

	return sizeof(struct xfs_fid) - sizeof(handle->ha_fid.fid_len);
}

static inline size_t
xfs_filehandle_init(
	struct xfs_mount	*mp,
	xfs_ino_t		ino,
	uint32_t		gen,
	struct xfs_handle	*handle)
{
	memcpy(&handle->ha_fsid, mp->m_fixedfsid, sizeof(struct xfs_fsid));

	handle->ha_fid.fid_len = xfs_filehandle_fid_len();
	handle->ha_fid.fid_pad = 0;
	handle->ha_fid.fid_gen = gen;
	handle->ha_fid.fid_ino = ino;

	return sizeof(struct xfs_handle);
}

static inline size_t
xfs_fshandle_init(
	struct xfs_mount	*mp,
	struct xfs_handle	*handle)
{
	memcpy(&handle->ha_fsid, mp->m_fixedfsid, sizeof(struct xfs_fsid));
	memset(&handle->ha_fid, 0, sizeof(handle->ha_fid));

	return sizeof(struct xfs_fsid);
}

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

	if (cmd == XFS_IOC_PATH_TO_FSHANDLE)
		hsize = xfs_fshandle_init(ip->i_mount, &handle);
	else
		hsize = xfs_filehandle_init(ip->i_mount, ip->i_ino,
				inode->i_generation, &handle);

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

/* Convert handle already copied to kernel space into a dentry. */
static struct dentry *
xfs_khandle_to_dentry(
	struct file		*file,
	struct xfs_handle	*handle)
{
	struct xfs_fid64        fid = {
		.ino		= handle->ha_fid.fid_ino,
		.gen		= handle->ha_fid.fid_gen,
	};

	/*
	 * Only allow handle opens under a directory.
	 */
	if (!S_ISDIR(file_inode(file)->i_mode))
		return ERR_PTR(-ENOTDIR);

	if (handle->ha_fid.fid_len != xfs_filehandle_fid_len())
		return ERR_PTR(-EINVAL);

	return exportfs_decode_fh(file->f_path.mnt, (struct fid *)&fid, 3,
			FILEID_INO32_GEN | XFS_FILEID_TYPE_64FLAG,
			xfs_handle_acceptable, NULL);
}

/* Convert handle already copied to kernel space into an xfs_inode. */
static struct xfs_inode *
xfs_khandle_to_inode(
	struct file		*file,
	struct xfs_handle	*handle)
{
	struct xfs_inode	*ip = XFS_I(file_inode(file));
	struct xfs_mount	*mp = ip->i_mount;
	struct inode		*inode;

	if (!S_ISDIR(VFS_I(ip)->i_mode))
		return ERR_PTR(-ENOTDIR);

	if (handle->ha_fid.fid_len != xfs_filehandle_fid_len())
		return ERR_PTR(-EINVAL);

	inode = xfs_nfs_get_inode(mp->m_super, handle->ha_fid.fid_ino,
			handle->ha_fid.fid_gen);
	if (IS_ERR(inode))
		return ERR_CAST(inode);

	return XFS_I(inode);
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

	if (hlen != sizeof(xfs_handle_t))
		return ERR_PTR(-EINVAL);
	if (copy_from_user(&handle, uhandle, hlen))
		return ERR_PTR(-EFAULT);

	return xfs_khandle_to_dentry(parfilp, &handle);
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
	void			*value,
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

static inline enum xfs_attr_update
xfs_xattr_flags(
	u32			ioc_flags,
	void			*value)
{
	if (!value)
		return XFS_ATTRUPDATE_REMOVE;
	if (ioc_flags & XFS_IOC_ATTR_CREATE)
		return XFS_ATTRUPDATE_CREATE;
	if (ioc_flags & XFS_IOC_ATTR_REPLACE)
		return XFS_ATTRUPDATE_REPLACE;
	return XFS_ATTRUPDATE_UPSERT;
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
	kvfree(buffer);
	return error;
}

int
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
	kvfree(args.value);
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

	error = xfs_attr_change(&args, xfs_xattr_flags(flags, args.value));
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

int
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

struct xfs_getparents_ctx {
	struct xfs_attr_list_context	context;
	struct xfs_getparents_by_handle	gph;

	/* File to target */
	struct xfs_inode		*ip;

	/* Internal buffer where we format records */
	void				*krecords;

	/* Last record filled out */
	struct xfs_getparents_rec	*lastrec;

	unsigned int			count;
};

static inline unsigned int
xfs_getparents_rec_sizeof(
	unsigned int		namelen)
{
	return round_up(sizeof(struct xfs_getparents_rec) + namelen + 1,
			sizeof(uint64_t));
}

static void
xfs_getparents_put_listent(
	struct xfs_attr_list_context	*context,
	int				flags,
	unsigned char			*name,
	int				namelen,
	void				*value,
	int				valuelen)
{
	struct xfs_getparents_ctx	*gpx =
		container_of(context, struct xfs_getparents_ctx, context);
	struct xfs_inode		*ip = context->dp;
	struct xfs_mount		*mp = ip->i_mount;
	struct xfs_getparents		*gp = &gpx->gph.gph_request;
	struct xfs_getparents_rec	*gpr = gpx->krecords + context->firstu;
	unsigned short			reclen =
		xfs_getparents_rec_sizeof(namelen);
	xfs_ino_t			ino;
	uint32_t			gen;
	int				error;

	if (!(flags & XFS_ATTR_PARENT))
		return;

	error = xfs_parent_from_attr(mp, flags, name, namelen, value, valuelen,
			&ino, &gen);
	if (error) {
		xfs_inode_mark_sick(ip, XFS_SICK_INO_PARENT);
		context->seen_enough = -EFSCORRUPTED;
		return;
	}

	/*
	 * We found a parent pointer, but we've filled up the buffer.  Signal
	 * to the caller that we did /not/ reach the end of the parent pointer
	 * recordset.
	 */
	if (context->firstu > context->bufsize - reclen) {
		context->seen_enough = 1;
		return;
	}

	/* Format the parent pointer directly into the caller buffer. */
	gpr->gpr_reclen = reclen;
	xfs_filehandle_init(mp, ino, gen, &gpr->gpr_parent);
	memcpy(gpr->gpr_name, name, namelen);
	gpr->gpr_name[namelen] = 0;

	trace_xfs_getparents_put_listent(ip, gp, context, gpr);

	context->firstu += reclen;
	gpx->count++;
	gpx->lastrec = gpr;
}

/* Expand the last record to fill the rest of the caller's buffer. */
static inline void
xfs_getparents_expand_lastrec(
	struct xfs_getparents_ctx	*gpx)
{
	struct xfs_getparents		*gp = &gpx->gph.gph_request;
	struct xfs_getparents_rec	*gpr = gpx->lastrec;

	if (!gpx->lastrec)
		gpr = gpx->krecords;

	gpr->gpr_reclen = gp->gp_bufsize - ((void *)gpr - gpx->krecords);

	trace_xfs_getparents_expand_lastrec(gpx->ip, gp, &gpx->context, gpr);
}

static inline void __user *u64_to_uptr(u64 val)
{
	return (void __user *)(uintptr_t)val;
}

/* Retrieve the parent pointers for a given inode. */
STATIC int
xfs_getparents(
	struct xfs_getparents_ctx	*gpx)
{
	struct xfs_getparents		*gp = &gpx->gph.gph_request;
	struct xfs_inode		*ip = gpx->ip;
	struct xfs_mount		*mp = ip->i_mount;
	size_t				bufsize;
	int				error;

	/* Check size of buffer requested by user */
	if (gp->gp_bufsize > XFS_XATTR_LIST_MAX)
		return -ENOMEM;
	if (gp->gp_bufsize < xfs_getparents_rec_sizeof(1))
		return -EINVAL;

	if (gp->gp_iflags & ~XFS_GETPARENTS_IFLAGS_ALL)
		return -EINVAL;
	if (gp->gp_reserved)
		return -EINVAL;

	bufsize = round_down(gp->gp_bufsize, sizeof(uint64_t));
	gpx->krecords = kvzalloc(bufsize, GFP_KERNEL);
	if (!gpx->krecords) {
		bufsize = min(bufsize, PAGE_SIZE);
		gpx->krecords = kvzalloc(bufsize, GFP_KERNEL);
		if (!gpx->krecords)
			return -ENOMEM;
	}

	gpx->context.dp = ip;
	gpx->context.resynch = 1;
	gpx->context.put_listent = xfs_getparents_put_listent;
	gpx->context.bufsize = bufsize;
	/* firstu is used to track the bytes filled in the buffer */
	gpx->context.firstu = 0;

	/* Copy the cursor provided by caller */
	memcpy(&gpx->context.cursor, &gp->gp_cursor,
			sizeof(struct xfs_attrlist_cursor));
	gpx->count = 0;
	gp->gp_oflags = 0;

	trace_xfs_getparents_begin(ip, gp, &gpx->context.cursor);

	error = xfs_attr_list(&gpx->context);
	if (error)
		goto out_free_buf;
	if (gpx->context.seen_enough < 0) {
		error = gpx->context.seen_enough;
		goto out_free_buf;
	}
	xfs_getparents_expand_lastrec(gpx);

	/* Update the caller with the current cursor position */
	memcpy(&gp->gp_cursor, &gpx->context.cursor,
			sizeof(struct xfs_attrlist_cursor));

	/* Is this the root directory? */
	if (ip->i_ino == mp->m_sb.sb_rootino)
		gp->gp_oflags |= XFS_GETPARENTS_OFLAG_ROOT;

	if (gpx->context.seen_enough == 0) {
		/*
		 * If we did not run out of buffer space, then we reached the
		 * end of the pptr recordset, so set the DONE flag.
		 */
		gp->gp_oflags |= XFS_GETPARENTS_OFLAG_DONE;
	} else if (gpx->count == 0) {
		/*
		 * If we ran out of buffer space before copying any parent
		 * pointers at all, the caller's buffer was too short.  Tell
		 * userspace that, erm, the message is too long.
		 */
		error = -EMSGSIZE;
		goto out_free_buf;
	}

	trace_xfs_getparents_end(ip, gp, &gpx->context.cursor);

	ASSERT(gpx->context.firstu <= gpx->gph.gph_request.gp_bufsize);

	/* Copy the records to userspace. */
	if (copy_to_user(u64_to_uptr(gpx->gph.gph_request.gp_buffer),
				gpx->krecords, gpx->context.firstu))
		error = -EFAULT;

out_free_buf:
	kvfree(gpx->krecords);
	gpx->krecords = NULL;
	return error;
}

/* Retrieve the parents of this file and pass them back to userspace. */
int
xfs_ioc_getparents(
	struct file			*file,
	struct xfs_getparents __user	*ureq)
{
	struct xfs_getparents_ctx	gpx = {
		.ip			= XFS_I(file_inode(file)),
	};
	struct xfs_getparents		*kreq = &gpx.gph.gph_request;
	struct xfs_mount		*mp = gpx.ip->i_mount;
	int				error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (!xfs_has_parent(mp))
		return -EOPNOTSUPP;
	if (copy_from_user(kreq, ureq, sizeof(*kreq)))
		return -EFAULT;

	error = xfs_getparents(&gpx);
	if (error)
		return error;

	if (copy_to_user(ureq, kreq, sizeof(*kreq)))
		return -EFAULT;

	return 0;
}

/* Retrieve the parents of this file handle and pass them back to userspace. */
int
xfs_ioc_getparents_by_handle(
	struct file			*file,
	struct xfs_getparents_by_handle __user	*ureq)
{
	struct xfs_getparents_ctx	gpx = { };
	struct xfs_inode		*ip = XFS_I(file_inode(file));
	struct xfs_mount		*mp = ip->i_mount;
	struct xfs_getparents_by_handle	*kreq = &gpx.gph;
	struct xfs_handle		*handle = &kreq->gph_handle;
	int				error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (!xfs_has_parent(mp))
		return -EOPNOTSUPP;
	if (copy_from_user(kreq, ureq, sizeof(*kreq)))
		return -EFAULT;

	/*
	 * We don't use exportfs_decode_fh because it does too much work here.
	 * If the handle refers to a directory, the exportfs code will walk
	 * upwards through the directory tree to connect the dentries to the
	 * root directory dentry.  For GETPARENTS we don't care about that
	 * because we're not actually going to open a file descriptor; we only
	 * want to open an inode and read its parent pointers.
	 *
	 * Note that xfs_scrub uses GETPARENTS to log that it will try to fix a
	 * corrupted file's metadata.  For this usecase we would really rather
	 * userspace single-step the path reconstruction to avoid loops or
	 * other strange things if the directory tree is corrupt.
	 */
	gpx.ip = xfs_khandle_to_inode(file, handle);
	if (IS_ERR(gpx.ip))
		return PTR_ERR(gpx.ip);

	error = xfs_getparents(&gpx);
	if (error)
		goto out_rele;

	if (copy_to_user(ureq, kreq, sizeof(*kreq)))
		error = -EFAULT;

out_rele:
	xfs_irele(gpx.ip);
	return error;
}
