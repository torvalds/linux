/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 *  Linux VFS namei operations.
 */

#include "protocol.h"
#include "pvfs2-kernel.h"

/*
 * Get a newly allocated inode to go with a negative dentry.
 */
static int pvfs2_create(struct inode *dir,
			struct dentry *dentry,
			umode_t mode,
			bool exclusive)
{
	struct pvfs2_inode_s *parent = PVFS2_I(dir);
	struct pvfs2_kernel_op_s *new_op;
	struct inode *inode;
	int ret;

	gossip_debug(GOSSIP_NAME_DEBUG, "%s: called\n", __func__);

	new_op = op_alloc(PVFS2_VFS_OP_CREATE);
	if (!new_op)
		return -ENOMEM;

	new_op->upcall.req.create.parent_refn = parent->refn;

	fill_default_sys_attrs(new_op->upcall.req.create.attributes,
			       PVFS_TYPE_METAFILE, mode);

	strncpy(new_op->upcall.req.create.d_name,
		dentry->d_name.name, PVFS2_NAME_LEN);

	ret = service_operation(new_op, __func__, get_interruptible_flag(dir));

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "Create Got PVFS2 handle %pU on fsid %d (ret=%d)\n",
		     &new_op->downcall.resp.create.refn.khandle,
		     new_op->downcall.resp.create.refn.fs_id, ret);

	if (ret < 0) {
		gossip_debug(GOSSIP_NAME_DEBUG,
			     "%s: failed with error code %d\n",
			     __func__, ret);
		goto out;
	}

	inode = pvfs2_new_inode(dir->i_sb, dir, S_IFREG | mode, 0,
				&new_op->downcall.resp.create.refn);
	if (IS_ERR(inode)) {
		gossip_err("*** Failed to allocate pvfs2 file inode\n");
		ret = PTR_ERR(inode);
		goto out;
	}

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "Assigned file inode new number of %pU\n",
		     get_khandle_from_ino(inode));

	d_instantiate(dentry, inode);
	unlock_new_inode(inode);

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "Inode (Regular File) %pU -> %s\n",
		     get_khandle_from_ino(inode),
		     dentry->d_name.name);

	SetMtimeFlag(parent);
	dir->i_mtime = dir->i_ctime = current_fs_time(dir->i_sb);
	mark_inode_dirty_sync(dir);
	ret = 0;
out:
	op_release(new_op);
	gossip_debug(GOSSIP_NAME_DEBUG, "%s: returning %d\n", __func__, ret);
	return ret;
}

/*
 * Attempt to resolve an object name (dentry->d_name), parent handle, and
 * fsid into a handle for the object.
 */
