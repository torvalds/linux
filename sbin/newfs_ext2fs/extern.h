/* $OpenBSD: extern.h,v 1.4 2016/03/14 20:30:34 natano Exp $ */
/*	$NetBSD: extern.h,v 1.4 2009/10/21 01:07:46 snj Exp $	*/

/*
 * Copyright (c) 1997 Christos Zoulas.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* XXX should be in <sys/ufs/ext2fs.h> */
#define EXT2_LOG_MAXBSIZE	12
#define EXT2_MAXBSIZE		(1 << EXT2_LOG_MAXBSIZE)

#ifndef nitems
#define nitems(_a)   (sizeof((_a)) / sizeof((_a)[0]))
#endif

/* prototypes */
void mke2fs(const char *, int);

/* variables set up by front end. */
extern int	Nflag;		/* run mkfs without writing file system */
extern int	Oflag;		/* format as an 4.3BSD file system */
extern int	verbosity;	/* amount of printf() output */
extern int64_t	fssize;		/* file system size */
extern uint16_t	inodesize;	/* bytes per inode */
extern uint	sectorsize;	/* sector size */
extern uint	fsize;		/* fragment size */
extern uint	bsize;		/* block size */
extern uint	minfree;	/* free space threshold */
extern uint	num_inodes;	/* number of inodes (overrides density) */
extern char	*volname;	/* volume name */
