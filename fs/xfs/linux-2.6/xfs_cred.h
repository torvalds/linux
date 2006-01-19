/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_CRED_H__
#define __XFS_CRED_H__

#include <linux/capability.h>

/*
 * Credentials
 */
typedef struct cred {
	/* EMPTY */
} cred_t;

extern struct cred *sys_cred;

/* this is a hack.. (assumes sys_cred is the only cred_t in the system) */
static __inline int capable_cred(cred_t *cr, int cid)
{
	return (cr == sys_cred) ? 1 : capable(cid);
}

#endif  /* __XFS_CRED_H__ */
