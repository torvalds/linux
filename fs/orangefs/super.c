// SPDX-License-Identifier: GPL-2.0
/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "protocol.h"
#include "orangefs-kernel.h"
#include "orangefs-bufmap.h"

#include <linux/parser.h>
#include <linux/hashtable.h>
#include <linux/seq_file.h>

/* a cache for orangefs-inode objects (i.e. orangefs inode private data) */
static struct kmem_cache *orangefs_inode_cache;

/* list for storing orangefs specific superblocks in use */
LIST_HEAD(orangefs_superblocks);

DEFINE_SPINLOCK(orangefs_superblocks_lock);

enum {
	Opt_intr,
	Opt_acl,
	Opt_local_lock,

	Opt_err
};

static const match_table_t tokens = {
	{ Opt_acl,		"acl" },
	{ Opt_intr,		"intr" },
	{ Opt_local_lock,	"local_lock" },
	{ Opt_err,	NULL }
};

uint64_t orangefs_features;

static int orangefs_show_options(struct seq_file *m, struct dentry *root)
{
	struct orangefs_sb_info_s *orangefs_sb = ORANGEFS_SB(root->d_sb);

	if (root->d_sb->s_flags & SB_POSIXACL)
		seq_puts(m, ",acl");
	if (orangefs_sb->flags & ORANGEFS_OPT_INTR)
		seq_puts(m, ",intr");
	if (orangefs_sb->flags & ORANGEFS_OPT_LOCAL_LOCK)
		seq_puts(m, ",local_lock");
	return 0;
}

static int parse_mount_options(struct super_block *sb, char *options,
		int silent)
{
	struct orangefs_sb_info_s *orangefs_sb = ORANGEFS_SB(sb);
	substring_t args[MAX_OPT_ARGS];
	char *p;

	/*
	 * Force any potential flags that might be set from the mount
	 * to zero, ie, initialize to unset.
	 */
	sb->s_flags &= ~SB_POSIXACL;
	orangefs_sb->flags &= ~ORANGEFS_OPT_INTR;
	orangefs_sb->flags &= ~ORANGEFS_OPT_LOCAL_LOCK;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;

		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_acl:
			sb->s_flags |= SB_POSIXACL;
			break;
		case Opt_intr:
			orangefs_sb->flags |= ORANGEFS_OPT_INTR;
			break;
		case Opt_local_lock:
			orangefs_sb->flags |= ORANGEFS_OPT_LOCAL_LOCK;
			break;
		default:
			goto fail;
		}
	}

	return 0;
fail:
	if (!silent)
		gossip_err("Error: mount option [%s] is not supported.\n", p);
	return -EINVAL;
}

static void orangefs_inode_cache_ctor(void *req)
{
	struct orangefs_inode_s *orangefs_inode = req;

	inode_init_once(&orangefs_inode->vfs_inode);
	init_rwsem(&orangefs_inode->xattr_sem);
}

static struct inode *orangefs_alloc_inode(struct super_block *sb)
{
	struct orangefs_inode_s *orangefs_inode;

	orangefs_inode = alloc_inode_sb(sb, orangefs_inode_cache, GFP_KERNEL);
	if (!orangefs_inode)
		return NULL;

	/*
	 * We want to clear everything except for rw_semaphore and the
	 * vfs_inode.
	 */
	memset(&orangefs_inode->refn.khandle, 0, 16);
	orangefs_inode->refn.fs_id = ORANGEFS_FS_ID_NULL;
	orangefs_inode->last_failed_block_index_read = 0;
	memset(orangefs_inode->link_target, 0, sizeof(orangefs_inode->link_target));

	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "orangefs_alloc_inode: allocated %p\n",
		     &orangefs_inode->vfs_inode);
	return &orangefs_inode->vfs_inode;
}

static void orangefs_free_inode(struct inode *inode)
{
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);
	struct orangefs_cached_xattr *cx;
	struct hlist_node *tmp;
	int i;

	hash_for_each_safe(orangefs_inode->xattr_cache, i, tmp, cx, node) {
		hlist_del(&cx->node);
		kfree(cx);
	}

	kmem_cache_free(orangefs_inode_cache, orangefs_inode);
}

static void orangefs_destroy_inode(struct inode *inode)
{
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);

	gossip_debug(GOSSIP_SUPER_DEBUG,
			"%s: deallocated %p destroying inode %pU\n",
			__func__, orangefs_inode, get_khandle_from_ino(inode));
}

