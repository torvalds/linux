/* SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * locks.h
 *
 * Function prototypes for Userspace file locking support
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#ifndef OCFS2_LOCKS_H
#define OCFS2_LOCKS_H

int ocfs2_flock(struct file *file, int cmd, struct file_lock *fl);
int ocfs2_lock(struct file *file, int cmd, struct file_lock *fl);

#endif /* OCFS2_LOCKS_H */
