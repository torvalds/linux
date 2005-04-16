/*
 * Copyright (c) 2000-2004 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
#ifndef	__XFS_SUPPORT_DEBUG_H__
#define	__XFS_SUPPORT_DEBUG_H__

#include <stdarg.h>

#define CE_DEBUG        7               /* debug        */
#define CE_CONT         6               /* continuation */
#define CE_NOTE         5               /* notice       */
#define CE_WARN         4               /* warning      */
#define CE_ALERT        1               /* alert        */
#define CE_PANIC        0               /* panic        */

extern void icmn_err(int, char *, va_list);
/* PRINTFLIKE2 */
extern void cmn_err(int, char *, ...);

#ifndef STATIC
# define STATIC static
#endif

#ifdef DEBUG
# ifdef lint
#  define ASSERT(EX)	((void)0) /* avoid "constant in conditional" babble */
# else
#  define ASSERT(EX) ((!doass||(EX))?((void)0):assfail(#EX, __FILE__, __LINE__))
# endif	/* lint */
#else
# define ASSERT(x)	((void)0)
#endif

extern int doass;		/* dynamically turn off asserts */
extern void assfail(char *, char *, int);
#ifdef DEBUG
extern unsigned long random(void);
extern int get_thread_id(void);
#endif

#define ASSERT_ALWAYS(EX)  ((EX)?((void)0):assfail(#EX, __FILE__, __LINE__))
#define	debug_stop_all_cpus(param)	/* param is "cpumask_t *" */

#endif  /* __XFS_SUPPORT_DEBUG_H__ */
