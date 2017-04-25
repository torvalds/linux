/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include "protocol.h"
#include "orangefs-kernel.h"
#include "orangefs-dev-proto.h"
#include "orangefs-bufmap.h"

__s32 fsid_of_op(struct orangefs_kernel_op_s *op)
{
	__s32 fsid = ORANGEFS_FS_ID_NULL;

	if (op) {
		switch (op->upcall.type) {
		case ORANGEFS_VFS_OP_FILE_IO:
			fsid = op->upcall.req.io.refn.fs_id;
			break;
		case ORANGEFS_VFS_OP_LOOKUP:
			fsid = op->upcall.req.lookup.parent_refn.fs_id;
			break;
		case ORANGEFS_VFS_OP_CREATE:
			fsid = op->upcall.req.create.parent_refn.fs_id;
			break;
		case ORANGEFS_VFS_OP_GETATTR:
			fsid = op->upcall.req.getattr.refn.fs_id;
			break;
		case ORANGEFS_VFS_OP_REMOVE:
			fsid = op->upcall.req.remove.parent_refn.fs_id;
			break;
		case ORANGEFS_VFS_OP_MKDIR:
			fsid = op->upcall.req.mkdir.parent_refn.fs_id;
			break;
		case ORANGEFS_VFS_OP_READDIR:
			fsid = op->upcall.req.readdir.refn.fs_id;
			break;
		case ORANGEFS_VFS_OP_SETATTR:
			fsid = op->upcall.req.setattr.refn.fs_id;
			break;
		case ORANGEFS_VFS_OP_SYMLINK:
			fsid = op->upcall.req.sym.parent_refn.fs_id;
			break;
		case ORANGEFS_VFS_OP_RENAME:
			fsid = op->upcall.req.rename.old_parent_refn.fs_id;
			break;
		case ORANGEFS_VFS_OP_STATFS:
			fsid = op->upcall.req.statfs.fs_id;
			break;
		case ORANGEFS_VFS_OP_TRUNCATE:
			fsid = op->upcall.req.truncate.refn.fs_id;
			break;
		case ORANGEFS_VFS_OP_RA_FLUSH:
			fsid = op->upcall.req.ra_cache_flush.refn.fs_id;
			break;
		case ORANGEFS_VFS_OP_FS_UMOUNT:
			fsid = op->upcall.req.fs_umount.fs_id;
			break;
		case ORANGEFS_VFS_OP_GETXATTR:
			fsid = op->upcall.req.getxattr.refn.fs_id;
			break;
		case ORANGEFS_VFS_OP_SETXATTR:
			fsid = op->upcall.req.setxattr.refn.fs_id;
			break;
		case ORANGEFS_VFS_OP_LISTXATTR:
			fsid = op->upcall.req.listxattr.refn.fs_id;
			break;
		case ORANGEFS_VFS_OP_REMOVEXATTR:
			fsid = op->upcall.req.removexattr.refn.fs_id;
			break;
		case ORANGEFS_VFS_OP_FSYNC:
			fsid = op->upcall.req.fsync.refn.fs_id;
			break;
		default:
			break;
		}
	}
	return fsid;
}

static int orangefs_inode_flags(struct ORANGEFS_sys_attr_s *attrs)
{
	int flags = 0;
	if (attrs->flags & ORANGEFS_IMMUTABLE_FL)
		flags |= S_IMMUTABLE;
	else
		flags &= ~S_IMMUTABLE;
	if (attrs->flags & ORANGEFS_APPEND_FL)
		flags |= S_APPEND;
	else
		flags &= ~S_APPEND;
	if (attrs->flags & ORANGEFS_NOATIME_FL)
		flags |= S_NOATIME;
	else
		flags &= ~S_NOATIME;
	return flags;
}

