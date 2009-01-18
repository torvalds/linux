/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs2_lockid.h
 *
 * Defines OCFS2 lockid bits.
 *
 * Copyright (C) 2002, 2005 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
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
	/* Need to differntiate from [R]ename.. serializing writes is the
	 * important job it does, anyway. */
	[OCFS2_LOCK_TYPE_RW] = "Write/Read",
	[OCFS2_LOCK_TYPE_DENTRY] = "Dentry",
	[OCFS2_LOCK_TYPE_OPEN] = "Open",
	[OCFS2_LOCK_TYPE_FLOCK] = "Flock",
	[OCFS2_LOCK_TYPE_QINFO] = "Quota",
};

static inline const char *ocfs2_lock_type_string(enum ocfs2_lock_type type)
{
#ifdef __KERNEL__
	BUG_ON(type >= OCFS2_NUM_LOCK_TYPES);
#endif
	return ocfs2_lock_type_strings[type];
}

#endif  /* OCFS2_LOCKID_H */
