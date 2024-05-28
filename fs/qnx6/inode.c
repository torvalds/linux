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
static struct inode *qnx6_alloc_inode(struct super_block *sb);
static void qnx6_free_inode(struct inode *inode);
static int qnx6_remount(struct super_block *sb, int *flags, char *data);
static int qnx6_statfs(struct dentry *dentry, struct kstatfs *buf);
static int qnx6_show_options(struct seq_file *seq, struct dentry *root);

static const struct super_operations qnx6_sops = {
	.alloc_inode	= qnx6_alloc_inode,
	.free_inode	= qnx6_free_inode,
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

static unsigned qnx6_block_map(struct inode *inode, unsigned iblock);

static int qnx6_get_block(struct inode *inode, sector_t iblock,
			struct buffer_head *bh, int create)
{
	unsigned phys;

	pr_debug("qnx6_get_block inode=[%ld] iblock=[%ld]\n",
		 inode->i_ino, (unsigned long)iblock);

	phys = qnx6_block_map(inode, iblock);
	if (phys) {
		/* logical block is before EOF */
		map_bh(bh, inode->i_sb, phys);
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
 * returns the block number for the no-th element in the tree
 * inodebits requred as there are multiple inodes in one inode block
 */
static unsigned qnx6_block_map(struct inode *inode, unsigned no)
{
	struct super_block *s = inode->i_sb;
	struct qnx6_sb_info *sbi = QNX6_SB(s);
	struct qnx6_inode_info *ei = QNX6_I(inode);
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
	levelptr = no >> bitdelta;

	if (levelptr > QNX6_NO_DIRECT_POINTERS - 1) {
		pr_err("Requested file block number (%u) too big.", no);
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
		levelptr = (no >> bitdelta) & mask;
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
	buf->f_files   = fs32_to_cpu(sbi, sbi->sb->sb_num_inodes);
	buf->f_ffree   = fs32_to_cpu(sbi, sbi->sb->sb_free_inodes);
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
	struct inode *root = d_inode(s->s_root);
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
	pr_debug("num_inodes: %08x\n", fs32_to_cpu(sbi, sb->sb_num_inodes));
	pr_debug("free_inodes: %08x\n", fs32_to_cpu(sbi, sb->sb_free_inodes));
	pr_debug("num_blocks: %08x\n", fs32_to_cpu(sbi, sb->sb_num_blocks));
	pr_debug("free_blocks: %08x\n", fs32_to_cpu(sbi, sb->sb_free_blocks));
	pr_debug("inode_levels: %02x\n", sb->Inode.levels);
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

static struct inode *qnx6_private_inode(struct super_block *s,
					struct qnx6_root_node *p);

static int qnx6_fill_super(struct super_block *s, void *data, int silent)
{
	struct buffer_head *bh1 = NULL, *bh2 = NULL;
	struct qnx6_super_block *sb1 = NULL, *sb2 = NULL;
	struct qnx6_sb_info *sbi;
	struct inode *root;
	const char *errmsg;
	struct qnx6_sb_info *qs;
	int ret = -EINVAL;
	u64 offset;
	int bootblock_offset = QNX6_BOOTBLOCK_SIZE;

	qs = kzalloc(sizeof(struct qnx6_sb_info), GFP_KERNEL);
	if (!qs)
		return -ENOMEM;
	s->s_fs_info = qs;

	/* Superblock always is 512 Byte long */
	if (!sb_set_blocksize(s, QNX6_SUPERBLOCK_SIZE)) {
		pr_err("unable to set blocksize\n");
		goto outnobh;
	}

	/* parse the mount-options */
	if (!qnx6_parse_options((char *) data, s)) {
		pr_err("invalid mount options.\n");
		goto outnobh;
	}
	if (test_opt(s, MMI_FS)) {
		sb1 = qnx6_mmi_fill_super(s, silent);
		if (sb1)
			goto mmi_success;
		else
			goto outnobh;
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
			goto outnobh;
		}
		/* seems that no bootblock at partition start */
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
		goto outnobh;
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
	if (sb1->Inode.levels > QNX6_PTR_MAX_LEVELS) {
		pr_err("too many inode levels (max %i, sb %i)\n",
		       QNX6_PTR_MAX_LEVELS, sb1->Inode.levels);
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
	sbi->inodes = qnx6_private_inode(s, &sb1->Inode);
	if (!sbi->inodes)
		goto out;
	sbi->longfile = qnx6_private_inode(s, &sb1->Longfile);
	if (!sbi->longfile)
		goto out1;

	/* prefetch root inode */
	root = qnx6_iget(s, QNX6_ROOT_INO);
	if (IS_ERR(root)) {
		pr_err("get inode failed\n");
		ret = PTR_ERR(root);
		goto out2;
	}

	ret = -ENOMEM;
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
	iput(sbi->inodes);
out:
	brelse(bh1);
	brelse(bh2);
outnobh:
	kfree(qs);
	s->s_fs_info = NULL;
	return ret;
}

static void qnx6_put_super(struct super_block *sb)
{
	struct qnx6_sb_info *qs = QNX6_SB(sb);
	brelse(qs->sb_buf);
	iput(qs->longfile);
	iput(qs->inodes);
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

static struct inode *qnx6_private_inode(struct super_block *s,
					struct qnx6_root_node *p)
{
	struct inode *inode = new_inode(s);
	if (inode) {
		struct qnx6_inode_info *ei = QNX6_I(inode);
		struct qnx6_sb_info *sbi = QNX6_SB(s);
		inode->i_size = fs64_to_cpu(sbi, p->size);
		memcpy(ei->di_block_ptr, p->ptr, sizeof(p->ptr));
		ei->di_filelevels = p->levels;
		inode->i_mode = S_IFREG | S_IRUSR; /* probably wrong */
		inode->i_mapping->a_ops = &qnx6_aops;
	}
	return inode;
}

struct inode *qnx6_iget(struct super_block *sb, unsigned ino)
{
	struct qnx6_sb_info *sbi = QNX6_SB(sb);
	struct qnx6_inode_entry *raw_inode;
	struct inode *inode;
	struct qnx6_inode_info	*ei;
	struct address_space *mapping;
	struct page *page;
	u32 n, offs;

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	ei = QNX6_I(inode);

	inode->i_mode = 0;

	if (ino == 0) {
		pr_err("bad inode number on dev %s: %u is out of range\n",
		       sb->s_id, ino);
		iget_failed(inode);
		return ERR_PTR(-EIO);
	}
	n = (ino - 1) >> (PAGE_SHIFT - QNX6_INODE_SIZE_BITS);
	offs = (ino - 1) & (~PAGE_MASK >> QNX6_INODE_SIZE_BITS);
	mapping = sbi->inodes->i_mapping;
	page = read_mapping_page(mapping, n, NULL);
	if (IS_ERR(page)) {
		pr_err("major problem: unable to read inode from dev %s\n",
		       sb->s_id);
		iget_failed(inode);
		return ERR_CAST(page);
	}
	kmap(page);
	raw_inode = ((struct qnx6_inode_entry *)page_address(page)) + offs;

	inode->i_mode    = fs16_to_cpu(sbi, raw_inode->di_mode);
	i_uid_write(inode, (uid_t)fs32_to_cpu(sbi, raw_inode->di_uid));
	i_gid_write(inode, (gid_t)fs32_to_cpu(sbi, raw_inode->di_gid));
	inode->i_size    = fs64_to_cpu(sbi, raw_inode->di_size);
	inode_set_mtime(inode, fs32_to_cpu(sbi, raw_inode->di_mtime), 0);
	inode_set_atime(inode, fs32_to_cpu(sbi, raw_inode->di_atime), 0);
	inode_set_ctime(inode, fs32_to_cpu(sbi, raw_inode->di_ctime), 0);

	/* calc blocks based on 512 byte blocksize */
	inode->i_blocks = (inode->i_size + 511) >> 9;

	memcpy(&ei->di_block_ptr, &raw_inode->di_block_ptr,
				sizeof(raw_inode->di_block_ptr));
	ei->di_filelevels = raw_inode->di_filelevels;

	if (S_ISREG(inode->i_mode)) {
		inode->i_fop = &generic_ro_fops;
		inode->i_mapping->a_ops = &qnx6_aops;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &qnx6_dir_inode_operations;
		inode->i_fop = &qnx6_dir_operations;
		inode->i_mapping->a_ops = &qnx6_aops;
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &page_symlink_inode_operations;
		inode_nohighmem(inode);
		inode->i_mapping->a_ops = &qnx6_aops;
	} else
		init_special_inode(inode, inode->i_mode, 0);
	qnx6_put_page(page);
	unlock_new_inode(inode);
	return inode;
}

static struct kmem_cache *qnx6_inode_cachep;

static struct inode *qnx6_alloc_inode(struct super_block *sb)
{
	struct qnx6_inode_info *ei;
	ei = alloc_inode_sb(sb, qnx6_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

static void qnx6_free_inode(struct inode *inode)
{
	kmem_cache_free(qnx6_inode_cachep, QNX6_I(inode));
}

static void init_once(void *foo)
{
	struct qnx6_inode_info *ei = (struct qnx6_inode_info *) foo;

	inode_init_once(&ei->vfs_inode);
}

static int init_inodecache(void)
{
	qnx6_inode_cachep = kmem_cache_create("qnx6_inode_cache",
					     sizeof(struct qnx6_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_ACCOUNT),
					     init_once);
	if (!qnx6_inode_cachep)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(qnx6_inode_cachep);
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

	err = init_inodecache();
	if (err)
		return err;

	err = register_filesystem(&qnx6_fs_type);
	if (err) {
		destroy_inodecache();
		return err;
	}

	pr_info("QNX6 filesystem 1.0.0 registered.\n");
	return 0;
}

static void __exit exit_qnx6_fs(void)
{
	unregister_filesystem(&qnx6_fs_type);
	destroy_inodecache();
}

module_init(init_qnx6_fs)
module_exit(exit_qnx6_fs)
MODULE_LICENSE("GPL");
