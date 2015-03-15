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

/*
 * Default list of target backend device attributes as defined by
 * struct se_dev_attrib
 */

#define DEF_TB_DEFAULT_ATTRIBS(_backend)				\
	DEF_TB_DEV_ATTRIB(_backend, emulate_model_alias);		\
	TB_DEV_ATTR(_backend, emulate_model_alias, S_IRUGO | S_IWUSR);	\
	DEF_TB_DEV_ATTRIB(_backend, emulate_dpo);			\
	TB_DEV_ATTR(_backend, emulate_dpo, S_IRUGO | S_IWUSR);		\
	DEF_TB_DEV_ATTRIB(_backend, emulate_fua_write);			\
	TB_DEV_ATTR(_backend, emulate_fua_write, S_IRUGO | S_IWUSR);	\
	DEF_TB_DEV_ATTRIB(_backend, emulate_fua_read);			\
	TB_DEV_ATTR(_backend, emulate_fua_read, S_IRUGO | S_IWUSR);	\
	DEF_TB_DEV_ATTRIB(_backend, emulate_write_cache);		\
	TB_DEV_ATTR(_backend, emulate_write_cache, S_IRUGO | S_IWUSR);	\
	DEF_TB_DEV_ATTRIB(_backend, emulate_ua_intlck_ctrl);		\
	TB_DEV_ATTR(_backend, emulate_ua_intlck_ctrl, S_IRUGO | S_IWUSR); \
	DEF_TB_DEV_ATTRIB(_backend, emulate_tas);			\
	TB_DEV_ATTR(_backend, emulate_tas, S_IRUGO | S_IWUSR);		\
	DEF_TB_DEV_ATTRIB(_backend, emulate_tpu);			\
	TB_DEV_ATTR(_backend, emulate_tpu, S_IRUGO | S_IWUSR);		\
	DEF_TB_DEV_ATTRIB(_backend, emulate_tpws);			\
	TB_DEV_ATTR(_backend, emulate_tpws, S_IRUGO | S_IWUSR);		\
	DEF_TB_DEV_ATTRIB(_backend, emulate_caw);			\
	TB_DEV_ATTR(_backend, emulate_caw, S_IRUGO | S_IWUSR);		\
	DEF_TB_DEV_ATTRIB(_backend, emulate_3pc);			\
	TB_DEV_ATTR(_backend, emulate_3pc, S_IRUGO | S_IWUSR);		\
	DEF_TB_DEV_ATTRIB(_backend, pi_prot_type);			\
	TB_DEV_ATTR(_backend, pi_prot_type, S_IRUGO | S_IWUSR);		\
	DEF_TB_DEV_ATTRIB_RO(_backend, hw_pi_prot_type);		\
	TB_DEV_ATTR_RO(_backend, hw_pi_prot_type);			\
	DEF_TB_DEV_ATTRIB(_backend, pi_prot_format);			\
	TB_DEV_ATTR(_backend, pi_prot_format, S_IRUGO | S_IWUSR);	\
	DEF_TB_DEV_ATTRIB(_backend, enforce_pr_isids);			\
	TB_DEV_ATTR(_backend, enforce_pr_isids, S_IRUGO | S_IWUSR);	\
	DEF_TB_DEV_ATTRIB(_backend, is_nonrot);				\
	TB_DEV_ATTR(_backend, is_nonrot, S_IRUGO | S_IWUSR);		\
	DEF_TB_DEV_ATTRIB(_backend, emulate_rest_reord);		\
	TB_DEV_ATTR(_backend, emulate_rest_reord, S_IRUGO | S_IWUSR);	\
	DEF_TB_DEV_ATTRIB(_backend, force_pr_aptpl);			\
	TB_DEV_ATTR(_backend, force_pr_aptpl, S_IRUGO | S_IWUSR);	\
	DEF_TB_DEV_ATTRIB_RO(_backend, hw_block_size);			\
	TB_DEV_ATTR_RO(_backend, hw_block_size);			\
	DEF_TB_DEV_ATTRIB(_backend, block_size);			\
	TB_DEV_ATTR(_backend, block_size, S_IRUGO | S_IWUSR);		\
	DEF_TB_DEV_ATTRIB_RO(_backend, hw_max_sectors);			\
	TB_DEV_ATTR_RO(_backend, hw_max_sectors);			\
	DEF_TB_DEV_ATTRIB(_backend, optimal_sectors);			\
	TB_DEV_ATTR(_backend, optimal_sectors, S_IRUGO | S_IWUSR);	\
	DEF_TB_DEV_ATTRIB_RO(_backend, hw_queue_depth);			\
	TB_DEV_ATTR_RO(_backend, hw_queue_depth);			\
	DEF_TB_DEV_ATTRIB(_backend, queue_depth);			\
	TB_DEV_ATTR(_backend, queue_depth, S_IRUGO | S_IWUSR);		\
	DEF_TB_DEV_ATTRIB(_backend, max_unmap_lba_count);		\
	TB_DEV_ATTR(_backend, max_unmap_lba_count, S_IRUGO | S_IWUSR);	\
	DEF_TB_DEV_ATTRIB(_backend, max_unmap_block_desc_count);	\
	TB_DEV_ATTR(_backend, max_unmap_block_desc_count, S_IRUGO | S_IWUSR); \
	DEF_TB_DEV_ATTRIB(_backend, unmap_granularity);			\
	TB_DEV_ATTR(_backend, unmap_granularity, S_IRUGO | S_IWUSR);	\
	DEF_TB_DEV_ATTRIB(_backend, unmap_granularity_alignment);	\
	TB_DEV_ATTR(_backend, unmap_granularity_alignment, S_IRUGO | S_IWUSR); \
	DEF_TB_DEV_ATTRIB(_backend, max_write_same_len);		\
	TB_DEV_ATTR(_backend, max_write_same_len, S_IRUGO | S_IWUSR);

#endif /* TARGET_CORE_BACKEND_CONFIGFS_H */
