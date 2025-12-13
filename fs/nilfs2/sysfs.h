/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Sysfs support declarations.
 *
 * Copyright (C) 2005-2014 Nippon Telegraph and Telephone Corporation.
 * Copyright (C) 2014 HGST, Inc., a Western Digital Company.
 *
 * Written by Vyacheslav Dubeyko <Vyacheslav.Dubeyko@hgst.com>
 */

#ifndef _NILFS_SYSFS_H
#define _NILFS_SYSFS_H

#include <linux/sysfs.h>

#define NILFS_ROOT_GROUP_NAME	"nilfs2"

/*
 * struct nilfs_sysfs_dev_subgroups - device subgroup kernel objects
 * @sg_superblock_kobj: /sys/fs/<nilfs>/<device>/superblock
 * @sg_superblock_kobj_unregister: completion state
 * @sg_segctor_kobj: /sys/fs/<nilfs>/<device>/segctor
 * @sg_segctor_kobj_unregister: completion state
 * @sg_mounted_snapshots_kobj: /sys/fs/<nilfs>/<device>/mounted_snapshots
 * @sg_mounted_snapshots_kobj_unregister: completion state
 * @sg_checkpoints_kobj: /sys/fs/<nilfs>/<device>/checkpoints
 * @sg_checkpoints_kobj_unregister: completion state
 * @sg_segments_kobj: /sys/fs/<nilfs>/<device>/segments
 * @sg_segments_kobj_unregister: completion state
 */
struct nilfs_sysfs_dev_subgroups {
	/* /sys/fs/<nilfs>/<device>/superblock */
	struct kobject sg_superblock_kobj;
	struct completion sg_superblock_kobj_unregister;

	/* /sys/fs/<nilfs>/<device>/segctor */
	struct kobject sg_segctor_kobj;
	struct completion sg_segctor_kobj_unregister;

	/* /sys/fs/<nilfs>/<device>/mounted_snapshots */
	struct kobject sg_mounted_snapshots_kobj;
	struct completion sg_mounted_snapshots_kobj_unregister;

	/* /sys/fs/<nilfs>/<device>/checkpoints */
	struct kobject sg_checkpoints_kobj;
	struct completion sg_checkpoints_kobj_unregister;

	/* /sys/fs/<nilfs>/<device>/segments */
	struct kobject sg_segments_kobj;
	struct completion sg_segments_kobj_unregister;
};

#define NILFS_KOBJ_ATTR_STRUCT(name) \
struct nilfs_##name##_attr { \
	struct attribute attr; \
	ssize_t (*show)(struct kobject *, struct kobj_attribute *, \
			char *); \
	ssize_t (*store)(struct kobject *, struct kobj_attribute *, \
			 const char *, size_t); \
}

NILFS_KOBJ_ATTR_STRUCT(feature);

#define NILFS_DEV_ATTR_STRUCT(name) \
struct nilfs_##name##_attr { \
	struct attribute attr; \
	ssize_t (*show)(struct nilfs_##name##_attr *, struct the_nilfs *, \
			char *); \
	ssize_t (*store)(struct nilfs_##name##_attr *, struct the_nilfs *, \
			 const char *, size_t); \
}

NILFS_DEV_ATTR_STRUCT(dev);
NILFS_DEV_ATTR_STRUCT(segments);
NILFS_DEV_ATTR_STRUCT(mounted_snapshots);
NILFS_DEV_ATTR_STRUCT(checkpoints);
NILFS_DEV_ATTR_STRUCT(superblock);
NILFS_DEV_ATTR_STRUCT(segctor);

#define NILFS_CP_ATTR_STRUCT(name) \
struct nilfs_##name##_attr { \
	struct attribute attr; \
	ssize_t (*show)(struct nilfs_##name##_attr *, struct nilfs_root *, \
			char *); \
	ssize_t (*store)(struct nilfs_##name##_attr *, struct nilfs_root *, \
			 const char *, size_t); \
}

NILFS_CP_ATTR_STRUCT(snapshot);

