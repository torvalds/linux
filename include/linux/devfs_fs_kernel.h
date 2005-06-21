#ifndef _LINUX_DEVFS_FS_KERNEL_H
#define _LINUX_DEVFS_FS_KERNEL_H

#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <asm/semaphore.h>

static inline void devfs_remove(const char *fmt, ...)
{
}
#endif				/*  _LINUX_DEVFS_FS_KERNEL_H  */
