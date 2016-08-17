/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
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

#ifndef _SPL_TYPES_H
#define	_SPL_TYPES_H

#include <linux/types.h>
#include <sys/sysmacros.h>

#ifndef ULLONG_MAX
#define ULLONG_MAX			(~0ULL)
#endif

#ifndef LLONG_MAX
#define LLONG_MAX			((long long)(~0ULL>>1))
#endif

typedef enum { B_FALSE=0, B_TRUE=1 }	boolean_t;
typedef unsigned long			intptr_t;
typedef unsigned long			ulong_t;
typedef unsigned int			uint_t;
typedef unsigned char			uchar_t;
typedef unsigned long long		u_longlong_t;
typedef unsigned long long		u_offset_t;
typedef unsigned long long		rlim64_t;
typedef long long			longlong_t;
typedef long long			offset_t;
typedef struct task_struct		kthread_t;
typedef struct task_struct		proc_t;
typedef short				pri_t;
typedef struct timespec			timestruc_t; /* definition per SVr4 */
typedef struct timespec			timespec_t;
typedef longlong_t			hrtime_t;
typedef unsigned short			ushort_t;
typedef u_longlong_t			len_t;
typedef longlong_t			diskaddr_t;
typedef ushort_t			o_mode_t;
typedef uint_t				major_t;
typedef uint_t				minor_t;
typedef ulong_t				pgcnt_t;
typedef long				spgcnt_t;
typedef short				index_t;
typedef int				id_t;

extern proc_t p0;

#endif	/* _SPL_TYPES_H */
