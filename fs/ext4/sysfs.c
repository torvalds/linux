/*
 *  linux/fs/ext4/sysfs.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Theodore Ts'o (tytso@mit.edu)
 *
 */

#include <linux/time.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>

#include "ext4.h"
#include "ext4_jbd2.h"

struct ext4_attr {
	struct attribute attr;
	ssize_t (*show)(struct ext4_attr *, struct ext4_sb_info *, char *);
	ssize_t (*store)(struct ext4_attr *, struct ext4_sb_info *,
			 const char *, size_t);
	union {
		int offset;
		int deprecated_val;
	} u;
};

static int parse_strtoull(const char *buf,
		unsigned long long max, unsigned long long *value)
{
	int ret;

	ret = kstrtoull(skip_spaces(buf), 0, value);
	if (!ret && *value > max)
		ret = -EINVAL;
	return ret;
}

static ssize_t delayed_allocation_blocks_show(struct ext4_attr *a,
					      struct ext4_sb_info *sbi,
					      char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%llu\n",
		(s64) EXT4_C2B(sbi,
			percpu_counter_sum(&sbi->s_dirtyclusters_counter)));
}

static ssize_t session_write_kbytes_show(struct ext4_attr *a,
					 struct ext4_sb_info *sbi, char *buf)
{
	struct super_block *sb = sbi->s_buddy_cache->i_sb;

	if (!sb->s_bdev->bd_part)
		return snprintf(buf, PAGE_SIZE, "0\n");
	return snprintf(buf, PAGE_SIZE, "%lu\n",
			(part_stat_read(sb->s_bdev->bd_part, sectors[1]) -
			 sbi->s_sectors_written_start) >> 1);
}

static ssize_t lifetime_write_kbytes_show(struct ext4_attr *a,
					  struct ext4_sb_info *sbi, char *buf)
{
	struct super_block *sb = sbi->s_buddy_cache->i_sb;

	if (!sb->s_bdev->bd_part)
		return snprintf(buf, PAGE_SIZE, "0\n");
	return snprintf(buf, PAGE_SIZE, "%llu\n",
			(unsigned long long)(sbi->s_kbytes_written +
			((part_stat_read(sb->s_bdev->bd_part, sectors[1]) -
			  EXT4_SB(sb)->s_sectors_written_start) >> 1)));
}

static ssize_t inode_readahead_blks_store(struct ext4_attr *a,
					  struct ext4_sb_info *sbi,
					  const char *buf, size_t count)
{
	unsigned long t;
	int ret;

	ret = kstrtoul(skip_spaces(buf), 0, &t);
	if (ret)
		return ret;

	if (t && (!is_power_of_2(t) || t > 0x40000000))
		return -EINVAL;

	sbi->s_inode_readahead_blks = t;
	return count;
}

static ssize_t sbi_ui_show(struct ext4_attr *a,
			   struct ext4_sb_info *sbi, char *buf)
{
	unsigned int *ui = (unsigned int *) (((char *) sbi) + a->u.offset);

	return snprintf(buf, PAGE_SIZE, "%u\n", *ui);
}

static ssize_t sbi_ui_store(struct ext4_attr *a,
			    struct ext4_sb_info *sbi,
			    const char *buf, size_t count)
{
	unsigned int *ui = (unsigned int *) (((char *) sbi) + a->u.offset);
	unsigned long t;
	int ret;

	ret = kstrtoul(skip_spaces(buf), 0, &t);
	if (ret)
		return ret;
	*ui = t;
	return count;
}

static ssize_t es_ui_show(struct ext4_attr *a,
			   struct ext4_sb_info *sbi, char *buf)
{

	unsigned int *ui = (unsigned int *) (((char *) sbi->s_es) +
			   a->u.offset);

	return snprintf(buf, PAGE_SIZE, "%u\n", *ui);
}

static ssize_t reserved_clusters_show(struct ext4_attr *a,
				  struct ext4_sb_info *sbi, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%llu\n",
		(unsigned long long) atomic64_read(&sbi->s_resv_clusters));
}

static ssize_t reserved_clusters_store(struct ext4_attr *a,
				   struct ext4_sb_info *sbi,
				   const char *buf, size_t count)
{
	unsigned long long val;
	ext4_fsblk_t clusters = (ext4_blocks_count(sbi->s_es) >>
				 sbi->s_cluster_bits);

	if (parse_strtoull(buf, -1ULL, &val))
		return -EINVAL;

	if (val >= clusters)
		return -EINVAL;

	atomic64_set(&sbi->s_resv_clusters, val);
	return count;
}

static ssize_t trigger_test_error(struct ext4_attr *a,
				  struct ext4_sb_info *sbi,
				  const char *buf, size_t count)
{
	int len = count;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (len && buf[len-1] == '\n')
		len--;

	if (len)
		ext4_error(sbi->s_sb, "%.*s", len, buf);
	return count;
}

