/*****************************************************************************\
 *  Copyright (C) 2015 Cluster Inc.
 *  Produced at ClusterHQ Inc (cf, DISCLAIMER).
 *  Written by Richard Yao <richard.yao@clusterhq.com>.
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

#ifndef _SPL_USER_H
#define _SPL_USER_H

/*
 * We have uf_info_t for areleasef(). We implement areleasef() using a global
 * linked list of all open file descriptors with the task structs referenced,
 * so accessing the correct descriptor from areleasef() only requires knowing
 * about the Linux task_struct. Since this is internal to our compatibility
 * layer, we make it an opaque type.
 *
 * XXX: If the descriptor changes under us, we would get an incorrect
 * reference.
 */

struct uf_info;
typedef struct uf_info uf_info_t;

#define P_FINFO(x) ((uf_info_t *)x)

#endif /* SPL_USER_H */
