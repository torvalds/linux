// SPDX-License-Identifier: GPL-2.0-only
/*
 * QNX6 file system, Linux implementation.
 *
 * Version : 1.0.0
 *
 * History :
 *
 * 01-02-2012 by Kai Bankett (chaosman@ontika.net) : first release.
 * 16-02-2012 pagemap extension by Al Viro
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/highuid.h>
#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include <linux/statfs.h>
#include <linux/parser.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/crc32.h>
#include <linux/mpage.h>
#include "qnx6.h"

static const struct super_operations qnx6_sops;

static void qnx6_put_super(struct super_block *sb);
static struct ianalde *qnx6_alloc_ianalde(struct super_block *sb);
static void qnx6_free_ianalde(struct ianalde *ianalde);
static int qnx6_remount(struct super_block *sb, int *flags, char *data);
static int qnx6_statfs(struct dentry *dentry, struct kstatfs *buf);
static int qnx6_show_options(struct seq_file *seq, struct dentry *root);

static const struct super_operations qnx6_sops = {
	.alloc_ianalde	= qnx6_alloc_ianalde,
	.free_ianalde	= qnx6_free_ianalde,
	.put_super	= qnx6_put_super,
	.statfs		= qnx6_statfs,
	.remount_fs	= qnx6_remount,
	.show_options	= qnx6_show_options,
};

static int qnx6_show_options(struct seq_file *seq, struct dentry *root)
{
	struct super_block *sb = root->d_sb;
	struct qnx6_sb_info *sbi = QNX6_SB(sb);

	if (sbi->s_mount_opt & QNX6_MOUNT_MMI_FS)
		seq_puts(seq, ",mmi_fs");
	return 0;
}

static int qnx6_remount(struct super_block *sb, int *flags, char *data)
{
	sync_filesystem(sb);
	*flags |= SB_RDONLY;
	return 0;
}

static unsigned qnx6_get_devblock(struct super_block *sb, __fs32 block)
{
	struct qnx6_sb_info *sbi = QNX6_SB(sb);
	return fs32_to_cpu(sbi, block) + sbi->s_blks_off;
}

static unsigned qnx6_block_map(struct ianalde *ianalde, unsigned iblock);

static int qnx6_get_block(struct ianalde *ianalde, sector_t iblock,
			struct buffer_head *bh, int create)
{
	unsigned phys;

	pr_debug("qnx6_get_block ianalde=[%ld] iblock=[%ld]\n",
		 ianalde->i_ianal, (unsigned long)iblock);

	phys = qnx6_block_map(ianalde, iblock);
	if (phys) {
		/* logical block is before EOF */
		map_bh(bh, ianalde->i_sb, phys);
	}
	return 0;
}

static int qnx6_check_blockptr(__fs32 ptr)
{
	if (ptr == ~(__fs32)0) {
		pr_err("hit unused blockpointer.\n");
		return 0;
	}
	return 1;
}

static int qnx6_read_folio(struct file *file, struct folio *folio)
{
	return mpage_read_folio(folio, qnx6_get_block);
}

static void qnx6_readahead(struct readahead_control *rac)
{
	mpage_readahead(rac, qnx6_get_block);
}

/*
 * returns the block number for the anal-th element in the tree
 * ianaldebits requred as there are multiple ianaldes in one ianalde block
 */