static int orangefs_inode_perms(struct ORANGEFS_sys_attr_s *attrs)
{
	int perm_mode = 0;

	if (attrs->perms & ORANGEFS_O_EXECUTE)
		perm_mode |= S_IXOTH;
	if (attrs->perms & ORANGEFS_O_WRITE)
		perm_mode |= S_IWOTH;
	if (attrs->perms & ORANGEFS_O_READ)
		perm_mode |= S_IROTH;

	if (attrs->perms & ORANGEFS_G_EXECUTE)
		perm_mode |= S_IXGRP;
	if (attrs->perms & ORANGEFS_G_WRITE)
		perm_mode |= S_IWGRP;
	if (attrs->perms & ORANGEFS_G_READ)
		perm_mode |= S_IRGRP;

	if (attrs->perms & ORANGEFS_U_EXECUTE)
		perm_mode |= S_IXUSR;
	if (attrs->perms & ORANGEFS_U_WRITE)
		perm_mode |= S_IWUSR;
	if (attrs->perms & ORANGEFS_U_READ)
		perm_mode |= S_IRUSR;

	if (attrs->perms & ORANGEFS_G_SGID)
		perm_mode |= S_ISGID;
	if (attrs->perms & ORANGEFS_U_SUID)
		perm_mode |= S_ISUID;

	return perm_mode;
}

/*
 * NOTE: in kernel land, we never use the sys_attr->link_target for
 * anything, so don't bother copying it into the sys_attr object here.
 */
static inline int copy_attributes_from_inode(struct inode *inode,
					     struct ORANGEFS_sys_attr_s *attrs,
					     struct iattr *iattr)
{
	umode_t tmp_mode;

	if (!iattr || !inode || !attrs) {
		gossip_err("NULL iattr (%p), inode (%p), attrs (%p) "
			   "in copy_attributes_from_inode!\n",
			   iattr,
			   inode,
			   attrs);
		return -EINVAL;
	}
	/*
	 * We need to be careful to only copy the attributes out of the
	 * iattr object that we know are valid.
	 */
	attrs->mask = 0;
	if (iattr->ia_valid & ATTR_UID) {
		attrs->owner = from_kuid(&init_user_ns, iattr->ia_uid);
		attrs->mask |= ORANGEFS_ATTR_SYS_UID;
		gossip_debug(GOSSIP_UTILS_DEBUG, "(UID) %d\n", attrs->owner);
	}
	if (iattr->ia_valid & ATTR_GID) {
		attrs->group = from_kgid(&init_user_ns, iattr->ia_gid);
		attrs->mask |= ORANGEFS_ATTR_SYS_GID;
		gossip_debug(GOSSIP_UTILS_DEBUG, "(GID) %d\n", attrs->group);
	}

	if (iattr->ia_valid & ATTR_ATIME) {
		attrs->mask |= ORANGEFS_ATTR_SYS_ATIME;
		if (iattr->ia_valid & ATTR_ATIME_SET) {
			attrs->atime = (time64_t)iattr->ia_atime.tv_sec;
			attrs->mask |= ORANGEFS_ATTR_SYS_ATIME_SET;
		}
	}
	if (iattr->ia_valid & ATTR_MTIME) {
		attrs->mask |= ORANGEFS_ATTR_SYS_MTIME;
		if (iattr->ia_valid & ATTR_MTIME_SET) {
			attrs->mtime = (time64_t)iattr->ia_mtime.tv_sec;
			attrs->mask |= ORANGEFS_ATTR_SYS_MTIME_SET;
		}
	}
	if (iattr->ia_valid & ATTR_CTIME)
		attrs->mask |= ORANGEFS_ATTR_SYS_CTIME;

	/*
	 * ORANGEFS cannot set size with a setattr operation.  Probably not likely
	 * to be requested through the VFS, but just in case, don't worry about
	 * ATTR_SIZE
	 */

	if (iattr->ia_valid & ATTR_MODE) {
		tmp_mode = iattr->ia_mode;
		if (tmp_mode & (S_ISVTX)) {
			if (is_root_handle(inode)) {
				/*
				 * allow sticky bit to be set on root (since
				 * it shows up that way by default anyhow),
				 * but don't show it to the server
				 */
				tmp_mode -= S_ISVTX;
			} else {
				gossip_debug(GOSSIP_UTILS_DEBUG,
					     "User attempted to set sticky bit on non-root directory; returning EINVAL.\n");
				return -EINVAL;
			}
		}

		if (tmp_mode & (S_ISUID)) {
			gossip_debug(GOSSIP_UTILS_DEBUG,
				     "Attempting to set setuid bit (not supported); returning EINVAL.\n");
			return -EINVAL;
		}

		attrs->perms = ORANGEFS_util_translate_mode(tmp_mode);
		attrs->mask |= ORANGEFS_ATTR_SYS_PERM;
	}

