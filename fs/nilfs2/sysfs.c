// SPDX-License-Identifier: GPL-2.0+
/*
 * sysfs.c - sysfs support implementation.
 *
 * Copyright (C) 2005-2014 Nippon Telegraph and Telephone Corporation.
 * Copyright (C) 2014 HGST, Inc., a Western Digital Company.
 *
 * Written by Vyacheslav Dubeyko <Vyacheslav.Dubeyko@hgst.com>
 */

#include <linux/kobject.h>

#include "nilfs.h"
#include "mdt.h"
#include "sufile.h"
#include "cpfile.h"
#include "sysfs.h"

/* /sys/fs/<nilfs>/ */
static struct kset *nilfs_kset;

#define NILFS_SHOW_TIME(time_t_val, buf) ({ \
		struct tm res; \
		int count = 0; \
		time64_to_tm(time_t_val, 0, &res); \
		res.tm_year += 1900; \
		res.tm_mon += 1; \
		count = scnprintf(buf, PAGE_SIZE, \
				    "%ld-%.2d-%.2d %.2d:%.2d:%.2d\n", \
				    res.tm_year, res.tm_mon, res.tm_mday, \
				    res.tm_hour, res.tm_min, res.tm_sec);\
		count; \
})

#define NILFS_DEV_INT_GROUP_OPS(name, parent_name) \
static ssize_t nilfs_##name##_attr_show(struct kobject *kobj, \
					struct attribute *attr, char *buf) \
{ \
	struct the_nilfs *nilfs = container_of(kobj->parent, \
						struct the_nilfs, \
						ns_##parent_name##_kobj); \
	struct nilfs_##name##_attr *a = container_of(attr, \
						struct nilfs_##name##_attr, \
						attr); \
	return a->show ? a->show(a, nilfs, buf) : 0; \
} \
static ssize_t nilfs_##name##_attr_store(struct kobject *kobj, \
					 struct attribute *attr, \
					 const char *buf, size_t len) \
{ \
	struct the_nilfs *nilfs = container_of(kobj->parent, \
						struct the_nilfs, \
						ns_##parent_name##_kobj); \
	struct nilfs_##name##_attr *a = container_of(attr, \
						struct nilfs_##name##_attr, \
						attr); \
	return a->store ? a->store(a, nilfs, buf, len) : 0; \
} \
static const struct sysfs_ops nilfs_##name##_attr_ops = { \
	.show	= nilfs_##name##_attr_show, \
	.store	= nilfs_##name##_attr_store, \
}

#define NILFS_DEV_INT_GROUP_TYPE(name, parent_name) \
static void nilfs_##name##_attr_release(struct kobject *kobj) \
{ \
	struct nilfs_sysfs_##parent_name##_subgroups *subgroups; \
	struct the_nilfs *nilfs = container_of(kobj->parent, \
						struct the_nilfs, \
						ns_##parent_name##_kobj); \
	subgroups = nilfs->ns_##parent_name##_subgroups; \
	complete(&subgroups->sg_##name##_kobj_unregister); \
} \
static struct kobj_type nilfs_##name##_ktype = { \
	.default_attrs	= nilfs_##name##_attrs, \
	.sysfs_ops	= &nilfs_##name##_attr_ops, \
	.release	= nilfs_##name##_attr_release, \
}

#define NILFS_DEV_INT_GROUP_FNS(name, parent_name) \
static int nilfs_sysfs_create_##name##_group(struct the_nilfs *nilfs) \
{ \
	struct kobject *parent; \
	struct kobject *kobj; \
	struct completion *kobj_unregister; \
	struct nilfs_sysfs_##parent_name##_subgroups *subgroups; \
	int err; \
	subgroups = nilfs->ns_##parent_name##_subgroups; \
	kobj = &subgroups->sg_##name##_kobj; \
	kobj_unregister = &subgroups->sg_##name##_kobj_unregister; \
	parent = &nilfs->ns_##parent_name##_kobj; \
	kobj->kset = nilfs_kset; \
	init_completion(kobj_unregister); \
	err = kobject_init_and_add(kobj, &nilfs_##name##_ktype, parent, \
				    #name); \
	if (err) \
		return err; \
	return 0; \
} \
static void nilfs_sysfs_delete_##name##_group(struct the_nilfs *nilfs) \
{ \
	kobject_del(&nilfs->ns_##parent_name##_subgroups->sg_##name##_kobj); \
}

/************************************************************************
 *                        NILFS snapshot attrs                          *
 ************************************************************************/

static ssize_t
nilfs_snapshot_inodes_count_show(struct nilfs_snapshot_attr *attr,
				 struct nilfs_root *root, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%llu\n",
			(unsigned long long)atomic64_read(&root->inodes_count));
}

static ssize_t
nilfs_snapshot_blocks_count_show(struct nilfs_snapshot_attr *attr,
				 struct nilfs_root *root, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%llu\n",
			(unsigned long long)atomic64_read(&root->blocks_count));
}

static const char snapshot_readme_str[] =
	"The group contains details about mounted snapshot.\n\n"
	"(1) inodes_count\n\tshow number of inodes for snapshot.\n\n"
	"(2) blocks_count\n\tshow number of blocks for snapshot.\n\n";

static ssize_t
nilfs_snapshot_README_show(struct nilfs_snapshot_attr *attr,
			    struct nilfs_root *root, char *buf)
{
	return snprintf(buf, PAGE_SIZE, snapshot_readme_str);
}

NILFS_SNAPSHOT_RO_ATTR(inodes_count);
NILFS_SNAPSHOT_RO_ATTR(blocks_count);
NILFS_SNAPSHOT_RO_ATTR(README);

static struct attribute *nilfs_snapshot_attrs[] = {
	NILFS_SNAPSHOT_ATTR_LIST(inodes_count),
	NILFS_SNAPSHOT_ATTR_LIST(blocks_count),
	NILFS_SNAPSHOT_ATTR_LIST(README),
	NULL,
};

static ssize_t nilfs_snapshot_attr_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	struct nilfs_root *root =
			container_of(kobj, struct nilfs_root, snapshot_kobj);
	struct nilfs_snapshot_attr *a =
			container_of(attr, struct nilfs_snapshot_attr, attr);

	return a->show ? a->show(a, root, buf) : 0;
}