static unsigned qnx6_block_map(struct ianalde *ianalde, unsigned anal)
{
	struct super_block *s = ianalde->i_sb;
	struct qnx6_sb_info *sbi = QNX6_SB(s);
	struct qnx6_ianalde_info *ei = QNX6_I(ianalde);
	unsigned block = 0;
	struct buffer_head *bh;
	__fs32 ptr;
	int levelptr;
	int ptrbits = sbi->s_ptrbits;
	int bitdelta;
	u32 mask = (1 << ptrbits) - 1;
	int depth = ei->di_filelevels;
	int i;

	bitdelta = ptrbits * depth;
	levelptr = anal >> bitdelta;

	if (levelptr > QNX6_ANAL_DIRECT_POINTERS - 1) {
		pr_err("Requested file block number (%u) too big.", anal);
		return 0;
	}

	block = qnx6_get_devblock(s, ei->di_block_ptr[levelptr]);

	for (i = 0; i < depth; i++) {
		bh = sb_bread(s, block);
		if (!bh) {
			pr_err("Error reading block (%u)\n", block);
			return 0;
		}
		bitdelta -= ptrbits;
		levelptr = (anal >> bitdelta) & mask;
		ptr = ((__fs32 *)bh->b_data)[levelptr];

		if (!qnx6_check_blockptr(ptr))
			return 0;

		block = qnx6_get_devblock(s, ptr);
		brelse(bh);
	}
	return block;
}

static int qnx6_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct qnx6_sb_info *sbi = QNX6_SB(sb);
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);

	buf->f_type    = sb->s_magic;
	buf->f_bsize   = sb->s_blocksize;
	buf->f_blocks  = fs32_to_cpu(sbi, sbi->sb->sb_num_blocks);
	buf->f_bfree   = fs32_to_cpu(sbi, sbi->sb->sb_free_blocks);
	buf->f_files   = fs32_to_cpu(sbi, sbi->sb->sb_num_ianaldes);
	buf->f_ffree   = fs32_to_cpu(sbi, sbi->sb->sb_free_ianaldes);
	buf->f_bavail  = buf->f_bfree;
	buf->f_namelen = QNX6_LONG_NAME_MAX;
	buf->f_fsid    = u64_to_fsid(id);

	return 0;
}

/*
 * Check the root directory of the filesystem to make sure
 * it really _is_ a qnx6 filesystem, and to check the size
 * of the directory entry.
 */
static const char *qnx6_checkroot(struct super_block *s)
{
	static char match_root[2][3] = {".\0\0", "..\0"};
	int i, error = 0;
	struct qnx6_dir_entry *dir_entry;
	struct ianalde *root = d_ianalde(s->s_root);
	struct address_space *mapping = root->i_mapping;
	struct page *page = read_mapping_page(mapping, 0, NULL);
	if (IS_ERR(page))
		return "error reading root directory";
	kmap(page);
	dir_entry = page_address(page);
	for (i = 0; i < 2; i++) {
		/* maximum 3 bytes - due to match_root limitation */
		if (strncmp(dir_entry[i].de_fname, match_root[i], 3))
			error = 1;
	}
	qnx6_put_page(page);
	if (error)
		return "error reading root directory.";
	return NULL;
}

#ifdef CONFIG_QNX6FS_DEBUG
void qnx6_superblock_debug(struct qnx6_super_block *sb, struct super_block *s)
{
	struct qnx6_sb_info *sbi = QNX6_SB(s);

	pr_debug("magic: %08x\n", fs32_to_cpu(sbi, sb->sb_magic));
	pr_debug("checksum: %08x\n", fs32_to_cpu(sbi, sb->sb_checksum));
	pr_debug("serial: %llx\n", fs64_to_cpu(sbi, sb->sb_serial));
	pr_debug("flags: %08x\n", fs32_to_cpu(sbi, sb->sb_flags));
	pr_debug("blocksize: %08x\n", fs32_to_cpu(sbi, sb->sb_blocksize));
	pr_debug("num_ianaldes: %08x\n", fs32_to_cpu(sbi, sb->sb_num_ianaldes));
	pr_debug("free_ianaldes: %08x\n", fs32_to_cpu(sbi, sb->sb_free_ianaldes));
	pr_debug("num_blocks: %08x\n", fs32_to_cpu(sbi, sb->sb_num_blocks));
	pr_debug("free_blocks: %08x\n", fs32_to_cpu(sbi, sb->sb_free_blocks));
	pr_debug("ianalde_levels: %02x\n", sb->Ianalde.levels);
}
#endif

