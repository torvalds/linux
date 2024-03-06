/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SYSFS_H_
#define _BCACHEFS_SYSFS_H_

#include <linux/sysfs.h>

#ifndef NO_BCACHEFS_SYSFS

struct attribute;
struct sysfs_ops;

extern struct attribute *bch2_fs_files[];
extern struct attribute *bch2_fs_counters_files[];
extern struct attribute *bch2_fs_internal_files[];
extern struct attribute *bch2_fs_opts_dir_files[];
extern struct attribute *bch2_fs_time_stats_files[];
extern struct attribute *bch2_dev_files[];

extern const struct sysfs_ops bch2_fs_sysfs_ops;
extern const struct sysfs_ops bch2_fs_counters_sysfs_ops;
extern const struct sysfs_ops bch2_fs_internal_sysfs_ops;
extern const struct sysfs_ops bch2_fs_opts_dir_sysfs_ops;
extern const struct sysfs_ops bch2_fs_time_stats_sysfs_ops;
extern const struct sysfs_ops bch2_dev_sysfs_ops;

int bch2_opts_create_sysfs_files(struct kobject *);

#else

static struct attribute *bch2_fs_files[] = {};
static struct attribute *bch2_fs_counters_files[] = {};
static struct attribute *bch2_fs_internal_files[] = {};
static struct attribute *bch2_fs_opts_dir_files[] = {};
static struct attribute *bch2_fs_time_stats_files[] = {};
static struct attribute *bch2_dev_files[] = {};

static const struct sysfs_ops bch2_fs_sysfs_ops;
static const struct sysfs_ops bch2_fs_counters_sysfs_ops;
static const struct sysfs_ops bch2_fs_internal_sysfs_ops;
static const struct sysfs_ops bch2_fs_opts_dir_sysfs_ops;
static const struct sysfs_ops bch2_fs_time_stats_sysfs_ops;
static const struct sysfs_ops bch2_dev_sysfs_ops;

static inline int bch2_opts_create_sysfs_files(struct kobject *kobj) { return 0; }

#endif /* NO_BCACHEFS_SYSFS */

#endif  /* _BCACHEFS_SYSFS_H_ */
