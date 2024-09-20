/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __LINUX_NSFS_H
#define __LINUX_NSFS_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define NSIO	0xb7

/* Returns a file descriptor that refers to an owning user namespace */
#define NS_GET_USERNS		_IO(NSIO, 0x1)
/* Returns a file descriptor that refers to a parent namespace */
#define NS_GET_PARENT		_IO(NSIO, 0x2)
/* Returns the type of namespace (CLONE_NEW* value) referred to by
   file descriptor */
#define NS_GET_NSTYPE		_IO(NSIO, 0x3)
/* Get owner UID (in the caller's user namespace) for a user namespace */
#define NS_GET_OWNER_UID	_IO(NSIO, 0x4)
/* Get the id for a mount namespace */
#define NS_GET_MNTNS_ID		_IOR(NSIO, 0x5, __u64)
/* Translate pid from target pid namespace into the caller's pid namespace. */
#define NS_GET_PID_FROM_PIDNS	_IOR(NSIO, 0x6, int)
/* Return thread-group leader id of pid in the callers pid namespace. */
#define NS_GET_TGID_FROM_PIDNS	_IOR(NSIO, 0x7, int)
/* Translate pid from caller's pid namespace into a target pid namespace. */
#define NS_GET_PID_IN_PIDNS	_IOR(NSIO, 0x8, int)
/* Return thread-group leader id of pid in the target pid namespace. */
#define NS_GET_TGID_IN_PIDNS	_IOR(NSIO, 0x9, int)

#endif /* __LINUX_NSFS_H */
