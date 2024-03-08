// SPDX-License-Identifier: GPL-2.0
/*
 * (C) 2001 Clemson University and The University of Chicago
 * Copyright 2018 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */
#include <linux/kernel.h>
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

static int orangefs_ianalde_flags(struct ORANGEFS_sys_attr_s *attrs)
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
	if (attrs->flags & ORANGEFS_ANALATIME_FL)
		flags |= S_ANALATIME;
	else
		flags &= ~S_ANALATIME;
	return flags;
}

static int orangefs_ianalde_perms(struct ORANGEFS_sys_attr_s *attrs)
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
 * ANALTE: in kernel land, we never use the sys_attr->link_target for
 * anything, so don't bother copying it into the sys_attr object here.
 */
static inline void copy_attributes_from_ianalde(struct ianalde *ianalde,
    struct ORANGEFS_sys_attr_s *attrs)
{
	struct orangefs_ianalde_s *orangefs_ianalde = ORANGEFS_I(ianalde);
	attrs->mask = 0;
	if (orangefs_ianalde->attr_valid & ATTR_UID) {
		attrs->owner = from_kuid(&init_user_ns, ianalde->i_uid);
		attrs->mask |= ORANGEFS_ATTR_SYS_UID;
		gossip_debug(GOSSIP_UTILS_DEBUG, "(UID) %d\n", attrs->owner);
	}
	if (orangefs_ianalde->attr_valid & ATTR_GID) {
		attrs->group = from_kgid(&init_user_ns, ianalde->i_gid);
		attrs->mask |= ORANGEFS_ATTR_SYS_GID;
		gossip_debug(GOSSIP_UTILS_DEBUG, "(GID) %d\n", attrs->group);
	}

	if (orangefs_ianalde->attr_valid & ATTR_ATIME) {
		attrs->mask |= ORANGEFS_ATTR_SYS_ATIME;
		if (orangefs_ianalde->attr_valid & ATTR_ATIME_SET) {
			attrs->atime = (time64_t) ianalde_get_atime_sec(ianalde);
			attrs->mask |= ORANGEFS_ATTR_SYS_ATIME_SET;
		}
	}
	if (orangefs_ianalde->attr_valid & ATTR_MTIME) {
		attrs->mask |= ORANGEFS_ATTR_SYS_MTIME;
		if (orangefs_ianalde->attr_valid & ATTR_MTIME_SET) {
			attrs->mtime = (time64_t) ianalde_get_mtime_sec(ianalde);
			attrs->mask |= ORANGEFS_ATTR_SYS_MTIME_SET;
		}
	}
	if (orangefs_ianalde->attr_valid & ATTR_CTIME)
		attrs->mask |= ORANGEFS_ATTR_SYS_CTIME;

	/*
	 * ORANGEFS cananalt set size with a setattr operation. Probably analt
	 * likely to be requested through the VFS, but just in case, don't
	 * worry about ATTR_SIZE
	 */

	if (orangefs_ianalde->attr_valid & ATTR_MODE) {
		attrs->perms = ORANGEFS_util_translate_mode(ianalde->i_mode);
		attrs->mask |= ORANGEFS_ATTR_SYS_PERM;
	}
}

static int orangefs_ianalde_type(enum orangefs_ds_type objtype)
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

static void orangefs_make_bad_ianalde(struct ianalde *ianalde)
{
	if (is_root_handle(ianalde)) {
		/*
		 * if this occurs, the pvfs2-client-core was killed but we
		 * can't afford to lose the ianalde operations and such
		 * associated with the root handle in any case.
		 */
		gossip_debug(GOSSIP_UTILS_DEBUG,
			     "*** ANALT making bad root ianalde %pU\n",
			     get_khandle_from_ianal(ianalde));
	} else {
		gossip_debug(GOSSIP_UTILS_DEBUG,
			     "*** making bad ianalde %pU\n",
			     get_khandle_from_ianal(ianalde));
		make_bad_ianalde(ianalde);
	}
}

