/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ocfs2_lockid.h
 *
 * Defines OCFS2 lockid bits.
 *
 * Copyright (C) 2002, 2005 Oracle.  All rights reserved.
 */

#ifndef OCFS2_LOCKID_H
#define OCFS2_LOCKID_H

/* lock ids are made up in the following manner:
 * name[0]     --> type
 * name[1-6]   --> 6 pad characters, reserved for now
 * name[7-22]  --> block number, expressed in hex as 16 chars
 * name[23-30] --> i_generation, expressed in hex 8 chars
 * name[31]    --> '\0' */
#define OCFS2_LOCK_ID_MAX_LEN  32
#define OCFS2_LOCK_ID_PAD "000000"

#define OCFS2_DENTRY_LOCK_INO_START 18

enum ocfs2_lock_type {
	OCFS2_LOCK_TYPE_META = 0,
	OCFS2_LOCK_TYPE_DATA,
	OCFS2_LOCK_TYPE_SUPER,
	OCFS2_LOCK_TYPE_RENAME,
	OCFS2_LOCK_TYPE_RW,
	OCFS2_LOCK_TYPE_DENTRY,
	OCFS2_LOCK_TYPE_OPEN,
	OCFS2_LOCK_TYPE_FLOCK,
	OCFS2_LOCK_TYPE_QINFO,
	OCFS2_LOCK_TYPE_NFS_SYNC,
	OCFS2_LOCK_TYPE_ORPHAN_SCAN,
	OCFS2_LOCK_TYPE_REFCOUNT,
	OCFS2_LOCK_TYPE_TRIM_FS,
	OCFS2_NUM_LOCK_TYPES
};

static inline char ocfs2_lock_type_char(enum ocfs2_lock_type type)
{
	char c;
	switch (type) {
		case OCFS2_LOCK_TYPE_META:
			c = 'M';
			break;
		case OCFS2_LOCK_TYPE_DATA:
			c = 'D';
			break;
		case OCFS2_LOCK_TYPE_SUPER:
			c = 'S';
			break;
		case OCFS2_LOCK_TYPE_RENAME:
			c = 'R';
			break;
		case OCFS2_LOCK_TYPE_RW:
			c = 'W';
			break;
		case OCFS2_LOCK_TYPE_DENTRY:
			c = 'N';
			break;
		case OCFS2_LOCK_TYPE_OPEN:
			c = 'O';
			break;
		case OCFS2_LOCK_TYPE_FLOCK:
			c = 'F';
			break;
		case OCFS2_LOCK_TYPE_QINFO:
			c = 'Q';
			break;
		case OCFS2_LOCK_TYPE_NFS_SYNC:
			c = 'Y';
			break;
		case OCFS2_LOCK_TYPE_ORPHAN_SCAN:
			c = 'P';
			break;
		case OCFS2_LOCK_TYPE_REFCOUNT:
			c = 'T';
			break;
		case OCFS2_LOCK_TYPE_TRIM_FS:
			c = 'I';
			break;
		default:
			c = '\0';
	}

	return c;
}

static char *ocfs2_lock_type_strings[] = {
	[OCFS2_LOCK_TYPE_META] = "Meta",
	[OCFS2_LOCK_TYPE_DATA] = "Data",
	[OCFS2_LOCK_TYPE_SUPER] = "Super",
	[OCFS2_LOCK_TYPE_RENAME] = "Rename",
	/* Need to differentiate from [R]ename.. serializing writes is the
	 * important job it does, anyway. */
	[OCFS2_LOCK_TYPE_RW] = "Write/Read",
	[OCFS2_LOCK_TYPE_DENTRY] = "Dentry",
	[OCFS2_LOCK_TYPE_OPEN] = "Open",
	[OCFS2_LOCK_TYPE_FLOCK] = "Flock",
	[OCFS2_LOCK_TYPE_QINFO] = "Quota",
	[OCFS2_LOCK_TYPE_NFS_SYNC] = "NFSSync",
	[OCFS2_LOCK_TYPE_ORPHAN_SCAN] = "OrphanScan",
	[OCFS2_LOCK_TYPE_REFCOUNT] = "Refcount",
	[OCFS2_LOCK_TYPE_TRIM_FS] = "TrimFs",
};

static inline const char *ocfs2_lock_type_string(enum ocfs2_lock_type type)
{
#ifdef __KERNEL__
	BUG_ON(type >= OCFS2_NUM_LOCK_TYPES);
#endif
	return ocfs2_lock_type_strings[type];
}

#endif  /* OCFS2_LOCKID_H */