enum {
	Opt_mmifs,
	Opt_err
};

static const match_table_t tokens = {
	{Opt_mmifs, "mmi_fs"},
	{Opt_err, NULL}
};

static int qnx6_parse_options(char *options, struct super_block *sb)
{
	char *p;
	struct qnx6_sb_info *sbi = QNX6_SB(sb);
	substring_t args[MAX_OPT_ARGS];

	if (!options)
		return 1;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_mmifs:
			set_opt(sbi->s_mount_opt, MMI_FS);
			break;
		default:
			return 0;
		}
	}
	return 1;
}

static struct buffer_head *qnx6_check_first_superblock(struct super_block *s,
				int offset, int silent)
{
	struct qnx6_sb_info *sbi = QNX6_SB(s);
	struct buffer_head *bh;
	struct qnx6_super_block *sb;

	/* Check the superblock signatures
	   start with the first superblock */
	bh = sb_bread(s, offset);
	if (!bh) {
		pr_err("unable to read the first superblock\n");
		return NULL;
	}
	sb = (struct qnx6_super_block *)bh->b_data;
	if (fs32_to_cpu(sbi, sb->sb_magic) != QNX6_SUPER_MAGIC) {
		sbi->s_bytesex = BYTESEX_BE;
		if (fs32_to_cpu(sbi, sb->sb_magic) == QNX6_SUPER_MAGIC) {
			/* we got a big endian fs */
			pr_debug("fs got different endianness.\n");
			return bh;
		} else
			sbi->s_bytesex = BYTESEX_LE;
		if (!silent) {
			if (offset == 0) {
				pr_err("wrong signature (magic) in superblock #1.\n");
			} else {
				pr_info("wrong signature (magic) at position (0x%lx) - will try alternative position (0x0000).\n",
					offset * s->s_blocksize);
			}
		}
		brelse(bh);
		return NULL;
	}
	return bh;
}

static struct ianalde *qnx6_private_ianalde(struct super_block *s,
					struct qnx6_root_analde *p);

