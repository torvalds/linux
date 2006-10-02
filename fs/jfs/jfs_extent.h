/*
 *   Copyright (C) International Business Machines Corp., 2000-2001
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef	_H_JFS_EXTENT
#define _H_JFS_EXTENT

/*  get block allocation allocation hint as location of disk inode */
#define	INOHINT(ip)	\
	(addressPXD(&(JFS_IP(ip)->ixpxd)) + lengthPXD(&(JFS_IP(ip)->ixpxd)) - 1)

extern int	extAlloc(struct inode *, s64, s64, xad_t *, bool);
extern int	extFill(struct inode *, xad_t *);
extern int	extHint(struct inode *, s64, xad_t *);
extern int	extRealloc(struct inode *, s64, xad_t *, bool);
extern int	extRecord(struct inode *, xad_t *);

#endif	/* _H_JFS_EXTENT */
