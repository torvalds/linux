/*
 *  linux/fs/hfsplus/super.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/vfs.h>
#include <linux/nls.h>

static struct inode *hfsplus_alloc_inode(struct super_block *sb);
static void hfsplus_free_inode(struct inode *inode);

#include "hfsplus_fs.h"
#include "xattr.h"

static int hfsplus_system_read_inode(struct inode *inode)
{
	struct hfsplus_vh *vhdr = HFSPLUS_SB(inode->i_sb)->s_vhdr;

	switch (inode->i_ino) {
	case HFSPLUS_EXT_CNID:
		hfsplus_inode_read_fork(inode, &vhdr->ext_file);
		inode->i_mapping->a_ops = &hfsplus_btree_aops;
		break;
	case HFSPLUS_CAT_CNID:
		hfsplus_inode_read_fork(inode, &vhdr->cat_file);
		inode->i_mapping->a_ops = &hfsplus_btree_aops;
		break;
	case HFSPLUS_ALLOC_CNID:
		hfsplus_inode_read_fork(inode, &vhdr->alloc_file);
		inode->i_mapping->a_ops = &hfsplus_aops;
		break;
	case HFSPLUS_START_CNID:
		hfsplus_inode_read_fork(inode, &vhdr->start_file);
		break;
	case HFSPLUS_ATTR_CNID:
		hfsplus_inode_read_fork(inode, &vhdr->attr_file);
		inode->i_mapping->a_ops = &hfsplus_btree_aops;
		break;
	default:
		return -EIO;
	}

	return 0;
}

struct inode *hfsplus_iget(struct super_block *sb, unsigned long ino)
{
	struct hfs_find_data fd;
	struct inode *inode;
	int err;

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	INIT_LIST_HEAD(&HFSPLUS_I(inode)->open_dir_list);
	spin_lock_init(&HFSPLUS_I(inode)->open_dir_lock);
	mutex_init(&HFSPLUS_I(inode)->extents_lock);
	HFSPLUS_I(inode)->flags = 0;
	HFSPLUS_I(inode)->extent_state = 0;
	HFSPLUS_I(inode)->rsrc_inode = NULL;
	atomic_set(&HFSPLUS_I(inode)->opencnt, 0);

	if (inode->i_ino >= HFSPLUS_FIRSTUSER_CNID ||
	    inode->i_ino == HFSPLUS_ROOT_CNID) {
		err = hfs_find_init(HFSPLUS_SB(inode->i_sb)->cat_tree, &fd);
		if (!err) {
			err = hfsplus_find_cat(inode->i_sb, inode->i_ino, &fd);
			if (!err)
				err = hfsplus_cat_read_inode(inode, &fd);
			hfs_find_exit(&fd);
		}
	} else {
		err = hfsplus_system_read_inode(inode);
	}

	if (err) {
		iget_failed(inode);
		return ERR_PTR(err);
	}

	unlock_new_inode(inode);
	return inode;
}

static int hfsplus_system_write_inode(struct inode *inode)
{
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(inode->i_sb);
	struct hfsplus_vh *vhdr = sbi->s_vhdr;
	struct hfsplus_fork_raw *fork;
	struct hfs_btree *tree = NULL;

	switch (inode->i_ino) {
	case HFSPLUS_EXT_CNID:
		fork = &vhdr->ext_file;
		tree = sbi->ext_tree;
		break;
	case HFSPLUS_CAT_CNID:
		fork = &vhdr->cat_file;
		tree = sbi->cat_tree;
		break;
	case HFSPLUS_ALLOC_CNID:
		fork = &vhdr->alloc_file;
		break;
	case HFSPLUS_START_CNID:
		fork = &vhdr->start_file;
		break;
	case HFSPLUS_ATTR_CNID:
		fork = &vhdr->attr_file;
		tree = sbi->attr_tree;
		break;
	default:
		return -EIO;
	}

	if (fork->total_size != cpu_to_be64(inode->i_size)) {
		set_bit(HFSPLUS_SB_WRITEBACKUP, &sbi->flags);
		hfsplus_mark_mdb_dirty(inode->i_sb);
	}
	hfsplus_inode_write_fork(inode, fork);
	if (tree) {
		int err = hfs_btree_write(tree);

		if (err) {
			pr_err("b-tree write err: %d, ino %lu\n",
			       err, inode->i_ino);
			return err;
		}
	}
	return 0;
}

static int hfsplus_write_inode(struct inode *inode,
		struct writeback_control *wbc)
{
	int err;

	hfs_dbg(INODE, "hfsplus_write_inode: %lu\n", inode->i_ino);

	err = hfsplus_ext_write_extent(inode);
	if (err)
		return err;

	if (inode->i_ino >= HFSPLUS_FIRSTUSER_CNID ||
	    inode->i_ino == HFSPLUS_ROOT_CNID)
		return hfsplus_cat_write_inode(inode);
	else
		return hfsplus_system_write_inode(inode);
}

static void hfsplus_evict_inode(struct inode *inode)
{
	hfs_dbg(INODE, "hfsplus_evict_inode: %lu\n", inode->i_ino);
	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);
	if (HFSPLUS_IS_RSRC(inode)) {
		HFSPLUS_I(HFSPLUS_I(inode)->rsrc_inode)->rsrc_inode = NULL;
		iput(HFSPLUS_I(inode)->rsrc_inode);
	}
}

static int hfsplus_sync_fs(struct super_block *sb, int wait)
{
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(sb);
	struct hfsplus_vh *vhdr = sbi->s_vhdr;
	int write_backup = 0;
	int error, error2;

	if (!wait)
		return 0;

	hfs_dbg(SUPER, "hfsplus_sync_fs\n");

	/*
	 * Explicitly write out the special metadata inodes.
	 *
	 * While these special inodes are marked as hashed and written
	 * out peridocically by the flusher threads we redirty them
	 * during writeout of normal inodes, and thus the life lock
	 * prevents us from getting the latest state to disk.
	 */
	error = filemap_write_and_wait(sbi->cat_tree->inode->i_mapping);
	error2 = filemap_write_and_wait(sbi->ext_tree->inode->i_mapping);
	if (!error)
		error = error2;
	if (sbi->attr_tree) {
		error2 =
		    filemap_write_and_wait(sbi->attr_tree->inode->i_mapping);
		if (!error)
			error = error2;
	}
	error2 = filemap_write_and_wait(sbi->alloc_file->i_mapping);
	if (!error)
		error = error2;

	mutex_lock(&sbi->vh_mutex);
	mutex_lock(&sbi->alloc_mutex);
	vhdr->free_blocks = cpu_to_be32(sbi->free_blocks);
	vhdr->next_cnid = cpu_to_be32(sbi->next_cnid);
	vhdr->folder_count = cpu_to_be32(sbi->folder_count);
	vhdr->file_count = cpu_to_be32(sbi->file_count);

	if (test_and_clear_bit(HFSPLUS_SB_WRITEBACKUP, &sbi->flags)) {
		memcpy(sbi->s_backup_vhdr, sbi->s_vhdr, sizeof(*sbi->s_vhdr));
		write_backup = 1;
	}

	error2 = hfsplus_submit_bio(sb,
				   sbi->part_start + HFSPLUS_VOLHEAD_SECTOR,
				   sbi->s_vhdr_buf, NULL, REQ_OP_WRITE,
				   REQ_SYNC);
	if (!error)
		error = error2;
	if (!write_backup)
		goto out;

	error2 = hfsplus_submit_bio(sb,
				  sbi->part_start + sbi->sect_count - 2,
				  sbi->s_backup_vhdr_buf, NULL, REQ_OP_WRITE,
				  REQ_SYNC);
	if (!error)
		error2 = error;
