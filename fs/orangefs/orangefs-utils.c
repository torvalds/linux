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
		case ORANGEFS_VFS_OP_MMAP_RA_FLUSH:
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

static void orangefs_set_inode_flags(struct inode *inode,
				     struct ORANGEFS_sys_attr_s *attrs)
{
	if (attrs->flags & ORANGEFS_IMMUTABLE_FL)
		inode->i_flags |= S_IMMUTABLE;
	else
		inode->i_flags &= ~S_IMMUTABLE;

	if (attrs->flags & ORANGEFS_APPEND_FL)
		inode->i_flags |= S_APPEND;
	else
		inode->i_flags &= ~S_APPEND;

	if (attrs->flags & ORANGEFS_NOATIME_FL)
		inode->i_flags |= S_NOATIME;
	else
		inode->i_flags &= ~S_NOATIME;

}

/* NOTE: symname is ignored unless the inode is a sym link */
static int copy_attributes_to_inode(struct inode *inode,
				    struct ORANGEFS_sys_attr_s *attrs,
				    char *symname)
{
	int ret = -1;
	int perm_mode = 0;
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);
	loff_t inode_size = 0;
	loff_t rounded_up_size = 0;


	/*
	 * arbitrarily set the inode block size; FIXME: we need to
	 * resolve the difference between the reported inode blocksize
	 * and the PAGE_CACHE_SIZE, since our block count will always
	 * be wrong.
	 *
	 * For now, we're setting the block count to be the proper
	 * number assuming the block size is 512 bytes, and the size is
	 * rounded up to the nearest 4K.  This is apparently required
	 * to get proper size reports from the 'du' shell utility.
	 *
	 * changing the inode->i_blkbits to something other than
	 * PAGE_CACHE_SHIFT breaks mmap/execution as we depend on that.
	 */
	gossip_debug(GOSSIP_UTILS_DEBUG,
		     "attrs->mask = %x (objtype = %s)\n",
		     attrs->mask,
		     attrs->objtype == ORANGEFS_TYPE_METAFILE ? "file" :
		     attrs->objtype == ORANGEFS_TYPE_DIRECTORY ? "directory" :
		     attrs->objtype == ORANGEFS_TYPE_SYMLINK ? "symlink" :
			"invalid/unknown");

	switch (attrs->objtype) {
	case ORANGEFS_TYPE_METAFILE:
		orangefs_set_inode_flags(inode, attrs);
		if (attrs->mask & ORANGEFS_ATTR_SYS_SIZE) {
			inode_size = (loff_t) attrs->size;
			rounded_up_size =
			    (inode_size + (4096 - (inode_size % 4096)));

			orangefs_lock_inode(inode);
			inode->i_bytes = inode_size;
			inode->i_blocks =
			    (unsigned long)(rounded_up_size / 512);
			orangefs_unlock_inode(inode);

			/*
			 * NOTE: make sure all the places we're called
			 * from have the inode->i_sem lock. We're fine
			 * in 99% of the cases since we're mostly
			 * called from a lookup.
			 */
			inode->i_size = inode_size;
		}
		break;
	case ORANGEFS_TYPE_SYMLINK:
		if (symname != NULL) {
			inode->i_size = (loff_t) strlen(symname);
			break;
		}
		/*FALLTHRU*/
	default:
		inode->i_size = PAGE_CACHE_SIZE;

		orangefs_lock_inode(inode);
		inode_set_bytes(inode, inode->i_size);
		orangefs_unlock_inode(inode);
		break;
	}

	inode->i_uid = make_kuid(&init_user_ns, attrs->owner);
	inode->i_gid = make_kgid(&init_user_ns, attrs->group);
	inode->i_atime.tv_sec = (time_t) attrs->atime;
	inode->i_mtime.tv_sec = (time_t) attrs->mtime;
	inode->i_ctime.tv_sec = (time_t) attrs->ctime;
	inode->i_atime.tv_nsec = 0;
	inode->i_mtime.tv_nsec = 0;
	inode->i_ctime.tv_nsec = 0;

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

	inode->i_mode = perm_mode;

	if (is_root_handle(inode)) {
		/* special case: mark the root inode as sticky */
		inode->i_mode |= S_ISVTX;
		gossip_debug(GOSSIP_UTILS_DEBUG,
			     "Marking inode %pU as sticky\n",
			     get_khandle_from_ino(inode));
	}

	switch (attrs->objtype) {
	case ORANGEFS_TYPE_METAFILE:
		inode->i_mode |= S_IFREG;
		ret = 0;
		break;
	case ORANGEFS_TYPE_DIRECTORY:
		inode->i_mode |= S_IFDIR;
		/* NOTE: we have no good way to keep nlink consistent
		 * for directories across clients; keep constant at 1.
		 * Why 1?  If we go with 2, then find(1) gets confused
		 * and won't work properly withouth the -noleaf option
		 */
		set_nlink(inode, 1);
		ret = 0;
		break;
	case ORANGEFS_TYPE_SYMLINK:
		inode->i_mode |= S_IFLNK;

		/* copy link target to inode private data */
		if (orangefs_inode && symname) {
			strncpy(orangefs_inode->link_target,
				symname,
				ORANGEFS_NAME_MAX);
			gossip_debug(GOSSIP_UTILS_DEBUG,
				     "Copied attr link target %s\n",
				     orangefs_inode->link_target);
		}
		gossip_debug(GOSSIP_UTILS_DEBUG,
			     "symlink mode %o\n",
			     inode->i_mode);
		ret = 0;
		break;
	default:
		gossip_err("orangefs: copy_attributes_to_inode: got invalid attribute type %x\n",
			attrs->objtype);
	}

	gossip_debug(GOSSIP_UTILS_DEBUG,
		     "orangefs: copy_attributes_to_inode: setting i_mode to %o, i_size to %lu\n",
		     inode->i_mode,
		     (unsigned long)i_size_read(inode));

	return ret;
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
		attrs->owner = from_kuid(current_user_ns(), iattr->ia_uid);
		attrs->mask |= ORANGEFS_ATTR_SYS_UID;
		gossip_debug(GOSSIP_UTILS_DEBUG, "(UID) %d\n", attrs->owner);
	}
	if (iattr->ia_valid & ATTR_GID) {
		attrs->group = from_kgid(current_user_ns(), iattr->ia_gid);
		attrs->mask |= ORANGEFS_ATTR_SYS_GID;
		gossip_debug(GOSSIP_UTILS_DEBUG, "(GID) %d\n", attrs->group);
	}

	if (iattr->ia_valid & ATTR_ATIME) {
		attrs->mask |= ORANGEFS_ATTR_SYS_ATIME;
		if (iattr->ia_valid & ATTR_ATIME_SET) {
			attrs->atime =
			    orangefs_convert_time_field(&iattr->ia_atime);
			attrs->mask |= ORANGEFS_ATTR_SYS_ATIME_SET;
		}
	}
	if (iattr->ia_valid & ATTR_MTIME) {
		attrs->mask |= ORANGEFS_ATTR_SYS_MTIME;
		if (iattr->ia_valid & ATTR_MTIME_SET) {
			attrs->mtime =
			    orangefs_convert_time_field(&iattr->ia_mtime);
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

/*
 * issues a orangefs getattr request and fills in the appropriate inode
 * attributes if successful.  returns 0 on success; -errno otherwise
 */
int orangefs_inode_getattr(struct inode *inode, __u32 getattr_mask)
{
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);
	struct orangefs_kernel_op_s *new_op;
	int ret = -EINVAL;

	gossip_debug(GOSSIP_UTILS_DEBUG,
		     "%s: called on inode %pU\n",
		     __func__,
		     get_khandle_from_ino(inode));

	new_op = op_alloc(ORANGEFS_VFS_OP_GETATTR);
	if (!new_op)
		return -ENOMEM;
	new_op->upcall.req.getattr.refn = orangefs_inode->refn;
	new_op->upcall.req.getattr.mask = getattr_mask;

	ret = service_operation(new_op, __func__,
				get_interruptible_flag(inode));
	if (ret != 0)
		goto out;

	if (copy_attributes_to_inode(inode,
			&new_op->downcall.resp.getattr.attributes,
			new_op->downcall.resp.getattr.link_target)) {
		gossip_err("%s: failed to copy attributes\n", __func__);
		ret = -ENOENT;
		goto out;
	}

	/*
	 * Store blksize in orangefs specific part of inode structure; we are
	 * only going to use this to report to stat to make sure it doesn't
	 * perturb any inode related code paths.
	 */
	if (new_op->downcall.resp.getattr.attributes.objtype ==
			ORANGEFS_TYPE_METAFILE) {
		orangefs_inode->blksize =
			new_op->downcall.resp.getattr.attributes.blksize;
	} else {
		/* mimic behavior of generic_fillattr() for other types. */
		orangefs_inode->blksize = (1 << inode->i_blkbits);

	}

out:
	gossip_debug(GOSSIP_UTILS_DEBUG,
		     "Getattr on handle %pU, "
		     "fsid %d\n  (inode ct = %d) returned %d\n",
		     &orangefs_inode->refn.khandle,
		     orangefs_inode->refn.fs_id,
		     (int)atomic_read(&inode->i_count),
		     ret);

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
	if (ret < 0) {
		op_release(new_op);
		return ret;
	}

	ret = service_operation(new_op, __func__,
				get_interruptible_flag(inode));

	gossip_debug(GOSSIP_UTILS_DEBUG,
		     "orangefs_inode_setattr: returning %d\n",
		     ret);

	/* when request is serviced properly, free req op struct */
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

int orangefs_unmount_sb(struct super_block *sb)
{
	int ret = -EINVAL;
	struct orangefs_kernel_op_s *new_op = NULL;

	gossip_debug(GOSSIP_UTILS_DEBUG,
		     "orangefs_unmount_sb called on sb %p\n",
		     sb);

	new_op = op_alloc(ORANGEFS_VFS_OP_FS_UMOUNT);
	if (!new_op)
		return -ENOMEM;
	new_op->upcall.req.fs_umount.id = ORANGEFS_SB(sb)->id;
	new_op->upcall.req.fs_umount.fs_id = ORANGEFS_SB(sb)->fs_id;
	strncpy(new_op->upcall.req.fs_umount.orangefs_config_server,
		ORANGEFS_SB(sb)->devname,
		ORANGEFS_MAX_SERVER_ADDR_LEN);

	gossip_debug(GOSSIP_UTILS_DEBUG,
		     "Attempting ORANGEFS Unmount via host %s\n",
		     new_op->upcall.req.fs_umount.orangefs_config_server);

	ret = service_operation(new_op, "orangefs_fs_umount", 0);

	gossip_debug(GOSSIP_UTILS_DEBUG,
		     "orangefs_unmount: got return value of %d\n", ret);
	if (ret)
		sb = ERR_PTR(ret);
	else
		ORANGEFS_SB(sb)->mount_pending = 1;

	op_release(new_op);
	return ret;
}

/*
 * NOTE: on successful cancellation, be sure to return -EINTR, as
 * that's the return value the caller expects
 */
int orangefs_cancel_op_in_progress(__u64 tag)
{
	int ret = -EINVAL;
	struct orangefs_kernel_op_s *new_op = NULL;

	gossip_debug(GOSSIP_UTILS_DEBUG,
		     "orangefs_cancel_op_in_progress called on tag %llu\n",
		     llu(tag));

	new_op = op_alloc(ORANGEFS_VFS_OP_CANCEL);
	if (!new_op)
		return -ENOMEM;
	new_op->upcall.req.cancel.op_tag = tag;

	gossip_debug(GOSSIP_UTILS_DEBUG,
		     "Attempting ORANGEFS operation cancellation of tag %llu\n",
		     llu(new_op->upcall.req.cancel.op_tag));

	ret = service_operation(new_op, "orangefs_cancel", ORANGEFS_OP_CANCELLATION);

	gossip_debug(GOSSIP_UTILS_DEBUG,
		     "orangefs_cancel_op_in_progress: got return value of %d\n",
		     ret);

	op_release(new_op);
	return ret;
}

void orangefs_op_initialize(struct orangefs_kernel_op_s *op)
{
	if (op) {
		spin_lock(&op->lock);
		op->io_completed = 0;

		op->upcall.type = ORANGEFS_VFS_OP_INVALID;
		op->downcall.type = ORANGEFS_VFS_OP_INVALID;
		op->downcall.status = -1;

		op->op_state = OP_VFS_STATE_UNKNOWN;
		op->tag = 0;
		spin_unlock(&op->lock);
	}
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

/* Block all blockable signals... */
void block_signals(sigset_t *orig_sigset)
{
	sigset_t mask;

	/*
	 * Initialize all entries in the signal set to the
	 * inverse of the given mask.
	 */
	siginitsetinv(&mask, sigmask(SIGKILL));

	/* Block 'em Danno... */
	sigprocmask(SIG_BLOCK, &mask, orig_sigset);
}

/* set the signal mask to the given template... */
void set_signals(sigset_t *sigset)
{
	sigprocmask(SIG_SETMASK, sigset, NULL);
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

/*
 * After obtaining a string representation of the client's debug
 * keywords and their associated masks, this function is called to build an
 * array of these values.
 */
int orangefs_prepare_cdm_array(char *debug_array_string)
{
	int i;
	int rc = -EINVAL;
	char *cds_head = NULL;
	char *cds_delimiter = NULL;
	int keyword_len = 0;

	gossip_debug(GOSSIP_UTILS_DEBUG, "%s: start\n", __func__);

	/*
	 * figure out how many elements the cdm_array needs.
	 */
	for (i = 0; i < strlen(debug_array_string); i++)
		if (debug_array_string[i] == '\n')
			cdm_element_count++;

	if (!cdm_element_count) {
		pr_info("No elements in client debug array string!\n");
		goto out;
	}

	cdm_array =
		kzalloc(cdm_element_count * sizeof(struct client_debug_mask),
			GFP_KERNEL);
	if (!cdm_array) {
		pr_info("malloc failed for cdm_array!\n");
		rc = -ENOMEM;
		goto out;
	}

	cds_head = debug_array_string;

	for (i = 0; i < cdm_element_count; i++) {
		cds_delimiter = strchr(cds_head, '\n');
		*cds_delimiter = '\0';

		keyword_len = strcspn(cds_head, " ");

		cdm_array[i].keyword = kzalloc(keyword_len + 1, GFP_KERNEL);
		if (!cdm_array[i].keyword) {
			rc = -ENOMEM;
			goto out;
		}

		sscanf(cds_head,
		       "%s %llx %llx",
		       cdm_array[i].keyword,
		       (unsigned long long *)&(cdm_array[i].mask1),
		       (unsigned long long *)&(cdm_array[i].mask2));

		if (!strcmp(cdm_array[i].keyword, ORANGEFS_VERBOSE))
			client_verbose_index = i;

		if (!strcmp(cdm_array[i].keyword, ORANGEFS_ALL))
			client_all_index = i;

		cds_head = cds_delimiter + 1;
	}

	rc = cdm_element_count;

	gossip_debug(GOSSIP_UTILS_DEBUG, "%s: rc:%d:\n", __func__, rc);

out:

	return rc;

}

/*
 * /sys/kernel/debug/orangefs/debug-help can be catted to
 * see all the available kernel and client debug keywords.
 *
 * When the kernel boots, we have no idea what keywords the
 * client supports, nor their associated masks.
 *
 * We pass through this function once at boot and stamp a
 * boilerplate "we don't know" message for the client in the
 * debug-help file. We pass through here again when the client
 * starts and then we can fill out the debug-help file fully.
 *
 * The client might be restarted any number of times between
 * reboots, we only build the debug-help file the first time.
 */
int orangefs_prepare_debugfs_help_string(int at_boot)
{
	int rc = -EINVAL;
	int i;
	int byte_count = 0;
	char *client_title = "Client Debug Keywords:\n";
	char *kernel_title = "Kernel Debug Keywords:\n";

	gossip_debug(GOSSIP_UTILS_DEBUG, "%s: start\n", __func__);

	if (at_boot) {
		byte_count += strlen(HELP_STRING_UNINITIALIZED);
		client_title = HELP_STRING_UNINITIALIZED;
	} else {
		/*
		 * fill the client keyword/mask array and remember
		 * how many elements there were.
		 */
		cdm_element_count =
			orangefs_prepare_cdm_array(client_debug_array_string);
		if (cdm_element_count <= 0)
			goto out;

		/* Count the bytes destined for debug_help_string. */
		byte_count += strlen(client_title);

		for (i = 0; i < cdm_element_count; i++) {
			byte_count += strlen(cdm_array[i].keyword + 2);
			if (byte_count >= DEBUG_HELP_STRING_SIZE) {
				pr_info("%s: overflow 1!\n", __func__);
				goto out;
			}
		}

		gossip_debug(GOSSIP_UTILS_DEBUG,
			     "%s: cdm_element_count:%d:\n",
			     __func__,
			     cdm_element_count);
	}

	byte_count += strlen(kernel_title);
	for (i = 0; i < num_kmod_keyword_mask_map; i++) {
		byte_count +=
			strlen(s_kmod_keyword_mask_map[i].keyword + 2);
		if (byte_count >= DEBUG_HELP_STRING_SIZE) {
			pr_info("%s: overflow 2!\n", __func__);
			goto out;
		}
	}

	/* build debug_help_string. */
	debug_help_string = kzalloc(DEBUG_HELP_STRING_SIZE, GFP_KERNEL);
	if (!debug_help_string) {
		rc = -ENOMEM;
		goto out;
	}

	strcat(debug_help_string, client_title);

	if (!at_boot) {
		for (i = 0; i < cdm_element_count; i++) {
			strcat(debug_help_string, "\t");
			strcat(debug_help_string, cdm_array[i].keyword);
			strcat(debug_help_string, "\n");
		}
	}

	strcat(debug_help_string, "\n");
	strcat(debug_help_string, kernel_title);

	for (i = 0; i < num_kmod_keyword_mask_map; i++) {
		strcat(debug_help_string, "\t");
		strcat(debug_help_string, s_kmod_keyword_mask_map[i].keyword);
		strcat(debug_help_string, "\n");
	}

	rc = 0;

out:

	return rc;

}

/*
 * kernel = type 0
 * client = type 1
 */
void debug_mask_to_string(void *mask, int type)
{
	int i;
	int len = 0;
	char *debug_string;
	int element_count = 0;

	gossip_debug(GOSSIP_UTILS_DEBUG, "%s: start\n", __func__);

	if (type) {
		debug_string = client_debug_string;
		element_count = cdm_element_count;
	} else {
		debug_string = kernel_debug_string;
		element_count = num_kmod_keyword_mask_map;
	}

	memset(debug_string, 0, ORANGEFS_MAX_DEBUG_STRING_LEN);

	/*
	 * Some keywords, like "all" or "verbose", are amalgams of
	 * numerous other keywords. Make a special check for those
	 * before grinding through the whole mask only to find out
	 * later...
	 */
	if (check_amalgam_keyword(mask, type))
		goto out;

	/* Build the debug string. */
	for (i = 0; i < element_count; i++)
		if (type)
			do_c_string(mask, i);
		else
			do_k_string(mask, i);

	len = strlen(debug_string);

	if ((len) && (type))
		client_debug_string[len - 1] = '\0';
	else if (len)
		kernel_debug_string[len - 1] = '\0';
	else if (type)
		strcpy(client_debug_string, "none");
	else
		strcpy(kernel_debug_string, "none");

out:
gossip_debug(GOSSIP_UTILS_DEBUG, "%s: string:%s:\n", __func__, debug_string);

	return;

}

void do_k_string(void *k_mask, int index)
{
	__u64 *mask = (__u64 *) k_mask;

	if (keyword_is_amalgam((char *) s_kmod_keyword_mask_map[index].keyword))
		goto out;

	if (*mask & s_kmod_keyword_mask_map[index].mask_val) {
		if ((strlen(kernel_debug_string) +
		     strlen(s_kmod_keyword_mask_map[index].keyword))
			< ORANGEFS_MAX_DEBUG_STRING_LEN - 1) {
				strcat(kernel_debug_string,
				       s_kmod_keyword_mask_map[index].keyword);
				strcat(kernel_debug_string, ",");
			} else {
				gossip_err("%s: overflow!\n", __func__);
				strcpy(kernel_debug_string, ORANGEFS_ALL);
				goto out;
			}
	}

out:

	return;
}

void do_c_string(void *c_mask, int index)
{
	struct client_debug_mask *mask = (struct client_debug_mask *) c_mask;

	if (keyword_is_amalgam(cdm_array[index].keyword))
		goto out;

	if ((mask->mask1 & cdm_array[index].mask1) ||
	    (mask->mask2 & cdm_array[index].mask2)) {
		if ((strlen(client_debug_string) +
		     strlen(cdm_array[index].keyword) + 1)
			< ORANGEFS_MAX_DEBUG_STRING_LEN - 2) {
				strcat(client_debug_string,
				       cdm_array[index].keyword);
				strcat(client_debug_string, ",");
			} else {
				gossip_err("%s: overflow!\n", __func__);
				strcpy(client_debug_string, ORANGEFS_ALL);
				goto out;
			}
	}
out:
	return;
}

int keyword_is_amalgam(char *keyword)
{
	int rc = 0;

	if ((!strcmp(keyword, ORANGEFS_ALL)) || (!strcmp(keyword, ORANGEFS_VERBOSE)))
		rc = 1;

	return rc;
}

/*
 * kernel = type 0
 * client = type 1
 *
 * return 1 if we found an amalgam.
 */
int check_amalgam_keyword(void *mask, int type)
{
	__u64 *k_mask;
	struct client_debug_mask *c_mask;
	int k_all_index = num_kmod_keyword_mask_map - 1;
	int rc = 0;

	if (type) {
		c_mask = (struct client_debug_mask *) mask;

		if ((c_mask->mask1 == cdm_array[client_all_index].mask1) &&
		    (c_mask->mask2 == cdm_array[client_all_index].mask2)) {
			strcpy(client_debug_string, ORANGEFS_ALL);
			rc = 1;
			goto out;
		}

		if ((c_mask->mask1 == cdm_array[client_verbose_index].mask1) &&
		    (c_mask->mask2 == cdm_array[client_verbose_index].mask2)) {
			strcpy(client_debug_string, ORANGEFS_VERBOSE);
			rc = 1;
			goto out;
		}

	} else {
		k_mask = (__u64 *) mask;

		if (*k_mask >= s_kmod_keyword_mask_map[k_all_index].mask_val) {
			strcpy(kernel_debug_string, ORANGEFS_ALL);
			rc = 1;
			goto out;
		}
	}

out:

	return rc;
}

/*
 * kernel = type 0
 * client = type 1
 */
void debug_string_to_mask(char *debug_string, void *mask, int type)
{
	char *unchecked_keyword;
	int i;
	char *strsep_fodder = kstrdup(debug_string, GFP_KERNEL);
	char *original_pointer;
	int element_count = 0;
	struct client_debug_mask *c_mask;
	__u64 *k_mask;

	gossip_debug(GOSSIP_UTILS_DEBUG, "%s: start\n", __func__);

	if (type) {
		c_mask = (struct client_debug_mask *)mask;
		element_count = cdm_element_count;
	} else {
		k_mask = (__u64 *)mask;
		*k_mask = 0;
		element_count = num_kmod_keyword_mask_map;
	}

	original_pointer = strsep_fodder;
	while ((unchecked_keyword = strsep(&strsep_fodder, ",")))
		if (strlen(unchecked_keyword)) {
			for (i = 0; i < element_count; i++)
				if (type)
					do_c_mask(i,
						  unchecked_keyword,
						  &c_mask);
				else
					do_k_mask(i,
						  unchecked_keyword,
						  &k_mask);
		}

	kfree(original_pointer);
}

void do_c_mask(int i,
	       char *unchecked_keyword,
	       struct client_debug_mask **sane_mask)
{

	if (!strcmp(cdm_array[i].keyword, unchecked_keyword)) {
		(**sane_mask).mask1 = (**sane_mask).mask1 | cdm_array[i].mask1;
		(**sane_mask).mask2 = (**sane_mask).mask2 | cdm_array[i].mask2;
	}
}

void do_k_mask(int i, char *unchecked_keyword, __u64 **sane_mask)
{

	if (!strcmp(s_kmod_keyword_mask_map[i].keyword, unchecked_keyword))
		**sane_mask = (**sane_mask) |
				s_kmod_keyword_mask_map[i].mask_val;
}
