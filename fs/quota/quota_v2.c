/*
 *	vfsv0 quota IO operations on file
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/dqblk_v2.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/quotaops.h>

#include <asm/byteorder.h>

#include "quota_tree.h"
#include "quotaio_v2.h"

MODULE_AUTHOR("Jan Kara");
MODULE_DESCRIPTION("Quota format v2 support");
MODULE_LICENSE("GPL");

#define __QUOTA_V2_PARANOIA

static void v2_mem2diskdqb(void *dp, struct dquot *dquot);
static void v2_disk2memdqb(struct dquot *dquot, void *dp);
static int v2_is_id(void *dp, struct dquot *dquot);

static struct qtree_fmt_operations v2_qtree_ops = {
	.mem2disk_dqblk = v2_mem2diskdqb,
	.disk2mem_dqblk = v2_disk2memdqb,
	.is_id = v2_is_id,
};

#define QUOTABLOCK_BITS 10
#define QUOTABLOCK_SIZE (1 << QUOTABLOCK_BITS)

static inline qsize_t v2_stoqb(qsize_t space)
{
	return (space + QUOTABLOCK_SIZE - 1) >> QUOTABLOCK_BITS;
}

static inline qsize_t v2_qbtos(qsize_t blocks)
{
	return blocks << QUOTABLOCK_BITS;
}

/* Check whether given file is really vfsv0 quotafile */
static int v2_check_quota_file(struct super_block *sb, int type)
{
	struct v2_disk_dqheader dqhead;
	ssize_t size;
	static const uint quota_magics[] = V2_INITQMAGICS;
	static const uint quota_versions[] = V2_INITQVERSIONS;
 
	size = sb->s_op->quota_read(sb, type, (char *)&dqhead,
				    sizeof(struct v2_disk_dqheader), 0);
	if (size != sizeof(struct v2_disk_dqheader)) {
		printk("quota_v2: failed read expected=%zd got=%zd\n",
			sizeof(struct v2_disk_dqheader), size);
		return 0;
	}
	if (le32_to_cpu(dqhead.dqh_magic) != quota_magics[type] ||
	    le32_to_cpu(dqhead.dqh_version) != quota_versions[type])
		return 0;
	return 1;
}

/* Read information header from quota file */
static int v2_read_file_info(struct super_block *sb, int type)
{
	struct v2_disk_dqinfo dinfo;
	struct mem_dqinfo *info = sb_dqinfo(sb, type);
	struct qtree_mem_dqinfo *qinfo;
	ssize_t size;

	size = sb->s_op->quota_read(sb, type, (char *)&dinfo,
	       sizeof(struct v2_disk_dqinfo), V2_DQINFOOFF);
	if (size != sizeof(struct v2_disk_dqinfo)) {
		printk(KERN_WARNING "Can't read info structure on device %s.\n",
			sb->s_id);
		return -1;
	}
	info->dqi_priv = kmalloc(sizeof(struct qtree_mem_dqinfo), GFP_NOFS);
	if (!info->dqi_priv) {
		printk(KERN_WARNING
		       "Not enough memory for quota information structure.\n");
		return -1;
	}
	qinfo = info->dqi_priv;
	/* limits are stored as unsigned 32-bit data */
	info->dqi_maxblimit = 0xffffffff;
	info->dqi_maxilimit = 0xffffffff;
	info->dqi_bgrace = le32_to_cpu(dinfo.dqi_bgrace);
	info->dqi_igrace = le32_to_cpu(dinfo.dqi_igrace);
	info->dqi_flags = le32_to_cpu(dinfo.dqi_flags);
	qinfo->dqi_sb = sb;
	qinfo->dqi_type = type;
	qinfo->dqi_blocks = le32_to_cpu(dinfo.dqi_blocks);
	qinfo->dqi_free_blk = le32_to_cpu(dinfo.dqi_free_blk);
	qinfo->dqi_free_entry = le32_to_cpu(dinfo.dqi_free_entry);
	qinfo->dqi_blocksize_bits = V2_DQBLKSIZE_BITS;
	qinfo->dqi_usable_bs = 1 << V2_DQBLKSIZE_BITS;
	qinfo->dqi_qtree_depth = qtree_depth(qinfo);
	qinfo->dqi_entry_size = sizeof(struct v2_disk_dqblk);
	qinfo->dqi_ops = &v2_qtree_ops;
	return 0;
}

/* Write information header to quota file */
static int v2_write_file_info(struct super_block *sb, int type)
{
	struct v2_disk_dqinfo dinfo;
	struct mem_dqinfo *info = sb_dqinfo(sb, type);
	struct qtree_mem_dqinfo *qinfo = info->dqi_priv;
	ssize_t size;

	spin_lock(&dq_data_lock);
	info->dqi_flags &= ~DQF_INFO_DIRTY;
	dinfo.dqi_bgrace = cpu_to_le32(info->dqi_bgrace);
	dinfo.dqi_igrace = cpu_to_le32(info->dqi_igrace);
	dinfo.dqi_flags = cpu_to_le32(info->dqi_flags & DQF_MASK);
	spin_unlock(&dq_data_lock);
	dinfo.dqi_blocks = cpu_to_le32(qinfo->dqi_blocks);
	dinfo.dqi_free_blk = cpu_to_le32(qinfo->dqi_free_blk);
	dinfo.dqi_free_entry = cpu_to_le32(qinfo->dqi_free_entry);
	size = sb->s_op->quota_write(sb, type, (char *)&dinfo,
	       sizeof(struct v2_disk_dqinfo), V2_DQINFOOFF);
	if (size != sizeof(struct v2_disk_dqinfo)) {
		printk(KERN_WARNING "Can't write info structure on device %s.\n",
			sb->s_id);
		return -1;
	}
	return 0;
}