static int orangefs_write_inode(struct inode *inode,
				struct writeback_control *wbc)
{
	gossip_debug(GOSSIP_SUPER_DEBUG, "orangefs_write_inode\n");
	return orangefs_inode_setattr(inode);
}

/*
 * NOTE: information filled in here is typically reflected in the
 * output of the system command 'df'
*/
static int orangefs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	int ret = -ENOMEM;
	struct orangefs_kernel_op_s *new_op = NULL;
	int flags = 0;
	struct super_block *sb = NULL;

	sb = dentry->d_sb;

	gossip_debug(GOSSIP_SUPER_DEBUG,
			"%s: called on sb %p (fs_id is %d)\n",
			__func__,
			sb,
			(int)(ORANGEFS_SB(sb)->fs_id));

	new_op = op_alloc(ORANGEFS_VFS_OP_STATFS);
	if (!new_op)
		return ret;
	new_op->upcall.req.statfs.fs_id = ORANGEFS_SB(sb)->fs_id;

	if (ORANGEFS_SB(sb)->flags & ORANGEFS_OPT_INTR)
		flags = ORANGEFS_OP_INTERRUPTIBLE;

	ret = service_operation(new_op, "orangefs_statfs", flags);

	if (new_op->downcall.status < 0)
		goto out_op_release;

	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "%s: got %ld blocks available | "
		     "%ld blocks total | %ld block size | "
		     "%ld files total | %ld files avail\n",
		     __func__,
		     (long)new_op->downcall.resp.statfs.blocks_avail,
		     (long)new_op->downcall.resp.statfs.blocks_total,
		     (long)new_op->downcall.resp.statfs.block_size,
		     (long)new_op->downcall.resp.statfs.files_total,
		     (long)new_op->downcall.resp.statfs.files_avail);

	buf->f_type = sb->s_magic;
	memcpy(&buf->f_fsid, &ORANGEFS_SB(sb)->fs_id, sizeof(buf->f_fsid));
	buf->f_bsize = new_op->downcall.resp.statfs.block_size;
	buf->f_namelen = ORANGEFS_NAME_MAX;

	buf->f_blocks = (sector_t) new_op->downcall.resp.statfs.blocks_total;
	buf->f_bfree = (sector_t) new_op->downcall.resp.statfs.blocks_avail;
	buf->f_bavail = (sector_t) new_op->downcall.resp.statfs.blocks_avail;
	buf->f_files = (sector_t) new_op->downcall.resp.statfs.files_total;
	buf->f_ffree = (sector_t) new_op->downcall.resp.statfs.files_avail;
	buf->f_frsize = 0;

out_op_release:
	op_release(new_op);
	gossip_debug(GOSSIP_SUPER_DEBUG, "%s: returning %d\n", __func__, ret);
	return ret;
}

/*
 * Remount as initiated by VFS layer.  We just need to reparse the mount
 * options, no need to signal pvfs2-client-core about it.
 */
static int orangefs_remount_fs(struct super_block *sb, int *flags, char *data)
{
	gossip_debug(GOSSIP_SUPER_DEBUG, "orangefs_remount_fs: called\n");
	return parse_mount_options(sb, data, 1);
}

/*
 * Remount as initiated by pvfs2-client-core on restart.  This is used to
 * repopulate mount information left from previous pvfs2-client-core.
 *
 * the idea here is that given a valid superblock, we're
 * re-initializing the user space client with the initial mount
 * information specified when the super block was first initialized.
 * this is very different than the first initialization/creation of a
 * superblock.  we use the special service_priority_operation to make
 * sure that the mount gets ahead of any other pending operation that
 * is waiting for servicing.  this means that the pvfs2-client won't
 * fail to start several times for all other pending operations before
 * the client regains all of the mount information from us.
 * NOTE: this function assumes that the request_mutex is already acquired!
 */