out:
	mutex_unlock(&sbi->alloc_mutex);
	mutex_unlock(&sbi->vh_mutex);

	if (!test_bit(HFSPLUS_SB_NOBARRIER, &sbi->flags))
		blkdev_issue_flush(sb->s_bdev, GFP_KERNEL, NULL);

	return error;
}

static void delayed_sync_fs(struct work_struct *work)
{
	int err;
	struct hfsplus_sb_info *sbi;

	sbi = container_of(work, struct hfsplus_sb_info, sync_work.work);

	spin_lock(&sbi->work_lock);
	sbi->work_queued = 0;
	spin_unlock(&sbi->work_lock);

	err = hfsplus_sync_fs(sbi->alloc_file->i_sb, 1);
	if (err)
		pr_err("delayed sync fs err %d\n", err);
}

void hfsplus_mark_mdb_dirty(struct super_block *sb)
{
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(sb);
	unsigned long delay;

	if (sb_rdonly(sb))
		return;

	spin_lock(&sbi->work_lock);
	if (!sbi->work_queued) {
		delay = msecs_to_jiffies(dirty_writeback_interval * 10);
		queue_delayed_work(system_long_wq, &sbi->sync_work, delay);
		sbi->work_queued = 1;
	}
	spin_unlock(&sbi->work_lock);
}

