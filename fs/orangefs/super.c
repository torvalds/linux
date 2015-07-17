/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "protocol.h"
#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"

#include <linux/parser.h>

/* a cache for pvfs2-inode objects (i.e. pvfs2 inode private data) */
static struct kmem_cache *pvfs2_inode_cache;

/* list for storing pvfs2 specific superblocks in use */
LIST_HEAD(pvfs2_superblocks);

DEFINE_SPINLOCK(pvfs2_superblocks_lock);

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


static int parse_mount_options(struct super_block *sb, char *options,
		int silent)
{
	struct pvfs2_sb_info_s *pvfs2_sb = PVFS2_SB(sb);
	substring_t args[MAX_OPT_ARGS];
	char *p;

	/*
	 * Force any potential flags that might be set from the mount
	 * to zero, ie, initialize to unset.
	 */
	sb->s_flags &= ~MS_POSIXACL;
	pvfs2_sb->flags &= ~PVFS2_OPT_INTR;
	pvfs2_sb->flags &= ~PVFS2_OPT_LOCAL_LOCK;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;

		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_acl:
			sb->s_flags |= MS_POSIXACL;
			break;
		case Opt_intr:
			pvfs2_sb->flags |= PVFS2_OPT_INTR;
			break;
		case Opt_local_lock:
			pvfs2_sb->flags |= PVFS2_OPT_LOCAL_LOCK;
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

static void pvfs2_inode_cache_ctor(void *req)
{
	struct pvfs2_inode_s *pvfs2_inode = req;

	inode_init_once(&pvfs2_inode->vfs_inode);
	init_rwsem(&pvfs2_inode->xattr_sem);

	pvfs2_inode->vfs_inode.i_version = 1;
}

static struct inode *pvfs2_alloc_inode(struct super_block *sb)
{
	struct pvfs2_inode_s *pvfs2_inode;

	pvfs2_inode = kmem_cache_alloc(pvfs2_inode_cache,
				       PVFS2_CACHE_ALLOC_FLAGS);
	if (pvfs2_inode == NULL) {
		gossip_err("Failed to allocate pvfs2_inode\n");
		return NULL;
	}

	/*
	 * We want to clear everything except for rw_semaphore and the
	 * vfs_inode.
	 */
	memset(&pvfs2_inode->refn.khandle, 0, 16);
	pvfs2_inode->refn.fs_id = PVFS_FS_ID_NULL;
	pvfs2_inode->last_failed_block_index_read = 0;
	memset(pvfs2_inode->link_target, 0, sizeof(pvfs2_inode->link_target));
	pvfs2_inode->pinode_flags = 0;

	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "pvfs2_alloc_inode: allocated %p\n",
		     &pvfs2_inode->vfs_inode);
	return &pvfs2_inode->vfs_inode;
}

static void pvfs2_destroy_inode(struct inode *inode)
{
	struct pvfs2_inode_s *pvfs2_inode = PVFS2_I(inode);

	gossip_debug(GOSSIP_SUPER_DEBUG,
			"%s: deallocated %p destroying inode %pU\n",
			__func__, pvfs2_inode, get_khandle_from_ino(inode));

	kmem_cache_free(pvfs2_inode_cache, pvfs2_inode);
}