int orangefs_remount(struct orangefs_sb_info_s *orangefs_sb)
{
	struct orangefs_kernel_op_s *new_op;
	int ret = -EINVAL;

	gossip_debug(GOSSIP_SUPER_DEBUG, "orangefs_remount: called\n");

	new_op = op_alloc(ORANGEFS_VFS_OP_FS_MOUNT);
	if (!new_op)
		return -ENOMEM;
	strscpy(new_op->upcall.req.fs_mount.orangefs_config_server,
		orangefs_sb->devname);

	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "Attempting ORANGEFS Remount via host %s\n",
		     new_op->upcall.req.fs_mount.orangefs_config_server);

	/*
	 * we assume that the calling function has already acquired the
	 * request_mutex to prevent other operations from bypassing
	 * this one
	 */
	ret = service_operation(new_op, "orangefs_remount",
		ORANGEFS_OP_PRIORITY | ORANGEFS_OP_NO_MUTEX);
	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "orangefs_remount: mount got return value of %d\n",
		     ret);
	if (ret == 0) {
		/*
		 * store the id assigned to this sb -- it's just a
		 * short-lived mapping that the system interface uses
		 * to map this superblock to a particular mount entry
		 */
		orangefs_sb->id = new_op->downcall.resp.fs_mount.id;
		orangefs_sb->mount_pending = 0;
	}

	op_release(new_op);

	if (orangefs_userspace_version >= 20906) {
		new_op = op_alloc(ORANGEFS_VFS_OP_FEATURES);
		if (!new_op)
			return -ENOMEM;
		new_op->upcall.req.features.features = 0;
		ret = service_operation(new_op, "orangefs_features",
		    ORANGEFS_OP_PRIORITY | ORANGEFS_OP_NO_MUTEX);
		if (!ret)
			orangefs_features =
			    new_op->downcall.resp.features.features;
		else
			orangefs_features = 0;
		op_release(new_op);
	} else {
		orangefs_features = 0;
	}

	return ret;
}

int fsid_key_table_initialize(void)
{
	return 0;
}

void fsid_key_table_finalize(void)
{
}

static const struct super_operations orangefs_s_ops = {
	.alloc_inode = orangefs_alloc_inode,
	.free_inode = orangefs_free_inode,
	.destroy_inode = orangefs_destroy_inode,
	.write_inode = orangefs_write_inode,
	.drop_inode = generic_delete_inode,
	.statfs = orangefs_statfs,
	.remount_fs = orangefs_remount_fs,
	.show_options = orangefs_show_options,
};

static struct dentry *orangefs_fh_to_dentry(struct super_block *sb,
				  struct fid *fid,
				  int fh_len,
				  int fh_type)
{
	struct orangefs_object_kref refn;

	if (fh_len < 5 || fh_type > 2)
		return NULL;

	ORANGEFS_khandle_from(&(refn.khandle), fid->raw, 16);
	refn.fs_id = (u32) fid->raw[4];
	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "fh_to_dentry: handle %pU, fs_id %d\n",
		     &refn.khandle,
		     refn.fs_id);

	return d_obtain_alias(orangefs_iget(sb, &refn));
}

static int orangefs_encode_fh(struct inode *inode,
		    __u32 *fh,
		    int *max_len,
		    struct inode *parent)
{
	int len = parent ? 10 : 5;
	int type = 1;
	struct orangefs_object_kref refn;

	if (*max_len < len) {
		gossip_err("fh buffer is too small for encoding\n");
		*max_len = len;
		type = 255;
		goto out;
	}

	refn = ORANGEFS_I(inode)->refn;
	ORANGEFS_khandle_to(&refn.khandle, fh, 16);
	fh[4] = refn.fs_id;

	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "Encoding fh: handle %pU, fsid %u\n",
		     &refn.khandle,
		     refn.fs_id);


	if (parent) {
		refn = ORANGEFS_I(parent)->refn;
		ORANGEFS_khandle_to(&refn.khandle, (char *) fh + 20, 16);
		fh[9] = refn.fs_id;

		type = 2;
		gossip_debug(GOSSIP_SUPER_DEBUG,
			     "Encoding parent: handle %pU, fsid %u\n",
			     &refn.khandle,
			     refn.fs_id);
	}
	*max_len = len;

out:
	return type;
}

static const struct export_operations orangefs_export_ops = {
	.encode_fh = orangefs_encode_fh,
	.fh_to_dentry = orangefs_fh_to_dentry,
};

