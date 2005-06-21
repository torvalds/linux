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
static inline void devfs_remove(const char *fmt, ...)
{
}
#endif				/*  _LINUX_DEVFS_FS_KERNEL_H  */