static struct dentry *pvfs2_lookup(struct inode *dir, struct dentry *dentry,
				   unsigned int flags)
{
	struct pvfs2_inode_s *parent = PVFS2_I(dir);
	struct pvfs2_kernel_op_s *new_op;
	struct inode *inode;
	struct dentry *res;
	int ret = -EINVAL;

	/*
	 * in theory we could skip a lookup here (if the intent is to
	 * create) in order to avoid a potentially failed lookup, but
	 * leaving it in can skip a valid lookup and try to create a file
	 * that already exists (e.g. the vfs already handles checking for
	 * -EEXIST on O_EXCL opens, which is broken if we skip this lookup
	 * in the create path)
	 */
	gossip_debug(GOSSIP_NAME_DEBUG, "%s called on %s\n",
		     __func__, dentry->d_name.name);

	if (dentry->d_name.len > (PVFS2_NAME_LEN - 1))
		return ERR_PTR(-ENAMETOOLONG);

	new_op = op_alloc(PVFS2_VFS_OP_LOOKUP);
	if (!new_op)
		return ERR_PTR(-ENOMEM);

	new_op->upcall.req.lookup.sym_follow = flags & LOOKUP_FOLLOW;

	gossip_debug(GOSSIP_NAME_DEBUG, "%s:%s:%d using parent %pU\n",
		     __FILE__,
		     __func__,
		     __LINE__,
		     &parent->refn.khandle);
	new_op->upcall.req.lookup.parent_refn = parent->refn;

	strncpy(new_op->upcall.req.lookup.d_name, dentry->d_name.name,
		PVFS2_NAME_LEN);

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "%s: doing lookup on %s under %pU,%d (follow=%s)\n",
		     __func__,
		     new_op->upcall.req.lookup.d_name,
		     &new_op->upcall.req.lookup.parent_refn.khandle,
		     new_op->upcall.req.lookup.parent_refn.fs_id,
		     ((new_op->upcall.req.lookup.sym_follow ==
		       PVFS2_LOOKUP_LINK_FOLLOW) ? "yes" : "no"));

	ret = service_operation(new_op, __func__, get_interruptible_flag(dir));

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "Lookup Got %pU, fsid %d (ret=%d)\n",
		     &new_op->downcall.resp.lookup.refn.khandle,
		     new_op->downcall.resp.lookup.refn.fs_id,
		     ret);

	if (ret < 0) {
		if (ret == -ENOENT) {
			/*
			 * if no inode was found, add a negative dentry to
			 * dcache anyway; if we don't, we don't hold expected
			 * lookup semantics and we most noticeably break
			 * during directory renames.
			 *
			 * however, if the operation failed or exited, do not
			 * add the dentry (e.g. in the case that a touch is
			 * issued on a file that already exists that was
			 * interrupted during this lookup -- no need to add
			 * another negative dentry for an existing file)
			 */

			gossip_debug(GOSSIP_NAME_DEBUG,
				     "pvfs2_lookup: Adding *negative* dentry "
				     "%p for %s\n",
				     dentry,
				     dentry->d_name.name);

			d_add(dentry, NULL);
			res = NULL;
			goto out;
		}

		/* must be a non-recoverable error */
		res = ERR_PTR(ret);
		goto out;
	}

	inode = pvfs2_iget(dir->i_sb, &new_op->downcall.resp.lookup.refn);
	if (IS_ERR(inode)) {
		gossip_debug(GOSSIP_NAME_DEBUG,
			"error %ld from iget\n", PTR_ERR(inode));
		res = ERR_CAST(inode);
		goto out;
	}

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "%s:%s:%d "
		     "Found good inode [%lu] with count [%d]\n",
		     __FILE__,
		     __func__,
		     __LINE__,
		     inode->i_ino,
		     (int)atomic_read(&inode->i_count));

	/* update dentry/inode pair into dcache */
	res = d_splice_alias(inode, dentry);

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "Lookup success (inode ct = %d)\n",
		     (int)atomic_read(&inode->i_count));
out:
	op_release(new_op);
	return res;
}

/* return 0 on success; non-zero otherwise */
static int pvfs2_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct pvfs2_inode_s *parent = PVFS2_I(dir);
	struct pvfs2_kernel_op_s *new_op;
	int ret;

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "%s: called on %s\n"
		     "  (inode %pU): Parent is %pU | fs_id %d\n",
		     __func__,
		     dentry->d_name.name,
		     get_khandle_from_ino(inode),
		     &parent->refn.khandle,
		     parent->refn.fs_id);

	new_op = op_alloc(PVFS2_VFS_OP_REMOVE);
	if (!new_op)
		return -ENOMEM;

	new_op->upcall.req.remove.parent_refn = parent->refn;
	strncpy(new_op->upcall.req.remove.d_name, dentry->d_name.name,
		PVFS2_NAME_LEN);

	ret = service_operation(new_op, "pvfs2_unlink",
				get_interruptible_flag(inode));

	/* when request is serviced properly, free req op struct */
	op_release(new_op);

	if (!ret) {
		drop_nlink(inode);

		SetMtimeFlag(parent);
		dir->i_mtime = dir->i_ctime = current_fs_time(dir->i_sb);
		mark_inode_dirty_sync(dir);
	}
	return ret;
}