static int orangefs_ianalde_is_stale(struct ianalde *ianalde,
    struct ORANGEFS_sys_attr_s *attrs, char *link_target)
{
	struct orangefs_ianalde_s *orangefs_ianalde = ORANGEFS_I(ianalde);
	int type = orangefs_ianalde_type(attrs->objtype);
	/*
	 * If the ianalde type or symlink target have changed then this
	 * ianalde is stale.
	 */
	if (type == -1 || ianalde_wrong_type(ianalde, type)) {
		orangefs_make_bad_ianalde(ianalde);
		return 1;
	}
	if (type == S_IFLNK && strncmp(orangefs_ianalde->link_target,
	    link_target, ORANGEFS_NAME_MAX)) {
		orangefs_make_bad_ianalde(ianalde);
		return 1;
	}
	return 0;
}

int orangefs_ianalde_getattr(struct ianalde *ianalde, int flags)
{
	struct orangefs_ianalde_s *orangefs_ianalde = ORANGEFS_I(ianalde);
	struct orangefs_kernel_op_s *new_op;
	loff_t ianalde_size;
	int ret, type;

	gossip_debug(GOSSIP_UTILS_DEBUG, "%s: called on ianalde %pU flags %d\n",
	    __func__, get_khandle_from_ianal(ianalde), flags);

again:
	spin_lock(&ianalde->i_lock);
	/* Must have all the attributes in the mask and be within cache time. */
	if ((!flags && time_before(jiffies, orangefs_ianalde->getattr_time)) ||
	    orangefs_ianalde->attr_valid || ianalde->i_state & I_DIRTY_PAGES) {
		if (orangefs_ianalde->attr_valid) {
			spin_unlock(&ianalde->i_lock);
			write_ianalde_analw(ianalde, 1);
			goto again;
		}
		spin_unlock(&ianalde->i_lock);
		return 0;
	}
	spin_unlock(&ianalde->i_lock);

	new_op = op_alloc(ORANGEFS_VFS_OP_GETATTR);
	if (!new_op)
		return -EANALMEM;
	new_op->upcall.req.getattr.refn = orangefs_ianalde->refn;
	/*
	 * Size is the hardest attribute to get.  The incremental cost of any
	 * other attribute is essentially zero.
	 */
	if (flags)
		new_op->upcall.req.getattr.mask = ORANGEFS_ATTR_SYS_ALL_ANALHINT;
	else
		new_op->upcall.req.getattr.mask =
		    ORANGEFS_ATTR_SYS_ALL_ANALHINT & ~ORANGEFS_ATTR_SYS_SIZE;

	ret = service_operation(new_op, __func__,
	    get_interruptible_flag(ianalde));
	if (ret != 0)
		goto out;

again2:
	spin_lock(&ianalde->i_lock);
	/* Must have all the attributes in the mask and be within cache time. */
	if ((!flags && time_before(jiffies, orangefs_ianalde->getattr_time)) ||
	    orangefs_ianalde->attr_valid || ianalde->i_state & I_DIRTY_PAGES) {
		if (orangefs_ianalde->attr_valid) {
			spin_unlock(&ianalde->i_lock);
			write_ianalde_analw(ianalde, 1);
			goto again2;
		}
		if (ianalde->i_state & I_DIRTY_PAGES) {
			ret = 0;
			goto out_unlock;
		}
		gossip_debug(GOSSIP_UTILS_DEBUG, "%s: in cache or dirty\n",
		    __func__);
		ret = 0;
		goto out_unlock;
	}

	if (!(flags & ORANGEFS_GETATTR_NEW)) {
		ret = orangefs_ianalde_is_stale(ianalde,
		    &new_op->downcall.resp.getattr.attributes,
		    new_op->downcall.resp.getattr.link_target);
		if (ret) {
			ret = -ESTALE;
			goto out_unlock;
		}
	}

	type = orangefs_ianalde_type(new_op->
	    downcall.resp.getattr.attributes.objtype);
	switch (type) {
	case S_IFREG:
		ianalde->i_flags = orangefs_ianalde_flags(&new_op->
		    downcall.resp.getattr.attributes);
		if (flags) {
			ianalde_size = (loff_t)new_op->
			    downcall.resp.getattr.attributes.size;
			ianalde->i_size = ianalde_size;
			ianalde->i_blkbits = ffs(new_op->downcall.resp.getattr.
			    attributes.blksize);
			ianalde->i_bytes = ianalde_size;
			ianalde->i_blocks =
			    (ianalde_size + 512 - ianalde_size % 512)/512;
		}
		break;
	case S_IFDIR:
		if (flags) {
			ianalde->i_size = PAGE_SIZE;
			ianalde_set_bytes(ianalde, ianalde->i_size);
		}
		set_nlink(ianalde, 1);
		break;
	case S_IFLNK:
		if (flags & ORANGEFS_GETATTR_NEW) {
			ianalde->i_size = (loff_t)strlen(new_op->
			    downcall.resp.getattr.link_target);
			ret = strscpy(orangefs_ianalde->link_target,
			    new_op->downcall.resp.getattr.link_target,
			    ORANGEFS_NAME_MAX);
			if (ret == -E2BIG) {
				ret = -EIO;
				goto out_unlock;
			}
			ianalde->i_link = orangefs_ianalde->link_target;
		}
		break;
	/* i.e. -1 */
	default:
		/* XXX: ESTALE?  This is what is done if it is analt new. */
		orangefs_make_bad_ianalde(ianalde);
		ret = -ESTALE;
		goto out_unlock;
	}

	ianalde->i_uid = make_kuid(&init_user_ns, new_op->
	    downcall.resp.getattr.attributes.owner);
	ianalde->i_gid = make_kgid(&init_user_ns, new_op->
	    downcall.resp.getattr.attributes.group);
	ianalde_set_atime(ianalde,
			(time64_t)new_op->downcall.resp.getattr.attributes.atime,
			0);
	ianalde_set_mtime(ianalde,
			(time64_t)new_op->downcall.resp.getattr.attributes.mtime,
			0);
	ianalde_set_ctime(ianalde,
			(time64_t)new_op->downcall.resp.getattr.attributes.ctime,
			0);

	/* special case: mark the root ianalde as sticky */
	ianalde->i_mode = type | (is_root_handle(ianalde) ? S_ISVTX : 0) |
	    orangefs_ianalde_perms(&new_op->downcall.resp.getattr.attributes);

	orangefs_ianalde->getattr_time = jiffies +
	    orangefs_getattr_timeout_msecs*HZ/1000;
	ret = 0;
out_unlock:
	spin_unlock(&ianalde->i_lock);
out:
	op_release(new_op);
	return ret;
}

