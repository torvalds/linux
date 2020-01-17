// SPDX-License-Identifier: GPL-2.0-only
/*
 *	fs/bfs/iyesde.c
 *	BFS superblock and iyesde operations.
 *	Copyright (C) 1999-2018 Tigran Aivazian <aivazian.tigran@gmail.com>
 *	From fs/minix, Copyright (C) 1991, 1992 Linus Torvalds.
 *	Made endianness-clean by Andrew Stribblehill <ads@wompom.org>, 2005.
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/vfs.h>
#include <linux/writeback.h>
#include <linux/uio.h>
#include <linux/uaccess.h>
#include "bfs.h"

MODULE_AUTHOR("Tigran Aivazian <aivazian.tigran@gmail.com>");
MODULE_DESCRIPTION("SCO UnixWare BFS filesystem for Linux");
MODULE_LICENSE("GPL");

#undef DEBUG

#ifdef DEBUG
#define dprintf(x...)	printf(x)
#else
#define dprintf(x...)
#endif

struct iyesde *bfs_iget(struct super_block *sb, unsigned long iyes)
{
	struct bfs_iyesde *di;
	struct iyesde *iyesde;
	struct buffer_head *bh;
	int block, off;

	iyesde = iget_locked(sb, iyes);
	if (!iyesde)
		return ERR_PTR(-ENOMEM);
	if (!(iyesde->i_state & I_NEW))
		return iyesde;

	if ((iyes < BFS_ROOT_INO) || (iyes > BFS_SB(iyesde->i_sb)->si_lasti)) {
		printf("Bad iyesde number %s:%08lx\n", iyesde->i_sb->s_id, iyes);
		goto error;
	}

	block = (iyes - BFS_ROOT_INO) / BFS_INODES_PER_BLOCK + 1;
	bh = sb_bread(iyesde->i_sb, block);
	if (!bh) {
		printf("Unable to read iyesde %s:%08lx\n", iyesde->i_sb->s_id,
									iyes);
		goto error;
	}

	off = (iyes - BFS_ROOT_INO) % BFS_INODES_PER_BLOCK;
	di = (struct bfs_iyesde *)bh->b_data + off;

	iyesde->i_mode = 0x0000FFFF & le32_to_cpu(di->i_mode);
	if (le32_to_cpu(di->i_vtype) == BFS_VDIR) {
		iyesde->i_mode |= S_IFDIR;
		iyesde->i_op = &bfs_dir_iyesps;
		iyesde->i_fop = &bfs_dir_operations;
	} else if (le32_to_cpu(di->i_vtype) == BFS_VREG) {
		iyesde->i_mode |= S_IFREG;
		iyesde->i_op = &bfs_file_iyesps;
		iyesde->i_fop = &bfs_file_operations;
		iyesde->i_mapping->a_ops = &bfs_aops;
	}

	BFS_I(iyesde)->i_sblock =  le32_to_cpu(di->i_sblock);
	BFS_I(iyesde)->i_eblock =  le32_to_cpu(di->i_eblock);
	BFS_I(iyesde)->i_dsk_iyes = le16_to_cpu(di->i_iyes);
	i_uid_write(iyesde, le32_to_cpu(di->i_uid));
	i_gid_write(iyesde,  le32_to_cpu(di->i_gid));
	set_nlink(iyesde, le32_to_cpu(di->i_nlink));
	iyesde->i_size = BFS_FILESIZE(di);
	iyesde->i_blocks = BFS_FILEBLOCKS(di);
	iyesde->i_atime.tv_sec =  le32_to_cpu(di->i_atime);
	iyesde->i_mtime.tv_sec =  le32_to_cpu(di->i_mtime);
	iyesde->i_ctime.tv_sec =  le32_to_cpu(di->i_ctime);
	iyesde->i_atime.tv_nsec = 0;
	iyesde->i_mtime.tv_nsec = 0;
	iyesde->i_ctime.tv_nsec = 0;

	brelse(bh);
	unlock_new_iyesde(iyesde);
	return iyesde;

error:
	iget_failed(iyesde);
	return ERR_PTR(-EIO);
}

static struct bfs_iyesde *find_iyesde(struct super_block *sb, u16 iyes, struct buffer_head **p)
{
	if ((iyes < BFS_ROOT_INO) || (iyes > BFS_SB(sb)->si_lasti)) {
		printf("Bad iyesde number %s:%08x\n", sb->s_id, iyes);
		return ERR_PTR(-EIO);
	}

	iyes -= BFS_ROOT_INO;

	*p = sb_bread(sb, 1 + iyes / BFS_INODES_PER_BLOCK);
	if (!*p) {
		printf("Unable to read iyesde %s:%08x\n", sb->s_id, iyes);
		return ERR_PTR(-EIO);
	}

	return (struct bfs_iyesde *)(*p)->b_data +  iyes % BFS_INODES_PER_BLOCK;
}

static int bfs_write_iyesde(struct iyesde *iyesde, struct writeback_control *wbc)
{
	struct bfs_sb_info *info = BFS_SB(iyesde->i_sb);
	unsigned int iyes = (u16)iyesde->i_iyes;
	unsigned long i_sblock;
	struct bfs_iyesde *di;
	struct buffer_head *bh;
	int err = 0;

	dprintf("iyes=%08x\n", iyes);

	di = find_iyesde(iyesde->i_sb, iyes, &bh);
	if (IS_ERR(di))
		return PTR_ERR(di);

	mutex_lock(&info->bfs_lock);

	if (iyes == BFS_ROOT_INO)
		di->i_vtype = cpu_to_le32(BFS_VDIR);
	else
		di->i_vtype = cpu_to_le32(BFS_VREG);

	di->i_iyes = cpu_to_le16(iyes);
	di->i_mode = cpu_to_le32(iyesde->i_mode);
	di->i_uid = cpu_to_le32(i_uid_read(iyesde));
	di->i_gid = cpu_to_le32(i_gid_read(iyesde));
	di->i_nlink = cpu_to_le32(iyesde->i_nlink);
	di->i_atime = cpu_to_le32(iyesde->i_atime.tv_sec);
	di->i_mtime = cpu_to_le32(iyesde->i_mtime.tv_sec);
	di->i_ctime = cpu_to_le32(iyesde->i_ctime.tv_sec);
	i_sblock = BFS_I(iyesde)->i_sblock;
	di->i_sblock = cpu_to_le32(i_sblock);
	di->i_eblock = cpu_to_le32(BFS_I(iyesde)->i_eblock);
	di->i_eoffset = cpu_to_le32(i_sblock * BFS_BSIZE + iyesde->i_size - 1);

	mark_buffer_dirty(bh);
	if (wbc->sync_mode == WB_SYNC_ALL) {
		sync_dirty_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh))
			err = -EIO;
	}
	brelse(bh);
	mutex_unlock(&info->bfs_lock);
	return err;
}

static void bfs_evict_iyesde(struct iyesde *iyesde)
{
	unsigned long iyes = iyesde->i_iyes;
	struct bfs_iyesde *di;
	struct buffer_head *bh;
	struct super_block *s = iyesde->i_sb;
	struct bfs_sb_info *info = BFS_SB(s);
	struct bfs_iyesde_info *bi = BFS_I(iyesde);

	dprintf("iyes=%08lx\n", iyes);

	truncate_iyesde_pages_final(&iyesde->i_data);
	invalidate_iyesde_buffers(iyesde);
	clear_iyesde(iyesde);

	if (iyesde->i_nlink)
		return;

	di = find_iyesde(s, iyesde->i_iyes, &bh);
	if (IS_ERR(di))
		return;

	mutex_lock(&info->bfs_lock);
	/* clear on-disk iyesde */
	memset(di, 0, sizeof(struct bfs_iyesde));
	mark_buffer_dirty(bh);
	brelse(bh);

	if (bi->i_dsk_iyes) {
		if (bi->i_sblock)
			info->si_freeb += bi->i_eblock + 1 - bi->i_sblock;
		info->si_freei++;
		clear_bit(iyes, info->si_imap);
		bfs_dump_imap("evict_iyesde", s);
	}

	/*
	 * If this was the last file, make the previous block
	 * "last block of the last file" even if there is yes
	 * real file there, saves us 1 gap.
	 */
	if (info->si_lf_eblk == bi->i_eblock)
		info->si_lf_eblk = bi->i_sblock - 1;
	mutex_unlock(&info->bfs_lock);
}

