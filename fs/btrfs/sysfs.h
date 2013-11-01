#ifndef _BTRFS_SYSFS_H_
#define _BTRFS_SYSFS_H_

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

struct btrfs_feature_attr {
	struct kobj_attribute kobj_attr;
	enum btrfs_feature_set feature_set;
	u64 feature_bit;
};

#define BTRFS_FEAT_ATTR(_name, _feature_set, _prefix, _feature_bit)	     \
static struct btrfs_feature_attr btrfs_attr_##_name = {			     \
	.kobj_attr = __INIT_KOBJ_ATTR(_name, S_IRUGO,			     \
				      btrfs_feature_attr_show, NULL),	     \
	.feature_set	= _feature_set,					     \
	.feature_bit	= _prefix ##_## _feature_bit,			     \
}
#define BTRFS_FEAT_ATTR_PTR(_name)    (&btrfs_attr_##_name.kobj_attr.attr)

#define BTRFS_FEAT_ATTR_COMPAT(name, feature) \
	BTRFS_FEAT_ATTR(name, FEAT_COMPAT, BTRFS_FEATURE_COMPAT, feature)
#define BTRFS_FEAT_ATTR_COMPAT_RO(name, feature) \
	BTRFS_FEAT_ATTR(name, FEAT_COMPAT_RO, BTRFS_FEATURE_COMPAT, feature)
#define BTRFS_FEAT_ATTR_INCOMPAT(name, feature) \
	BTRFS_FEAT_ATTR(name, FEAT_INCOMPAT, BTRFS_FEATURE_INCOMPAT, feature)

/* convert from attribute */
#define to_btrfs_feature_attr(a) \
			container_of(a, struct btrfs_feature_attr, kobj_attr)
#endif /* _BTRFS_SYSFS_H_ */
