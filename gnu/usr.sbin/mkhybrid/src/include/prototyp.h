/* @(#)prototyp.h	1.7 98/10/08 Copyright 1995 J. Schilling */
/*
 *	Definitions for dealing with ANSI / KR C-Compilers
 *
 *	Copyright (c) 1995 J. Schilling
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

#ifndef	_PROTOTYP_H
#define	_PROTOTYP_H

#ifndef	PROTOTYPES
	/*
	 * If this has already been defined,
	 * someone else knows better than us...
	 */
#	ifdef	__STDC__
#		if	__STDC__				/* ANSI C */
#			define	PROTOTYPES
#		endif
#		if	defined(sun) && __STDC__ - 0 == 0	/* Sun C */
#			define	PROTOTYPES
#		endif
#	endif
#endif	/* PROTOTYPES */

/*
 * If we have prototypes, we should have stdlib.h string.h stdarg.h
 */
#ifdef	PROTOTYPES
#if	!(defined(SABER) && defined(sun))
#	ifndef	HAVE_STDARG_H
#		define	HAVE_STDARG_H
#	endif
#endif
#	ifndef	HAVE_STDLIB_H
#		define	HAVE_STDLIB_H
#	endif
#	ifndef	HAVE_STRING_H
#		define	HAVE_STRING_H
#	endif
#	ifndef	HAVE_STDC_HEADERS
#		define	HAVE_STDC_HEADERS
#	endif
#	ifndef	STDC_HEADERS
#		define	STDC_HEADERS	/* GNU name */
#	endif
#endif

#ifdef	NO_PROTOTYPES		/* Force not to use prototypes */
#	undef	PROTOTYPES
#endif

#ifdef	PROTOTYPES
#	define	__PR(a)	a
#else
#	define	__PR(a)	()
#endif

#endif	/* _PROTOTYP_H */