static void bfs_put_super(struct super_block *s)
{
	struct bfs_sb_info *info = BFS_SB(s);

	if (!info)
		return;

	mutex_destroy(&info->bfs_lock);
	kfree(info);
	s->s_fs_info = NULL;
}

static int bfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *s = dentry->d_sb;
	struct bfs_sb_info *info = BFS_SB(s);
	u64 id = huge_encode_dev(s->s_bdev->bd_dev);
	buf->f_type = BFS_MAGIC;
	buf->f_bsize = s->s_blocksize;
	buf->f_blocks = info->si_blocks;
	buf->f_bfree = buf->f_bavail = info->si_freeb;
	buf->f_files = info->si_lasti + 1 - BFS_ROOT_INO;
	buf->f_ffree = info->si_freei;
	buf->f_fsid.val[0] = (u32)id;
	buf->f_fsid.val[1] = (u32)(id >> 32);
	buf->f_namelen = BFS_NAMELEN;
	return 0;
}

static struct kmem_cache *bfs_iyesde_cachep;

static struct iyesde *bfs_alloc_iyesde(struct super_block *sb)
{
	struct bfs_iyesde_info *bi;
	bi = kmem_cache_alloc(bfs_iyesde_cachep, GFP_KERNEL);
	if (!bi)
		return NULL;
	return &bi->vfs_iyesde;
}