	return 0;
}

static int orangefs_inode_type(enum orangefs_ds_type objtype)
{
	if (objtype == ORANGEFS_TYPE_METAFILE)
		return S_IFREG;
	else if (objtype == ORANGEFS_TYPE_DIRECTORY)
		return S_IFDIR;
	else if (objtype == ORANGEFS_TYPE_SYMLINK)
		return S_IFLNK;
	else
		return -1;
}

static int orangefs_inode_is_stale(struct inode *inode, int new,
    struct ORANGEFS_sys_attr_s *attrs, char *link_target)
{
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);
	int type = orangefs_inode_type(attrs->objtype);
	if (!new) {
		/*
		 * If the inode type or symlink target have changed then this
		 * inode is stale.
		 */
		if (type == -1 || !(inode->i_mode & type)) {
			orangefs_make_bad_inode(inode);
			return 1;
		}
		if (type == S_IFLNK && strncmp(orangefs_inode->link_target,
		    link_target, ORANGEFS_NAME_MAX)) {
			orangefs_make_bad_inode(inode);
			return 1;
		}
	}
	return 0;
}

int orangefs_inode_getattr(struct inode *inode, int new, int bypass,
    u32 request_mask)
{
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);
	struct orangefs_kernel_op_s *new_op;
	loff_t inode_size, rounded_up_size;
	int ret, type;

	gossip_debug(GOSSIP_UTILS_DEBUG, "%s: called on inode %pU\n", __func__,
	    get_khandle_from_ino(inode));

	if (!new && !bypass) {
		/*
		 * Must have all the attributes in the mask and be within cache
		 * time.
		 */
		if ((request_mask & orangefs_inode->getattr_mask) ==
		    request_mask &&
		    time_before(jiffies, orangefs_inode->getattr_time))
			return 0;
	}

	new_op = op_alloc(ORANGEFS_VFS_OP_GETATTR);
	if (!new_op)
		return -ENOMEM;
	new_op->upcall.req.getattr.refn = orangefs_inode->refn;
	/*
	 * Size is the hardest attribute to get.  The incremental cost of any
	 * other attribute is essentially zero.
	 */
	if (request_mask & STATX_SIZE || new)
		new_op->upcall.req.getattr.mask = ORANGEFS_ATTR_SYS_ALL_NOHINT;
	else
		new_op->upcall.req.getattr.mask =
		    ORANGEFS_ATTR_SYS_ALL_NOHINT & ~ORANGEFS_ATTR_SYS_SIZE;

	ret = service_operation(new_op, __func__,
	    get_interruptible_flag(inode));
	if (ret != 0)
		goto out;

	type = orangefs_inode_type(new_op->
	    downcall.resp.getattr.attributes.objtype);
	ret = orangefs_inode_is_stale(inode, new,
	    &new_op->downcall.resp.getattr.attributes,
	    new_op->downcall.resp.getattr.link_target);
	if (ret) {
		ret = -ESTALE;
		goto out;
	}

	switch (type) {
	case S_IFREG:
		inode->i_flags = orangefs_inode_flags(&new_op->
		    downcall.resp.getattr.attributes);
		if (request_mask & STATX_SIZE || new) {
			inode_size = (loff_t)new_op->
			    downcall.resp.getattr.attributes.size;
			rounded_up_size =
			    (inode_size + (4096 - (inode_size % 4096)));
			inode->i_size = inode_size;
			orangefs_inode->blksize =
			    new_op->downcall.resp.getattr.attributes.blksize;
			spin_lock(&inode->i_lock);
			inode->i_bytes = inode_size;
			inode->i_blocks =
			    (unsigned long)(rounded_up_size / 512);
			spin_unlock(&inode->i_lock);
		}
		break;
	case S_IFDIR:
		if (request_mask & STATX_SIZE || new) {
			inode->i_size = PAGE_SIZE;
			orangefs_inode->blksize = i_blocksize(inode);
			spin_lock(&inode->i_lock);
			inode_set_bytes(inode, inode->i_size);
			spin_unlock(&inode->i_lock);
		}
		set_nlink(inode, 1);
		break;
	case S_IFLNK:
		if (new) {
			inode->i_size = (loff_t)strlen(new_op->
			    downcall.resp.getattr.link_target);
			orangefs_inode->blksize = i_blocksize(inode);
			ret = strscpy(orangefs_inode->link_target,
			    new_op->downcall.resp.getattr.link_target,
			    ORANGEFS_NAME_MAX);
			if (ret == -E2BIG) {
				ret = -EIO;
				goto out;
			}
			inode->i_link = orangefs_inode->link_target;
		}
		break;
	}

	inode->i_uid = make_kuid(&init_user_ns, new_op->
	    downcall.resp.getattr.attributes.owner);
	inode->i_gid = make_kgid(&init_user_ns, new_op->
	    downcall.resp.getattr.attributes.group);
	inode->i_atime.tv_sec = (time64_t)new_op->
	    downcall.resp.getattr.attributes.atime;
	inode->i_mtime.tv_sec = (time64_t)new_op->
	    downcall.resp.getattr.attributes.mtime;
	inode->i_ctime.tv_sec = (time64_t)new_op->
	    downcall.resp.getattr.attributes.ctime;
	inode->i_atime.tv_nsec = 0;
	inode->i_mtime.tv_nsec = 0;
	inode->i_ctime.tv_nsec = 0;

	/* special case: mark the root inode as sticky */
	inode->i_mode = type | (is_root_handle(inode) ? S_ISVTX : 0) |
	    orangefs_inode_perms(&new_op->downcall.resp.getattr.attributes);

	orangefs_inode->getattr_time = jiffies +
	    orangefs_getattr_timeout_msecs*HZ/1000;
	if (request_mask & STATX_SIZE || new)
		orangefs_inode->getattr_mask = STATX_BASIC_STATS;
	else
		orangefs_inode->getattr_mask = STATX_BASIC_STATS & ~STATX_SIZE;
	ret = 0;