/*
 * NOTE: information filled in here is typically reflected in the
 * output of the system command 'df'
*/
static int pvfs2_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	int ret = -ENOMEM;
	struct pvfs2_kernel_op_s *new_op = NULL;
	int flags = 0;
	struct super_block *sb = NULL;

	sb = dentry->d_sb;

	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "pvfs2_statfs: called on sb %p (fs_id is %d)\n",
		     sb,
		     (int)(PVFS2_SB(sb)->fs_id));

	new_op = op_alloc(PVFS2_VFS_OP_STATFS);
	if (!new_op)
		return ret;
	new_op->upcall.req.statfs.fs_id = PVFS2_SB(sb)->fs_id;

	if (PVFS2_SB(sb)->flags & PVFS2_OPT_INTR)
		flags = PVFS2_OP_INTERRUPTIBLE;

	ret = service_operation(new_op, "pvfs2_statfs", flags);

	if (new_op->downcall.status < 0)
		goto out_op_release;

	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "pvfs2_statfs: got %ld blocks available | "
		     "%ld blocks total | %ld block size\n",
		     (long)new_op->downcall.resp.statfs.blocks_avail,
		     (long)new_op->downcall.resp.statfs.blocks_total,
		     (long)new_op->downcall.resp.statfs.block_size);

	buf->f_type = sb->s_magic;
	memcpy(&buf->f_fsid, &PVFS2_SB(sb)->fs_id, sizeof(buf->f_fsid));
	buf->f_bsize = new_op->downcall.resp.statfs.block_size;
	buf->f_namelen = PVFS2_NAME_LEN;

	buf->f_blocks = (sector_t) new_op->downcall.resp.statfs.blocks_total;
	buf->f_bfree = (sector_t) new_op->downcall.resp.statfs.blocks_avail;
	buf->f_bavail = (sector_t) new_op->downcall.resp.statfs.blocks_avail;
	buf->f_files = (sector_t) new_op->downcall.resp.statfs.files_total;
	buf->f_ffree = (sector_t) new_op->downcall.resp.statfs.files_avail;
	buf->f_frsize = sb->s_blocksize;

out_op_release:
	op_release(new_op);
	gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_statfs: returning %d\n", ret);
	return ret;
}

/*
 * Remount as initiated by VFS layer.  We just need to reparse the mount
 * options, no need to signal pvfs2-client-core about it.
 */
static int pvfs2_remount_fs(struct super_block *sb, int *flags, char *data)
{
	gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_remount_fs: called\n");
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
int pvfs2_remount(struct super_block *sb)
{
	struct pvfs2_kernel_op_s *new_op;
	int ret = -EINVAL;

	gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_remount: called\n");

	new_op = op_alloc(PVFS2_VFS_OP_FS_MOUNT);
	if (!new_op)
		return -ENOMEM;
	strncpy(new_op->upcall.req.fs_mount.pvfs2_config_server,
		PVFS2_SB(sb)->devname,
		PVFS_MAX_SERVER_ADDR_LEN);

	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "Attempting PVFS2 Remount via host %s\n",
		     new_op->upcall.req.fs_mount.pvfs2_config_server);

	/*
	 * we assume that the calling function has already acquire the
	 * request_mutex to prevent other operations from bypassing
	 * this one
	 */
	ret = service_operation(new_op, "pvfs2_remount",
		PVFS2_OP_PRIORITY | PVFS2_OP_NO_SEMAPHORE);
	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "pvfs2_remount: mount got return value of %d\n",
		     ret);
	if (ret == 0) {
		/*
		 * store the id assigned to this sb -- it's just a
		 * short-lived mapping that the system interface uses
		 * to map this superblock to a particular mount entry
		 */
		PVFS2_SB(sb)->id = new_op->downcall.resp.fs_mount.id;
		PVFS2_SB(sb)->mount_pending = 0;
	}

	op_release(new_op);
	return ret;
}

int fsid_key_table_initialize(void)
{
	return 0;
}

void fsid_key_table_finalize(void)
{
}

/* Called whenever the VFS dirties the inode in response to atime updates */
static void pvfs2_dirty_inode(struct inode *inode, int flags)
{
	struct pvfs2_inode_s *pvfs2_inode = PVFS2_I(inode);

	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "pvfs2_dirty_inode: %pU\n",
		     get_khandle_from_ino(inode));
	SetAtimeFlag(pvfs2_inode);
}

struct super_operations pvfs2_s_ops = {
	.alloc_inode = pvfs2_alloc_inode,
	.destroy_inode = pvfs2_destroy_inode,
	.dirty_inode = pvfs2_dirty_inode,
	.drop_inode = generic_delete_inode,
	.statfs = pvfs2_statfs,
	.remount_fs = pvfs2_remount_fs,
	.show_options = generic_show_options,
};

struct dentry *pvfs2_fh_to_dentry(struct super_block *sb,
				  struct fid *fid,
				  int fh_len,
				  int fh_type)
{
	struct pvfs2_object_kref refn;

	if (fh_len < 5 || fh_type > 2)
		return NULL;

	PVFS_khandle_from(&(refn.khandle), fid->raw, 16);
	refn.fs_id = (u32) fid->raw[4];
	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "fh_to_dentry: handle %pU, fs_id %d\n",
		     &refn.khandle,
		     refn.fs_id);

	return d_obtain_alias(pvfs2_iget(sb, &refn));
}

