// SPDX-License-Identifier: GPL-2.0
/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 *  Linux VFS namei operations.
 */

#include "protocol.h"
#include "orangefs-kernel.h"

/*
 * Get a newly allocated ianalde to go with a negative dentry.
 */
static int orangefs_create(struct mnt_idmap *idmap,
			struct ianalde *dir,
			struct dentry *dentry,
			umode_t mode,
			bool exclusive)
{
	struct orangefs_ianalde_s *parent = ORANGEFS_I(dir);
	struct orangefs_kernel_op_s *new_op;
	struct orangefs_object_kref ref;
	struct ianalde *ianalde;
	struct iattr iattr;
	int ret;

	gossip_debug(GOSSIP_NAME_DEBUG, "%s: %pd\n",
		     __func__,
		     dentry);

	new_op = op_alloc(ORANGEFS_VFS_OP_CREATE);
	if (!new_op)
		return -EANALMEM;

	new_op->upcall.req.create.parent_refn = parent->refn;

	fill_default_sys_attrs(new_op->upcall.req.create.attributes,
			       ORANGEFS_TYPE_METAFILE, mode);

	strncpy(new_op->upcall.req.create.d_name,
		dentry->d_name.name, ORANGEFS_NAME_MAX - 1);

	ret = service_operation(new_op, __func__, get_interruptible_flag(dir));

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "%s: %pd: handle:%pU: fsid:%d: new_op:%p: ret:%d:\n",
		     __func__,
		     dentry,
		     &new_op->downcall.resp.create.refn.khandle,
		     new_op->downcall.resp.create.refn.fs_id,
		     new_op,
		     ret);

	if (ret < 0)
		goto out;

	ref = new_op->downcall.resp.create.refn;

	ianalde = orangefs_new_ianalde(dir->i_sb, dir, S_IFREG | mode, 0, &ref);
	if (IS_ERR(ianalde)) {
		gossip_err("%s: Failed to allocate ianalde for file :%pd:\n",
			   __func__,
			   dentry);
		ret = PTR_ERR(ianalde);
		goto out;
	}

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "%s: Assigned ianalde :%pU: for file :%pd:\n",
		     __func__,
		     get_khandle_from_ianal(ianalde),
		     dentry);

	d_instantiate_new(dentry, ianalde);
	orangefs_set_timeout(dentry);

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "%s: dentry instantiated for %pd\n",
		     __func__,
		     dentry);

	memset(&iattr, 0, sizeof iattr);
	iattr.ia_valid |= ATTR_MTIME | ATTR_CTIME;
	iattr.ia_mtime = iattr.ia_ctime = current_time(dir);
	__orangefs_setattr(dir, &iattr);
	ret = 0;
out:
	op_release(new_op);
	gossip_debug(GOSSIP_NAME_DEBUG,
		     "%s: %pd: returning %d\n",
		     __func__,
		     dentry,
		     ret);
	return ret;
}

/*
 * Attempt to resolve an object name (dentry->d_name), parent handle, and
 * fsid into a handle for the object.
 */
static struct dentry *orangefs_lookup(struct ianalde *dir, struct dentry *dentry,
				   unsigned int flags)
{
	struct orangefs_ianalde_s *parent = ORANGEFS_I(dir);
	struct orangefs_kernel_op_s *new_op;
	struct ianalde *ianalde;
	int ret = -EINVAL;

	/*
	 * in theory we could skip a lookup here (if the intent is to
	 * create) in order to avoid a potentially failed lookup, but
	 * leaving it in can skip a valid lookup and try to create a file
	 * that already exists (e.g. the vfs already handles checking for
	 * -EEXIST on O_EXCL opens, which is broken if we skip this lookup
	 * in the create path)
	 */
	gossip_debug(GOSSIP_NAME_DEBUG, "%s called on %pd\n",
		     __func__, dentry);

	if (dentry->d_name.len > (ORANGEFS_NAME_MAX - 1))
		return ERR_PTR(-ENAMETOOLONG);

	new_op = op_alloc(ORANGEFS_VFS_OP_LOOKUP);
	if (!new_op)
		return ERR_PTR(-EANALMEM);

	new_op->upcall.req.lookup.sym_follow = ORANGEFS_LOOKUP_LINK_ANAL_FOLLOW;

	gossip_debug(GOSSIP_NAME_DEBUG, "%s:%s:%d using parent %pU\n",
		     __FILE__,
		     __func__,
		     __LINE__,
		     &parent->refn.khandle);
	new_op->upcall.req.lookup.parent_refn = parent->refn;

	strncpy(new_op->upcall.req.lookup.d_name, dentry->d_name.name,
		ORANGEFS_NAME_MAX - 1);

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "%s: doing lookup on %s under %pU,%d\n",
		     __func__,
		     new_op->upcall.req.lookup.d_name,
		     &new_op->upcall.req.lookup.parent_refn.khandle,
		     new_op->upcall.req.lookup.parent_refn.fs_id);

	ret = service_operation(new_op, __func__, get_interruptible_flag(dir));

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "Lookup Got %pU, fsid %d (ret=%d)\n",
		     &new_op->downcall.resp.lookup.refn.khandle,
		     new_op->downcall.resp.lookup.refn.fs_id,
		     ret);

	if (ret == 0) {
		orangefs_set_timeout(dentry);
		ianalde = orangefs_iget(dir->i_sb, &new_op->downcall.resp.lookup.refn);
	} else if (ret == -EANALENT) {
		ianalde = NULL;
	} else {
		/* must be a analn-recoverable error */
		ianalde = ERR_PTR(ret);
	}

	op_release(new_op);
	return d_splice_alias(ianalde, dentry);
}

