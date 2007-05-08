/*
 * Copyright (c) 2000,2002,2005 Silicon Graphics, Inc.
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
#ifndef	__XFS_FS_SUBR_H__
#define __XFS_FS_SUBR_H__

struct cred;
extern int  fs_noerr(void);
extern int  fs_nosys(void);
extern void fs_noval(void);
extern void fs_tosspages(bhv_desc_t *, xfs_off_t, xfs_off_t, int);
extern int  fs_flushinval_pages(bhv_desc_t *, xfs_off_t, xfs_off_t, int);
extern int  fs_flush_pages(bhv_desc_t *, xfs_off_t, xfs_off_t, uint64_t, int);

#endif	/* __XFS_FS_SUBR_H__ */
