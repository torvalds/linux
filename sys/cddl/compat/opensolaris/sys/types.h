/*-
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _OPENSOLARIS_SYS_TYPES_H_
#define	_OPENSOLARIS_SYS_TYPES_H_

/*
 * This is a bag of dirty hacks to keep things compiling.
 */

#include <sys/stdint.h>

#ifdef _KERNEL
typedef	int64_t		clock_t;
#define	_CLOCK_T_DECLARED
#endif

#include_next <sys/types.h>

#define	MAXNAMELEN	256

typedef	struct timespec	timestruc_t;
typedef	struct timespec	timespec_t;
typedef u_int		uint_t;
typedef u_char		uchar_t;
typedef u_short		ushort_t;
typedef u_long		ulong_t;
typedef long long	longlong_t;  
typedef unsigned long long u_longlong_t;
#ifndef	_OFF64_T_DECLARED
#define	_OFF64_T_DECLARED
typedef off_t		off64_t;
#endif
typedef id_t		taskid_t;
typedef id_t		projid_t;
typedef id_t		poolid_t;
typedef id_t		zoneid_t;
typedef id_t		ctid_t;
typedef	mode_t		o_mode_t;
typedef	uint64_t	pgcnt_t;
typedef	u_int		minor_t;

#ifdef _KERNEL

#define	B_FALSE	0
#define	B_TRUE	1

typedef	short		index_t;
typedef	off_t		offset_t;
#ifndef _PTRDIFF_T_DECLARED
typedef	__ptrdiff_t		ptrdiff_t;	/* pointer difference */
#define _PTRDIFF_T_DECLARED
#endif
typedef	int64_t		rlim64_t;
typedef	int		major_t;

#else
#ifdef NEED_SOLARIS_BOOLEAN
#if defined(__XOPEN_OR_POSIX)
typedef enum { _B_FALSE, _B_TRUE }	boolean_t;
#else
typedef enum { B_FALSE, B_TRUE }	boolean_t;
#endif /* defined(__XOPEN_OR_POSIX) */
#endif

typedef	longlong_t	offset_t;
typedef	u_longlong_t	u_offset_t;
typedef	uint64_t	upad64_t;
typedef	short		pri_t;
typedef	int32_t		daddr32_t;
typedef	int32_t		time32_t;
typedef	u_longlong_t	diskaddr_t;

#endif	/* !_KERNEL */

#endif	/* !_OPENSOLARIS_SYS_TYPES_H_ */