static void hfsplus_put_super(struct super_block *sb)
{
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(sb);

	hfs_dbg(SUPER, "hfsplus_put_super\n");

	cancel_delayed_work_sync(&sbi->sync_work);

	if (!sb_rdonly(sb) && sbi->s_vhdr) {
		struct hfsplus_vh *vhdr = sbi->s_vhdr;

		vhdr->modify_date = hfsp_now2mt();
		vhdr->attributes |= cpu_to_be32(HFSPLUS_VOL_UNMNT);
		vhdr->attributes &= cpu_to_be32(~HFSPLUS_VOL_INCNSTNT);

		hfsplus_sync_fs(sb, 1);
	}

	hfs_btree_close(sbi->attr_tree);
	hfs_btree_close(sbi->cat_tree);
	hfs_btree_close(sbi->ext_tree);
	iput(sbi->alloc_file);
	iput(sbi->hidden_dir);
	kfree(sbi->s_vhdr_buf);
	kfree(sbi->s_backup_vhdr_buf);
	unload_nls(sbi->nls);
	kfree(sb->s_fs_info);
	sb->s_fs_info = NULL;
}

static int hfsplus_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(sb);
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);

	buf->f_type = HFSPLUS_SUPER_MAGIC;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = sbi->total_blocks << sbi->fs_shift;
	buf->f_bfree = sbi->free_blocks << sbi->fs_shift;
	buf->f_bavail = buf->f_bfree;
	buf->f_files = 0xFFFFFFFF;
	buf->f_ffree = 0xFFFFFFFF - sbi->next_cnid;
	buf->f_fsid.val[0] = (u32)id;
	buf->f_fsid.val[1] = (u32)(id >> 32);
	buf->f_namelen = HFSPLUS_MAX_STRLEN;

	return 0;
}

static int hfsplus_remount(struct super_block *sb, int *flags, char *data)
{
	sync_filesystem(sb);
	if ((bool)(*flags & SB_RDONLY) == sb_rdonly(sb))
		return 0;
	if (!(*flags & SB_RDONLY)) {
		struct hfsplus_vh *vhdr = HFSPLUS_SB(sb)->s_vhdr;
		int force = 0;

		if (!hfsplus_parse_options_remount(data, &force))
			return -EINVAL;

		if (!(vhdr->attributes & cpu_to_be32(HFSPLUS_VOL_UNMNT))) {
			pr_warn("filesystem was not cleanly unmounted, running fsck.hfsplus is recommended.  leaving read-only.\n");
			sb->s_flags |= SB_RDONLY;
			*flags |= SB_RDONLY;
		} else if (force) {
			/* nothing */
		} else if (vhdr->attributes &
				cpu_to_be32(HFSPLUS_VOL_SOFTLOCK)) {
			pr_warn("filesystem is marked locked, leaving read-only.\n");
			sb->s_flags |= SB_RDONLY;
			*flags |= SB_RDONLY;
		} else if (vhdr->attributes &
				cpu_to_be32(HFSPLUS_VOL_JOURNALED)) {
			pr_warn("filesystem is marked journaled, leaving read-only.\n");
			sb->s_flags |= SB_RDONLY;
			*flags |= SB_RDONLY;
		}
	}
	return 0;
}