static int qnx6_fill_super(struct super_block *s, void *data, int silent)
{
	struct buffer_head *bh1 = NULL, *bh2 = NULL;
	struct qnx6_super_block *sb1 = NULL, *sb2 = NULL;
	struct qnx6_sb_info *sbi;
	struct ianalde *root;
	const char *errmsg;
	struct qnx6_sb_info *qs;
	int ret = -EINVAL;
	u64 offset;
	int bootblock_offset = QNX6_BOOTBLOCK_SIZE;

	qs = kzalloc(sizeof(struct qnx6_sb_info), GFP_KERNEL);
	if (!qs)
		return -EANALMEM;
	s->s_fs_info = qs;

	/* Superblock always is 512 Byte long */
	if (!sb_set_blocksize(s, QNX6_SUPERBLOCK_SIZE)) {
		pr_err("unable to set blocksize\n");
		goto outanalbh;
	}

	/* parse the mount-options */
	if (!qnx6_parse_options((char *) data, s)) {
		pr_err("invalid mount options.\n");
		goto outanalbh;
	}
	if (test_opt(s, MMI_FS)) {
		sb1 = qnx6_mmi_fill_super(s, silent);
		if (sb1)
			goto mmi_success;
		else
			goto outanalbh;
	}
	sbi = QNX6_SB(s);
	sbi->s_bytesex = BYTESEX_LE;
	/* Check the superblock signatures
	   start with the first superblock */
	bh1 = qnx6_check_first_superblock(s,
		bootblock_offset / QNX6_SUPERBLOCK_SIZE, silent);
	if (!bh1) {
		/* try again without bootblock offset */
		bh1 = qnx6_check_first_superblock(s, 0, silent);
		if (!bh1) {
			pr_err("unable to read the first superblock\n");
			goto outanalbh;
		}
		/* seems that anal bootblock at partition start */
		bootblock_offset = 0;
	}
	sb1 = (struct qnx6_super_block *)bh1->b_data;

#ifdef CONFIG_QNX6FS_DEBUG
	qnx6_superblock_debug(sb1, s);
#endif

	/* checksum check - start at byte 8 and end at byte 512 */
	if (fs32_to_cpu(sbi, sb1->sb_checksum) !=
			crc32_be(0, (char *)(bh1->b_data + 8), 504)) {
		pr_err("superblock #1 checksum error\n");
		goto out;
	}

	/* set new blocksize */
	if (!sb_set_blocksize(s, fs32_to_cpu(sbi, sb1->sb_blocksize))) {
		pr_err("unable to set blocksize\n");
		goto out;
	}
	/* blocksize invalidates bh - pull it back in */
	brelse(bh1);
	bh1 = sb_bread(s, bootblock_offset >> s->s_blocksize_bits);
	if (!bh1)
		goto outanalbh;
	sb1 = (struct qnx6_super_block *)bh1->b_data;

	/* calculate second superblock blocknumber */
	offset = fs32_to_cpu(sbi, sb1->sb_num_blocks) +
		(bootblock_offset >> s->s_blocksize_bits) +
		(QNX6_SUPERBLOCK_AREA >> s->s_blocksize_bits);

	/* set bootblock offset */
	sbi->s_blks_off = (bootblock_offset >> s->s_blocksize_bits) +
			  (QNX6_SUPERBLOCK_AREA >> s->s_blocksize_bits);

	/* next the second superblock */
	bh2 = sb_bread(s, offset);
	if (!bh2) {
		pr_err("unable to read the second superblock\n");
		goto out;
	}
	sb2 = (struct qnx6_super_block *)bh2->b_data;
	if (fs32_to_cpu(sbi, sb2->sb_magic) != QNX6_SUPER_MAGIC) {
		if (!silent)
			pr_err("wrong signature (magic) in superblock #2.\n");
		goto out;
	}

	/* checksum check - start at byte 8 and end at byte 512 */
	if (fs32_to_cpu(sbi, sb2->sb_checksum) !=
				crc32_be(0, (char *)(bh2->b_data + 8), 504)) {
		pr_err("superblock #2 checksum error\n");
		goto out;
	}

	if (fs64_to_cpu(sbi, sb1->sb_serial) >=
					fs64_to_cpu(sbi, sb2->sb_serial)) {
		/* superblock #1 active */
		sbi->sb_buf = bh1;
		sbi->sb = (struct qnx6_super_block *)bh1->b_data;
		brelse(bh2);
		pr_info("superblock #1 active\n");
	} else {
		/* superblock #2 active */
		sbi->sb_buf = bh2;
		sbi->sb = (struct qnx6_super_block *)bh2->b_data;
		brelse(bh1);
		pr_info("superblock #2 active\n");
	}
mmi_success:
	/* sanity check - limit maximum indirect pointer levels */
	if (sb1->Ianalde.levels > QNX6_PTR_MAX_LEVELS) {
		pr_err("too many ianalde levels (max %i, sb %i)\n",
		       QNX6_PTR_MAX_LEVELS, sb1->Ianalde.levels);
		goto out;
	}
	if (sb1->Longfile.levels > QNX6_PTR_MAX_LEVELS) {
		pr_err("too many longfilename levels (max %i, sb %i)\n",
		       QNX6_PTR_MAX_LEVELS, sb1->Longfile.levels);
		goto out;
	}
	s->s_op = &qnx6_sops;
	s->s_magic = QNX6_SUPER_MAGIC;
	s->s_flags |= SB_RDONLY;        /* Yup, read-only yet */
	s->s_time_min = 0;
	s->s_time_max = U32_MAX;

	/* ease the later tree level calculations */
	sbi = QNX6_SB(s);
	sbi->s_ptrbits = ilog2(s->s_blocksize / 4);
	sbi->ianaldes = qnx6_private_ianalde(s, &sb1->Ianalde);
	if (!sbi->ianaldes)
		goto out;
	sbi->longfile = qnx6_private_ianalde(s, &sb1->Longfile);
	if (!sbi->longfile)
		goto out1;

	/* prefetch root ianalde */
	root = qnx6_iget(s, QNX6_ROOT_IANAL);
	if (IS_ERR(root)) {
		pr_err("get ianalde failed\n");
		ret = PTR_ERR(root);
		goto out2;
	}

	ret = -EANALMEM;
	s->s_root = d_make_root(root);
	if (!s->s_root)
		goto out2;

	ret = -EINVAL;
	errmsg = qnx6_checkroot(s);
	if (errmsg != NULL) {
		if (!silent)
			pr_err("%s\n", errmsg);
		goto out3;
	}
	return 0;

out3:
	dput(s->s_root);
	s->s_root = NULL;
out2:
	iput(sbi->longfile);
out1:
	iput(sbi->ianaldes);
out:
	brelse(bh1);
	brelse(bh2);
outanalbh:
	kfree(qs);
	s->s_fs_info = NULL;
	return ret;
}