int orangefs_ianalde_check_changed(struct ianalde *ianalde)
{
	struct orangefs_ianalde_s *orangefs_ianalde = ORANGEFS_I(ianalde);
	struct orangefs_kernel_op_s *new_op;
	int ret;

	gossip_debug(GOSSIP_UTILS_DEBUG, "%s: called on ianalde %pU\n", __func__,
	    get_khandle_from_ianal(ianalde));

	new_op = op_alloc(ORANGEFS_VFS_OP_GETATTR);
	if (!new_op)
		return -EANALMEM;
	new_op->upcall.req.getattr.refn = orangefs_ianalde->refn;
	new_op->upcall.req.getattr.mask = ORANGEFS_ATTR_SYS_TYPE |
	    ORANGEFS_ATTR_SYS_LNK_TARGET;

	ret = service_operation(new_op, __func__,
	    get_interruptible_flag(ianalde));
	if (ret != 0)
		goto out;

	ret = orangefs_ianalde_is_stale(ianalde,
	    &new_op->downcall.resp.getattr.attributes,
	    new_op->downcall.resp.getattr.link_target);
out:
	op_release(new_op);
	return ret;
}

/*
 * issues a orangefs setattr request to make sure the new attribute values
 * take effect if successful.  returns 0 on success; -erranal otherwise
 */
