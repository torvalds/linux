/*	$OpenBSD: mtree.h,v 1.14 2023/08/11 05:07:28 guenther Exp $	*/
/*	$NetBSD: mtree.h,v 1.7 1995/03/07 21:26:27 cgd Exp $	*/

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
 *	@(#)mtree.h	8.1 (Berkeley) 6/6/93
 */

#include <string.h>
#include <stdlib.h>

#define	MTREE_O_FLAGS \
	(O_RDONLY | O_NONBLOCK | O_NOFOLLOW)

#define	KEYDEFAULT \
	(F_GID | F_MODE | F_NLINK | F_SIZE | F_SLINK | F_TIME | F_UID)

#define	ERROREXIT	1
#define	MISMATCHEXIT	2

typedef struct _node {
	struct _node	*parent, *child;	/* up, down */
	struct _node	*prev, *next;		/* left, right */
	off_t	st_size;			/* size */
	struct timespec	st_mtim;		/* last modification time */
	u_int32_t cksum;			/* check sum */
	char	*md5digest;			/* MD5 digest */
	char	*rmd160digest;			/* RIPEMD-160 digest */
	char	*sha1digest;			/* SHA-1 digest */
	char	*sha256digest;			/* SHA-256 digest */
	char	*slink;				/* symbolic link reference */
	uid_t	st_uid;				/* uid */
	gid_t	st_gid;				/* gid */
#define	MBITS	(S_ISUID|S_ISGID|S_ISTXT|S_IRWXU|S_IRWXG|S_IRWXO)
	mode_t	st_mode;			/* mode */
	nlink_t	st_nlink;			/* link count */
	u_int32_t file_flags;			/* file flags */

#define	F_CKSUM		0x000001		/* checksum */
#define	F_DONE		0x000002		/* directory done */
#define	F_GID		0x000004		/* gid */
#define	F_GNAME		0x000008		/* group name */
#define	F_IGN		0x000010		/* ignore */
#define	F_MAGIC		0x000020		/* name has magic chars */
#define	F_MD5		0x000040		/* MD5 digest */
#define	F_MODE		0x000080		/* mode */
#define	F_NLINK		0x000100		/* number of links */
#define F_OPT		0x000200		/* existence optional */
#define	F_RMD160	0x000400		/* RIPEMD-160 digest */
#define	F_SHA1		0x000800		/* SHA-1 digest */
#define	F_SIZE		0x001000		/* size */
#define	F_SLINK		0x002000		/* link count */
#define	F_TIME		0x004000		/* modification time */
#define	F_TYPE		0x008000		/* file type */
#define	F_UID		0x010000		/* uid */
#define	F_UNAME		0x020000		/* user name */
#define	F_VISIT		0x040000		/* file visited */
#define	F_FLAGS		0x080000		/* file flags */
#define	F_NOCHANGE	0x100000		/* do not change owner/mode */
#define	F_SHA256	0x200000		/* SHA-256 digest */
	u_int32_t flags;			/* items set */

#define	F_BLOCK	0x001				/* block special */
#define	F_CHAR	0x002				/* char special */
#define	F_DIR	0x004				/* directory */
#define	F_FIFO	0x008				/* fifo */
#define	F_FILE	0x010				/* regular file */
#define	F_LINK	0x020				/* symbolic link */
#define	F_SOCK	0x040				/* socket */
	u_char	type;				/* file type */

	char	name[1];			/* file name (must be last) */
} NODE;

#define	RP(p)	\
	((p)->fts_path[0] == '.' && (p)->fts_path[1] == '/' ? \
	    (p)->fts_path + 2 : (p)->fts_path)
