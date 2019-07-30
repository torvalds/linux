/* SPDX-License-Identifier: GPL-2.0-only */
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * move_extents.h
 *
 * Copyright (C) 2011 Oracle.  All rights reserved.
 */
#ifndef OCFS2_MOVE_EXTENTS_H
#define OCFS2_MOVE_EXTENTS_H

int ocfs2_ioctl_move_extents(struct file *filp,  void __user *argp);

#endif /* OCFS2_MOVE_EXTENTS_H */
