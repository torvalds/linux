/* @(#)fctldefs.h	1.2 98/10/08 Copyright 1996 J. Schilling */
/*
 *	Generic header for users of open(), creat() and chmod()
 *
 *	Copyright (c) 1996 J. Schilling
 */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _FCTLDEFS_H
#define	_FCTLDEFS_H

#ifndef	_MCONFIG_H
#include <mconfig.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>

#ifdef	HAVE_FCNTL_H

#	include <fcntl.h>

#else	/* HAVE_FCNTL_H */

#	include <sys/file.h>

#endif	/* HAVE_FCNTL_H */

/*
 * Do not define more than O_RDONLY / O_WRONLY / O_RDWR
 * The values may differ.
 */
#ifndef	O_RDONLY
#define	O_RDONLY	0
#endif
#ifndef	O_WRONLY
#define	O_WRONLY	1
#endif
#ifndef	O_RDWR
#define	O_RDWR		2
#endif

#endif	/* _FCTLDEFS_H */
