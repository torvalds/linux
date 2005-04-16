#ifndef _LINUX_DEVFS_FS_KERNEL_H
#define _LINUX_DEVFS_FS_KERNEL_H

#include <linux/fs.h>
#include <linux/config.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <asm/semaphore.h>

#define DEVFS_SUPER_MAGIC                0x1373

#ifdef CONFIG_DEVFS_FS
extern int devfs_mk_bdev(dev_t dev, umode_t mode, const char *fmt, ...)
    __attribute__ ((format(printf, 3, 4)));
extern int devfs_mk_cdev(dev_t dev, umode_t mode, const char *fmt, ...)
    __attribute__ ((format(printf, 3, 4)));
extern int devfs_mk_symlink(const char *name, const char *link);
extern int devfs_mk_dir(const char *fmt, ...)
    __attribute__ ((format(printf, 1, 2)));
extern void devfs_remove(const char *fmt, ...)
    __attribute__ ((format(printf, 1, 2)));
extern int devfs_register_tape(const char *name);
extern void devfs_unregister_tape(int num);
extern void mount_devfs_fs(void);
#else				/*  CONFIG_DEVFS_FS  */
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
static inline void mount_devfs_fs(void)
{
	return;
}
#endif				/*  CONFIG_DEVFS_FS  */
#endif				/*  _LINUX_DEVFS_FS_KERNEL_H  */
