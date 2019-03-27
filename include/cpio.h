/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Mike Barcroft <mike@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef _CPIO_H_
#define	_CPIO_H_

#define	C_ISSOCK 0140000	/* Socket. */
#define	C_ISLNK	0120000		/* Symbolic link. */
#define	C_ISCTG	0110000		/* Reserved. */
#define	C_ISREG	0100000		/* Regular file. */
#define	C_ISBLK	0060000		/* Block special. */
#define	C_ISDIR	0040000		/* Directory. */
#define	C_ISCHR	0020000		/* Character special. */
#define	C_ISFIFO 0010000	/* FIFO. */
#define	C_ISUID	0004000		/* Set user ID. */
#define	C_ISGID	0002000		/* Set group ID. */
#define	C_ISVTX	0001000		/* On directories, restricted deletion flag. */
#define	C_IRUSR	0000400		/* Read by owner. */
#define	C_IWUSR	0000200		/* Write by owner. */
#define	C_IXUSR	0000100		/* Execute by owner. */
#define	C_IRGRP	0000040		/* Read by group. */
#define	C_IWGRP	0000020		/* Write by group. */
#define	C_IXGRP	0000010		/* Execute by group. */
#define	C_IROTH	0000004		/* Read by others. */
#define	C_IWOTH	0000002		/* Write by others. */
#define	C_IXOTH	0000001		/* Execute by others. */

#define	MAGIC	"070707"

#endif /* _CPIO_H_ */
