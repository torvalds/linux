#ifndef TARGET_CORE_BACKEND_CONFIGFS_H
#define TARGET_CORE_BACKEND_CONFIGFS_H

#include <target/configfs_macros.h>

#define DEF_TB_DEV_ATTRIB_SHOW(_backend, _name)				\
static ssize_t _backend##_dev_show_attr_##_name(			\
	struct se_dev_attrib *da,					\
	char *page)							\
{									\
	return snprintf(page, PAGE_SIZE, "%u\n",			\
			(u32)da->da_dev->dev_attrib._name);		\
}

#define DEF_TB_DEV_ATTRIB_STORE(_backend, _name)			\
static ssize_t _backend##_dev_store_attr_##_name(			\
	struct se_dev_attrib *da,					\
	const char *page,						\
	size_t count)							\
{									\
	unsigned long val;						\
	int ret;							\
									\
	ret = kstrtoul(page, 0, &val);					\
	if (ret < 0) {							\
		pr_err("kstrtoul() failed with ret: %d\n", ret);	\
		return -EINVAL;						\
	}								\
	ret = se_dev_set_##_name(da->da_dev, (u32)val);			\
									\
	return (!ret) ? count : -EINVAL;				\
}

#define DEF_TB_DEV_ATTRIB(_backend, _name)				\
DEF_TB_DEV_ATTRIB_SHOW(_backend, _name);				\
DEF_TB_DEV_ATTRIB_STORE(_backend, _name);

#define DEF_TB_DEV_ATTRIB_RO(_backend, name)				\
DEF_TB_DEV_ATTRIB_SHOW(_backend, name);

CONFIGFS_EATTR_STRUCT(target_backend_dev_attrib, se_dev_attrib);
#define TB_DEV_ATTR(_backend, _name, _mode)				\
static struct target_backend_dev_attrib_attribute _backend##_dev_attrib_##_name = \
		__CONFIGFS_EATTR(_name, _mode,				\
		_backend##_dev_show_attr_##_name,			\
		_backend##_dev_store_attr_##_name);

#define TB_DEV_ATTR_RO(_backend, _name)						\
static struct target_backend_dev_attrib_attribute _backend##_dev_attrib_##_name = \
	__CONFIGFS_EATTR_RO(_name,					\
	_backend##_dev_show_attr_##_name);

#endif /* TARGET_CORE_BACKEND_CONFIGFS_H */
