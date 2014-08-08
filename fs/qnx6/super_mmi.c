/*
 * QNX6 file system, Linux implementation.
 *
 * Version : 1.0.0
 *
 * History :
 *
 * 01-02-2012 by Kai Bankett (chaosman@ontika.net) : first release.
 *
 */

#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/crc32.h>
#include "qnx6.h"

static void qnx6_mmi_copy_sb(struct qnx6_super_block *qsb,
		struct qnx6_mmi_super_block *sb)
{
	qsb->sb_magic = sb->sb_magic;
	qsb->sb_checksum = sb->sb_checksum;
	qsb->sb_serial = sb->sb_serial;
	qsb->sb_blocksize = sb->sb_blocksize;
	qsb->sb_num_inodes = sb->sb_num_inodes;
	qsb->sb_free_inodes = sb->sb_free_inodes;
	qsb->sb_num_blocks = sb->sb_num_blocks;
	qsb->sb_free_blocks = sb->sb_free_blocks;

	/* the rest of the superblock is the same */
	memcpy(&qsb->Inode, &sb->Inode, sizeof(sb->Inode));
	memcpy(&qsb->Bitmap, &sb->Bitmap, sizeof(sb->Bitmap));
	memcpy(&qsb->Longfile, &sb->Longfile, sizeof(sb->Longfile));
}

struct qnx6_super_block *qnx6_mmi_fill_super(struct super_block *s, int silent)
{
	struct buffer_head *bh1, *bh2 = NULL;
	struct qnx6_mmi_super_block *sb1, *sb2;
	struct qnx6_super_block *qsb = NULL;
	struct qnx6_sb_info *sbi;
	__u64 offset;

	/* Check the superblock signatures
	   start with the first superblock */
	bh1 = sb_bread(s, 0);
	if (!bh1) {
		pr_err("qnx6: Unable to read first mmi superblock\n");
		return NULL;
	}
	sb1 = (struct qnx6_mmi_super_block *)bh1->b_data;
	sbi = QNX6_SB(s);
	if (fs32_to_cpu(sbi, sb1->sb_magic) != QNX6_SUPER_MAGIC) {
		if (!silent) {
			pr_err("qnx6: wrong signature (magic) in superblock #1.\n");
			goto out;
		}
	}

	/* checksum check - start at byte 8 and end at byte 512 */
	if (fs32_to_cpu(sbi, sb1->sb_checksum) !=
				crc32_be(0, (char *)(bh1->b_data + 8), 504)) {
		pr_err("qnx6: superblock #1 checksum error\n");
		goto out;
	}

	/* calculate second superblock blocknumber */
	offset = fs32_to_cpu(sbi, sb1->sb_num_blocks) + QNX6_SUPERBLOCK_AREA /
					fs32_to_cpu(sbi, sb1->sb_blocksize);

	/* set new blocksize */
	if (!sb_set_blocksize(s, fs32_to_cpu(sbi, sb1->sb_blocksize))) {
		pr_err("qnx6: unable to set blocksize\n");
		goto out;
	}
	/* blocksize invalidates bh - pull it back in */
	brelse(bh1);
	bh1 = sb_bread(s, 0);
	if (!bh1)
		goto out;
	sb1 = (struct qnx6_mmi_super_block *)bh1->b_data;

	/* read second superblock */
	bh2 = sb_bread(s, offset);
	if (!bh2) {
		pr_err("qnx6: unable to read the second superblock\n");
		goto out;
	}
	sb2 = (struct qnx6_mmi_super_block *)bh2->b_data;
	if (fs32_to_cpu(sbi, sb2->sb_magic) != QNX6_SUPER_MAGIC) {
		if (!silent)
			pr_err("qnx6: wrong signature (magic) in superblock #2.\n");
		goto out;
	}

	/* checksum check - start at byte 8 and end at byte 512 */
	if (fs32_to_cpu(sbi, sb2->sb_checksum)
			!= crc32_be(0, (char *)(bh2->b_data + 8), 504)) {
		pr_err("qnx6: superblock #1 checksum error\n");
		goto out;
	}

	qsb = kmalloc(sizeof(*qsb), GFP_KERNEL);
	if (!qsb) {
		pr_err("qnx6: unable to allocate memory.\n");
		goto out;
	}

	if (fs64_to_cpu(sbi, sb1->sb_serial) >
					fs64_to_cpu(sbi, sb2->sb_serial)) {
		/* superblock #1 active */
		qnx6_mmi_copy_sb(qsb, sb1);
#ifdef CONFIG_QNX6FS_DEBUG
		qnx6_superblock_debug(qsb, s);
#endif
		memcpy(bh1->b_data, qsb, sizeof(struct qnx6_super_block));

		sbi->sb_buf = bh1;
		sbi->sb = (struct qnx6_super_block *)bh1->b_data;
		brelse(bh2);
		pr_info("qnx6: superblock #1 active\n");
	} else {
		/* superblock #2 active */
		qnx6_mmi_copy_sb(qsb, sb2);
#ifdef CONFIG_QNX6FS_DEBUG
		qnx6_superblock_debug(qsb, s);
#endif
		memcpy(bh2->b_data, qsb, sizeof(struct qnx6_super_block));

		sbi->sb_buf = bh2;
		sbi->sb = (struct qnx6_super_block *)bh2->b_data;
		brelse(bh1);
		pr_info("qnx6: superblock #2 active\n");
	}
	kfree(qsb);

	/* offset for mmi_fs is just SUPERBLOCK_AREA bytes */
	sbi->s_blks_off = QNX6_SUPERBLOCK_AREA / s->s_blocksize;

	/* success */
	return sbi->sb;

out:
	if (bh1 != NULL)
		brelse(bh1);
	if (bh2 != NULL)
		brelse(bh2);
	return NULL;
}