static const struct super_operations hfsplus_sops = {
	.alloc_inode	= hfsplus_alloc_inode,
	.free_inode	= hfsplus_free_inode,
	.write_inode	= hfsplus_write_inode,
	.evict_inode	= hfsplus_evict_inode,
	.put_super	= hfsplus_put_super,
	.sync_fs	= hfsplus_sync_fs,
	.statfs		= hfsplus_statfs,
	.remount_fs	= hfsplus_remount,
	.show_options	= hfsplus_show_options,
};

static int hfsplus_fill_super(struct super_block *sb, void *data, int silent)
{
	struct hfsplus_vh *vhdr;
	struct hfsplus_sb_info *sbi;
	hfsplus_cat_entry entry;
	struct hfs_find_data fd;
	struct inode *root, *inode;
	struct qstr str;
	struct nls_table *nls = NULL;
	u64 last_fs_block, last_fs_page;
	int err;

	err = -ENOMEM;
	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		goto out;

	sb->s_fs_info = sbi;
	mutex_init(&sbi->alloc_mutex);
	mutex_init(&sbi->vh_mutex);
	spin_lock_init(&sbi->work_lock);
	INIT_DELAYED_WORK(&sbi->sync_work, delayed_sync_fs);
	hfsplus_fill_defaults(sbi);

	err = -EINVAL;
	if (!hfsplus_parse_options(data, sbi)) {
		pr_err("unable to parse mount options\n");
		goto out_unload_nls;
	}

	/* temporarily use utf8 to correctly find the hidden dir below */
	nls = sbi->nls;
	sbi->nls = load_nls("utf8");
	if (!sbi->nls) {
		pr_err("unable to load nls for utf8\n");
		goto out_unload_nls;
	}

	/* Grab the volume header */
	if (hfsplus_read_wrapper(sb)) {
		if (!silent)
			pr_warn("unable to find HFS+ superblock\n");
		goto out_unload_nls;
	}
	vhdr = sbi->s_vhdr;

	/* Copy parts of the volume header into the superblock */
	sb->s_magic = HFSPLUS_VOLHEAD_SIG;
	if (be16_to_cpu(vhdr->version) < HFSPLUS_MIN_VERSION ||
	    be16_to_cpu(vhdr->version) > HFSPLUS_CURRENT_VERSION) {
		pr_err("wrong filesystem version\n");
		goto out_free_vhdr;
	}
	sbi->total_blocks = be32_to_cpu(vhdr->total_blocks);
	sbi->free_blocks = be32_to_cpu(vhdr->free_blocks);
	sbi->next_cnid = be32_to_cpu(vhdr->next_cnid);
	sbi->file_count = be32_to_cpu(vhdr->file_count);
	sbi->folder_count = be32_to_cpu(vhdr->folder_count);
	sbi->data_clump_blocks =
		be32_to_cpu(vhdr->data_clump_sz) >> sbi->alloc_blksz_shift;
	if (!sbi->data_clump_blocks)
		sbi->data_clump_blocks = 1;
	sbi->rsrc_clump_blocks =
		be32_to_cpu(vhdr->rsrc_clump_sz) >> sbi->alloc_blksz_shift;
	if (!sbi->rsrc_clump_blocks)
		sbi->rsrc_clump_blocks = 1;

	err = -EFBIG;
	last_fs_block = sbi->total_blocks - 1;
	last_fs_page = (last_fs_block << sbi->alloc_blksz_shift) >>
			PAGE_SHIFT;

	if ((last_fs_block > (sector_t)(~0ULL) >> (sbi->alloc_blksz_shift - 9)) ||
	    (last_fs_page > (pgoff_t)(~0ULL))) {
		pr_err("filesystem size too large\n");
		goto out_free_vhdr;
	}

	/* Set up operations so we can load metadata */
	sb->s_op = &hfsplus_sops;
	sb->s_maxbytes = MAX_LFS_FILESIZE;

	if (!(vhdr->attributes & cpu_to_be32(HFSPLUS_VOL_UNMNT))) {
		pr_warn("Filesystem was not cleanly unmounted, running fsck.hfsplus is recommended.  mounting read-only.\n");
		sb->s_flags |= SB_RDONLY;
	} else if (test_and_clear_bit(HFSPLUS_SB_FORCE, &sbi->flags)) {
		/* nothing */
	} else if (vhdr->attributes & cpu_to_be32(HFSPLUS_VOL_SOFTLOCK)) {
		pr_warn("Filesystem is marked locked, mounting read-only.\n");
		sb->s_flags |= SB_RDONLY;
	} else if ((vhdr->attributes & cpu_to_be32(HFSPLUS_VOL_JOURNALED)) &&
			!sb_rdonly(sb)) {
		pr_warn("write access to a journaled filesystem is not supported, use the force option at your own risk, mounting read-only.\n");
		sb->s_flags |= SB_RDONLY;
	}

	err = -EINVAL;

	/* Load metadata objects (B*Trees) */
	sbi->ext_tree = hfs_btree_open(sb, HFSPLUS_EXT_CNID);
	if (!sbi->ext_tree) {
		pr_err("failed to load extents file\n");
		goto out_free_vhdr;
	}
	sbi->cat_tree = hfs_btree_open(sb, HFSPLUS_CAT_CNID);
	if (!sbi->cat_tree) {
		pr_err("failed to load catalog file\n");
		goto out_close_ext_tree;
	}
	atomic_set(&sbi->attr_tree_state, HFSPLUS_EMPTY_ATTR_TREE);
	if (vhdr->attr_file.total_blocks != 0) {
		sbi->attr_tree = hfs_btree_open(sb, HFSPLUS_ATTR_CNID);
		if (!sbi->attr_tree) {
			pr_err("failed to load attributes file\n");
			goto out_close_cat_tree;
		}
		atomic_set(&sbi->attr_tree_state, HFSPLUS_VALID_ATTR_TREE);
	}
	sb->s_xattr = hfsplus_xattr_handlers;

	inode = hfsplus_iget(sb, HFSPLUS_ALLOC_CNID);
	if (IS_ERR(inode)) {
		pr_err("failed to load allocation file\n");
		err = PTR_ERR(inode);
		goto out_close_attr_tree;
	}
	sbi->alloc_file = inode;

	/* Load the root directory */
	root = hfsplus_iget(sb, HFSPLUS_ROOT_CNID);
	if (IS_ERR(root)) {
		pr_err("failed to load root directory\n");
		err = PTR_ERR(root);
		goto out_put_alloc_file;
	}

	sb->s_d_op = &hfsplus_dentry_operations;
	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		err = -ENOMEM;
		goto out_put_alloc_file;
	}

	str.len = sizeof(HFSP_HIDDENDIR_NAME) - 1;
	str.name = HFSP_HIDDENDIR_NAME;
	err = hfs_find_init(sbi->cat_tree, &fd);
	if (err)
		goto out_put_root;
	err = hfsplus_cat_build_key(sb, fd.search_key, HFSPLUS_ROOT_CNID, &str);
	if (unlikely(err < 0))
		goto out_put_root;
	if (!hfs_brec_read(&fd, &entry, sizeof(entry))) {
		hfs_find_exit(&fd);
		if (entry.type != cpu_to_be16(HFSPLUS_FOLDER)) {
			err = -EINVAL;
			goto out_put_root;
		}
		inode = hfsplus_iget(sb, be32_to_cpu(entry.folder.id));
		if (IS_ERR(inode)) {
			err = PTR_ERR(inode);
			goto out_put_root;
		}
		sbi->hidden_dir = inode;
	} else
		hfs_find_exit(&fd);

	if (!sb_rdonly(sb)) {
		/*
		 * H+LX == hfsplusutils, H+Lx == this driver, H+lx is unused
		 * all three are registered with Apple for our use
		 */
		vhdr->last_mount_vers = cpu_to_be32(HFSP_MOUNT_VERSION);
		vhdr->modify_date = hfsp_now2mt();
		be32_add_cpu(&vhdr->write_count, 1);
		vhdr->attributes &= cpu_to_be32(~HFSPLUS_VOL_UNMNT);
		vhdr->attributes |= cpu_to_be32(HFSPLUS_VOL_INCNSTNT);
		hfsplus_sync_fs(sb, 1);

		if (!sbi->hidden_dir) {
			mutex_lock(&sbi->vh_mutex);
			sbi->hidden_dir = hfsplus_new_inode(sb, root, S_IFDIR);
			if (!sbi->hidden_dir) {
				mutex_unlock(&sbi->vh_mutex);
				err = -ENOMEM;
				goto out_put_root;
			}
			err = hfsplus_create_cat(sbi->hidden_dir->i_ino, root,
						 &str, sbi->hidden_dir);
			if (err) {
				mutex_unlock(&sbi->vh_mutex);
				goto out_put_hidden_dir;
			}

			err = hfsplus_init_security(sbi->hidden_dir,
							root, &str);
			if (err == -EOPNOTSUPP)
				err = 0; /* Operation is not supported. */
			else if (err) {
				/*
				 * Try to delete anyway without
				 * error analysis.
				 */
				hfsplus_delete_cat(sbi->hidden_dir->i_ino,
							root, &str);
				mutex_unlock(&sbi->vh_mutex);
				goto out_put_hidden_dir;
			}

			mutex_unlock(&sbi->vh_mutex);
			hfsplus_mark_inode_dirty(sbi->hidden_dir,
						 HFSPLUS_I_CAT_DIRTY);
		}
	}

	unload_nls(sbi->nls);
	sbi->nls = nls;
	return 0;