static ssize_t nilfs_snapshot_attr_store(struct kobject *kobj,
					 struct attribute *attr,
					 const char *buf, size_t len)
{
	struct nilfs_root *root =
			container_of(kobj, struct nilfs_root, snapshot_kobj);
	struct nilfs_snapshot_attr *a =
			container_of(attr, struct nilfs_snapshot_attr, attr);

	return a->store ? a->store(a, root, buf, len) : 0;
}

static void nilfs_snapshot_attr_release(struct kobject *kobj)
{
	struct nilfs_root *root = container_of(kobj, struct nilfs_root,
						snapshot_kobj);
	complete(&root->snapshot_kobj_unregister);
}

static const struct sysfs_ops nilfs_snapshot_attr_ops = {
	.show	= nilfs_snapshot_attr_show,
	.store	= nilfs_snapshot_attr_store,
};

static struct kobj_type nilfs_snapshot_ktype = {
	.default_attrs	= nilfs_snapshot_attrs,
	.sysfs_ops	= &nilfs_snapshot_attr_ops,
	.release	= nilfs_snapshot_attr_release,
};

int nilfs_sysfs_create_snapshot_group(struct nilfs_root *root)
{
	struct the_nilfs *nilfs;
	struct kobject *parent;
	int err;

	nilfs = root->nilfs;
	parent = &nilfs->ns_dev_subgroups->sg_mounted_snapshots_kobj;
	root->snapshot_kobj.kset = nilfs_kset;
	init_completion(&root->snapshot_kobj_unregister);

	if (root->cno == NILFS_CPTREE_CURRENT_CNO) {
		err = kobject_init_and_add(&root->snapshot_kobj,
					    &nilfs_snapshot_ktype,
					    &nilfs->ns_dev_kobj,
					    "current_checkpoint");
	} else {
		err = kobject_init_and_add(&root->snapshot_kobj,
					    &nilfs_snapshot_ktype,
					    parent,
					    "%llu", root->cno);
	}

	if (err)
		return err;

	return 0;
}

void nilfs_sysfs_delete_snapshot_group(struct nilfs_root *root)
{
	kobject_del(&root->snapshot_kobj);
}

/************************************************************************
 *                    NILFS mounted snapshots attrs                     *
 ************************************************************************/

static const char mounted_snapshots_readme_str[] =
	"The mounted_snapshots group contains group for\n"
	"every mounted snapshot.\n";

static ssize_t
nilfs_mounted_snapshots_README_show(struct nilfs_mounted_snapshots_attr *attr,
				    struct the_nilfs *nilfs, char *buf)
{
	return snprintf(buf, PAGE_SIZE, mounted_snapshots_readme_str);
}

NILFS_MOUNTED_SNAPSHOTS_RO_ATTR(README);

static struct attribute *nilfs_mounted_snapshots_attrs[] = {
	NILFS_MOUNTED_SNAPSHOTS_ATTR_LIST(README),
	NULL,
};

NILFS_DEV_INT_GROUP_OPS(mounted_snapshots, dev);
NILFS_DEV_INT_GROUP_TYPE(mounted_snapshots, dev);
NILFS_DEV_INT_GROUP_FNS(mounted_snapshots, dev);

/************************************************************************
 *                      NILFS checkpoints attrs                         *
 ************************************************************************/

static ssize_t
nilfs_checkpoints_checkpoints_number_show(struct nilfs_checkpoints_attr *attr,
					    struct the_nilfs *nilfs,
					    char *buf)
{
	__u64 ncheckpoints;
	struct nilfs_cpstat cpstat;
	int err;

	down_read(&nilfs->ns_segctor_sem);
	err = nilfs_cpfile_get_stat(nilfs->ns_cpfile, &cpstat);
	up_read(&nilfs->ns_segctor_sem);
	if (err < 0) {
		nilfs_err(nilfs->ns_sb, "unable to get checkpoint stat: err=%d",
			  err);
		return err;
	}

	ncheckpoints = cpstat.cs_ncps;

	return snprintf(buf, PAGE_SIZE, "%llu\n", ncheckpoints);
}

