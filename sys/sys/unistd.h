/*	$OpenBSD: unistd.h,v 1.31 2015/07/20 00:56:10 guenther Exp $	*/
/*	$NetBSD: unistd.h,v 1.10 1994/06/29 06:46:06 cgd Exp $	*/

/*
 * Copyright (c) 1989, 1993
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
 *	@(#)unistd.h	8.2 (Berkeley) 1/7/94
 */

#ifndef _SYS_UNISTD_H_
#define	_SYS_UNISTD_H_

#include <sys/cdefs.h>

#define	_POSIX_VDISABLE		(0377)
#define	_POSIX_ASYNC_IO		(-1)
#define	_POSIX_PRIO_IO		(-1)
#define	_POSIX_SYNC_IO		(-1)

/* Define the POSIX.1 version we target for compliance. */
#define	_POSIX_VERSION		200809L

/* access function */
#define	F_OK		0	/* test for existence of file */
#define	X_OK		0x01	/* test for execute or search permission */
#define	W_OK		0x02	/* test for write permission */
#define	R_OK		0x04	/* test for read permission */

/* whence values for lseek(2) */
#define	SEEK_SET	0	/* set file offset to offset */
#define	SEEK_CUR	1	/* set file offset to current plus offset */
#define	SEEK_END	2	/* set file offset to EOF plus offset */

#if __BSD_VISIBLE
/* old BSD whence values for lseek(2); renamed by POSIX 1003.1 */
#define	L_SET		SEEK_SET
#define	L_INCR		SEEK_CUR
#define	L_XTND		SEEK_END

/* the parameters argument passed to the __tfork() syscall */
struct __tfork {
	void	*tf_tcb;
	pid_t	*tf_tid;
	void	*tf_stack;
};

/* the parameters argument for the kbind() syscall */
struct __kbind {
	void	*kb_addr;
	size_t	kb_size;
};
#define	KBIND_BLOCK_MAX	2	/* powerpc, sparc, and sparc64 need 2 blocks */
#define	KBIND_DATA_MAX	24	/* sparc64 needs 6, four-byte words */
#endif

/* the pathconf(2) variable values are part of the ABI */

/* configurable pathname variables */
#define	_PC_LINK_MAX			 1
#define	_PC_MAX_CANON			 2
#define	_PC_MAX_INPUT			 3
#define	_PC_NAME_MAX			 4
#define	_PC_PATH_MAX			 5
#define	_PC_PIPE_BUF			 6
#define	_PC_CHOWN_RESTRICTED		 7
#define	_PC_NO_TRUNC			 8
#define	_PC_VDISABLE			 9
#define	_PC_2_SYMLINKS			10
#define	_PC_ALLOC_SIZE_MIN		11
#define	_PC_ASYNC_IO			12
#define	_PC_FILESIZEBITS		13
#define	_PC_PRIO_IO			14
#define	_PC_REC_INCR_XFER_SIZE		15
#define	_PC_REC_MAX_XFER_SIZE		16
#define	_PC_REC_MIN_XFER_SIZE		17
#define	_PC_REC_XFER_ALIGN		18
#define	_PC_SYMLINK_MAX			19
#define	_PC_SYNC_IO			20
#define	_PC_TIMESTAMP_RESOLUTION	21

#endif /* !_SYS_UNISTD_H_ */