out_put_hidden_dir:
	cancel_delayed_work_sync(&sbi->sync_work);
	iput(sbi->hidden_dir);
out_put_root:
	dput(sb->s_root);
	sb->s_root = NULL;
out_put_alloc_file:
	iput(sbi->alloc_file);
out_close_attr_tree:
	hfs_btree_close(sbi->attr_tree);
out_close_cat_tree:
	hfs_btree_close(sbi->cat_tree);
out_close_ext_tree:
	hfs_btree_close(sbi->ext_tree);
out_free_vhdr:
	kfree(sbi->s_vhdr_buf);
	kfree(sbi->s_backup_vhdr_buf);
out_unload_nls:
	unload_nls(sbi->nls);
	unload_nls(nls);
	kfree(sbi);
out:
	return err;
}

MODULE_AUTHOR("Brad Boyer");
MODULE_DESCRIPTION("Extended Macintosh Filesystem");
MODULE_LICENSE("GPL");

static struct kmem_cache *hfsplus_inode_cachep;

static struct inode *hfsplus_alloc_inode(struct super_block *sb)
{
	struct hfsplus_inode_info *i;

	i = kmem_cache_alloc(hfsplus_inode_cachep, GFP_KERNEL);
	return i ? &i->vfs_inode : NULL;
}