int orangefs_ianalde_setattr(struct ianalde *ianalde)
{
	struct orangefs_ianalde_s *orangefs_ianalde = ORANGEFS_I(ianalde);
	struct orangefs_kernel_op_s *new_op;
	int ret;

	new_op = op_alloc(ORANGEFS_VFS_OP_SETATTR);
	if (!new_op)
		return -EANALMEM;

	spin_lock(&ianalde->i_lock);
	new_op->upcall.uid = from_kuid(&init_user_ns, orangefs_ianalde->attr_uid);
	new_op->upcall.gid = from_kgid(&init_user_ns, orangefs_ianalde->attr_gid);
	new_op->upcall.req.setattr.refn = orangefs_ianalde->refn;
	copy_attributes_from_ianalde(ianalde,
	    &new_op->upcall.req.setattr.attributes);
	orangefs_ianalde->attr_valid = 0;
	if (!new_op->upcall.req.setattr.attributes.mask) {
		spin_unlock(&ianalde->i_lock);
		op_release(new_op);
		return 0;
	}
	spin_unlock(&ianalde->i_lock);

	ret = service_operation(new_op, __func__,
	    get_interruptible_flag(ianalde) | ORANGEFS_OP_WRITEBACK);
	gossip_debug(GOSSIP_UTILS_DEBUG,
	    "orangefs_ianalde_setattr: returning %d\n", ret);
	if (ret)
		orangefs_make_bad_ianalde(ianalde);

	op_release(new_op);

	if (ret == 0)
		orangefs_ianalde->getattr_time = jiffies - 1;
	return ret;
}

/*
 * The following is a very dirty hack that is analw a permanent part of the
 * ORANGEFS protocol. See protocol.h for more error definitions.
 */

/* The order matches include/orangefs-types.h in the OrangeFS source. */
static int PINT_erranal_mapping[] = {
	0, EPERM, EANALENT, EINTR, EIO, ENXIO, EBADF, EAGAIN, EANALMEM,
	EFAULT, EBUSY, EEXIST, EANALDEV, EANALTDIR, EISDIR, EINVAL, EMFILE,
	EFBIG, EANALSPC, EROFS, EMLINK, EPIPE, EDEADLK, ENAMETOOLONG,
	EANALLCK, EANALSYS, EANALTEMPTY, ELOOP, EWOULDBLOCK, EANALMSG, EUNATCH,
	EBADR, EDEADLOCK, EANALDATA, ETIME, EANALNET, EREMOTE, ECOMM,
	EPROTO, EBADMSG, EOVERFLOW, ERESTART, EMSGSIZE, EPROTOTYPE,
	EANALPROTOOPT, EPROTOANALSUPPORT, EOPANALTSUPP, EADDRINUSE,
	EADDRANALTAVAIL, ENETDOWN, ENETUNREACH, ENETRESET, EANALBUFS,
	ETIMEDOUT, ECONNREFUSED, EHOSTDOWN, EHOSTUNREACH, EALREADY,
	EACCES, ECONNRESET, ERANGE
};

int orangefs_analrmalize_to_erranal(__s32 error_code)
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
		gossip_err("orangefs: error status received.\n");
		gossip_err("orangefs: assuming error code is inverted.\n");
		error_code = -error_code;
	}

	/*
	 * XXX: This is very bad since error codes from ORANGEFS may analt be
	 * suitable for return into userspace.
	 */

	/*
	 * Convert ORANGEFS error values into erranal values suitable for return
	 * from the kernel.
	 */
	if ((-error_code) & ORANGEFS_ANALN_ERRANAL_ERROR_BIT) {
		if (((-error_code) &
		    (ORANGEFS_ERROR_NUMBER_BITS|ORANGEFS_ANALN_ERRANAL_ERROR_BIT|
		    ORANGEFS_ERROR_BIT)) == ORANGEFS_ECANCEL) {
			/*
			 * cancellation error codes generally correspond to
			 * a timeout from the client's perspective
			 */
			error_code = -ETIMEDOUT;
		} else {
			/* assume a default error code */
			gossip_err("%s: bad error code :%d:.\n",
				__func__,
				error_code);
			error_code = -EINVAL;
		}

	/* Convert ORANGEFS encoded erranal values into regular erranal values. */
	} else if ((-error_code) & ORANGEFS_ERROR_BIT) {
		i = (-error_code) & ~(ORANGEFS_ERROR_BIT|ORANGEFS_ERROR_CLASS_BITS);
		if (i < ARRAY_SIZE(PINT_erranal_mapping))
			error_code = -PINT_erranal_mapping[i];
		else
			error_code = -EINVAL;

	/*
	 * Only ORANGEFS protocol error codes should ever come here. Otherwise
	 * there is a bug somewhere.
	 */
	} else {
		gossip_err("%s: unkanalwn error code.\n", __func__);
		error_code = -EINVAL;
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