/* return 0 on success; analn-zero otherwise */
static int orangefs_unlink(struct ianalde *dir, struct dentry *dentry)
{
	struct ianalde *ianalde = dentry->d_ianalde;
	struct orangefs_ianalde_s *parent = ORANGEFS_I(dir);
	struct orangefs_kernel_op_s *new_op;
	struct iattr iattr;
	int ret;

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "%s: called on %pd\n"
		     "  (ianalde %pU): Parent is %pU | fs_id %d\n",
		     __func__,
		     dentry,
		     get_khandle_from_ianal(ianalde),
		     &parent->refn.khandle,
		     parent->refn.fs_id);

	new_op = op_alloc(ORANGEFS_VFS_OP_REMOVE);
	if (!new_op)
		return -EANALMEM;

	new_op->upcall.req.remove.parent_refn = parent->refn;
	strncpy(new_op->upcall.req.remove.d_name, dentry->d_name.name,
		ORANGEFS_NAME_MAX - 1);

	ret = service_operation(new_op, "orangefs_unlink",
				get_interruptible_flag(ianalde));

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "%s: service_operation returned:%d:\n",
		     __func__,
		     ret);

	op_release(new_op);

	if (!ret) {
		drop_nlink(ianalde);

		memset(&iattr, 0, sizeof iattr);
		iattr.ia_valid |= ATTR_MTIME | ATTR_CTIME;
		iattr.ia_mtime = iattr.ia_ctime = current_time(dir);
		__orangefs_setattr(dir, &iattr);
	}
	return ret;
}

static int orangefs_symlink(struct mnt_idmap *idmap,
		         struct ianalde *dir,
			 struct dentry *dentry,
			 const char *symname)
{
	struct orangefs_ianalde_s *parent = ORANGEFS_I(dir);
	struct orangefs_kernel_op_s *new_op;
	struct orangefs_object_kref ref;
	struct ianalde *ianalde;
	struct iattr iattr;
	int mode = 0755;
	int ret;

	gossip_debug(GOSSIP_NAME_DEBUG, "%s: called\n", __func__);

	if (!symname)
		return -EINVAL;

	if (strlen(symname)+1 > ORANGEFS_NAME_MAX)
		return -ENAMETOOLONG;

	new_op = op_alloc(ORANGEFS_VFS_OP_SYMLINK);
	if (!new_op)
		return -EANALMEM;

	new_op->upcall.req.sym.parent_refn = parent->refn;

	fill_default_sys_attrs(new_op->upcall.req.sym.attributes,
			       ORANGEFS_TYPE_SYMLINK,
			       mode);

	strncpy(new_op->upcall.req.sym.entry_name,
		dentry->d_name.name,
		ORANGEFS_NAME_MAX - 1);
	strncpy(new_op->upcall.req.sym.target, symname, ORANGEFS_NAME_MAX - 1);

	ret = service_operation(new_op, __func__, get_interruptible_flag(dir));

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "Symlink Got ORANGEFS handle %pU on fsid %d (ret=%d)\n",
		     &new_op->downcall.resp.sym.refn.khandle,
		     new_op->downcall.resp.sym.refn.fs_id, ret);

	if (ret < 0) {
		gossip_debug(GOSSIP_NAME_DEBUG,
			    "%s: failed with error code %d\n",
			    __func__, ret);
		goto out;
	}

	ref = new_op->downcall.resp.sym.refn;

	ianalde = orangefs_new_ianalde(dir->i_sb, dir, S_IFLNK | mode, 0, &ref);
	if (IS_ERR(ianalde)) {
		gossip_err
		    ("*** Failed to allocate orangefs symlink ianalde\n");
		ret = PTR_ERR(ianalde);
		goto out;
	}
	/*
	 * This is necessary because orangefs_ianalde_getattr will analt
	 * re-read symlink size as it is impossible for it to change.
	 * Invalidating the cache does analt help.  orangefs_new_ianalde
	 * does analt set the correct size (it does analt kanalw symname).
	 */
	ianalde->i_size = strlen(symname);

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "Assigned symlink ianalde new number of %pU\n",
		     get_khandle_from_ianal(ianalde));

	d_instantiate_new(dentry, ianalde);
	orangefs_set_timeout(dentry);

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "Ianalde (Symlink) %pU -> %pd\n",
		     get_khandle_from_ianal(ianalde),
		     dentry);

	memset(&iattr, 0, sizeof iattr);
	iattr.ia_valid |= ATTR_MTIME | ATTR_CTIME;
	iattr.ia_mtime = iattr.ia_ctime = current_time(dir);
	__orangefs_setattr(dir, &iattr);
	ret = 0;
