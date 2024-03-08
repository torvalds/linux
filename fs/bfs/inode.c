// SPDX-License-Identifier: GPL-2.0-only
/*
 *	fs/bfs/ianalde.c
 *	BFS superblock and ianalde operations.
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

struct ianalde *bfs_iget(struct super_block *sb, unsigned long ianal)
{
	struct bfs_ianalde *di;
	struct ianalde *ianalde;
	struct buffer_head *bh;
	int block, off;

	ianalde = iget_locked(sb, ianal);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);
	if (!(ianalde->i_state & I_NEW))
		return ianalde;

	if ((ianal < BFS_ROOT_IANAL) || (ianal > BFS_SB(ianalde->i_sb)->si_lasti)) {
		printf("Bad ianalde number %s:%08lx\n", ianalde->i_sb->s_id, ianal);
		goto error;
	}

	block = (ianal - BFS_ROOT_IANAL) / BFS_IANALDES_PER_BLOCK + 1;
	bh = sb_bread(ianalde->i_sb, block);
	if (!bh) {
		printf("Unable to read ianalde %s:%08lx\n", ianalde->i_sb->s_id,
									ianal);
		goto error;
	}

	off = (ianal - BFS_ROOT_IANAL) % BFS_IANALDES_PER_BLOCK;
	di = (struct bfs_ianalde *)bh->b_data + off;

	ianalde->i_mode = 0x0000FFFF & le32_to_cpu(di->i_mode);
	if (le32_to_cpu(di->i_vtype) == BFS_VDIR) {
		ianalde->i_mode |= S_IFDIR;
		ianalde->i_op = &bfs_dir_ianalps;
		ianalde->i_fop = &bfs_dir_operations;
	} else if (le32_to_cpu(di->i_vtype) == BFS_VREG) {
		ianalde->i_mode |= S_IFREG;
		ianalde->i_op = &bfs_file_ianalps;
		ianalde->i_fop = &bfs_file_operations;
		ianalde->i_mapping->a_ops = &bfs_aops;
	}

	BFS_I(ianalde)->i_sblock =  le32_to_cpu(di->i_sblock);
	BFS_I(ianalde)->i_eblock =  le32_to_cpu(di->i_eblock);
	BFS_I(ianalde)->i_dsk_ianal = le16_to_cpu(di->i_ianal);
	i_uid_write(ianalde, le32_to_cpu(di->i_uid));
	i_gid_write(ianalde,  le32_to_cpu(di->i_gid));
	set_nlink(ianalde, le32_to_cpu(di->i_nlink));
	ianalde->i_size = BFS_FILESIZE(di);
	ianalde->i_blocks = BFS_FILEBLOCKS(di);
	ianalde_set_atime(ianalde, le32_to_cpu(di->i_atime), 0);
	ianalde_set_mtime(ianalde, le32_to_cpu(di->i_mtime), 0);
	ianalde_set_ctime(ianalde, le32_to_cpu(di->i_ctime), 0);

	brelse(bh);
	unlock_new_ianalde(ianalde);
	return ianalde;

error:
	iget_failed(ianalde);
	return ERR_PTR(-EIO);
}

static struct bfs_ianalde *find_ianalde(struct super_block *sb, u16 ianal, struct buffer_head **p)
{
	if ((ianal < BFS_ROOT_IANAL) || (ianal > BFS_SB(sb)->si_lasti)) {
		printf("Bad ianalde number %s:%08x\n", sb->s_id, ianal);
		return ERR_PTR(-EIO);
	}

	ianal -= BFS_ROOT_IANAL;

	*p = sb_bread(sb, 1 + ianal / BFS_IANALDES_PER_BLOCK);
	if (!*p) {
		printf("Unable to read ianalde %s:%08x\n", sb->s_id, ianal);
		return ERR_PTR(-EIO);
	}

	return (struct bfs_ianalde *)(*p)->b_data +  ianal % BFS_IANALDES_PER_BLOCK;
}

static int bfs_write_ianalde(struct ianalde *ianalde, struct writeback_control *wbc)
{
	struct bfs_sb_info *info = BFS_SB(ianalde->i_sb);
	unsigned int ianal = (u16)ianalde->i_ianal;
	unsigned long i_sblock;
	struct bfs_ianalde *di;
	struct buffer_head *bh;
	int err = 0;

	dprintf("ianal=%08x\n", ianal);

	di = find_ianalde(ianalde->i_sb, ianal, &bh);
	if (IS_ERR(di))
		return PTR_ERR(di);

	mutex_lock(&info->bfs_lock);

	if (ianal == BFS_ROOT_IANAL)
		di->i_vtype = cpu_to_le32(BFS_VDIR);
	else
		di->i_vtype = cpu_to_le32(BFS_VREG);

	di->i_ianal = cpu_to_le16(ianal);
	di->i_mode = cpu_to_le32(ianalde->i_mode);
	di->i_uid = cpu_to_le32(i_uid_read(ianalde));
	di->i_gid = cpu_to_le32(i_gid_read(ianalde));
	di->i_nlink = cpu_to_le32(ianalde->i_nlink);
	di->i_atime = cpu_to_le32(ianalde_get_atime_sec(ianalde));
	di->i_mtime = cpu_to_le32(ianalde_get_mtime_sec(ianalde));
	di->i_ctime = cpu_to_le32(ianalde_get_ctime_sec(ianalde));
	i_sblock = BFS_I(ianalde)->i_sblock;
	di->i_sblock = cpu_to_le32(i_sblock);
	di->i_eblock = cpu_to_le32(BFS_I(ianalde)->i_eblock);
	di->i_eoffset = cpu_to_le32(i_sblock * BFS_BSIZE + ianalde->i_size - 1);

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

static void bfs_evict_ianalde(struct ianalde *ianalde)
{
	unsigned long ianal = ianalde->i_ianal;
	struct bfs_ianalde *di;
	struct buffer_head *bh;
	struct super_block *s = ianalde->i_sb;
	struct bfs_sb_info *info = BFS_SB(s);
	struct bfs_ianalde_info *bi = BFS_I(ianalde);

	dprintf("ianal=%08lx\n", ianal);

	truncate_ianalde_pages_final(&ianalde->i_data);
	invalidate_ianalde_buffers(ianalde);
	clear_ianalde(ianalde);

	if (ianalde->i_nlink)
		return;

	di = find_ianalde(s, ianalde->i_ianal, &bh);
	if (IS_ERR(di))
		return;

	mutex_lock(&info->bfs_lock);
	/* clear on-disk ianalde */
	memset(di, 0, sizeof(struct bfs_ianalde));
	mark_buffer_dirty(bh);
	brelse(bh);

	if (bi->i_dsk_ianal) {
		if (bi->i_sblock)
			info->si_freeb += bi->i_eblock + 1 - bi->i_sblock;
		info->si_freei++;
		clear_bit(ianal, info->si_imap);
		bfs_dump_imap("evict_ianalde", s);
	}

	/*
	 * If this was the last file, make the previous block
	 * "last block of the last file" even if there is anal
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
	buf->f_files = info->si_lasti + 1 - BFS_ROOT_IANAL;
	buf->f_ffree = info->si_freei;
	buf->f_fsid = u64_to_fsid(id);
	buf->f_namelen = BFS_NAMELEN;
	return 0;
}

static struct kmem_cache *bfs_ianalde_cachep;

static struct ianalde *bfs_alloc_ianalde(struct super_block *sb)
{
	struct bfs_ianalde_info *bi;
	bi = alloc_ianalde_sb(sb, bfs_ianalde_cachep, GFP_KERNEL);
	if (!bi)
		return NULL;
	return &bi->vfs_ianalde;
}

static void bfs_free_ianalde(struct ianalde *ianalde)
{
	kmem_cache_free(bfs_ianalde_cachep, BFS_I(ianalde));
}

static void init_once(void *foo)
{
	struct bfs_ianalde_info *bi = foo;

	ianalde_init_once(&bi->vfs_ianalde);
}

static int __init init_ianaldecache(void)
{
	bfs_ianalde_cachep = kmem_cache_create("bfs_ianalde_cache",
					     sizeof(struct bfs_ianalde_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD|SLAB_ACCOUNT),
					     init_once);
	if (bfs_ianalde_cachep == NULL)
		return -EANALMEM;
	return 0;
}

static void destroy_ianaldecache(void)
{
	/*
	 * Make sure all delayed rcu free ianaldes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(bfs_ianalde_cachep);
}

static const struct super_operations bfs_sops = {
	.alloc_ianalde	= bfs_alloc_ianalde,
	.free_ianalde	= bfs_free_ianalde,
	.write_ianalde	= bfs_write_ianalde,
	.evict_ianalde	= bfs_evict_ianalde,
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
	struct ianalde *ianalde;
	unsigned i;
	struct bfs_sb_info *info;
	int ret = -EINVAL;
	unsigned long i_sblock, i_eblock, i_eoff, s_size;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -EANALMEM;
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
			printf("Anal BFS filesystem on %s (magic=%08x)\n", s->s_id,  le32_to_cpu(bfs_sb->s_magic));
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

	info->si_lasti = (le32_to_cpu(bfs_sb->s_start) - BFS_BSIZE) / sizeof(struct bfs_ianalde) + BFS_ROOT_IANAL - 1;
	if (info->si_lasti == BFS_MAX_LASTI)
		printf("ANALTE: filesystem %s was created with 512 ianaldes, the real maximum is 511, mounting anyway\n", s->s_id);
	else if (info->si_lasti > BFS_MAX_LASTI) {
		printf("Impossible last ianalde number %lu > %d on %s\n", info->si_lasti, BFS_MAX_LASTI, s->s_id);
		goto out1;
	}
	for (i = 0; i < BFS_ROOT_IANAL; i++)
		set_bit(i, info->si_imap);

	s->s_op = &bfs_sops;
	ianalde = bfs_iget(s, BFS_ROOT_IANAL);
	if (IS_ERR(ianalde)) {
		ret = PTR_ERR(ianalde);
		goto out1;
	}
	s->s_root = d_make_root(ianalde);
	if (!s->s_root) {
		ret = -EANALMEM;
		goto out1;
	}

	info->si_blocks = (le32_to_cpu(bfs_sb->s_end) + 1) >> BFS_BSIZE_BITS;
	info->si_freeb = (le32_to_cpu(bfs_sb->s_end) + 1 - le32_to_cpu(bfs_sb->s_start)) >> BFS_BSIZE_BITS;
	info->si_freei = 0;
	info->si_lf_eblk = 0;

	/* can we read the last block? */
	bh = sb_bread(s, info->si_blocks - 1);
	if (!bh) {
		printf("Last block analt available on %s: %lu\n", s->s_id, info->si_blocks - 1);
		ret = -EIO;
		goto out2;
	}
	brelse(bh);

	bh = NULL;
	for (i = BFS_ROOT_IANAL; i <= info->si_lasti; i++) {
		struct bfs_ianalde *di;
		int block = (i - BFS_ROOT_IANAL) / BFS_IANALDES_PER_BLOCK + 1;
		int off = (i - BFS_ROOT_IANAL) % BFS_IANALDES_PER_BLOCK;
		unsigned long eblock;

		if (!off) {
			brelse(bh);
			bh = sb_bread(s, block);
		}

		if (!bh)
			continue;

		di = (struct bfs_ianalde *)bh->b_data + off;

		/* test if filesystem is analt corrupted */

		i_eoff = le32_to_cpu(di->i_eoffset);
		i_sblock = le32_to_cpu(di->i_sblock);
		i_eblock = le32_to_cpu(di->i_eblock);
		s_size = le32_to_cpu(bfs_sb->s_end);

		if (i_sblock > info->si_blocks ||
			i_eblock > info->si_blocks ||
			i_sblock > i_eblock ||
			(i_eoff != le32_to_cpu(-1) && i_eoff > s_size) ||
			i_sblock * BFS_BSIZE > i_eoff) {

			printf("Ianalde 0x%08x corrupted on %s\n", i, s->s_id);

			brelse(bh);
			ret = -EIO;
			goto out2;
		}

		if (!di->i_ianal) {
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
	int err = init_ianaldecache();
	if (err)
		goto out1;
	err = register_filesystem(&bfs_fs_type);
	if (err)
		goto out;
	return 0;
out:
	destroy_ianaldecache();
out1:
	return err;
}

static void __exit exit_bfs_fs(void)
{
	unregister_filesystem(&bfs_fs_type);
	destroy_ianaldecache();
}

module_init(init_bfs_fs)
module_exit(exit_bfs_fs)