out:
	op_release(new_op);
	return ret;
}

int orangefs_inode_check_changed(struct inode *inode)
{
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);
	struct orangefs_kernel_op_s *new_op;
	int ret;

	gossip_debug(GOSSIP_UTILS_DEBUG, "%s: called on inode %pU\n", __func__,
	    get_khandle_from_ino(inode));

	new_op = op_alloc(ORANGEFS_VFS_OP_GETATTR);
	if (!new_op)
		return -ENOMEM;
	new_op->upcall.req.getattr.refn = orangefs_inode->refn;
	new_op->upcall.req.getattr.mask = ORANGEFS_ATTR_SYS_TYPE |
	    ORANGEFS_ATTR_SYS_LNK_TARGET;

	ret = service_operation(new_op, __func__,
	    get_interruptible_flag(inode));
	if (ret != 0)
		goto out;

	ret = orangefs_inode_is_stale(inode, 0,
	    &new_op->downcall.resp.getattr.attributes,
	    new_op->downcall.resp.getattr.link_target);
out:
	op_release(new_op);
	return ret;
}

/*
 * issues a orangefs setattr request to make sure the new attribute values
 * take effect if successful.  returns 0 on success; -errno otherwise
 */
int orangefs_inode_setattr(struct inode *inode, struct iattr *iattr)
{
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);
	struct orangefs_kernel_op_s *new_op;
	int ret;

	new_op = op_alloc(ORANGEFS_VFS_OP_SETATTR);
	if (!new_op)
		return -ENOMEM;

	new_op->upcall.req.setattr.refn = orangefs_inode->refn;
	ret = copy_attributes_from_inode(inode,
		       &new_op->upcall.req.setattr.attributes,
		       iattr);
	if (ret >= 0) {
		ret = service_operation(new_op, __func__,
				get_interruptible_flag(inode));

		gossip_debug(GOSSIP_UTILS_DEBUG,
			     "orangefs_inode_setattr: returning %d\n",
			     ret);
	}

	op_release(new_op);

	/*
	 * successful setattr should clear the atime, mtime and
	 * ctime flags.
	 */
	if (ret == 0) {
		ClearAtimeFlag(orangefs_inode);
		ClearMtimeFlag(orangefs_inode);
		ClearCtimeFlag(orangefs_inode);
		ClearModeFlag(orangefs_inode);
		orangefs_inode->getattr_time = jiffies - 1;
	}

	return ret;
}

