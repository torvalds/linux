#ifndef _BTRFS_SYSFS_H_
#define _BTRFS_SYSFS_H_

/*
 * Data exported through sysfs
 */
extern u64 btrfs_debugfs_test;

enum btrfs_feature_set {
	FEAT_COMPAT,
	FEAT_COMPAT_RO,
	FEAT_INCOMPAT,
	FEAT_MAX
};

#define __INIT_KOBJ_ATTR(_name, _mode, _show, _store)			\
{									\
	.attr	= { .name = __stringify(_name), .mode = _mode },	\
	.show	= _show,						\
	.store	= _store,						\
}

#define BTRFS_ATTR_RW(_name, _show, _store)			\
	static struct kobj_attribute btrfs_attr_##_name =		\
			__INIT_KOBJ_ATTR(_name, 0644, _show, _store)

#define BTRFS_ATTR(_name, _show)					\
	static struct kobj_attribute btrfs_attr_##_name =		\
			__INIT_KOBJ_ATTR(_name, 0444, _show, NULL)

#define BTRFS_ATTR_PTR(_name)    (&btrfs_attr_##_name.attr)

#define BTRFS_RAID_ATTR(_name, _show)					\
	static struct kobj_attribute btrfs_raid_attr_##_name =		\
			__INIT_KOBJ_ATTR(_name, 0444, _show, NULL)

#define BTRFS_RAID_ATTR_PTR(_name)    (&btrfs_raid_attr_##_name.attr)


struct btrfs_feature_attr {
	struct kobj_attribute kobj_attr;
	enum btrfs_feature_set feature_set;
	u64 feature_bit;
};

#define BTRFS_FEAT_ATTR(_name, _feature_set, _prefix, _feature_bit)	     \
static struct btrfs_feature_attr btrfs_attr_##_name = {			     \
	.kobj_attr = __INIT_KOBJ_ATTR(_name, S_IRUGO,			     \
				      btrfs_feature_attr_show,		     \
				      btrfs_feature_attr_store),	     \
	.feature_set	= _feature_set,					     \
	.feature_bit	= _prefix ##_## _feature_bit,			     \
}
#define BTRFS_FEAT_ATTR_PTR(_name)    (&btrfs_attr_##_name.kobj_attr.attr)

#define BTRFS_FEAT_ATTR_COMPAT(name, feature) \
	BTRFS_FEAT_ATTR(name, FEAT_COMPAT, BTRFS_FEATURE_COMPAT, feature)
#define BTRFS_FEAT_ATTR_COMPAT_RO(name, feature) \
	BTRFS_FEAT_ATTR(name, FEAT_COMPAT_RO, BTRFS_FEATURE_COMPAT_RO, feature)
#define BTRFS_FEAT_ATTR_INCOMPAT(name, feature) \
	BTRFS_FEAT_ATTR(name, FEAT_INCOMPAT, BTRFS_FEATURE_INCOMPAT, feature)

/* convert from attribute */
static inline struct btrfs_feature_attr *
to_btrfs_feature_attr(struct kobj_attribute *a)
{
	return container_of(a, struct btrfs_feature_attr, kobj_attr);
}

static inline struct kobj_attribute *attr_to_btrfs_attr(struct attribute *attr)
{
	return container_of(attr, struct kobj_attribute, attr);
}

static inline struct btrfs_feature_attr *
attr_to_btrfs_feature_attr(struct attribute *attr)
{
	return to_btrfs_feature_attr(attr_to_btrfs_attr(attr));
}

char *btrfs_printable_features(enum btrfs_feature_set set, u64 flags);
extern const char * const btrfs_feature_set_names[3];
extern struct kobj_type space_info_ktype;
extern struct kobj_type btrfs_raid_ktype;
int btrfs_sysfs_add_device_link(struct btrfs_fs_devices *fs_devices,
		struct btrfs_device *one_device);
int btrfs_sysfs_rm_device_link(struct btrfs_fs_devices *fs_devices,
                struct btrfs_device *one_device);
int btrfs_sysfs_add_fsid(struct btrfs_fs_devices *fs_devs,
				struct kobject *parent);
int btrfs_sysfs_add_device(struct btrfs_fs_devices *fs_devs);
void btrfs_sysfs_remove_fsid(struct btrfs_fs_devices *fs_devs);
void btrfs_sysfs_feature_update(struct btrfs_fs_info *fs_info,
		u64 bit, enum btrfs_feature_set set);

#endif /* _BTRFS_SYSFS_H_ */
