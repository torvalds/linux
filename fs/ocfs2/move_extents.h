/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * move_extents.h
 *
 * Copyright (C) 2011 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#ifndef OCFS2_MOVE_EXTENTS_H
#define OCFS2_MOVE_EXTENTS_H

int ocfs2_ioctl_move_extents(struct file *filp,  void __user *argp);

#endif /* OCFS2_MOVE_EXTENTS_H */
