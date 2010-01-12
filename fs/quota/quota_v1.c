#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/quota.h>
#include <linux/quotaops.h>
#include <linux/dqblk_v1.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/byteorder.h>

#include "quotaio_v1.h"

MODULE_AUTHOR("Jan Kara");
MODULE_DESCRIPTION("Old quota format support");
MODULE_LICENSE("GPL");

#define QUOTABLOCK_BITS 10
#define QUOTABLOCK_SIZE (1 << QUOTABLOCK_BITS)

static inline qsize_t v1_stoqb(qsize_t space)
{
	return (space + QUOTABLOCK_SIZE - 1) >> QUOTABLOCK_BITS;
}

static inline qsize_t v1_qbtos(qsize_t blocks)
{
	return blocks << QUOTABLOCK_BITS;
}

static void v1_disk2mem_dqblk(struct mem_dqblk *m, struct v1_disk_dqblk *d)
{
	m->dqb_ihardlimit = d->dqb_ihardlimit;
	m->dqb_isoftlimit = d->dqb_isoftlimit;
	m->dqb_curinodes = d->dqb_curinodes;
	m->dqb_bhardlimit = v1_qbtos(d->dqb_bhardlimit);
	m->dqb_bsoftlimit = v1_qbtos(d->dqb_bsoftlimit);
	m->dqb_curspace = v1_qbtos(d->dqb_curblocks);
	m->dqb_itime = d->dqb_itime;
	m->dqb_btime = d->dqb_btime;
}

static void v1_mem2disk_dqblk(struct v1_disk_dqblk *d, struct mem_dqblk *m)
{
	d->dqb_ihardlimit = m->dqb_ihardlimit;
	d->dqb_isoftlimit = m->dqb_isoftlimit;
	d->dqb_curinodes = m->dqb_curinodes;
	d->dqb_bhardlimit = v1_stoqb(m->dqb_bhardlimit);
	d->dqb_bsoftlimit = v1_stoqb(m->dqb_bsoftlimit);
	d->dqb_curblocks = v1_stoqb(m->dqb_curspace);
	d->dqb_itime = m->dqb_itime;
	d->dqb_btime = m->dqb_btime;
}

static int v1_read_dqblk(struct dquot *dquot)
{
	int type = dquot->dq_type;
	struct v1_disk_dqblk dqblk;

	if (!sb_dqopt(dquot->dq_sb)->files[type])
		return -EINVAL;

	/* Set structure to 0s in case read fails/is after end of file */
	memset(&dqblk, 0, sizeof(struct v1_disk_dqblk));
	dquot->dq_sb->s_op->quota_read(dquot->dq_sb, type, (char *)&dqblk,
			sizeof(struct v1_disk_dqblk), v1_dqoff(dquot->dq_id));

	v1_disk2mem_dqblk(&dquot->dq_dqb, &dqblk);
	if (dquot->dq_dqb.dqb_bhardlimit == 0 &&
	    dquot->dq_dqb.dqb_bsoftlimit == 0 &&
	    dquot->dq_dqb.dqb_ihardlimit == 0 &&
	    dquot->dq_dqb.dqb_isoftlimit == 0)
		set_bit(DQ_FAKE_B, &dquot->dq_flags);
	dqstats.reads++;

	return 0;
}

static int v1_commit_dqblk(struct dquot *dquot)
{
	short type = dquot->dq_type;
	ssize_t ret;
	struct v1_disk_dqblk dqblk;

	v1_mem2disk_dqblk(&dqblk, &dquot->dq_dqb);
	if (dquot->dq_id == 0) {
		dqblk.dqb_btime =
			sb_dqopt(dquot->dq_sb)->info[type].dqi_bgrace;
		dqblk.dqb_itime =
			sb_dqopt(dquot->dq_sb)->info[type].dqi_igrace;
	}
	ret = 0;
	if (sb_dqopt(dquot->dq_sb)->files[type])
		ret = dquot->dq_sb->s_op->quota_write(dquot->dq_sb, type,
			(char *)&dqblk, sizeof(struct v1_disk_dqblk),
			v1_dqoff(dquot->dq_id));
	if (ret != sizeof(struct v1_disk_dqblk)) {
		printk(KERN_WARNING "VFS: dquota write failed on dev %s\n",
			dquot->dq_sb->s_id);
		if (ret >= 0)
			ret = -EIO;
		goto out;
	}
	ret = 0;

out:
	dqstats.writes++;

	return ret;
}

/* Magics of new quota format */
#define V2_INITQMAGICS {\
	0xd9c01f11,     /* USRQUOTA */\
	0xd9c01927      /* GRPQUOTA */\
}

