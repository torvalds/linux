/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2007 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef _LINUX_DLM_DEVICE_H
#define _LINUX_DLM_DEVICE_H

/* This is the device interface for dlm, most users will use a library
 * interface.
 */

#include <linux/dlm.h>
#include <linux/types.h>

#define DLM_USER_LVB_LEN	32

/* Version of the device interface */
#define DLM_DEVICE_VERSION_MAJOR 6
#define DLM_DEVICE_VERSION_MINOR 0
#define DLM_DEVICE_VERSION_PATCH 2

/* struct passed to the lock write */
struct dlm_lock_params {
	__u8 mode;
	__u8 namelen;
	__u16 unused;
	__u32 flags;
	__u32 lkid;
	__u32 parent;
	__u64 xid;
	__u64 timeout;
	void __user *castparam;
	void __user *castaddr;
	void __user *bastparam;
	void __user *bastaddr;
	struct dlm_lksb __user *lksb;
	char lvb[DLM_USER_LVB_LEN];
	char name[0];
};

struct dlm_lspace_params {
	__u32 flags;
	__u32 minor;
	char name[0];
};

struct dlm_purge_params {
	__u32 nodeid;
	__u32 pid;
};

struct dlm_write_request {
	__u32 version[3];
	__u8 cmd;
	__u8 is64bit;
	__u8 unused[2];

	union  {
		struct dlm_lock_params   lock;
		struct dlm_lspace_params lspace;
		struct dlm_purge_params  purge;
	} i;
};

struct dlm_device_version {
	__u32 version[3];
};

/* struct read from the "device" fd,
   consists mainly of userspace pointers for the library to use */

struct dlm_lock_result {
	__u32 version[3];
	__u32 length;
	void __user * user_astaddr;
	void __user * user_astparam;
	struct dlm_lksb __user * user_lksb;
	struct dlm_lksb lksb;
	__u8 bast_mode;
	__u8 unused[3];
	/* Offsets may be zero if no data is present */
	__u32 lvb_offset;
};

/* Commands passed to the device */
#define DLM_USER_LOCK         1
#define DLM_USER_UNLOCK       2
#define DLM_USER_QUERY        3
#define DLM_USER_CREATE_LOCKSPACE  4
#define DLM_USER_REMOVE_LOCKSPACE  5
#define DLM_USER_PURGE        6
#define DLM_USER_DEADLOCK     7

/* Lockspace flags */
#define DLM_USER_LSFLG_AUTOFREE   1
#define DLM_USER_LSFLG_FORCEFREE  2

#endif