static ssize_t
nilfs_checkpoints_snapshots_number_show(struct nilfs_checkpoints_attr *attr,
					struct the_nilfs *nilfs,
					char *buf)
{
	__u64 nsnapshots;
	struct nilfs_cpstat cpstat;
	int err;

	down_read(&nilfs->ns_segctor_sem);
	err = nilfs_cpfile_get_stat(nilfs->ns_cpfile, &cpstat);
	up_read(&nilfs->ns_segctor_sem);
	if (err < 0) {
		nilfs_err(nilfs->ns_sb, "unable to get checkpoint stat: err=%d",
			  err);
		return err;
	}

	nsnapshots = cpstat.cs_nsss;

	return snprintf(buf, PAGE_SIZE, "%llu\n", nsnapshots);
}

static ssize_t
nilfs_checkpoints_last_seg_checkpoint_show(struct nilfs_checkpoints_attr *attr,
					    struct the_nilfs *nilfs,
					    char *buf)
{
	__u64 last_cno;

	spin_lock(&nilfs->ns_last_segment_lock);
	last_cno = nilfs->ns_last_cno;
	spin_unlock(&nilfs->ns_last_segment_lock);

	return snprintf(buf, PAGE_SIZE, "%llu\n", last_cno);
}

static ssize_t
nilfs_checkpoints_next_checkpoint_show(struct nilfs_checkpoints_attr *attr,
					struct the_nilfs *nilfs,
					char *buf)
{
	__u64 cno;

	down_read(&nilfs->ns_segctor_sem);
	cno = nilfs->ns_cno;
	up_read(&nilfs->ns_segctor_sem);

	return snprintf(buf, PAGE_SIZE, "%llu\n", cno);
}

static const char checkpoints_readme_str[] =
	"The checkpoints group contains attributes that describe\n"
	"details about volume's checkpoints.\n\n"
	"(1) checkpoints_number\n\tshow number of checkpoints on volume.\n\n"
	"(2) snapshots_number\n\tshow number of snapshots on volume.\n\n"
	"(3) last_seg_checkpoint\n"
	"\tshow checkpoint number of the latest segment.\n\n"
	"(4) next_checkpoint\n\tshow next checkpoint number.\n\n";

static ssize_t
nilfs_checkpoints_README_show(struct nilfs_checkpoints_attr *attr,
				struct the_nilfs *nilfs, char *buf)
{
	return snprintf(buf, PAGE_SIZE, checkpoints_readme_str);
}

NILFS_CHECKPOINTS_RO_ATTR(checkpoints_number);
NILFS_CHECKPOINTS_RO_ATTR(snapshots_number);
NILFS_CHECKPOINTS_RO_ATTR(last_seg_checkpoint);
NILFS_CHECKPOINTS_RO_ATTR(next_checkpoint);
NILFS_CHECKPOINTS_RO_ATTR(README);

static struct attribute *nilfs_checkpoints_attrs[] = {
	NILFS_CHECKPOINTS_ATTR_LIST(checkpoints_number),
	NILFS_CHECKPOINTS_ATTR_LIST(snapshots_number),
	NILFS_CHECKPOINTS_ATTR_LIST(last_seg_checkpoint),
	NILFS_CHECKPOINTS_ATTR_LIST(next_checkpoint),
	NILFS_CHECKPOINTS_ATTR_LIST(README),
	NULL,
};

NILFS_DEV_INT_GROUP_OPS(checkpoints, dev);
NILFS_DEV_INT_GROUP_TYPE(checkpoints, dev);
NILFS_DEV_INT_GROUP_FNS(checkpoints, dev);

/************************************************************************
 *                        NILFS segments attrs                          *
 ************************************************************************/

static ssize_t
nilfs_segments_segments_number_show(struct nilfs_segments_attr *attr,
				     struct the_nilfs *nilfs,
				     char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%lu\n", nilfs->ns_nsegments);
}

static ssize_t
nilfs_segments_blocks_per_segment_show(struct nilfs_segments_attr *attr,
					struct the_nilfs *nilfs,
					char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%lu\n", nilfs->ns_blocks_per_segment);
}

static ssize_t
nilfs_segments_clean_segments_show(struct nilfs_segments_attr *attr,
				    struct the_nilfs *nilfs,
				    char *buf)
{
	unsigned long ncleansegs;

	down_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);
	ncleansegs = nilfs_sufile_get_ncleansegs(nilfs->ns_sufile);
	up_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);

	return snprintf(buf, PAGE_SIZE, "%lu\n", ncleansegs);
}

static ssize_t
nilfs_segments_dirty_segments_show(struct nilfs_segments_attr *attr,
				    struct the_nilfs *nilfs,
				    char *buf)
{
	struct nilfs_sustat sustat;
	int err;

	down_read(&nilfs->ns_segctor_sem);
	err = nilfs_sufile_get_stat(nilfs->ns_sufile, &sustat);
	up_read(&nilfs->ns_segctor_sem);
	if (err < 0) {
		nilfs_err(nilfs->ns_sb, "unable to get segment stat: err=%d",
			  err);
		return err;
	}

	return snprintf(buf, PAGE_SIZE, "%llu\n", sustat.ss_ndirtysegs);
}

static const char segments_readme_str[] =
	"The segments group contains attributes that describe\n"
	"details about volume's segments.\n\n"
	"(1) segments_number\n\tshow number of segments on volume.\n\n"
	"(2) blocks_per_segment\n\tshow number of blocks in segment.\n\n"
	"(3) clean_segments\n\tshow count of clean segments.\n\n"
	"(4) dirty_segments\n\tshow count of dirty segments.\n\n";