int orangefs_flush_inode(struct inode *inode)
{
	/*
	 * If it is a dirty inode, this function gets called.
	 * Gather all the information that needs to be setattr'ed
	 * Right now, this will only be used for mode, atime, mtime
	 * and/or ctime.
	 */
	struct iattr wbattr;
	int ret;
	int mtime_flag;
	int ctime_flag;
	int atime_flag;
	int mode_flag;
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);

	memset(&wbattr, 0, sizeof(wbattr));

	/*
	 * check inode flags up front, and clear them if they are set.  This
	 * will prevent multiple processes from all trying to flush the same
	 * inode if they call close() simultaneously
	 */
	mtime_flag = MtimeFlag(orangefs_inode);
	ClearMtimeFlag(orangefs_inode);
	ctime_flag = CtimeFlag(orangefs_inode);
	ClearCtimeFlag(orangefs_inode);
	atime_flag = AtimeFlag(orangefs_inode);
	ClearAtimeFlag(orangefs_inode);
	mode_flag = ModeFlag(orangefs_inode);
	ClearModeFlag(orangefs_inode);

	/*  -- Lazy atime,mtime and ctime update --
	 * Note: all times are dictated by server in the new scheme
	 * and not by the clients
	 *
	 * Also mode updates are being handled now..
	 */

	if (mtime_flag)
		wbattr.ia_valid |= ATTR_MTIME;
	if (ctime_flag)
		wbattr.ia_valid |= ATTR_CTIME;
	if (atime_flag)
		wbattr.ia_valid |= ATTR_ATIME;

	if (mode_flag) {
		wbattr.ia_mode = inode->i_mode;
		wbattr.ia_valid |= ATTR_MODE;
	}

	gossip_debug(GOSSIP_UTILS_DEBUG,
		     "*********** orangefs_flush_inode: %pU "
		     "(ia_valid %d)\n",
		     get_khandle_from_ino(inode),
		     wbattr.ia_valid);
	if (wbattr.ia_valid == 0) {
		gossip_debug(GOSSIP_UTILS_DEBUG,
			     "orangefs_flush_inode skipping setattr()\n");
		return 0;
	}

	gossip_debug(GOSSIP_UTILS_DEBUG,
		     "orangefs_flush_inode (%pU) writing mode %o\n",
		     get_khandle_from_ino(inode),
		     inode->i_mode);

	ret = orangefs_inode_setattr(inode, &wbattr);

	return ret;
}

void orangefs_make_bad_inode(struct inode *inode)
{
	if (is_root_handle(inode)) {
		/*
		 * if this occurs, the pvfs2-client-core was killed but we
		 * can't afford to lose the inode operations and such
		 * associated with the root handle in any case.
		 */
		gossip_debug(GOSSIP_UTILS_DEBUG,
			     "*** NOT making bad root inode %pU\n",
			     get_khandle_from_ino(inode));
	} else {
		gossip_debug(GOSSIP_UTILS_DEBUG,
			     "*** making bad inode %pU\n",
			     get_khandle_from_ino(inode));
		make_bad_inode(inode);
	}
}

/*
 * The following is a very dirty hack that is now a permanent part of the
 * ORANGEFS protocol. See protocol.h for more error definitions.
 */