static void v2_disk2memdqb(struct dquot *dquot, void *dp)
{
	struct v2_disk_dqblk *d = dp, empty;
	struct mem_dqblk *m = &dquot->dq_dqb;

	m->dqb_ihardlimit = le32_to_cpu(d->dqb_ihardlimit);
	m->dqb_isoftlimit = le32_to_cpu(d->dqb_isoftlimit);
	m->dqb_curinodes = le32_to_cpu(d->dqb_curinodes);
	m->dqb_itime = le64_to_cpu(d->dqb_itime);
	m->dqb_bhardlimit = v2_qbtos(le32_to_cpu(d->dqb_bhardlimit));
	m->dqb_bsoftlimit = v2_qbtos(le32_to_cpu(d->dqb_bsoftlimit));
	m->dqb_curspace = le64_to_cpu(d->dqb_curspace);
	m->dqb_btime = le64_to_cpu(d->dqb_btime);
	/* We need to escape back all-zero structure */
	memset(&empty, 0, sizeof(struct v2_disk_dqblk));
	empty.dqb_itime = cpu_to_le64(1);
	if (!memcmp(&empty, dp, sizeof(struct v2_disk_dqblk)))
		m->dqb_itime = 0;
}

static void v2_mem2diskdqb(void *dp, struct dquot *dquot)
{
	struct v2_disk_dqblk *d = dp;
	struct mem_dqblk *m = &dquot->dq_dqb;
	struct qtree_mem_dqinfo *info =
			sb_dqinfo(dquot->dq_sb, dquot->dq_type)->dqi_priv;

	d->dqb_ihardlimit = cpu_to_le32(m->dqb_ihardlimit);
	d->dqb_isoftlimit = cpu_to_le32(m->dqb_isoftlimit);
	d->dqb_curinodes = cpu_to_le32(m->dqb_curinodes);
	d->dqb_itime = cpu_to_le64(m->dqb_itime);
	d->dqb_bhardlimit = cpu_to_le32(v2_stoqb(m->dqb_bhardlimit));
	d->dqb_bsoftlimit = cpu_to_le32(v2_stoqb(m->dqb_bsoftlimit));
	d->dqb_curspace = cpu_to_le64(m->dqb_curspace);
	d->dqb_btime = cpu_to_le64(m->dqb_btime);
	d->dqb_id = cpu_to_le32(dquot->dq_id);
	if (qtree_entry_unused(info, dp))
		d->dqb_itime = cpu_to_le64(1);
}

static int v2_is_id(void *dp, struct dquot *dquot)
{
	struct v2_disk_dqblk *d = dp;
	struct qtree_mem_dqinfo *info =
			sb_dqinfo(dquot->dq_sb, dquot->dq_type)->dqi_priv;

	if (qtree_entry_unused(info, dp))
		return 0;
	return le32_to_cpu(d->dqb_id) == dquot->dq_id;
}

static int v2_read_dquot(struct dquot *dquot)
{
	return qtree_read_dquot(sb_dqinfo(dquot->dq_sb, dquot->dq_type)->dqi_priv, dquot);
}

static int v2_write_dquot(struct dquot *dquot)
{
	return qtree_write_dquot(sb_dqinfo(dquot->dq_sb, dquot->dq_type)->dqi_priv, dquot);
}

static int v2_release_dquot(struct dquot *dquot)
{
	return qtree_release_dquot(sb_dqinfo(dquot->dq_sb, dquot->dq_type)->dqi_priv, dquot);
}

static int v2_free_file_info(struct super_block *sb, int type)
{
	kfree(sb_dqinfo(sb, type)->dqi_priv);
	return 0;
}

static struct quota_format_ops v2_format_ops = {
	.check_quota_file	= v2_check_quota_file,
	.read_file_info		= v2_read_file_info,
	.write_file_info	= v2_write_file_info,
	.free_file_info		= v2_free_file_info,
	.read_dqblk		= v2_read_dquot,
	.commit_dqblk		= v2_write_dquot,
	.release_dqblk		= v2_release_dquot,
};

static struct quota_format_type v2_quota_format = {
	.qf_fmt_id	= QFMT_VFS_V0,
	.qf_ops		= &v2_format_ops,
	.qf_owner	= THIS_MODULE
};

static int __init init_v2_quota_format(void)
{
	return register_quota_format(&v2_quota_format);
}

static void __exit exit_v2_quota_format(void)
{
	unregister_quota_format(&v2_quota_format);
}

module_init(init_v2_quota_format);
module_exit(exit_v2_quota_format);
