/*
 * drivers/i2c/chips/SenseTek/stk_lk_defs.h
 * Basic Defines for Linux Kernel Driver
 *
 */

#ifndef __STK__LK_DEFS_H
#define __STK__LK_DEFS_H

#define CONFIG_STK_SHOW_INFO

#define ERR(format, args...) \
	printk(KERN_ERR "%s: " format, DEVICE_NAME, ## args)
#define WARNING(format, args...) \
	printk(KERN_WARNING "%s: " format, DEVICE_NAME, ## args)
#ifdef CONFIG_STK_SHOW_INFO
#define INFO(format, args...) \
	printk(KERN_INFO "%s: " format, DEVICE_NAME, ## args)
#else
#define INFO(format,args...)
#endif

#define __ATTR_BIN(_name,_mode,_read,_write,_size) { \
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.read	= _read,					\
	.write	= _write,					\
	.size = _size,                      \
}

#define __ATTR_BIN_RO(_name,_read,_size) __ATTR_BIN(_name,0444,_read,NULL,_size)
#ifdef CONFIG_STK_SYSFS_DBG
#define __ATTR_BIN_RW(_name,_read,_write,_size) __ATTR_BIN(_name,0666,_read,_write,_size)
#else
#define __ATTR_BIN_RW(_name,_read,_write,_size) __ATTR_BIN(_name,0644,_read,_write,_size)
#endif

#ifdef CONFIG_STK_SYSFS_DBG
#define __ATTR_RW(_name) __ATTR(_name,0666,_name##_show,_name##_store)
#else
#define __ATTR_RW(_name) __ATTR(_name,0666,_name##_show,_name##_store)
#endif

#define STK_LOCK(x) STK_LOCK##x

#include <linux/version.h>

#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
#define stk_bin_sysfs_read(name) name##_read(struct file *fptr,struct kobject *kobj, struct bin_attribute *bin_attr,	char * buffer, loff_t offset, size_t count)
#define stk_bin_sysfs_write(name) name##_write(struct file *fptr,struct kobject *kobj, struct bin_attribute *bin_attr,char * buffer, loff_t offset, size_t count)
#else
#define stk_bin_sysfs_read(name) name##_read(struct kobject *kobj, struct bin_attribute *bin_attr,	char * buffer, loff_t offset, size_t count)
#define stk_bin_sysfs_write(name) name##_write(struct kobject *kobj, struct bin_attribute *bin_attr,char * buffer, loff_t offset, size_t count)
#endif


#endif // __STK__LK_DEFS_H