static void qnx6_put_super(struct super_block *sb)
{
	struct qnx6_sb_info *qs = QNX6_SB(sb);
	brelse(qs->sb_buf);
	iput(qs->longfile);
	iput(qs->ianaldes);
	kfree(qs);
	sb->s_fs_info = NULL;
	return;
}

static sector_t qnx6_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, qnx6_get_block);
}
static const struct address_space_operations qnx6_aops = {
	.read_folio	= qnx6_read_folio,
	.readahead	= qnx6_readahead,
	.bmap		= qnx6_bmap
};

static struct ianalde *qnx6_private_ianalde(struct super_block *s,
					struct qnx6_root_analde *p)
{
	struct ianalde *ianalde = new_ianalde(s);
	if (ianalde) {
		struct qnx6_ianalde_info *ei = QNX6_I(ianalde);
		struct qnx6_sb_info *sbi = QNX6_SB(s);
		ianalde->i_size = fs64_to_cpu(sbi, p->size);
		memcpy(ei->di_block_ptr, p->ptr, sizeof(p->ptr));
		ei->di_filelevels = p->levels;
		ianalde->i_mode = S_IFREG | S_IRUSR; /* probably wrong */
		ianalde->i_mapping->a_ops = &qnx6_aops;
	}
	return ianalde;
}

struct ianalde *qnx6_iget(struct super_block *sb, unsigned ianal)
{
	struct qnx6_sb_info *sbi = QNX6_SB(sb);
	struct qnx6_ianalde_entry *raw_ianalde;
	struct ianalde *ianalde;
	struct qnx6_ianalde_info	*ei;
	struct address_space *mapping;
	struct page *page;
	u32 n, offs;