static void hfsplus_free_inode(struct inode *inode)
{
	kmem_cache_free(hfsplus_inode_cachep, HFSPLUS_I(inode));
}

#define HFSPLUS_INODE_SIZE	sizeof(struct hfsplus_inode_info)

static struct dentry *hfsplus_mount(struct file_system_type *fs_type,
			  int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, hfsplus_fill_super);
}

static struct file_system_type hfsplus_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "hfsplus",
	.mount		= hfsplus_mount,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("hfsplus");

static void hfsplus_init_once(void *p)
{
	struct hfsplus_inode_info *i = p;

	inode_init_once(&i->vfs_inode);
}

static int __init init_hfsplus_fs(void)
{
	int err;

	hfsplus_inode_cachep = kmem_cache_create("hfsplus_icache",
		HFSPLUS_INODE_SIZE, 0, SLAB_HWCACHE_ALIGN|SLAB_ACCOUNT,
		hfsplus_init_once);
	if (!hfsplus_inode_cachep)
		return -ENOMEM;
	err = hfsplus_create_attr_tree_cache();
	if (err)
		goto destroy_inode_cache;
	err = register_filesystem(&hfsplus_fs_type);
	if (err)
		goto destroy_attr_tree_cache;
	return 0;

destroy_attr_tree_cache:
	hfsplus_destroy_attr_tree_cache();

destroy_inode_cache:
	kmem_cache_destroy(hfsplus_inode_cachep);

	return err;
}

static void __exit exit_hfsplus_fs(void)
{
	unregister_filesystem(&hfsplus_fs_type);

	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	hfsplus_destroy_attr_tree_cache();
	kmem_cache_destroy(hfsplus_inode_cachep);
}

module_init(init_hfsplus_fs)
module_exit(exit_hfsplus_fs)
