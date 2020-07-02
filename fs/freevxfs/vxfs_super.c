/*
 * Copyright (c) 2000-2001 Christoph Hellwig.
 * Copyright (c) 2016 Krzysztof Blaszkowski
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Veritas filesystem driver - superblock related routines.
 */
#include <linux/init.h>
#include <linux/module.h>

#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/vfs.h>
#include <linux/mount.h>

#include "vxfs.h"
#include "vxfs_extern.h"
#include "vxfs_dir.h"
#include "vxfs_inode.h"


MODULE_AUTHOR("Christoph Hellwig, Krzysztof Blaszkowski");
MODULE_DESCRIPTION("Veritas Filesystem (VxFS) driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(ANDROID_GKI_VFS_EXPORT_ONLY);

static struct kmem_cache *vxfs_inode_cachep;

/**
 * vxfs_put_super - free superblock resources
 * @sbp:	VFS superblock.
 *
 * Description:
 *   vxfs_put_super frees all resources allocated for @sbp
 *   after the last instance of the filesystem is unmounted.
 */

static void
vxfs_put_super(struct super_block *sbp)
{
	struct vxfs_sb_info	*infp = VXFS_SBI(sbp);

	iput(infp->vsi_fship);
	iput(infp->vsi_ilist);
	iput(infp->vsi_stilist);

	brelse(infp->vsi_bp);
	kfree(infp);
}

/**
 * vxfs_statfs - get filesystem information
 * @dentry:	VFS dentry to locate superblock
 * @bufp:	output buffer
 *
 * Description:
 *   vxfs_statfs fills the statfs buffer @bufp with information
 *   about the filesystem described by @dentry.
 *
 * Returns:
 *   Zero.
 *
 * Locking:
 *   No locks held.
 *
 * Notes:
 *   This is everything but complete...
 */
static int
vxfs_statfs(struct dentry *dentry, struct kstatfs *bufp)
{
	struct vxfs_sb_info		*infp = VXFS_SBI(dentry->d_sb);
	struct vxfs_sb *raw_sb = infp->vsi_raw;

	bufp->f_type = VXFS_SUPER_MAGIC;
	bufp->f_bsize = dentry->d_sb->s_blocksize;
	bufp->f_blocks = fs32_to_cpu(infp, raw_sb->vs_dsize);
	bufp->f_bfree = fs32_to_cpu(infp, raw_sb->vs_free);
	bufp->f_bavail = 0;
	bufp->f_files = 0;
	bufp->f_ffree = fs32_to_cpu(infp, raw_sb->vs_ifree);
	bufp->f_namelen = VXFS_NAMELEN;

	return 0;
}

static int vxfs_remount(struct super_block *sb, int *flags, char *data)
{
	sync_filesystem(sb);
	*flags |= SB_RDONLY;
	return 0;
}

static struct inode *vxfs_alloc_inode(struct super_block *sb)
{
	struct vxfs_inode_info *vi;

	vi = kmem_cache_alloc(vxfs_inode_cachep, GFP_KERNEL);
	if (!vi)
		return NULL;
	inode_init_once(&vi->vfs_inode);
	return &vi->vfs_inode;
}

static void vxfs_free_inode(struct inode *inode)
{
	kmem_cache_free(vxfs_inode_cachep, VXFS_INO(inode));
}

static const struct super_operations vxfs_super_ops = {
	.alloc_inode		= vxfs_alloc_inode,
	.free_inode		= vxfs_free_inode,
	.evict_inode		= vxfs_evict_inode,
	.put_super		= vxfs_put_super,
	.statfs			= vxfs_statfs,
	.remount_fs		= vxfs_remount,
};

static int vxfs_try_sb_magic(struct super_block *sbp, int silent,
		unsigned blk, __fs32 magic)
{
	struct buffer_head *bp;
	struct vxfs_sb *rsbp;
	struct vxfs_sb_info *infp = VXFS_SBI(sbp);
	int rc = -ENOMEM;

	bp = sb_bread(sbp, blk);
	do {
		if (!bp || !buffer_mapped(bp)) {
			if (!silent) {
				printk(KERN_WARNING
					"vxfs: unable to read disk superblock at %u\n",
					blk);
			}
			break;
		}

		rc = -EINVAL;
		rsbp = (struct vxfs_sb *)bp->b_data;
		if (rsbp->vs_magic != magic) {
			if (!silent)
				printk(KERN_NOTICE
					"vxfs: WRONG superblock magic %08x at %u\n",
					rsbp->vs_magic, blk);
			break;
		}

		rc = 0;
		infp->vsi_raw = rsbp;
		infp->vsi_bp = bp;
	} while (0);

	if (rc) {
		infp->vsi_raw = NULL;
		infp->vsi_bp = NULL;
		brelse(bp);
	}

	return rc;
}

/**
 * vxfs_read_super - read superblock into memory and initialize filesystem
 * @sbp:		VFS superblock (to fill)
 * @dp:			fs private mount data
 * @silent:		do not complain loudly when sth is wrong
 *
 * Description:
 *   We are called on the first mount of a filesystem to read the
 *   superblock into memory and do some basic setup.
 *
 * Returns:
 *   The superblock on success, else %NULL.
 *
 * Locking:
 *   We are under @sbp->s_lock.
 */
static int vxfs_fill_super(struct super_block *sbp, void *dp, int silent)
{
	struct vxfs_sb_info	*infp;
	struct vxfs_sb		*rsbp;
	u_long			bsize;
	struct inode *root;
	int ret = -EINVAL;
	u32 j;

	sbp->s_flags |= SB_RDONLY;

	infp = kzalloc(sizeof(*infp), GFP_KERNEL);
	if (!infp) {
		printk(KERN_WARNING "vxfs: unable to allocate incore superblock\n");
		return -ENOMEM;
	}

	bsize = sb_min_blocksize(sbp, BLOCK_SIZE);
	if (!bsize) {
		printk(KERN_WARNING "vxfs: unable to set blocksize\n");
		goto out;
	}

	sbp->s_op = &vxfs_super_ops;
	sbp->s_fs_info = infp;
	sbp->s_time_min = 0;
	sbp->s_time_max = U32_MAX;

	if (!vxfs_try_sb_magic(sbp, silent, 1,
			(__force __fs32)cpu_to_le32(VXFS_SUPER_MAGIC))) {
		/* Unixware, x86 */
		infp->byte_order = VXFS_BO_LE;
	} else if (!vxfs_try_sb_magic(sbp, silent, 8,
			(__force __fs32)cpu_to_be32(VXFS_SUPER_MAGIC))) {
		/* HP-UX, parisc */
		infp->byte_order = VXFS_BO_BE;
	} else {
		if (!silent)
			printk(KERN_NOTICE "vxfs: can't find superblock.\n");
		goto out;
	}

	rsbp = infp->vsi_raw;
	j = fs32_to_cpu(infp, rsbp->vs_version);
	if ((j < 2 || j > 4) && !silent) {
		printk(KERN_NOTICE "vxfs: unsupported VxFS version (%d)\n", j);
		goto out;
	}

#ifdef DIAGNOSTIC
	printk(KERN_DEBUG "vxfs: supported VxFS version (%d)\n", j);
	printk(KERN_DEBUG "vxfs: blocksize: %d\n",
		fs32_to_cpu(infp, rsbp->vs_bsize));
#endif

	sbp->s_magic = fs32_to_cpu(infp, rsbp->vs_magic);

	infp->vsi_oltext = fs32_to_cpu(infp, rsbp->vs_oltext[0]);
	infp->vsi_oltsize = fs32_to_cpu(infp, rsbp->vs_oltsize);

	j = fs32_to_cpu(infp, rsbp->vs_bsize);
	if (!sb_set_blocksize(sbp, j)) {
		printk(KERN_WARNING "vxfs: unable to set final block size\n");
		goto out;
	}

	if (vxfs_read_olt(sbp, bsize)) {
		printk(KERN_WARNING "vxfs: unable to read olt\n");
		goto out;
	}

	if (vxfs_read_fshead(sbp)) {
		printk(KERN_WARNING "vxfs: unable to read fshead\n");
		goto out;
	}

	root = vxfs_iget(sbp, VXFS_ROOT_INO);
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		goto out;
	}
	sbp->s_root = d_make_root(root);
	if (!sbp->s_root) {
		printk(KERN_WARNING "vxfs: unable to get root dentry.\n");
		goto out_free_ilist;
	}

	return 0;
	
out_free_ilist:
	iput(infp->vsi_fship);
	iput(infp->vsi_ilist);
	iput(infp->vsi_stilist);
out:
	brelse(infp->vsi_bp);
	kfree(infp);
	return ret;
}

/*
 * The usual module blurb.
 */
static struct dentry *vxfs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, vxfs_fill_super);
}

static struct file_system_type vxfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "vxfs",
	.mount		= vxfs_mount,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("vxfs"); /* makes mount -t vxfs autoload the module */
MODULE_ALIAS("vxfs");

static int __init
vxfs_init(void)
{
	int rv;

	vxfs_inode_cachep = kmem_cache_create_usercopy("vxfs_inode",
			sizeof(struct vxfs_inode_info), 0,
			SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD,
			offsetof(struct vxfs_inode_info, vii_immed.vi_immed),
			sizeof_field(struct vxfs_inode_info,
				vii_immed.vi_immed),
			NULL);
	if (!vxfs_inode_cachep)
		return -ENOMEM;
	rv = register_filesystem(&vxfs_fs_type);
	if (rv < 0)
		kmem_cache_destroy(vxfs_inode_cachep);
	return rv;
}

static void __exit
vxfs_cleanup(void)
{
	unregister_filesystem(&vxfs_fs_type);
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(vxfs_inode_cachep);
}

module_init(vxfs_init);
module_exit(vxfs_cleanup);
