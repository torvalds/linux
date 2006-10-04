/*
 * Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#ifndef __LOCK_DLM_PLOCK_DOT_H__
#define __LOCK_DLM_PLOCK_DOT_H__

#define GDLM_PLOCK_MISC_NAME		"lock_dlm_plock"

#define GDLM_PLOCK_VERSION_MAJOR	1
#define GDLM_PLOCK_VERSION_MINOR	1
#define GDLM_PLOCK_VERSION_PATCH	0

enum {
	GDLM_PLOCK_OP_LOCK = 1,
	GDLM_PLOCK_OP_UNLOCK,
	GDLM_PLOCK_OP_GET,
};

struct gdlm_plock_info {
	__u32 version[3];
	__u8 optype;
	__u8 ex;
	__u8 wait;
	__u8 pad;
	__u32 pid;
	__s32 nodeid;
	__s32 rv;
	__u32 fsid;
	__u64 number;
	__u64 start;
	__u64 end;
	__u64 owner;
};

#endif

