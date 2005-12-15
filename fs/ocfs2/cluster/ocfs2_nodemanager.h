/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs2_nodemanager.h
 *
 * Header describing the interface between userspace and the kernel
 * for the ocfs2_nodemanager module.
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
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
 *
 */

#ifndef _OCFS2_NODEMANAGER_H
#define _OCFS2_NODEMANAGER_H

#define O2NM_API_VERSION	5

#define O2NM_MAX_NODES		255
#define O2NM_INVALID_NODE_NUM	255

/* host name, group name, cluster name all 64 bytes */
#define O2NM_MAX_NAME_LEN        64    // __NEW_UTS_LEN

#endif /* _OCFS2_NODEMANAGER_H */