static int orangefs_unmount(int id, __s32 fs_id, const char *devname)
{
	struct orangefs_kernel_op_s *op;
	int r;
	op = op_alloc(ORANGEFS_VFS_OP_FS_UMOUNT);
	if (!op)
		return -ENOMEM;
	op->upcall.req.fs_umount.id = id;
	op->upcall.req.fs_umount.fs_id = fs_id;
	strscpy(op->upcall.req.fs_umount.orangefs_config_server, devname);
	r = service_operation(op, "orangefs_fs_umount", 0);
	/* Not much to do about an error here. */
	if (r)
		gossip_err("orangefs_unmount: service_operation %d\n", r);
	op_release(op);
	return r;
}

static int orangefs_fill_sb(struct super_block *sb,
		struct orangefs_fs_mount_response *fs_mount,
		void *data, int silent)
{
	int ret;
	struct inode *root;
	struct dentry *root_dentry;
	struct orangefs_object_kref root_object;

	ORANGEFS_SB(sb)->sb = sb;

	ORANGEFS_SB(sb)->root_khandle = fs_mount->root_khandle;
	ORANGEFS_SB(sb)->fs_id = fs_mount->fs_id;
	ORANGEFS_SB(sb)->id = fs_mount->id;

	if (data) {
		ret = parse_mount_options(sb, data, silent);
		if (ret)
			return ret;
	}

	/* Hang the xattr handlers off the superblock */
	sb->s_xattr = orangefs_xattr_handlers;
	sb->s_magic = ORANGEFS_SUPER_MAGIC;
	sb->s_op = &orangefs_s_ops;
	sb->s_d_op = &orangefs_dentry_operations;

	sb->s_blocksize = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
	sb->s_maxbytes = MAX_LFS_FILESIZE;

	ret = super_setup_bdi(sb);
	if (ret)
		return ret;

	root_object.khandle = ORANGEFS_SB(sb)->root_khandle;
	root_object.fs_id = ORANGEFS_SB(sb)->fs_id;
	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "get inode %pU, fsid %d\n",
		     &root_object.khandle,
		     root_object.fs_id);

	root = orangefs_iget(sb, &root_object);
	if (IS_ERR(root))
		return PTR_ERR(root);

	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "Allocated root inode [%p] with mode %x\n",
		     root,
		     root->i_mode);

	/* allocates and places root dentry in dcache */
	root_dentry = d_make_root(root);
	if (!root_dentry)
		return -ENOMEM;

	sb->s_export_op = &orangefs_export_ops;
	sb->s_root = root_dentry;
	return 0;
}

struct dentry *orangefs_mount(struct file_system_type *fst,
			   int flags,
			   const char *devname,
			   void *data)
{
	int ret;
	struct super_block *sb = ERR_PTR(-EINVAL);
	struct orangefs_kernel_op_s *new_op;
	struct dentry *d = ERR_PTR(-EINVAL);

	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "orangefs_mount: called with devname %s\n",
		     devname);

	if (!devname) {
		gossip_err("ERROR: device name not specified.\n");
		return ERR_PTR(-EINVAL);
	}

	new_op = op_alloc(ORANGEFS_VFS_OP_FS_MOUNT);
	if (!new_op)
		return ERR_PTR(-ENOMEM);

	strscpy(new_op->upcall.req.fs_mount.orangefs_config_server, devname);

	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "Attempting ORANGEFS Mount via host %s\n",
		     new_op->upcall.req.fs_mount.orangefs_config_server);

	ret = service_operation(new_op, "orangefs_mount", 0);
	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "orangefs_mount: mount got return value of %d\n", ret);
	if (ret)
		goto free_op;

	if (new_op->downcall.resp.fs_mount.fs_id == ORANGEFS_FS_ID_NULL) {
		gossip_err("ERROR: Retrieved null fs_id\n");
		ret = -EINVAL;
		goto free_op;
	}

	sb = sget(fst, NULL, set_anon_super, flags, NULL);

	if (IS_ERR(sb)) {
		d = ERR_CAST(sb);
		orangefs_unmount(new_op->downcall.resp.fs_mount.id,
		    new_op->downcall.resp.fs_mount.fs_id, devname);
		goto free_op;
	}

	/* alloc and init our private orangefs sb info */
	sb->s_fs_info = kzalloc(sizeof(struct orangefs_sb_info_s), GFP_KERNEL);
	if (!ORANGEFS_SB(sb)) {
		d = ERR_PTR(-ENOMEM);
		goto free_op;
	}

	ret = orangefs_fill_sb(sb,
	      &new_op->downcall.resp.fs_mount, data,
	      flags & SB_SILENT ? 1 : 0);

	if (ret) {
		d = ERR_PTR(ret);
		goto free_sb_and_op;
	}

	/*
	 * on successful mount, store the devname and data
	 * used
	 */
	strscpy(ORANGEFS_SB(sb)->devname, devname);


	/* mount_pending must be cleared */
	ORANGEFS_SB(sb)->mount_pending = 0;

	/*
	 * finally, add this sb to our list of known orangefs
	 * sb's
	 */
	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "Adding SB %p to orangefs superblocks\n",
		     ORANGEFS_SB(sb));
	spin_lock(&orangefs_superblocks_lock);
	list_add_tail(&ORANGEFS_SB(sb)->list, &orangefs_superblocks);
	spin_unlock(&orangefs_superblocks_lock);
	op_release(new_op);

	/* Must be removed from the list now. */
	ORANGEFS_SB(sb)->no_list = 0;

	if (orangefs_userspace_version >= 20906) {
		new_op = op_alloc(ORANGEFS_VFS_OP_FEATURES);
		if (!new_op)
			return ERR_PTR(-ENOMEM);
		new_op->upcall.req.features.features = 0;
		ret = service_operation(new_op, "orangefs_features", 0);
		orangefs_features = new_op->downcall.resp.features.features;
		op_release(new_op);
	} else {
		orangefs_features = 0;
	}

	return dget(sb->s_root);

