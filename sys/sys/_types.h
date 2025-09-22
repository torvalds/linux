/*	$OpenBSD: _types.h,v 1.10 2022/08/06 13:31:13 semarie Exp $	*/

/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)types.h	8.3 (Berkeley) 1/5/94
 */

#ifndef _SYS__TYPES_H_
#define	_SYS__TYPES_H_

#include <machine/_types.h>

typedef __int64_t	__blkcnt_t;	/* blocks allocated for file */
typedef __int32_t	__blksize_t;	/* optimal blocksize for I/O */
typedef	__int64_t	__clock_t;	/* ticks in CLOCKS_PER_SEC */
typedef	__int32_t	__clockid_t;	/* CLOCK_* identifiers */
typedef	unsigned long	__cpuid_t;	/* CPU id */
typedef	__int32_t	__dev_t;	/* device number */
typedef	__uint32_t	__fixpt_t;	/* fixed point number */
typedef	__uint64_t	__fsblkcnt_t;	/* file system block count */
typedef	__uint64_t	__fsfilcnt_t;	/* file system file count */
typedef	__uint32_t	__gid_t;	/* group id */
typedef	__uint32_t	__id_t;		/* may contain pid, uid or gid */
typedef	__uint32_t	__in_addr_t;	/* base type for internet address */
typedef	__uint16_t	__in_port_t;	/* IP port type */
typedef	__uint64_t	__ino_t;	/* inode number */
typedef	long		__key_t;	/* IPC key (for Sys V IPC) */
typedef	__uint32_t	__mode_t;	/* permissions */
typedef	__uint32_t	__nlink_t;	/* link count */
typedef	__int64_t	__off_t;	/* file offset or size */
typedef	__int32_t	__pid_t;	/* process id */
typedef	__uint64_t	__rlim_t;	/* resource limit */
typedef	__uint8_t	__sa_family_t;	/* sockaddr address family type */
typedef	__int32_t	__segsz_t;	/* segment size */
typedef	__uint32_t	__socklen_t;	/* length type for network syscalls */
typedef	long		__suseconds_t;	/* microseconds (signed) */
typedef	__int64_t	__time_t;	/* epoch time */
typedef	__int32_t	__timer_t;	/* POSIX timer identifiers */
typedef	__uint32_t	__uid_t;	/* user id */
typedef	__uint32_t	__useconds_t;	/* microseconds */

/*
 * mbstate_t is an opaque object to keep conversion state, during multibyte
 * stream conversions. The content must not be referenced by user programs.
 */
typedef union {
	char __mbstate8[128];
	__int64_t __mbstateL;			/* for alignment */
} __mbstate_t;

#endif /* !_SYS__TYPES_H_ */
