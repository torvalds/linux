// SPDX-License-Identifier: GPL-2.0
//
#ifndef __SELFTEST_OVERLAYFS_WRAPPERS_H__
#define __SELFTEST_OVERLAYFS_WRAPPERS_H__

#define _GNU_SOURCE

#include <linux/types.h>
#include <linux/mount.h>
#include <sys/syscall.h>

static inline int sys_fsopen(const char *fsname, unsigned int flags)
{
	return syscall(__NR_fsopen, fsname, flags);
}

static inline int sys_fsconfig(int fd, unsigned int cmd, const char *key,
			       const char *value, int aux)
{
	return syscall(__NR_fsconfig, fd, cmd, key, value, aux);
}

static inline int sys_fsmount(int fd, unsigned int flags,
			      unsigned int attr_flags)
{
	return syscall(__NR_fsmount, fd, flags, attr_flags);
}

static inline int sys_mount(const char *src, const char *tgt, const char *fst,
			    unsigned long flags, const void *data)
{
	return syscall(__NR_mount, src, tgt, fst, flags, data);
}

#ifndef MOVE_MOUNT_F_EMPTY_PATH
#define MOVE_MOUNT_F_EMPTY_PATH 0x00000004 /* Empty from path permitted */
#endif

static inline int sys_move_mount(int from_dfd, const char *from_pathname,
				 int to_dfd, const char *to_pathname,
				 unsigned int flags)
{
	return syscall(__NR_move_mount, from_dfd, from_pathname, to_dfd,
		       to_pathname, flags);
}

#ifndef OPEN_TREE_CLONE
#define OPEN_TREE_CLONE 1
#endif

#ifndef OPEN_TREE_CLOEXEC
#define OPEN_TREE_CLOEXEC O_CLOEXEC
#endif

#ifndef AT_RECURSIVE
#define AT_RECURSIVE 0x8000
#endif

static inline int sys_open_tree(int dfd, const char *filename, unsigned int flags)
{
	return syscall(__NR_open_tree, dfd, filename, flags);
}

#endif