static ssize_t sbi_deprecated_show(struct ext4_attr *a,
				   struct ext4_sb_info *sbi, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", a->u.deprecated_val);
}

#define EXT4_ATTR_OFFSET(_name,_mode,_show,_store,_elname) \
static struct ext4_attr ext4_attr_##_name = {			\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.show	= _show,					\
	.store	= _store,					\
	.u = {							\
		.offset = offsetof(struct ext4_sb_info, _elname),\
	},							\
}

#define EXT4_ATTR_OFFSET_ES(_name,_mode,_show,_store,_elname)		\
static struct ext4_attr ext4_attr_##_name = {				\
	.attr = {.name = __stringify(_name), .mode = _mode },		\
	.show	= _show,						\
	.store	= _store,						\
	.u = {								\
		.offset = offsetof(struct ext4_super_block, _elname),	\
	},								\
}

#define EXT4_ATTR(name, mode, show, store) \
static struct ext4_attr ext4_attr_##name = __ATTR(name, mode, show, store)

#define EXT4_INFO_ATTR(name) EXT4_ATTR(name, 0444, NULL, NULL)
#define EXT4_RO_ATTR(name) EXT4_ATTR(name, 0444, name##_show, NULL)
#define EXT4_RW_ATTR(name) EXT4_ATTR(name, 0644, name##_show, name##_store)

#define EXT4_RO_ATTR_ES_UI(name, elname)	\
	EXT4_ATTR_OFFSET_ES(name, 0444, es_ui_show, NULL, elname)
#define EXT4_RW_ATTR_SBI_UI(name, elname)	\
	EXT4_ATTR_OFFSET(name, 0644, sbi_ui_show, sbi_ui_store, elname)

#define ATTR_LIST(name) &ext4_attr_##name.attr
#define EXT4_DEPRECATED_ATTR(_name, _val)	\
static struct ext4_attr ext4_attr_##_name = {			\
	.attr = {.name = __stringify(_name), .mode = 0444 },	\
	.show	= sbi_deprecated_show,				\
	.u = {							\
		.deprecated_val = _val,				\
	},							\
}

EXT4_RO_ATTR(delayed_allocation_blocks);
EXT4_RO_ATTR(session_write_kbytes);
EXT4_RO_ATTR(lifetime_write_kbytes);
EXT4_RW_ATTR(reserved_clusters);
EXT4_ATTR_OFFSET(inode_readahead_blks, 0644, sbi_ui_show,
		 inode_readahead_blks_store, s_inode_readahead_blks);
EXT4_RW_ATTR_SBI_UI(inode_goal, s_inode_goal);
EXT4_RW_ATTR_SBI_UI(mb_stats, s_mb_stats);
EXT4_RW_ATTR_SBI_UI(mb_max_to_scan, s_mb_max_to_scan);
EXT4_RW_ATTR_SBI_UI(mb_min_to_scan, s_mb_min_to_scan);
EXT4_RW_ATTR_SBI_UI(mb_order2_req, s_mb_order2_reqs);
EXT4_RW_ATTR_SBI_UI(mb_stream_req, s_mb_stream_request);
EXT4_RW_ATTR_SBI_UI(mb_group_prealloc, s_mb_group_prealloc);
EXT4_DEPRECATED_ATTR(max_writeback_mb_bump, 128);
EXT4_RW_ATTR_SBI_UI(extent_max_zeroout_kb, s_extent_max_zeroout_kb);
EXT4_ATTR(trigger_fs_error, 0200, NULL, trigger_test_error);
EXT4_RW_ATTR_SBI_UI(err_ratelimit_interval_ms, s_err_ratelimit_state.interval);
EXT4_RW_ATTR_SBI_UI(err_ratelimit_burst, s_err_ratelimit_state.burst);
EXT4_RW_ATTR_SBI_UI(warning_ratelimit_interval_ms, s_warning_ratelimit_state.interval);
EXT4_RW_ATTR_SBI_UI(warning_ratelimit_burst, s_warning_ratelimit_state.burst);
EXT4_RW_ATTR_SBI_UI(msg_ratelimit_interval_ms, s_msg_ratelimit_state.interval);
EXT4_RW_ATTR_SBI_UI(msg_ratelimit_burst, s_msg_ratelimit_state.burst);
EXT4_RO_ATTR_ES_UI(errors_count, s_error_count);
EXT4_RO_ATTR_ES_UI(first_error_time, s_first_error_time);
EXT4_RO_ATTR_ES_UI(last_error_time, s_last_error_time);