#define NILFS_ATTR(type, name, mode, show, store) \
	static struct nilfs_##type##_attr nilfs_##type##_attr_##name = \
		__ATTR(name, mode, show, store)

#define NILFS_INFO_ATTR(type, name) \
	NILFS_ATTR(type, name, 0444, NULL, NULL)
#define NILFS_RO_ATTR(type, name) \
	NILFS_ATTR(type, name, 0444, nilfs_##type##_##name##_show, NULL)
#define NILFS_RW_ATTR(type, name) \
	NILFS_ATTR(type, name, 0644, \
		    nilfs_##type##_##name##_show, \
		    nilfs_##type##_##name##_store)

#define NILFS_FEATURE_INFO_ATTR(name) \
	NILFS_INFO_ATTR(feature, name)
#define NILFS_FEATURE_RO_ATTR(name) \
	NILFS_RO_ATTR(feature, name)
#define NILFS_FEATURE_RW_ATTR(name) \
	NILFS_RW_ATTR(feature, name)

#define NILFS_DEV_INFO_ATTR(name) \
	NILFS_INFO_ATTR(dev, name)
#define NILFS_DEV_RO_ATTR(name) \
	NILFS_RO_ATTR(dev, name)
#define NILFS_DEV_RW_ATTR(name) \
	NILFS_RW_ATTR(dev, name)

#define NILFS_SEGMENTS_RO_ATTR(name) \
	NILFS_RO_ATTR(segments, name)
#define NILFS_SEGMENTS_RW_ATTR(name) \
	NILFS_RW_ATTR(segs_info, name)

#define NILFS_MOUNTED_SNAPSHOTS_RO_ATTR(name) \
	NILFS_RO_ATTR(mounted_snapshots, name)

#define NILFS_CHECKPOINTS_RO_ATTR(name) \
	NILFS_RO_ATTR(checkpoints, name)
#define NILFS_CHECKPOINTS_RW_ATTR(name) \
	NILFS_RW_ATTR(checkpoints, name)

#define NILFS_SNAPSHOT_INFO_ATTR(name) \
	NILFS_INFO_ATTR(snapshot, name)
#define NILFS_SNAPSHOT_RO_ATTR(name) \
	NILFS_RO_ATTR(snapshot, name)
#define NILFS_SNAPSHOT_RW_ATTR(name) \
	NILFS_RW_ATTR(snapshot, name)

#define NILFS_SUPERBLOCK_RO_ATTR(name) \
	NILFS_RO_ATTR(superblock, name)
#define NILFS_SUPERBLOCK_RW_ATTR(name) \
	NILFS_RW_ATTR(superblock, name)

#define NILFS_SEGCTOR_INFO_ATTR(name) \
	NILFS_INFO_ATTR(segctor, name)
#define NILFS_SEGCTOR_RO_ATTR(name) \
	NILFS_RO_ATTR(segctor, name)
#define NILFS_SEGCTOR_RW_ATTR(name) \
	NILFS_RW_ATTR(segctor, name)

#define NILFS_FEATURE_ATTR_LIST(name) \
	(&nilfs_feature_attr_##name.attr)
#define NILFS_DEV_ATTR_LIST(name) \
	(&nilfs_dev_attr_##name.attr)
#define NILFS_SEGMENTS_ATTR_LIST(name) \
	(&nilfs_segments_attr_##name.attr)
#define NILFS_MOUNTED_SNAPSHOTS_ATTR_LIST(name) \
	(&nilfs_mounted_snapshots_attr_##name.attr)
#define NILFS_CHECKPOINTS_ATTR_LIST(name) \
	(&nilfs_checkpoints_attr_##name.attr)
#define NILFS_SNAPSHOT_ATTR_LIST(name) \
	(&nilfs_snapshot_attr_##name.attr)
#define NILFS_SUPERBLOCK_ATTR_LIST(name) \
	(&nilfs_superblock_attr_##name.attr)
#define NILFS_SEGCTOR_ATTR_LIST(name) \
	(&nilfs_segctor_attr_##name.attr)

#endif /* _NILFS_SYSFS_H */