/*
 * pvfs2_link() is only implemented here to make sure that we return a
 * reasonable error code (the kernel will return a misleading EPERM
 * otherwise).  PVFS2 does not support hard links.
 */
static int pvfs2_link(struct dentry *old_dentry,
		      struct inode *dir,
		      struct dentry *dentry)
{
	return -EOPNOTSUPP;
}

/*
 * pvfs2_mknod() is only implemented here to make sure that we return a
 * reasonable error code (the kernel will return a misleading EPERM
 * otherwise).  PVFS2 does not support special files such as fifos or devices.
 */
static int pvfs2_mknod(struct inode *dir,
		       struct dentry *dentry,
		       umode_t mode,
		       dev_t rdev)
{
	return -EOPNOTSUPP;
}

static int pvfs2_symlink(struct inode *dir,
			 struct dentry *dentry,
			 const char *symname)
{
	struct pvfs2_inode_s *parent = PVFS2_I(dir);
	struct pvfs2_kernel_op_s *new_op;
	struct inode *inode;
	int mode = 755;
	int ret;

	gossip_debug(GOSSIP_NAME_DEBUG, "%s: called\n", __func__);

	if (!symname)
		return -EINVAL;

	new_op = op_alloc(PVFS2_VFS_OP_SYMLINK);
	if (!new_op)
		return -ENOMEM;

	new_op->upcall.req.sym.parent_refn = parent->refn;

	fill_default_sys_attrs(new_op->upcall.req.sym.attributes,
			       PVFS_TYPE_SYMLINK,
			       mode);

	strncpy(new_op->upcall.req.sym.entry_name,
		dentry->d_name.name,
		PVFS2_NAME_LEN);
	strncpy(new_op->upcall.req.sym.target, symname, PVFS2_NAME_LEN);

	ret = service_operation(new_op, __func__, get_interruptible_flag(dir));

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "Symlink Got PVFS2 handle %pU on fsid %d (ret=%d)\n",
		     &new_op->downcall.resp.sym.refn.khandle,
		     new_op->downcall.resp.sym.refn.fs_id, ret);

	if (ret < 0) {
		gossip_debug(GOSSIP_NAME_DEBUG,
			    "%s: failed with error code %d\n",
			    __func__, ret);
		goto out;
	}

	inode = pvfs2_new_inode(dir->i_sb, dir, S_IFLNK | mode, 0,
				&new_op->downcall.resp.sym.refn);
	if (IS_ERR(inode)) {
		gossip_err
		    ("*** Failed to allocate pvfs2 symlink inode\n");
		ret = PTR_ERR(inode);
		goto out;
	}

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "Assigned symlink inode new number of %pU\n",
		     get_khandle_from_ino(inode));

	d_instantiate(dentry, inode);
	unlock_new_inode(inode);

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "Inode (Symlink) %pU -> %s\n",
		     get_khandle_from_ino(inode),
		     dentry->d_name.name);

	SetMtimeFlag(parent);
	dir->i_mtime = dir->i_ctime = current_fs_time(dir->i_sb);
	mark_inode_dirty_sync(dir);
	ret = 0;
out:
	op_release(new_op);
	return ret;
}