static struct attribute *ext4_attrs[] = {
	ATTR_LIST(delayed_allocation_blocks),
	ATTR_LIST(session_write_kbytes),
	ATTR_LIST(lifetime_write_kbytes),
	ATTR_LIST(reserved_clusters),
	ATTR_LIST(inode_readahead_blks),
	ATTR_LIST(inode_goal),
	ATTR_LIST(mb_stats),
	ATTR_LIST(mb_max_to_scan),
	ATTR_LIST(mb_min_to_scan),
	ATTR_LIST(mb_order2_req),
	ATTR_LIST(mb_stream_req),
	ATTR_LIST(mb_group_prealloc),
	ATTR_LIST(max_writeback_mb_bump),
	ATTR_LIST(extent_max_zeroout_kb),
	ATTR_LIST(trigger_fs_error),
	ATTR_LIST(err_ratelimit_interval_ms),
	ATTR_LIST(err_ratelimit_burst),
	ATTR_LIST(warning_ratelimit_interval_ms),
	ATTR_LIST(warning_ratelimit_burst),
	ATTR_LIST(msg_ratelimit_interval_ms),
	ATTR_LIST(msg_ratelimit_burst),
	ATTR_LIST(errors_count),
	ATTR_LIST(first_error_time),
	ATTR_LIST(last_error_time),
	NULL,
};

/* Features this copy of ext4 supports */
EXT4_INFO_ATTR(lazy_itable_init);
EXT4_INFO_ATTR(batched_discard);
EXT4_INFO_ATTR(meta_bg_resize);
EXT4_INFO_ATTR(encryption);

static struct attribute *ext4_feat_attrs[] = {
	ATTR_LIST(lazy_itable_init),
	ATTR_LIST(batched_discard),
	ATTR_LIST(meta_bg_resize),
	ATTR_LIST(encryption),
	NULL,
};

static ssize_t ext4_attr_show(struct kobject *kobj,
			      struct attribute *attr, char *buf)
{
	struct ext4_sb_info *sbi = container_of(kobj, struct ext4_sb_info,
						s_kobj);
	struct ext4_attr *a = container_of(attr, struct ext4_attr, attr);

	return a->show ? a->show(a, sbi, buf) : 0;
}

static ssize_t ext4_attr_store(struct kobject *kobj,
			       struct attribute *attr,
			       const char *buf, size_t len)
{
	struct ext4_sb_info *sbi = container_of(kobj, struct ext4_sb_info,
						s_kobj);
	struct ext4_attr *a = container_of(attr, struct ext4_attr, attr);

	return a->store ? a->store(a, sbi, buf, len) : 0;
}

static void ext4_sb_release(struct kobject *kobj)
{
	struct ext4_sb_info *sbi = container_of(kobj, struct ext4_sb_info,
						s_kobj);
	complete(&sbi->s_kobj_unregister);
}

static const struct sysfs_ops ext4_attr_ops = {
	.show	= ext4_attr_show,
	.store	= ext4_attr_store,
};

static struct kobj_type ext4_sb_ktype = {
	.default_attrs	= ext4_attrs,
	.sysfs_ops	= &ext4_attr_ops,
	.release	= ext4_sb_release,
};

static struct kobj_type ext4_ktype = {
	.sysfs_ops	= &ext4_attr_ops,
};

static struct kset ext4_kset = {
	.kobj   = {.ktype = &ext4_ktype},
};

static ssize_t ext4_feat_show(struct kobject *kobj,
			      struct attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "supported\n");
}

/*
 * We can not use ext4_attr_show/store because it relies on the kobject
 * being embedded in the ext4_sb_info structure which is definitely not
 * true in this case.
 */
static const struct sysfs_ops ext4_feat_ops = {
	.show	= ext4_feat_show,
	.store	= NULL,
};

static struct kobj_type ext4_feat_ktype = {
	.default_attrs	= ext4_feat_attrs,
	.sysfs_ops	= &ext4_feat_ops,
};

static struct kobject ext4_feat = {
	.kset	= &ext4_kset,
};

int ext4_register_sysfs(struct super_block *sb)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	sbi->s_kobj.kset = &ext4_kset;
	init_completion(&sbi->s_kobj_unregister);
	return kobject_init_and_add(&sbi->s_kobj, &ext4_sb_ktype, NULL,
				    "%s", sb->s_id);
}

int __init ext4_init_sysfs(void)
{
	int ret;

	kobject_set_name(&ext4_kset.kobj, "ext4");
	ext4_kset.kobj.parent = fs_kobj;
	ret = kset_register(&ext4_kset);
	if (ret)
		return ret;

	ret = kobject_init_and_add(&ext4_feat, &ext4_feat_ktype,
				   NULL, "features");
	if (ret)
		kset_unregister(&ext4_kset);
	return ret;
}

void ext4_exit_sysfs(void)
{
	kobject_put(&ext4_feat);
	kset_unregister(&ext4_kset);
}