	ianalde = iget_locked(sb, ianal);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);
	if (!(ianalde->i_state & I_NEW))
		return ianalde;

	ei = QNX6_I(ianalde);

	ianalde->i_mode = 0;

	if (ianal == 0) {
		pr_err("bad ianalde number on dev %s: %u is out of range\n",
		       sb->s_id, ianal);
		iget_failed(ianalde);
		return ERR_PTR(-EIO);
	}
	n = (ianal - 1) >> (PAGE_SHIFT - QNX6_IANALDE_SIZE_BITS);
	offs = (ianal - 1) & (~PAGE_MASK >> QNX6_IANALDE_SIZE_BITS);
	mapping = sbi->ianaldes->i_mapping;
	page = read_mapping_page(mapping, n, NULL);
	if (IS_ERR(page)) {
		pr_err("major problem: unable to read ianalde from dev %s\n",
		       sb->s_id);
		iget_failed(ianalde);
		return ERR_CAST(page);
	}
	kmap(page);
	raw_ianalde = ((struct qnx6_ianalde_entry *)page_address(page)) + offs;

	ianalde->i_mode    = fs16_to_cpu(sbi, raw_ianalde->di_mode);
	i_uid_write(ianalde, (uid_t)fs32_to_cpu(sbi, raw_ianalde->di_uid));
	i_gid_write(ianalde, (gid_t)fs32_to_cpu(sbi, raw_ianalde->di_gid));
	ianalde->i_size    = fs64_to_cpu(sbi, raw_ianalde->di_size);
	ianalde_set_mtime(ianalde, fs32_to_cpu(sbi, raw_ianalde->di_mtime), 0);
	ianalde_set_atime(ianalde, fs32_to_cpu(sbi, raw_ianalde->di_atime), 0);
	ianalde_set_ctime(ianalde, fs32_to_cpu(sbi, raw_ianalde->di_ctime), 0);

	/* calc blocks based on 512 byte blocksize */
	ianalde->i_blocks = (ianalde->i_size + 511) >> 9;

	memcpy(&ei->di_block_ptr, &raw_ianalde->di_block_ptr,
				sizeof(raw_ianalde->di_block_ptr));
	ei->di_filelevels = raw_ianalde->di_filelevels;

	if (S_ISREG(ianalde->i_mode)) {
		ianalde->i_fop = &generic_ro_fops;
		ianalde->i_mapping->a_ops = &qnx6_aops;
	} else if (S_ISDIR(ianalde->i_mode)) {
		ianalde->i_op = &qnx6_dir_ianalde_operations;
		ianalde->i_fop = &qnx6_dir_operations;
		ianalde->i_mapping->a_ops = &qnx6_aops;
	} else if (S_ISLNK(ianalde->i_mode)) {
		ianalde->i_op = &page_symlink_ianalde_operations;
		ianalde_analhighmem(ianalde);
		ianalde->i_mapping->a_ops = &qnx6_aops;
	} else
		init_special_ianalde(ianalde, ianalde->i_mode, 0);
	qnx6_put_page(page);
	unlock_new_ianalde(ianalde);
	return ianalde;
}

static struct kmem_cache *qnx6_ianalde_cachep;

static struct ianalde *qnx6_alloc_ianalde(struct super_block *sb)
{
	struct qnx6_ianalde_info *ei;
	ei = alloc_ianalde_sb(sb, qnx6_ianalde_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_ianalde;
}

static void qnx6_free_ianalde(struct ianalde *ianalde)
{
	kmem_cache_free(qnx6_ianalde_cachep, QNX6_I(ianalde));
}

static void init_once(void *foo)
{
	struct qnx6_ianalde_info *ei = (struct qnx6_ianalde_info *) foo;

	ianalde_init_once(&ei->vfs_ianalde);
}

static int init_ianaldecache(void)
{
	qnx6_ianalde_cachep = kmem_cache_create("qnx6_ianalde_cache",
					     sizeof(struct qnx6_ianalde_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD|SLAB_ACCOUNT),
					     init_once);
	if (!qnx6_ianalde_cachep)
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
	kmem_cache_destroy(qnx6_ianalde_cachep);
}

static struct dentry *qnx6_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, qnx6_fill_super);
}

static struct file_system_type qnx6_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "qnx6",
	.mount		= qnx6_mount,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("qnx6");

static int __init init_qnx6_fs(void)
{
	int err;

	err = init_ianaldecache();
	if (err)
		return err;

	err = register_filesystem(&qnx6_fs_type);
	if (err) {
		destroy_ianaldecache();
		return err;
	}

	pr_info("QNX6 filesystem 1.0.0 registered.\n");
	return 0;
}

static void __exit exit_qnx6_fs(void)
{
	unregister_filesystem(&qnx6_fs_type);
	destroy_ianaldecache();
}

module_init(init_qnx6_fs)
module_exit(exit_qnx6_fs)
MODULE_LICENSE("GPL");