static ssize_t
nilfs_segments_README_show(struct nilfs_segments_attr *attr,
			    struct the_nilfs *nilfs,
			    char *buf)
{
	return snprintf(buf, PAGE_SIZE, segments_readme_str);
}

NILFS_SEGMENTS_RO_ATTR(segments_number);
NILFS_SEGMENTS_RO_ATTR(blocks_per_segment);
NILFS_SEGMENTS_RO_ATTR(clean_segments);
NILFS_SEGMENTS_RO_ATTR(dirty_segments);
NILFS_SEGMENTS_RO_ATTR(README);

static struct attribute *nilfs_segments_attrs[] = {
	NILFS_SEGMENTS_ATTR_LIST(segments_number),
	NILFS_SEGMENTS_ATTR_LIST(blocks_per_segment),
	NILFS_SEGMENTS_ATTR_LIST(clean_segments),
	NILFS_SEGMENTS_ATTR_LIST(dirty_segments),
	NILFS_SEGMENTS_ATTR_LIST(README),
	NULL,
};

NILFS_DEV_INT_GROUP_OPS(segments, dev);
NILFS_DEV_INT_GROUP_TYPE(segments, dev);
NILFS_DEV_INT_GROUP_FNS(segments, dev);

/************************************************************************
 *                        NILFS segctor attrs                           *
 ************************************************************************/

static ssize_t
nilfs_segctor_last_pseg_block_show(struct nilfs_segctor_attr *attr,
				    struct the_nilfs *nilfs,
				    char *buf)
{
	sector_t last_pseg;

	spin_lock(&nilfs->ns_last_segment_lock);
	last_pseg = nilfs->ns_last_pseg;
	spin_unlock(&nilfs->ns_last_segment_lock);

	return snprintf(buf, PAGE_SIZE, "%llu\n",
			(unsigned long long)last_pseg);
}

static ssize_t
nilfs_segctor_last_seg_sequence_show(struct nilfs_segctor_attr *attr,
					struct the_nilfs *nilfs,
					char *buf)
{
	u64 last_seq;

	spin_lock(&nilfs->ns_last_segment_lock);
	last_seq = nilfs->ns_last_seq;
	spin_unlock(&nilfs->ns_last_segment_lock);

	return snprintf(buf, PAGE_SIZE, "%llu\n", last_seq);
}

static ssize_t
nilfs_segctor_last_seg_checkpoint_show(struct nilfs_segctor_attr *attr,
					struct the_nilfs *nilfs,
					char *buf)
{
	__u64 last_cno;

	spin_lock(&nilfs->ns_last_segment_lock);
	last_cno = nilfs->ns_last_cno;
	spin_unlock(&nilfs->ns_last_segment_lock);

	return snprintf(buf, PAGE_SIZE, "%llu\n", last_cno);
}

static ssize_t
nilfs_segctor_current_seg_sequence_show(struct nilfs_segctor_attr *attr,
					struct the_nilfs *nilfs,
					char *buf)
{
	u64 seg_seq;

	down_read(&nilfs->ns_segctor_sem);
	seg_seq = nilfs->ns_seg_seq;
	up_read(&nilfs->ns_segctor_sem);

	return snprintf(buf, PAGE_SIZE, "%llu\n", seg_seq);
}

static ssize_t
nilfs_segctor_current_last_full_seg_show(struct nilfs_segctor_attr *attr,
					 struct the_nilfs *nilfs,
					 char *buf)
{
	__u64 segnum;

	down_read(&nilfs->ns_segctor_sem);
	segnum = nilfs->ns_segnum;
	up_read(&nilfs->ns_segctor_sem);

	return snprintf(buf, PAGE_SIZE, "%llu\n", segnum);
}

static ssize_t
nilfs_segctor_next_full_seg_show(struct nilfs_segctor_attr *attr,
				 struct the_nilfs *nilfs,
				 char *buf)
{
	__u64 nextnum;

	down_read(&nilfs->ns_segctor_sem);
	nextnum = nilfs->ns_nextnum;
	up_read(&nilfs->ns_segctor_sem);

	return snprintf(buf, PAGE_SIZE, "%llu\n", nextnum);
}

static ssize_t
nilfs_segctor_next_pseg_offset_show(struct nilfs_segctor_attr *attr,
					struct the_nilfs *nilfs,
					char *buf)
{
	unsigned long pseg_offset;

	down_read(&nilfs->ns_segctor_sem);
	pseg_offset = nilfs->ns_pseg_offset;
	up_read(&nilfs->ns_segctor_sem);

	return snprintf(buf, PAGE_SIZE, "%lu\n", pseg_offset);
}

static ssize_t
nilfs_segctor_next_checkpoint_show(struct nilfs_segctor_attr *attr,
					struct the_nilfs *nilfs,
					char *buf)
{
	__u64 cno;

	down_read(&nilfs->ns_segctor_sem);
	cno = nilfs->ns_cno;
	up_read(&nilfs->ns_segctor_sem);

	return snprintf(buf, PAGE_SIZE, "%llu\n", cno);
}

static ssize_t
nilfs_segctor_last_seg_write_time_show(struct nilfs_segctor_attr *attr,
					struct the_nilfs *nilfs,
					char *buf)
{
	time64_t ctime;

	down_read(&nilfs->ns_segctor_sem);
	ctime = nilfs->ns_ctime;
	up_read(&nilfs->ns_segctor_sem);

	return NILFS_SHOW_TIME(ctime, buf);
}