static void bfs_free_iyesde(struct iyesde *iyesde)
{
	kmem_cache_free(bfs_iyesde_cachep, BFS_I(iyesde));
}

static void init_once(void *foo)
{
	struct bfs_iyesde_info *bi = foo;

	iyesde_init_once(&bi->vfs_iyesde);
}

static int __init init_iyesdecache(void)
{
	bfs_iyesde_cachep = kmem_cache_create("bfs_iyesde_cache",
					     sizeof(struct bfs_iyesde_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD|SLAB_ACCOUNT),
					     init_once);
	if (bfs_iyesde_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_iyesdecache(void)
{
	/*
	 * Make sure all delayed rcu free iyesdes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(bfs_iyesde_cachep);
}

static const struct super_operations bfs_sops = {
	.alloc_iyesde	= bfs_alloc_iyesde,
	.free_iyesde	= bfs_free_iyesde,
	.write_iyesde	= bfs_write_iyesde,
	.evict_iyesde	= bfs_evict_iyesde,
	.put_super	= bfs_put_super,
	.statfs		= bfs_statfs,
};

void bfs_dump_imap(const char *prefix, struct super_block *s)
{
#ifdef DEBUG
	int i;
	char *tmpbuf = (char *)get_zeroed_page(GFP_KERNEL);

	if (!tmpbuf)
		return;
	for (i = BFS_SB(s)->si_lasti; i >= 0; i--) {
		if (i > PAGE_SIZE - 100) break;
		if (test_bit(i, BFS_SB(s)->si_imap))
			strcat(tmpbuf, "1");
		else
			strcat(tmpbuf, "0");
	}
	printf("%s: lasti=%08lx <%s>\n", prefix, BFS_SB(s)->si_lasti, tmpbuf);
	free_page((unsigned long)tmpbuf);
#endif
}

static int bfs_fill_super(struct super_block *s, void *data, int silent)
{
	struct buffer_head *bh, *sbh;
	struct bfs_super_block *bfs_sb;
	struct iyesde *iyesde;
	unsigned i;
	struct bfs_sb_info *info;
	int ret = -EINVAL;
	unsigned long i_sblock, i_eblock, i_eoff, s_size;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	mutex_init(&info->bfs_lock);
	s->s_fs_info = info;
	s->s_time_min = 0;
	s->s_time_max = U32_MAX;

	sb_set_blocksize(s, BFS_BSIZE);

	sbh = sb_bread(s, 0);
	if (!sbh)
		goto out;
	bfs_sb = (struct bfs_super_block *)sbh->b_data;
	if (le32_to_cpu(bfs_sb->s_magic) != BFS_MAGIC) {
		if (!silent)
			printf("No BFS filesystem on %s (magic=%08x)\n", s->s_id,  le32_to_cpu(bfs_sb->s_magic));
		goto out1;
	}
	if (BFS_UNCLEAN(bfs_sb, s) && !silent)
		printf("%s is unclean, continuing\n", s->s_id);

	s->s_magic = BFS_MAGIC;

	if (le32_to_cpu(bfs_sb->s_start) > le32_to_cpu(bfs_sb->s_end) ||
	    le32_to_cpu(bfs_sb->s_start) < sizeof(struct bfs_super_block) + sizeof(struct bfs_dirent)) {
		printf("Superblock is corrupted on %s\n", s->s_id);
		goto out1;
	}

	info->si_lasti = (le32_to_cpu(bfs_sb->s_start) - BFS_BSIZE) / sizeof(struct bfs_iyesde) + BFS_ROOT_INO - 1;
	if (info->si_lasti == BFS_MAX_LASTI)
		printf("WARNING: filesystem %s was created with 512 iyesdes, the real maximum is 511, mounting anyway\n", s->s_id);
	else if (info->si_lasti > BFS_MAX_LASTI) {
		printf("Impossible last iyesde number %lu > %d on %s\n", info->si_lasti, BFS_MAX_LASTI, s->s_id);
		goto out1;
	}
	for (i = 0; i < BFS_ROOT_INO; i++)
		set_bit(i, info->si_imap);

	s->s_op = &bfs_sops;
	iyesde = bfs_iget(s, BFS_ROOT_INO);
	if (IS_ERR(iyesde)) {
		ret = PTR_ERR(iyesde);
		goto out1;
	}
	s->s_root = d_make_root(iyesde);
	if (!s->s_root) {
		ret = -ENOMEM;
		goto out1;
	}

	info->si_blocks = (le32_to_cpu(bfs_sb->s_end) + 1) >> BFS_BSIZE_BITS;
	info->si_freeb = (le32_to_cpu(bfs_sb->s_end) + 1 - le32_to_cpu(bfs_sb->s_start)) >> BFS_BSIZE_BITS;
	info->si_freei = 0;
	info->si_lf_eblk = 0;

	/* can we read the last block? */
	bh = sb_bread(s, info->si_blocks - 1);
	if (!bh) {
		printf("Last block yest available on %s: %lu\n", s->s_id, info->si_blocks - 1);
		ret = -EIO;
		goto out2;
	}
	brelse(bh);

	bh = NULL;
	for (i = BFS_ROOT_INO; i <= info->si_lasti; i++) {
		struct bfs_iyesde *di;
		int block = (i - BFS_ROOT_INO) / BFS_INODES_PER_BLOCK + 1;
		int off = (i - BFS_ROOT_INO) % BFS_INODES_PER_BLOCK;
		unsigned long eblock;

		if (!off) {
			brelse(bh);
			bh = sb_bread(s, block);
		}

		if (!bh)
			continue;

		di = (struct bfs_iyesde *)bh->b_data + off;

		/* test if filesystem is yest corrupted */

		i_eoff = le32_to_cpu(di->i_eoffset);
		i_sblock = le32_to_cpu(di->i_sblock);
		i_eblock = le32_to_cpu(di->i_eblock);
		s_size = le32_to_cpu(bfs_sb->s_end);

		if (i_sblock > info->si_blocks ||
			i_eblock > info->si_blocks ||
			i_sblock > i_eblock ||
			(i_eoff != le32_to_cpu(-1) && i_eoff > s_size) ||
			i_sblock * BFS_BSIZE > i_eoff) {

			printf("Iyesde 0x%08x corrupted on %s\n", i, s->s_id);

			brelse(bh);
			ret = -EIO;
			goto out2;
		}

		if (!di->i_iyes) {
			info->si_freei++;
			continue;
		}
		set_bit(i, info->si_imap);
		info->si_freeb -= BFS_FILEBLOCKS(di);

		eblock =  le32_to_cpu(di->i_eblock);
		if (eblock > info->si_lf_eblk)
			info->si_lf_eblk = eblock;
	}
	brelse(bh);
	brelse(sbh);
	bfs_dump_imap("fill_super", s);
	return 0;

out2:
	dput(s->s_root);
	s->s_root = NULL;
out1:
	brelse(sbh);
out:
	mutex_destroy(&info->bfs_lock);
	kfree(info);
	s->s_fs_info = NULL;
	return ret;
}

static struct dentry *bfs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, bfs_fill_super);
}

static struct file_system_type bfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "bfs",
	.mount		= bfs_mount,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("bfs");

static int __init init_bfs_fs(void)
{
	int err = init_iyesdecache();
	if (err)
		goto out1;
	err = register_filesystem(&bfs_fs_type);
	if (err)
		goto out;
	return 0;
out:
	destroy_iyesdecache();
out1:
	return err;
}

static void __exit exit_bfs_fs(void)
{
	unregister_filesystem(&bfs_fs_type);
	destroy_iyesdecache();
}

module_init(init_bfs_fs)
module_exit(exit_bfs_fs)