int pvfs2_encode_fh(struct inode *inode,
		    __u32 *fh,
		    int *max_len,
		    struct inode *parent)
{
	int len = parent ? 10 : 5;
	int type = 1;
	struct pvfs2_object_kref refn;

	if (*max_len < len) {
		gossip_lerr("fh buffer is too small for encoding\n");
		*max_len = len;
		type = 255;
		goto out;
	}

	refn = PVFS2_I(inode)->refn;
	PVFS_khandle_to(&refn.khandle, fh, 16);
	fh[4] = refn.fs_id;

	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "Encoding fh: handle %pU, fsid %u\n",
		     &refn.khandle,
		     refn.fs_id);


	if (parent) {
		refn = PVFS2_I(parent)->refn;
		PVFS_khandle_to(&refn.khandle, (char *) fh + 20, 16);
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

static struct export_operations pvfs2_export_ops = {
	.encode_fh = pvfs2_encode_fh,
	.fh_to_dentry = pvfs2_fh_to_dentry,
};

int pvfs2_fill_sb(struct super_block *sb, void *data, int silent)
{
	int ret = -EINVAL;
	struct inode *root = NULL;
	struct dentry *root_dentry = NULL;
	struct pvfs2_mount_sb_info_s *mount_sb_info =
		(struct pvfs2_mount_sb_info_s *) data;
	struct pvfs2_object_kref root_object;

	/* alloc and init our private pvfs2 sb info */
	sb->s_fs_info =
		kmalloc(sizeof(struct pvfs2_sb_info_s), PVFS2_GFP_FLAGS);
	if (!PVFS2_SB(sb))
		return -ENOMEM;
	memset(sb->s_fs_info, 0, sizeof(struct pvfs2_sb_info_s));
	PVFS2_SB(sb)->sb = sb;

	PVFS2_SB(sb)->root_khandle = mount_sb_info->root_khandle;
	PVFS2_SB(sb)->fs_id = mount_sb_info->fs_id;
	PVFS2_SB(sb)->id = mount_sb_info->id;

	if (mount_sb_info->data) {
		ret = parse_mount_options(sb, mount_sb_info->data,
					  silent);
		if (ret)
			return ret;
	}

	/* Hang the xattr handlers off the superblock */
	sb->s_xattr = pvfs2_xattr_handlers;
	sb->s_magic = PVFS2_SUPER_MAGIC;
	sb->s_op = &pvfs2_s_ops;
	sb->s_d_op = &pvfs2_dentry_operations;

	sb->s_blocksize = pvfs_bufmap_size_query();
	sb->s_blocksize_bits = pvfs_bufmap_shift_query();
	sb->s_maxbytes = MAX_LFS_FILESIZE;

	root_object.khandle = PVFS2_SB(sb)->root_khandle;
	root_object.fs_id = PVFS2_SB(sb)->fs_id;
	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "get inode %pU, fsid %d\n",
		     &root_object.khandle,
		     root_object.fs_id);

	root = pvfs2_iget(sb, &root_object);
	if (IS_ERR(root))
		return PTR_ERR(root);

	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "Allocated root inode [%p] with mode %x\n",
		     root,
		     root->i_mode);

	/* allocates and places root dentry in dcache */
	root_dentry = d_make_root(root);
	if (!root_dentry) {
		iput(root);
		return -ENOMEM;
	}

	sb->s_export_op = &pvfs2_export_ops;
	sb->s_root = root_dentry;
	return 0;
}