static ssize_t
nilfs_segctor_last_seg_write_time_secs_show(struct nilfs_segctor_attr *attr,
					    struct the_nilfs *nilfs,
					    char *buf)
{
	time64_t ctime;

	down_read(&nilfs->ns_segctor_sem);
	ctime = nilfs->ns_ctime;
	up_read(&nilfs->ns_segctor_sem);

	return snprintf(buf, PAGE_SIZE, "%llu\n", ctime);
}

static ssize_t
nilfs_segctor_last_nongc_write_time_show(struct nilfs_segctor_attr *attr,
					 struct the_nilfs *nilfs,
					 char *buf)
{
	time64_t nongc_ctime;

	down_read(&nilfs->ns_segctor_sem);
	nongc_ctime = nilfs->ns_nongc_ctime;
	up_read(&nilfs->ns_segctor_sem);

	return NILFS_SHOW_TIME(nongc_ctime, buf);
}

static ssize_t
nilfs_segctor_last_nongc_write_time_secs_show(struct nilfs_segctor_attr *attr,
						struct the_nilfs *nilfs,
						char *buf)
{
	time64_t nongc_ctime;

	down_read(&nilfs->ns_segctor_sem);
	nongc_ctime = nilfs->ns_nongc_ctime;
	up_read(&nilfs->ns_segctor_sem);

	return snprintf(buf, PAGE_SIZE, "%llu\n", nongc_ctime);
}

static ssize_t
nilfs_segctor_dirty_data_blocks_count_show(struct nilfs_segctor_attr *attr,
					    struct the_nilfs *nilfs,
					    char *buf)
{
	u32 ndirtyblks;

	down_read(&nilfs->ns_segctor_sem);
	ndirtyblks = atomic_read(&nilfs->ns_ndirtyblks);
	up_read(&nilfs->ns_segctor_sem);

	return snprintf(buf, PAGE_SIZE, "%u\n", ndirtyblks);
}

static const char segctor_readme_str[] =
	"The segctor group contains attributes that describe\n"
	"segctor thread activity details.\n\n"
	"(1) last_pseg_block\n"
	"\tshow start block number of the latest segment.\n\n"
	"(2) last_seg_sequence\n"
	"\tshow sequence value of the latest segment.\n\n"
	"(3) last_seg_checkpoint\n"
	"\tshow checkpoint number of the latest segment.\n\n"
	"(4) current_seg_sequence\n\tshow segment sequence counter.\n\n"
	"(5) current_last_full_seg\n"
	"\tshow index number of the latest full segment.\n\n"
	"(6) next_full_seg\n"
	"\tshow index number of the full segment index to be used next.\n\n"
	"(7) next_pseg_offset\n"
	"\tshow offset of next partial segment in the current full segment.\n\n"
	"(8) next_checkpoint\n\tshow next checkpoint number.\n\n"
	"(9) last_seg_write_time\n"
	"\tshow write time of the last segment in human-readable format.\n\n"
	"(10) last_seg_write_time_secs\n"
	"\tshow write time of the last segment in seconds.\n\n"
	"(11) last_nongc_write_time\n"
	"\tshow write time of the last segment not for cleaner operation "
	"in human-readable format.\n\n"
	"(12) last_nongc_write_time_secs\n"
	"\tshow write time of the last segment not for cleaner operation "
	"in seconds.\n\n"
	"(13) dirty_data_blocks_count\n"
	"\tshow number of dirty data blocks.\n\n";

static ssize_t
nilfs_segctor_README_show(struct nilfs_segctor_attr *attr,
			  struct the_nilfs *nilfs, char *buf)
{
	return snprintf(buf, PAGE_SIZE, segctor_readme_str);
}

NILFS_SEGCTOR_RO_ATTR(last_pseg_block);
NILFS_SEGCTOR_RO_ATTR(last_seg_sequence);
NILFS_SEGCTOR_RO_ATTR(last_seg_checkpoint);
NILFS_SEGCTOR_RO_ATTR(current_seg_sequence);
NILFS_SEGCTOR_RO_ATTR(current_last_full_seg);
NILFS_SEGCTOR_RO_ATTR(next_full_seg);
NILFS_SEGCTOR_RO_ATTR(next_pseg_offset);
NILFS_SEGCTOR_RO_ATTR(next_checkpoint);
NILFS_SEGCTOR_RO_ATTR(last_seg_write_time);
NILFS_SEGCTOR_RO_ATTR(last_seg_write_time_secs);
NILFS_SEGCTOR_RO_ATTR(last_nongc_write_time);
NILFS_SEGCTOR_RO_ATTR(last_nongc_write_time_secs);
NILFS_SEGCTOR_RO_ATTR(dirty_data_blocks_count);
NILFS_SEGCTOR_RO_ATTR(README);

