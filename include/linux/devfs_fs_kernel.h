#ifndef _LINUX_DEVFS_FS_KERNEL_H
#define _LINUX_DEVFS_FS_KERNEL_H

#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <asm/semaphore.h>

static inline int devfs_mk_bdev(dev_t dev, umode_t mode, const char *fmt, ...)
{
	return 0;
}
static inline int devfs_mk_cdev(dev_t dev, umode_t mode, const char *fmt, ...)
{
	return 0;
}
static inline int devfs_mk_symlink(const char *name, const char *link)
{
	return 0;
}
static inline int devfs_mk_dir(const char *fmt, ...)
{
	return 0;
}
static inline void devfs_remove(const char *fmt, ...)
{
}
static inline int devfs_register_tape(const char *name)
{
	return -1;
}
static inline void devfs_unregister_tape(int num)
{
}
#endif				/*  _LINUX_DEVFS_FS_KERNEL_H  */