/* The order matches include/orangefs-types.h in the OrangeFS source. */
static int PINT_errno_mapping[] = {
	0, EPERM, ENOENT, EINTR, EIO, ENXIO, EBADF, EAGAIN, ENOMEM,
	EFAULT, EBUSY, EEXIST, ENODEV, ENOTDIR, EISDIR, EINVAL, EMFILE,
	EFBIG, ENOSPC, EROFS, EMLINK, EPIPE, EDEADLK, ENAMETOOLONG,
	ENOLCK, ENOSYS, ENOTEMPTY, ELOOP, EWOULDBLOCK, ENOMSG, EUNATCH,
	EBADR, EDEADLOCK, ENODATA, ETIME, ENONET, EREMOTE, ECOMM,
	EPROTO, EBADMSG, EOVERFLOW, ERESTART, EMSGSIZE, EPROTOTYPE,
	ENOPROTOOPT, EPROTONOSUPPORT, EOPNOTSUPP, EADDRINUSE,
	EADDRNOTAVAIL, ENETDOWN, ENETUNREACH, ENETRESET, ENOBUFS,
	ETIMEDOUT, ECONNREFUSED, EHOSTDOWN, EHOSTUNREACH, EALREADY,
	EACCES, ECONNRESET, ERANGE
};

int orangefs_normalize_to_errno(__s32 error_code)
{
	__u32 i;

	/* Success */
	if (error_code == 0) {
		return 0;
	/*
	 * This shouldn't ever happen. If it does it should be fixed on the
	 * server.
	 */
	} else if (error_code > 0) {
		gossip_err("orangefs: error status receieved.\n");
		gossip_err("orangefs: assuming error code is inverted.\n");
		error_code = -error_code;
	}

	/*
	 * XXX: This is very bad since error codes from ORANGEFS may not be
	 * suitable for return into userspace.
	 */

	/*
	 * Convert ORANGEFS error values into errno values suitable for return
	 * from the kernel.
	 */
	if ((-error_code) & ORANGEFS_NON_ERRNO_ERROR_BIT) {
		if (((-error_code) &
		    (ORANGEFS_ERROR_NUMBER_BITS|ORANGEFS_NON_ERRNO_ERROR_BIT|
		    ORANGEFS_ERROR_BIT)) == ORANGEFS_ECANCEL) {
			/*
			 * cancellation error codes generally correspond to
			 * a timeout from the client's perspective
			 */
			error_code = -ETIMEDOUT;
		} else {
			/* assume a default error code */
			gossip_err("orangefs: warning: got error code without errno equivalent: %d.\n", error_code);
			error_code = -EINVAL;
		}

	/* Convert ORANGEFS encoded errno values into regular errno values. */
	} else if ((-error_code) & ORANGEFS_ERROR_BIT) {
		i = (-error_code) & ~(ORANGEFS_ERROR_BIT|ORANGEFS_ERROR_CLASS_BITS);
		if (i < sizeof(PINT_errno_mapping)/sizeof(*PINT_errno_mapping))
			error_code = -PINT_errno_mapping[i];
		else
			error_code = -EINVAL;

	/*
	 * Only ORANGEFS protocol error codes should ever come here. Otherwise
	 * there is a bug somewhere.
	 */
	} else {
		gossip_err("orangefs: orangefs_normalize_to_errno: got error code which is not from ORANGEFS.\n");
	}
	return error_code;
}

#define NUM_MODES 11
__s32 ORANGEFS_util_translate_mode(int mode)
{
	int ret = 0;
	int i = 0;
	static int modes[NUM_MODES] = {
		S_IXOTH, S_IWOTH, S_IROTH,
		S_IXGRP, S_IWGRP, S_IRGRP,
		S_IXUSR, S_IWUSR, S_IRUSR,
		S_ISGID, S_ISUID
	};
	static int orangefs_modes[NUM_MODES] = {
		ORANGEFS_O_EXECUTE, ORANGEFS_O_WRITE, ORANGEFS_O_READ,
		ORANGEFS_G_EXECUTE, ORANGEFS_G_WRITE, ORANGEFS_G_READ,
		ORANGEFS_U_EXECUTE, ORANGEFS_U_WRITE, ORANGEFS_U_READ,
		ORANGEFS_G_SGID, ORANGEFS_U_SUID
	};

	for (i = 0; i < NUM_MODES; i++)
		if (mode & modes[i])
			ret |= orangefs_modes[i];

	return ret;
}
#undef NUM_MODES