static struct attribute *nilfs_segctor_attrs[] = {
	NILFS_SEGCTOR_ATTR_LIST(last_pseg_block),
	NILFS_SEGCTOR_ATTR_LIST(last_seg_sequence),
	NILFS_SEGCTOR_ATTR_LIST(last_seg_checkpoint),
	NILFS_SEGCTOR_ATTR_LIST(current_seg_sequence),
	NILFS_SEGCTOR_ATTR_LIST(current_last_full_seg),
	NILFS_SEGCTOR_ATTR_LIST(next_full_seg),
	NILFS_SEGCTOR_ATTR_LIST(next_pseg_offset),
	NILFS_SEGCTOR_ATTR_LIST(next_checkpoint),
	NILFS_SEGCTOR_ATTR_LIST(last_seg_write_time),
	NILFS_SEGCTOR_ATTR_LIST(last_seg_write_time_secs),
	NILFS_SEGCTOR_ATTR_LIST(last_nongc_write_time),
	NILFS_SEGCTOR_ATTR_LIST(last_nongc_write_time_secs),
	NILFS_SEGCTOR_ATTR_LIST(dirty_data_blocks_count),
	NILFS_SEGCTOR_ATTR_LIST(README),
	NULL,
};

NILFS_DEV_INT_GROUP_OPS(segctor, dev);
NILFS_DEV_INT_GROUP_TYPE(segctor, dev);
NILFS_DEV_INT_GROUP_FNS(segctor, dev);

/************************************************************************
 *                        NILFS superblock attrs                        *
 ************************************************************************/

static ssize_t
nilfs_superblock_sb_write_time_show(struct nilfs_superblock_attr *attr,
				     struct the_nilfs *nilfs,
				     char *buf)
{
	time64_t sbwtime;

	down_read(&nilfs->ns_sem);
	sbwtime = nilfs->ns_sbwtime;
	up_read(&nilfs->ns_sem);

	return NILFS_SHOW_TIME(sbwtime, buf);
}

static ssize_t
nilfs_superblock_sb_write_time_secs_show(struct nilfs_superblock_attr *attr,
					 struct the_nilfs *nilfs,
					 char *buf)
{
	time64_t sbwtime;

	down_read(&nilfs->ns_sem);
	sbwtime = nilfs->ns_sbwtime;
	up_read(&nilfs->ns_sem);

	return snprintf(buf, PAGE_SIZE, "%llu\n", sbwtime);
}

static ssize_t
nilfs_superblock_sb_write_count_show(struct nilfs_superblock_attr *attr,
				      struct the_nilfs *nilfs,
				      char *buf)
{
	unsigned int sbwcount;

	down_read(&nilfs->ns_sem);
	sbwcount = nilfs->ns_sbwcount;
	up_read(&nilfs->ns_sem);

	return snprintf(buf, PAGE_SIZE, "%u\n", sbwcount);
}

static ssize_t
nilfs_superblock_sb_update_frequency_show(struct nilfs_superblock_attr *attr,
					    struct the_nilfs *nilfs,
					    char *buf)
{
	unsigned int sb_update_freq;

	down_read(&nilfs->ns_sem);
	sb_update_freq = nilfs->ns_sb_update_freq;
	up_read(&nilfs->ns_sem);

	return snprintf(buf, PAGE_SIZE, "%u\n", sb_update_freq);
}

static ssize_t
nilfs_superblock_sb_update_frequency_store(struct nilfs_superblock_attr *attr,
					    struct the_nilfs *nilfs,
					    const char *buf, size_t count)
{
	unsigned int val;
	int err;

	err = kstrtouint(skip_spaces(buf), 0, &val);
	if (err) {
		nilfs_err(nilfs->ns_sb, "unable to convert string: err=%d",
			  err);
		return err;
	}

	if (val < NILFS_SB_FREQ) {
		val = NILFS_SB_FREQ;
		nilfs_warn(nilfs->ns_sb,
			   "superblock update frequency cannot be lesser than 10 seconds");
	}

	down_write(&nilfs->ns_sem);
	nilfs->ns_sb_update_freq = val;
	up_write(&nilfs->ns_sem);

	return count;
}

static const char sb_readme_str[] =
	"The superblock group contains attributes that describe\n"
	"superblock's details.\n\n"
	"(1) sb_write_time\n\tshow previous write time of super block "
	"in human-readable format.\n\n"
	"(2) sb_write_time_secs\n\tshow previous write time of super block "
	"in seconds.\n\n"
	"(3) sb_write_count\n\tshow write count of super block.\n\n"
	"(4) sb_update_frequency\n"
	"\tshow/set interval of periodical update of superblock (in seconds).\n\n"
	"\tYou can set preferable frequency of superblock update by command:\n\n"
	"\t'echo <val> > /sys/fs/<nilfs>/<dev>/superblock/sb_update_frequency'\n";

static ssize_t
nilfs_superblock_README_show(struct nilfs_superblock_attr *attr,
				struct the_nilfs *nilfs, char *buf)
{
	return snprintf(buf, PAGE_SIZE, sb_readme_str);
}

NILFS_SUPERBLOCK_RO_ATTR(sb_write_time);
NILFS_SUPERBLOCK_RO_ATTR(sb_write_time_secs);
NILFS_SUPERBLOCK_RO_ATTR(sb_write_count);
NILFS_SUPERBLOCK_RW_ATTR(sb_update_frequency);
NILFS_SUPERBLOCK_RO_ATTR(README);

