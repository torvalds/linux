/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2007 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#ifndef _DLM_NETLINK_H
#define _DLM_NETLINK_H

#include <linux/types.h>
#include <linux/dlmconstants.h>

enum {
	DLM_STATUS_WAITING = 1,
	DLM_STATUS_GRANTED = 2,
	DLM_STATUS_CONVERT = 3,
};

#define DLM_LOCK_DATA_VERSION 1

struct dlm_lock_data {
	__u16 version;
	__u32 lockspace_id;
	int nodeid;
	int ownpid;
	__u32 id;
	__u32 remid;
	__u64 xid;
	__s8 status;
	__s8 grmode;
	__s8 rqmode;
	unsigned long timestamp;
	int resource_namelen;
	char resource_name[DLM_RESNAME_MAXLEN];
};

enum {
	DLM_CMD_UNSPEC = 0,
	DLM_CMD_HELLO,		/* user->kernel */
	DLM_CMD_TIMEOUT,	/* kernel->user */
	__DLM_CMD_MAX,
};

#define DLM_CMD_MAX (__DLM_CMD_MAX - 1)

enum {
	DLM_TYPE_UNSPEC = 0,
	DLM_TYPE_LOCK,
	__DLM_TYPE_MAX,
};

#define DLM_TYPE_MAX (__DLM_TYPE_MAX - 1)

#define DLM_GENL_VERSION 0x1
#define DLM_GENL_NAME "DLM"

#endif /* _DLM_NETLINK_H */