/* Header of new quota format */
struct v2_disk_dqheader {
	__le32 dqh_magic;        /* Magic number identifying file */
	__le32 dqh_version;      /* File version */
};

static int v1_check_quota_file(struct super_block *sb, int type)
{
	struct inode *inode = sb_dqopt(sb)->files[type];
	ulong blocks;
	size_t off; 
	struct v2_disk_dqheader dqhead;
	ssize_t size;
	loff_t isize;
	static const uint quota_magics[] = V2_INITQMAGICS;

	isize = i_size_read(inode);
	if (!isize)
		return 0;
	blocks = isize >> BLOCK_SIZE_BITS;
	off = isize & (BLOCK_SIZE - 1);
	if ((blocks % sizeof(struct v1_disk_dqblk) * BLOCK_SIZE + off) %
	    sizeof(struct v1_disk_dqblk))
		return 0;
	/* Doublecheck whether we didn't get file with new format - with old
	 * quotactl() this could happen */
	size = sb->s_op->quota_read(sb, type, (char *)&dqhead,
				    sizeof(struct v2_disk_dqheader), 0);
	if (size != sizeof(struct v2_disk_dqheader))
		return 1;	/* Probably not new format */
	if (le32_to_cpu(dqhead.dqh_magic) != quota_magics[type])
		return 1;	/* Definitely not new format */
	printk(KERN_INFO
	       "VFS: %s: Refusing to turn on old quota format on given file."
	       " It probably contains newer quota format.\n", sb->s_id);
        return 0;		/* Seems like a new format file -> refuse it */
}

static int v1_read_file_info(struct super_block *sb, int type)
{
	struct quota_info *dqopt = sb_dqopt(sb);
	struct v1_disk_dqblk dqblk;
	int ret;

	ret = sb->s_op->quota_read(sb, type, (char *)&dqblk,
				sizeof(struct v1_disk_dqblk), v1_dqoff(0));
	if (ret != sizeof(struct v1_disk_dqblk)) {
		if (ret >= 0)
			ret = -EIO;
		goto out;
	}
	ret = 0;
	/* limits are stored as unsigned 32-bit data */
	dqopt->info[type].dqi_maxblimit = 0xffffffff;
	dqopt->info[type].dqi_maxilimit = 0xffffffff;
	dqopt->info[type].dqi_igrace =
			dqblk.dqb_itime ? dqblk.dqb_itime : MAX_IQ_TIME;
	dqopt->info[type].dqi_bgrace =
			dqblk.dqb_btime ? dqblk.dqb_btime : MAX_DQ_TIME;
out:
	return ret;
}

static int v1_write_file_info(struct super_block *sb, int type)
{
	struct quota_info *dqopt = sb_dqopt(sb);
	struct v1_disk_dqblk dqblk;
	int ret;

	dqopt->info[type].dqi_flags &= ~DQF_INFO_DIRTY;
	ret = sb->s_op->quota_read(sb, type, (char *)&dqblk,
				sizeof(struct v1_disk_dqblk), v1_dqoff(0));
	if (ret != sizeof(struct v1_disk_dqblk)) {
		if (ret >= 0)
			ret = -EIO;
		goto out;
	}
	dqblk.dqb_itime = dqopt->info[type].dqi_igrace;
	dqblk.dqb_btime = dqopt->info[type].dqi_bgrace;
	ret = sb->s_op->quota_write(sb, type, (char *)&dqblk,
	      sizeof(struct v1_disk_dqblk), v1_dqoff(0));
	if (ret == sizeof(struct v1_disk_dqblk))
		ret = 0;
	else if (ret > 0)
		ret = -EIO;
out:
	return ret;
}

static const struct quota_format_ops v1_format_ops = {
	.check_quota_file	= v1_check_quota_file,
	.read_file_info		= v1_read_file_info,
	.write_file_info	= v1_write_file_info,
	.free_file_info		= NULL,
	.read_dqblk		= v1_read_dqblk,
	.commit_dqblk		= v1_commit_dqblk,
};

static struct quota_format_type v1_quota_format = {
	.qf_fmt_id	= QFMT_VFS_OLD,
	.qf_ops		= &v1_format_ops,
	.qf_owner	= THIS_MODULE
};

static int __init init_v1_quota_format(void)
{
        return register_quota_format(&v1_quota_format);
}

static void __exit exit_v1_quota_format(void)
{
        unregister_quota_format(&v1_quota_format);
}

module_init(init_v1_quota_format);
module_exit(exit_v1_quota_format);