static struct attribute *nilfs_superblock_attrs[] = {
	NILFS_SUPERBLOCK_ATTR_LIST(sb_write_time),
	NILFS_SUPERBLOCK_ATTR_LIST(sb_write_time_secs),
	NILFS_SUPERBLOCK_ATTR_LIST(sb_write_count),
	NILFS_SUPERBLOCK_ATTR_LIST(sb_update_frequency),
	NILFS_SUPERBLOCK_ATTR_LIST(README),
	NULL,
};

NILFS_DEV_INT_GROUP_OPS(superblock, dev);
NILFS_DEV_INT_GROUP_TYPE(superblock, dev);
NILFS_DEV_INT_GROUP_FNS(superblock, dev);

/************************************************************************
 *                        NILFS device attrs                            *
 ************************************************************************/

static
ssize_t nilfs_dev_revision_show(struct nilfs_dev_attr *attr,
				struct the_nilfs *nilfs,
				char *buf)
{
	struct nilfs_super_block **sbp = nilfs->ns_sbp;
	u32 major = le32_to_cpu(sbp[0]->s_rev_level);
	u16 minor = le16_to_cpu(sbp[0]->s_minor_rev_level);

	return snprintf(buf, PAGE_SIZE, "%d.%d\n", major, minor);
}

static
ssize_t nilfs_dev_blocksize_show(struct nilfs_dev_attr *attr,
				 struct the_nilfs *nilfs,
				 char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", nilfs->ns_blocksize);
}

static
ssize_t nilfs_dev_device_size_show(struct nilfs_dev_attr *attr,
				    struct the_nilfs *nilfs,
				    char *buf)
{
	struct nilfs_super_block **sbp = nilfs->ns_sbp;
	u64 dev_size = le64_to_cpu(sbp[0]->s_dev_size);

	return snprintf(buf, PAGE_SIZE, "%llu\n", dev_size);
}

static
ssize_t nilfs_dev_free_blocks_show(struct nilfs_dev_attr *attr,
				   struct the_nilfs *nilfs,
				   char *buf)
{
	sector_t free_blocks = 0;

	nilfs_count_free_blocks(nilfs, &free_blocks);
	return snprintf(buf, PAGE_SIZE, "%llu\n",
			(unsigned long long)free_blocks);
}

static
ssize_t nilfs_dev_uuid_show(struct nilfs_dev_attr *attr,
			    struct the_nilfs *nilfs,
			    char *buf)
{
	struct nilfs_super_block **sbp = nilfs->ns_sbp;

	return snprintf(buf, PAGE_SIZE, "%pUb\n", sbp[0]->s_uuid);
}

static
ssize_t nilfs_dev_volume_name_show(struct nilfs_dev_attr *attr,
				    struct the_nilfs *nilfs,
				    char *buf)
{
	struct nilfs_super_block **sbp = nilfs->ns_sbp;

	return scnprintf(buf, sizeof(sbp[0]->s_volume_name), "%s\n",
			 sbp[0]->s_volume_name);
}

static const char dev_readme_str[] =
	"The <device> group contains attributes that describe file system\n"
	"partition's details.\n\n"
	"(1) revision\n\tshow NILFS file system revision.\n\n"
	"(2) blocksize\n\tshow volume block size in bytes.\n\n"
	"(3) device_size\n\tshow volume size in bytes.\n\n"
	"(4) free_blocks\n\tshow count of free blocks on volume.\n\n"
	"(5) uuid\n\tshow volume's UUID.\n\n"
	"(6) volume_name\n\tshow volume's name.\n\n";

static ssize_t nilfs_dev_README_show(struct nilfs_dev_attr *attr,
				     struct the_nilfs *nilfs,
				     char *buf)
{
	return snprintf(buf, PAGE_SIZE, dev_readme_str);
}

NILFS_DEV_RO_ATTR(revision);
NILFS_DEV_RO_ATTR(blocksize);
NILFS_DEV_RO_ATTR(device_size);
NILFS_DEV_RO_ATTR(free_blocks);
NILFS_DEV_RO_ATTR(uuid);
NILFS_DEV_RO_ATTR(volume_name);
NILFS_DEV_RO_ATTR(README);

static struct attribute *nilfs_dev_attrs[] = {
	NILFS_DEV_ATTR_LIST(revision),
	NILFS_DEV_ATTR_LIST(blocksize),
	NILFS_DEV_ATTR_LIST(device_size),
	NILFS_DEV_ATTR_LIST(free_blocks),
	NILFS_DEV_ATTR_LIST(uuid),
	NILFS_DEV_ATTR_LIST(volume_name),
	NILFS_DEV_ATTR_LIST(README),
	NULL,
};

static ssize_t nilfs_dev_attr_show(struct kobject *kobj,
				    struct attribute *attr, char *buf)
{
	struct the_nilfs *nilfs = container_of(kobj, struct the_nilfs,
						ns_dev_kobj);
	struct nilfs_dev_attr *a = container_of(attr, struct nilfs_dev_attr,
						attr);

	return a->show ? a->show(a, nilfs, buf) : 0;
}

static ssize_t nilfs_dev_attr_store(struct kobject *kobj,
				    struct attribute *attr,
				    const char *buf, size_t len)
{
	struct the_nilfs *nilfs = container_of(kobj, struct the_nilfs,
						ns_dev_kobj);
	struct nilfs_dev_attr *a = container_of(attr, struct nilfs_dev_attr,
						attr);

	return a->store ? a->store(a, nilfs, buf, len) : 0;
}