free_sb_and_op:
	/* Will call orangefs_kill_sb with sb not in list. */
	ORANGEFS_SB(sb)->no_list = 1;
	/* ORANGEFS_VFS_OP_FS_UMOUNT is done by orangefs_kill_sb. */
	deactivate_locked_super(sb);
free_op:
	gossip_err("orangefs_mount: mount request failed with %d\n", ret);
	if (ret == -EINVAL) {
		gossip_err("Ensure that all orangefs-servers have the same FS configuration files\n");
		gossip_err("Look at pvfs2-client-core log file (typically /tmp/pvfs2-client.log) for more details\n");
	}

	op_release(new_op);

	return d;
}

void orangefs_kill_sb(struct super_block *sb)
{
	int r;
	gossip_debug(GOSSIP_SUPER_DEBUG, "orangefs_kill_sb: called\n");

	/* provided sb cleanup */
	kill_anon_super(sb);

	if (!ORANGEFS_SB(sb)) {
		mutex_lock(&orangefs_request_mutex);
		mutex_unlock(&orangefs_request_mutex);
		return;
	}
	/*
	 * issue the unmount to userspace to tell it to remove the
	 * dynamic mount info it has for this superblock
	 */
	r = orangefs_unmount(ORANGEFS_SB(sb)->id, ORANGEFS_SB(sb)->fs_id,
	    ORANGEFS_SB(sb)->devname);
	if (!r)
		ORANGEFS_SB(sb)->mount_pending = 1;

	if (!ORANGEFS_SB(sb)->no_list) {
		/* remove the sb from our list of orangefs specific sb's */
		spin_lock(&orangefs_superblocks_lock);
		/* not list_del_init */
		__list_del_entry(&ORANGEFS_SB(sb)->list);
		ORANGEFS_SB(sb)->list.prev = NULL;
		spin_unlock(&orangefs_superblocks_lock);
	}

	/*
	 * make sure that ORANGEFS_DEV_REMOUNT_ALL loop that might've seen us
	 * gets completed before we free the dang thing.
	 */
	mutex_lock(&orangefs_request_mutex);
	mutex_unlock(&orangefs_request_mutex);

	/* free the orangefs superblock private data */
	kfree(ORANGEFS_SB(sb));
}

int orangefs_inode_cache_initialize(void)
{
	orangefs_inode_cache = kmem_cache_create_usercopy(
					"orangefs_inode_cache",
					sizeof(struct orangefs_inode_s),
					0,
					0,
					offsetof(struct orangefs_inode_s,
						link_target),
					sizeof_field(struct orangefs_inode_s,
						link_target),
					orangefs_inode_cache_ctor);

	if (!orangefs_inode_cache) {
		gossip_err("Cannot create orangefs_inode_cache\n");
		return -ENOMEM;
	}
	return 0;
}

int orangefs_inode_cache_finalize(void)
{
	kmem_cache_destroy(orangefs_inode_cache);
	return 0;
}