struct dentry *pvfs2_mount(struct file_system_type *fst,
			   int flags,
			   const char *devname,
			   void *data)
{
	int ret = -EINVAL;
	struct super_block *sb = ERR_PTR(-EINVAL);
	struct pvfs2_kernel_op_s *new_op;
	struct pvfs2_mount_sb_info_s mount_sb_info;
	struct dentry *mnt_sb_d = ERR_PTR(-EINVAL);

	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "pvfs2_mount: called with devname %s\n",
		     devname);

	if (!devname) {
		gossip_err("ERROR: device name not specified.\n");
		return ERR_PTR(-EINVAL);
	}

	new_op = op_alloc(PVFS2_VFS_OP_FS_MOUNT);
	if (!new_op)
		return ERR_PTR(-ENOMEM);

	strncpy(new_op->upcall.req.fs_mount.pvfs2_config_server,
		devname,
		PVFS_MAX_SERVER_ADDR_LEN);

	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "Attempting PVFS2 Mount via host %s\n",
		     new_op->upcall.req.fs_mount.pvfs2_config_server);

	ret = service_operation(new_op, "pvfs2_mount", 0);
	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "pvfs2_mount: mount got return value of %d\n", ret);
	if (ret)
		goto free_op;

	if (new_op->downcall.resp.fs_mount.fs_id == PVFS_FS_ID_NULL) {
		gossip_err("ERROR: Retrieved null fs_id\n");
		ret = -EINVAL;
		goto free_op;
	}

	/* fill in temporary structure passed to fill_sb method */
	mount_sb_info.data = data;
	mount_sb_info.root_khandle =
		new_op->downcall.resp.fs_mount.root_khandle;
	mount_sb_info.fs_id = new_op->downcall.resp.fs_mount.fs_id;
	mount_sb_info.id = new_op->downcall.resp.fs_mount.id;

	/*
	 * the mount_sb_info structure looks odd, but it's used because
	 * the private sb info isn't allocated until we call
	 * pvfs2_fill_sb, yet we have the info we need to fill it with
	 * here.  so we store it temporarily and pass all of the info
	 * to fill_sb where it's properly copied out
	 */
	mnt_sb_d = mount_nodev(fst,
			       flags,
			       (void *)&mount_sb_info,
			       pvfs2_fill_sb);
	if (IS_ERR(mnt_sb_d)) {
		sb = ERR_CAST(mnt_sb_d);
		goto free_op;
	}

	sb = mnt_sb_d->d_sb;

	/*
	 * on successful mount, store the devname and data
	 * used
	 */
	strncpy(PVFS2_SB(sb)->devname,
		devname,
		PVFS_MAX_SERVER_ADDR_LEN);

	/* mount_pending must be cleared */
	PVFS2_SB(sb)->mount_pending = 0;

	/*
	 * finally, add this sb to our list of known pvfs2
	 * sb's
	 */
	add_pvfs2_sb(sb);
	op_release(new_op);
	return mnt_sb_d;

free_op:
	gossip_err("pvfs2_mount: mount request failed with %d\n", ret);
	if (ret == -EINVAL) {
		gossip_err("Ensure that all pvfs2-servers have the same FS configuration files\n");
		gossip_err("Look at pvfs2-client-core log file (typically /tmp/pvfs2-client.log) for more details\n");
	}

	op_release(new_op);

	gossip_debug(GOSSIP_SUPER_DEBUG,
		     "pvfs2_mount: returning dentry %p\n",
		     mnt_sb_d);
	return mnt_sb_d;
}

void pvfs2_kill_sb(struct super_block *sb)
{
	gossip_debug(GOSSIP_SUPER_DEBUG, "pvfs2_kill_sb: called\n");

	/*
	 * issue the unmount to userspace to tell it to remove the
	 * dynamic mount info it has for this superblock
	 */
	pvfs2_unmount_sb(sb);

	/* remove the sb from our list of pvfs2 specific sb's */
	remove_pvfs2_sb(sb);

	/* provided sb cleanup */
	kill_anon_super(sb);

	/* free the pvfs2 superblock private data */
	kfree(PVFS2_SB(sb));
}

int pvfs2_inode_cache_initialize(void)
{
	pvfs2_inode_cache = kmem_cache_create("pvfs2_inode_cache",
					      sizeof(struct pvfs2_inode_s),
					      0,
					      PVFS2_CACHE_CREATE_FLAGS,
					      pvfs2_inode_cache_ctor);

	if (!pvfs2_inode_cache) {
		gossip_err("Cannot create pvfs2_inode_cache\n");
		return -ENOMEM;
	}
	return 0;
}

int pvfs2_inode_cache_finalize(void)
{
	kmem_cache_destroy(pvfs2_inode_cache);
	return 0;
}