static void nilfs_dev_attr_release(struct kobject *kobj)
{
	struct the_nilfs *nilfs = container_of(kobj, struct the_nilfs,
						ns_dev_kobj);
	complete(&nilfs->ns_dev_kobj_unregister);
}

static const struct sysfs_ops nilfs_dev_attr_ops = {
	.show	= nilfs_dev_attr_show,
	.store	= nilfs_dev_attr_store,
};

static struct kobj_type nilfs_dev_ktype = {
	.default_attrs	= nilfs_dev_attrs,
	.sysfs_ops	= &nilfs_dev_attr_ops,
	.release	= nilfs_dev_attr_release,
};

int nilfs_sysfs_create_device_group(struct super_block *sb)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	size_t devgrp_size = sizeof(struct nilfs_sysfs_dev_subgroups);
	int err;

	nilfs->ns_dev_subgroups = kzalloc(devgrp_size, GFP_KERNEL);
	if (unlikely(!nilfs->ns_dev_subgroups)) {
		err = -ENOMEM;
		nilfs_err(sb, "unable to allocate memory for device group");
		goto failed_create_device_group;
	}

	nilfs->ns_dev_kobj.kset = nilfs_kset;
	init_completion(&nilfs->ns_dev_kobj_unregister);
	err = kobject_init_and_add(&nilfs->ns_dev_kobj, &nilfs_dev_ktype, NULL,
				    "%s", sb->s_id);
	if (err)
		goto cleanup_dev_kobject;

	err = nilfs_sysfs_create_mounted_snapshots_group(nilfs);
	if (err)
		goto cleanup_dev_kobject;

	err = nilfs_sysfs_create_checkpoints_group(nilfs);
	if (err)
		goto delete_mounted_snapshots_group;

	err = nilfs_sysfs_create_segments_group(nilfs);
	if (err)
		goto delete_checkpoints_group;

	err = nilfs_sysfs_create_superblock_group(nilfs);
	if (err)
		goto delete_segments_group;

	err = nilfs_sysfs_create_segctor_group(nilfs);
	if (err)
		goto delete_superblock_group;

	return 0;

delete_superblock_group:
	nilfs_sysfs_delete_superblock_group(nilfs);

delete_segments_group:
	nilfs_sysfs_delete_segments_group(nilfs);

delete_checkpoints_group:
	nilfs_sysfs_delete_checkpoints_group(nilfs);

delete_mounted_snapshots_group:
	nilfs_sysfs_delete_mounted_snapshots_group(nilfs);

cleanup_dev_kobject:
	kobject_put(&nilfs->ns_dev_kobj);
	kfree(nilfs->ns_dev_subgroups);

failed_create_device_group:
	return err;
}

void nilfs_sysfs_delete_device_group(struct the_nilfs *nilfs)
{
	nilfs_sysfs_delete_mounted_snapshots_group(nilfs);
	nilfs_sysfs_delete_checkpoints_group(nilfs);
	nilfs_sysfs_delete_segments_group(nilfs);
	nilfs_sysfs_delete_superblock_group(nilfs);
	nilfs_sysfs_delete_segctor_group(nilfs);
	kobject_del(&nilfs->ns_dev_kobj);
	kobject_put(&nilfs->ns_dev_kobj);
	kfree(nilfs->ns_dev_subgroups);
}

/************************************************************************
 *                        NILFS feature attrs                           *
 ************************************************************************/

static ssize_t nilfs_feature_revision_show(struct kobject *kobj,
					    struct attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d.%d\n",
			NILFS_CURRENT_REV, NILFS_MINOR_REV);
}

static const char features_readme_str[] =
	"The features group contains attributes that describe NILFS file\n"
	"system driver features.\n\n"
	"(1) revision\n\tshow current revision of NILFS file system driver.\n";

static ssize_t nilfs_feature_README_show(struct kobject *kobj,
					 struct attribute *attr,
					 char *buf)
{
	return snprintf(buf, PAGE_SIZE, features_readme_str);
}

NILFS_FEATURE_RO_ATTR(revision);
NILFS_FEATURE_RO_ATTR(README);

static struct attribute *nilfs_feature_attrs[] = {
	NILFS_FEATURE_ATTR_LIST(revision),
	NILFS_FEATURE_ATTR_LIST(README),
	NULL,
};

static const struct attribute_group nilfs_feature_attr_group = {
	.name = "features",
	.attrs = nilfs_feature_attrs,
};

int __init nilfs_sysfs_init(void)
{
	int err;

	nilfs_kset = kset_create_and_add(NILFS_ROOT_GROUP_NAME, NULL, fs_kobj);
	if (!nilfs_kset) {
		err = -ENOMEM;
		nilfs_err(NULL, "unable to create sysfs entry: err=%d", err);
		goto failed_sysfs_init;
	}

	err = sysfs_create_group(&nilfs_kset->kobj, &nilfs_feature_attr_group);
	if (unlikely(err)) {
		nilfs_err(NULL, "unable to create feature group: err=%d", err);
		goto cleanup_sysfs_init;
	}

	return 0;

cleanup_sysfs_init:
	kset_unregister(nilfs_kset);

failed_sysfs_init:
	return err;
}

void nilfs_sysfs_exit(void)
{
	sysfs_remove_group(&nilfs_kset->kobj, &nilfs_feature_attr_group);
	kset_unregister(nilfs_kset);
}