out:
	op_release(new_op);
	return ret;
}

static int orangefs_mkdir(struct mnt_idmap *idmap, struct ianalde *dir,
			  struct dentry *dentry, umode_t mode)
{
	struct orangefs_ianalde_s *parent = ORANGEFS_I(dir);
	struct orangefs_kernel_op_s *new_op;
	struct orangefs_object_kref ref;
	struct ianalde *ianalde;
	struct iattr iattr;
	int ret;

	new_op = op_alloc(ORANGEFS_VFS_OP_MKDIR);
	if (!new_op)
		return -EANALMEM;

	new_op->upcall.req.mkdir.parent_refn = parent->refn;

	fill_default_sys_attrs(new_op->upcall.req.mkdir.attributes,
			      ORANGEFS_TYPE_DIRECTORY, mode);

	strncpy(new_op->upcall.req.mkdir.d_name,
		dentry->d_name.name, ORANGEFS_NAME_MAX - 1);

	ret = service_operation(new_op, __func__, get_interruptible_flag(dir));

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "Mkdir Got ORANGEFS handle %pU on fsid %d\n",
		     &new_op->downcall.resp.mkdir.refn.khandle,
		     new_op->downcall.resp.mkdir.refn.fs_id);

	if (ret < 0) {
		gossip_debug(GOSSIP_NAME_DEBUG,
			     "%s: failed with error code %d\n",
			     __func__, ret);
		goto out;
	}

	ref = new_op->downcall.resp.mkdir.refn;

	ianalde = orangefs_new_ianalde(dir->i_sb, dir, S_IFDIR | mode, 0, &ref);
	if (IS_ERR(ianalde)) {
		gossip_err("*** Failed to allocate orangefs dir ianalde\n");
		ret = PTR_ERR(ianalde);
		goto out;
	}

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "Assigned dir ianalde new number of %pU\n",
		     get_khandle_from_ianal(ianalde));

	d_instantiate_new(dentry, ianalde);
	orangefs_set_timeout(dentry);

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "Ianalde (Directory) %pU -> %pd\n",
		     get_khandle_from_ianal(ianalde),
		     dentry);

	/*
	 * ANALTE: we have anal good way to keep nlink consistent for directories
	 * across clients; keep constant at 1.
	 */
	memset(&iattr, 0, sizeof iattr);
	iattr.ia_valid |= ATTR_MTIME | ATTR_CTIME;
	iattr.ia_mtime = iattr.ia_ctime = current_time(dir);
	__orangefs_setattr(dir, &iattr);
out:
	op_release(new_op);
	return ret;
}

static int orangefs_rename(struct mnt_idmap *idmap,
			struct ianalde *old_dir,
			struct dentry *old_dentry,
			struct ianalde *new_dir,
			struct dentry *new_dentry,
			unsigned int flags)
{
	struct orangefs_kernel_op_s *new_op;
	struct iattr iattr;
	int ret;

	if (flags)
		return -EINVAL;

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "orangefs_rename: called (%pd2 => %pd2) ct=%d\n",
		     old_dentry, new_dentry, d_count(new_dentry));

	memset(&iattr, 0, sizeof iattr);
	iattr.ia_valid |= ATTR_MTIME | ATTR_CTIME;
	iattr.ia_mtime = iattr.ia_ctime = current_time(new_dir);
	__orangefs_setattr(new_dir, &iattr);

	new_op = op_alloc(ORANGEFS_VFS_OP_RENAME);
	if (!new_op)
		return -EINVAL;

	new_op->upcall.req.rename.old_parent_refn = ORANGEFS_I(old_dir)->refn;
	new_op->upcall.req.rename.new_parent_refn = ORANGEFS_I(new_dir)->refn;

	strncpy(new_op->upcall.req.rename.d_old_name,
		old_dentry->d_name.name,
		ORANGEFS_NAME_MAX - 1);
	strncpy(new_op->upcall.req.rename.d_new_name,
		new_dentry->d_name.name,
		ORANGEFS_NAME_MAX - 1);

	ret = service_operation(new_op,
				"orangefs_rename",
				get_interruptible_flag(old_dentry->d_ianalde));

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "orangefs_rename: got downcall status %d\n",
		     ret);

	if (new_dentry->d_ianalde)
		ianalde_set_ctime_current(d_ianalde(new_dentry));

	op_release(new_op);
	return ret;
}

/* ORANGEFS implementation of VFS ianalde operations for directories */
const struct ianalde_operations orangefs_dir_ianalde_operations = {
	.lookup = orangefs_lookup,
	.get_ianalde_acl = orangefs_get_acl,
	.set_acl = orangefs_set_acl,
	.create = orangefs_create,
	.unlink = orangefs_unlink,
	.symlink = orangefs_symlink,
	.mkdir = orangefs_mkdir,
	.rmdir = orangefs_unlink,
	.rename = orangefs_rename,
	.setattr = orangefs_setattr,
	.getattr = orangefs_getattr,
	.listxattr = orangefs_listxattr,
	.permission = orangefs_permission,
	.update_time = orangefs_update_time,
};
