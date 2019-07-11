/* SPDX-License-Identifier: GPL-2.0-only */
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * filecheck.h
 *
 * Online file check.
 *
 * Copyright (C) 2016 SuSE.  All rights reserved.
 */


#ifndef FILECHECK_H
#define FILECHECK_H

#include <linux/types.h>
#include <linux/list.h>


/* File check errno */
enum {
	OCFS2_FILECHECK_ERR_SUCCESS = 0,	/* Success */
	OCFS2_FILECHECK_ERR_FAILED = 1000,	/* Other failure */
	OCFS2_FILECHECK_ERR_INPROGRESS,		/* In progress */
	OCFS2_FILECHECK_ERR_READONLY,		/* Read only */
	OCFS2_FILECHECK_ERR_INJBD,		/* Buffer in jbd */
	OCFS2_FILECHECK_ERR_INVALIDINO,		/* Invalid ino */
	OCFS2_FILECHECK_ERR_BLOCKECC,		/* Block ecc */
	OCFS2_FILECHECK_ERR_BLOCKNO,		/* Block number */
	OCFS2_FILECHECK_ERR_VALIDFLAG,		/* Inode valid flag */
	OCFS2_FILECHECK_ERR_GENERATION,		/* Inode generation */
	OCFS2_FILECHECK_ERR_UNSUPPORTED		/* Unsupported */
};

#define OCFS2_FILECHECK_ERR_START	OCFS2_FILECHECK_ERR_FAILED
#define OCFS2_FILECHECK_ERR_END		OCFS2_FILECHECK_ERR_UNSUPPORTED

struct ocfs2_filecheck {
	struct list_head fc_head;	/* File check entry list head */
	spinlock_t fc_lock;
	unsigned int fc_max;	/* Maximum number of entry in list */
	unsigned int fc_size;	/* Current entry count in list */
	unsigned int fc_done;	/* Finished entry count in list */
};

#define OCFS2_FILECHECK_MAXSIZE		100
#define OCFS2_FILECHECK_MINSIZE		10

/* File check operation type */
enum {
	OCFS2_FILECHECK_TYPE_CHK = 0,	/* Check a file(inode) */
	OCFS2_FILECHECK_TYPE_FIX,	/* Fix a file(inode) */
	OCFS2_FILECHECK_TYPE_SET = 100	/* Set entry list maximum size */
};

struct ocfs2_filecheck_sysfs_entry {	/* sysfs entry per partition */
	struct kobject fs_kobj;
	struct completion fs_kobj_unregister;
	struct ocfs2_filecheck *fs_fcheck;
};


int ocfs2_filecheck_create_sysfs(struct ocfs2_super *osb);
void ocfs2_filecheck_remove_sysfs(struct ocfs2_super *osb);

#endif  /* FILECHECK_H */
