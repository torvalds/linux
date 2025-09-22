/* @(#)statdefs.h	1.1 98/11/22 Copyright 1998 J. Schilling */
/*
 *	Definitions for stat() file mode
 *
 *	Copyright (c) 1998 J. Schilling
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

#ifndef	_STATDEFS_H
#define	_STATDEFS_H

#ifndef _MCONFIG_H
#include <mconfig.h>
#endif

#ifdef	STAT_MACROS_BROKEN
#undef	S_ISFIFO			/* Named pipe		*/
#undef	S_ISCHR				/* Character special	*/
#undef	S_ISMPC				/* UNUSED multiplexed c	*/
#undef	S_ISDIR				/* Directory		*/
#undef	S_ISNAM				/* Named file (XENIX)	*/
#undef	S_ISBLK				/* Block special	*/
#undef	S_ISMPB				/* UNUSED multiplexed b	*/
#undef	S_ISREG				/* Regular file		*/
#undef	S_ISCNT				/* Contiguous file	*/
#undef	S_ISLNK				/* Symbolic link	*/
#undef	S_ISSHAD			/* Solaris shadow inode	*/
#undef	S_ISSOCK			/* UNIX domain socket	*/
#undef	S_ISDOOR			/* Solaris DOOR		*/
#endif

#ifndef	S_ISFIFO			/* Named pipe		*/
#	ifdef	S_IFIFO
#		define	S_ISFIFO(m)	(((m) & S_IFMT) == S_IFIFO)
#	else
#		define	S_ISFIFO(m)	(0)
#	endif
#endif
#ifndef	S_ISCHR				/* Character special	*/
#	ifdef	S_IFCHR
#		define	S_ISCHR(m)	(((m) & S_IFMT) == S_IFCHR)
#	else
#		define	S_ISCHR(m)	(0)
#	endif
#endif
#ifndef	S_ISMPC				/* UNUSED multiplexed c	*/
#	ifdef	S_IFMPC
#		define	S_ISMPC(m)	(((m) & S_IFMT) == S_IFMPC)
#	else
#		define	S_ISMPC(m)	(0)
#	endif
#endif
#ifndef	S_ISDIR				/* Directory		*/
#	ifdef	S_IFDIR
#		define	S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR)
#	else
#		define	S_ISDIR(m)	(0)
#	endif
#endif
#ifndef	S_ISNAM				/* Named file (XENIX)	*/
#	ifdef	S_IFNAM
#		define	S_ISNAM(m)	(((m) & S_IFMT) == S_IFNAM)
#	else
#		define	S_ISNAM(m)	(0)
#	endif
#endif
#ifndef	S_ISBLK				/* Block special	*/
#	ifdef	S_IFBLK
#		define	S_ISBLK(m)	(((m) & S_IFMT) == S_IFBLK)
#	else
#		define	S_ISBLK(m)	(0)
#	endif
#endif
#ifndef	S_ISMPB				/* UNUSED multiplexed b	*/
#	ifdef	S_IFMPB
#		define	S_ISMPB(m)	(((m) & S_IFMT) == S_IFMPB)
#	else
#		define	S_ISMPB(m)	(0)
#	endif
#endif
#ifndef	S_ISREG				/* Regular file		*/
#	ifdef	S_IFREG
#		define	S_ISREG(m)	(((m) & S_IFMT) == S_IFREG)
#	else
#		define	S_ISREG(m)	(0)
#	endif
#endif
#ifndef	S_ISCNT				/* Contiguous file	*/
#	ifdef	S_IFCNT
#		define	S_ISCNT(m)	(((m) & S_IFMT) == S_IFCNT)
#	else
#		define	S_ISCNT(m)	(0)
#	endif
#endif
#ifndef	S_ISLNK				/* Symbolic link	*/
#	ifdef	S_IFLNK
#		define	S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)
#	else
#		define	S_ISLNK(m)	(0)
#	endif
#endif
#ifndef	S_ISSHAD			/* Solaris shadow inode	*/
#	ifdef	S_IFSHAD
#		define	S_ISSHAD(m)	(((m) & S_IFMT) == S_IFSHAD)
#	else
#		define	S_ISSHAD(m)	(0)
#	endif
#endif
#ifndef	S_ISSOCK			/* UNIX domain socket	*/
#	ifdef	S_IFSOCK
#		define	S_ISSOCK(m)	(((m) & S_IFMT) == S_IFSOCK)
#	else
#		define	S_ISSOCK(m)	(0)
#	endif
#endif
#ifndef	S_ISDOOR			/* Solaris DOOR		*/
#	ifdef	S_IFDOOR
#		define	S_ISDOOR(m)	(((m) & S_IFMT) == S_IFDOOR)
#	else
#		define	S_ISDOOR(m)	(0)
#	endif
#endif

#endif	/* _STATDEFS_H */

