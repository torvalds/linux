/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
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
#ifndef __XFS_DMAPI_PRIV_H__
#define __XFS_DMAPI_PRIV_H__

/*
 *	Based on IO_ISDIRECT, decide which i_ flag is set.
 */
#define DM_SEM_FLAG_RD(ioflags) (((ioflags) & IO_ISDIRECT) ? \
			      DM_FLAGS_IMUX : 0)
#define DM_SEM_FLAG_WR	(DM_FLAGS_IALLOCSEM_WR | DM_FLAGS_IMUX)

#endif /*__XFS_DMAPI_PRIV_H__*/