static int pvfs2_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct pvfs2_inode_s *parent = PVFS2_I(dir);
	struct pvfs2_kernel_op_s *new_op;
	struct inode *inode;
	int ret;

	new_op = op_alloc(PVFS2_VFS_OP_MKDIR);
	if (!new_op)
		return -ENOMEM;

	new_op->upcall.req.mkdir.parent_refn = parent->refn;

	fill_default_sys_attrs(new_op->upcall.req.mkdir.attributes,
			       PVFS_TYPE_DIRECTORY, mode);

	strncpy(new_op->upcall.req.mkdir.d_name,
		dentry->d_name.name, PVFS2_NAME_LEN);

	ret = service_operation(new_op, __func__, get_interruptible_flag(dir));

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "Mkdir Got PVFS2 handle %pU on fsid %d\n",
		     &new_op->downcall.resp.mkdir.refn.khandle,
		     new_op->downcall.resp.mkdir.refn.fs_id);

	if (ret < 0) {
		gossip_debug(GOSSIP_NAME_DEBUG,
			     "%s: failed with error code %d\n",
			     __func__, ret);
		goto out;
	}

	inode = pvfs2_new_inode(dir->i_sb, dir, S_IFDIR | mode, 0,
				&new_op->downcall.resp.mkdir.refn);
	if (IS_ERR(inode)) {
		gossip_err("*** Failed to allocate pvfs2 dir inode\n");
		ret = PTR_ERR(inode);
		goto out;
	}

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "Assigned dir inode new number of %pU\n",
		     get_khandle_from_ino(inode));

	d_instantiate(dentry, inode);
	unlock_new_inode(inode);

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "Inode (Directory) %pU -> %s\n",
		     get_khandle_from_ino(inode),
		     dentry->d_name.name);

	/*
	 * NOTE: we have no good way to keep nlink consistent for directories
	 * across clients; keep constant at 1.
	 */
	SetMtimeFlag(parent);
	dir->i_mtime = dir->i_ctime = current_fs_time(dir->i_sb);
	mark_inode_dirty_sync(dir);
out:
	op_release(new_op);
	return ret;
}

static int pvfs2_rename(struct inode *old_dir,
			struct dentry *old_dentry,
			struct inode *new_dir,
			struct dentry *new_dentry)
{
	struct pvfs2_kernel_op_s *new_op;
	int ret;

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "pvfs2_rename: called (%s/%s => %s/%s) ct=%d\n",
		     old_dentry->d_parent->d_name.name,
		     old_dentry->d_name.name,
		     new_dentry->d_parent->d_name.name,
		     new_dentry->d_name.name,
		     d_count(new_dentry));

	new_op = op_alloc(PVFS2_VFS_OP_RENAME);
	if (!new_op)
		return -EINVAL;

	new_op->upcall.req.rename.old_parent_refn = PVFS2_I(old_dir)->refn;
	new_op->upcall.req.rename.new_parent_refn = PVFS2_I(new_dir)->refn;

	strncpy(new_op->upcall.req.rename.d_old_name,
		old_dentry->d_name.name,
		PVFS2_NAME_LEN);
	strncpy(new_op->upcall.req.rename.d_new_name,
		new_dentry->d_name.name,
		PVFS2_NAME_LEN);

	ret = service_operation(new_op,
				"pvfs2_rename",
				get_interruptible_flag(old_dentry->d_inode));

	gossip_debug(GOSSIP_NAME_DEBUG,
		     "pvfs2_rename: got downcall status %d\n",
		     ret);

	if (new_dentry->d_inode)
		new_dentry->d_inode->i_ctime = CURRENT_TIME;

	op_release(new_op);
	return ret;
}

/* PVFS2 implementation of VFS inode operations for directories */
struct inode_operations pvfs2_dir_inode_operations = {
	.lookup = pvfs2_lookup,
	.get_acl = pvfs2_get_acl,
	.set_acl = pvfs2_set_acl,
	.create = pvfs2_create,
	.link = pvfs2_link,
	.unlink = pvfs2_unlink,
	.symlink = pvfs2_symlink,
	.mkdir = pvfs2_mkdir,
	.rmdir = pvfs2_unlink,
	.mknod = pvfs2_mknod,
	.rename = pvfs2_rename,
	.setattr = pvfs2_setattr,
	.getattr = pvfs2_getattr,
	.setxattr = generic_setxattr,
	.getxattr = generic_getxattr,
	.removexattr = generic_removexattr,
	.listxattr = pvfs2_listxattr,
};
