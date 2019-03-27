/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 1995, 1996, 1997 Wolfgang Solfrank
 * Copyright (c) 1995 Martin Husemann
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *	$NetBSD: ext.h,v 1.6 2000/04/25 23:02:51 jdolecek Exp $
 * $FreeBSD$
 */

#ifndef EXT_H
#define	EXT_H

#include <sys/types.h>

#include "dosfs.h"

#define	LOSTDIR	"LOST.DIR"

/*
 * Options:
 */
extern int alwaysno;	/* assume "no" for all questions */
extern int alwaysyes;	/* assume "yes" for all questions */
extern int preen;	/* we are preening */
extern int rdonly;	/* device is opened read only (supersedes above) */
extern int skipclean;	/* skip clean file systems if preening */

/*
 * function declarations
 */
int ask(int, const char *, ...) __printflike(2, 3);

/*
 * Check the dirty flag.  If the file system is clean, then return 1.
 * Otherwise, return 0 (this includes the case of FAT12 file systems --
 * they have no dirty flag, so they must be assumed to be unclean).
 */
int checkdirty(int, struct bootblock *);

/*
 * Check file system given as arg
 */
int checkfilesys(const char *);

/*
 * Return values of various functions
 */
#define	FSOK		0		/* Check was OK */
#define	FSBOOTMOD	1		/* Boot block was modified */
#define	FSDIRMOD	2		/* Some directory was modified */
#define	FSFATMOD	4		/* The FAT was modified */
#define	FSERROR		8		/* Some unrecovered error remains */
#define	FSFATAL		16		/* Some unrecoverable error occurred */
#define	FSDIRTY		32		/* File system is dirty */
#define	FSFIXFAT	64		/* Fix file system FAT */

/*
 * read a boot block in a machine independent fashion and translate
 * it into our struct bootblock.
 */
int readboot(int, struct bootblock *);

/*
 * Correct the FSInfo block.
 */
int writefsinfo(int, struct bootblock *);

/*
 * Read one of the FAT copies and return a pointer to the new
 * allocated array holding our description of it.
 */
int readfat(int, struct bootblock *, u_int, struct fatEntry **);

/*
 * Check two FAT copies for consistency and merge changes into the
 * first if necessary.
 */
int comparefat(struct bootblock *, struct fatEntry *, struct fatEntry *, u_int);

/*
 * Check a FAT
 */
int checkfat(struct bootblock *, struct fatEntry *);

/*
 * Write back FAT entries
 */
int writefat(int, struct bootblock *, struct fatEntry *, int);

/*
 * Read a directory
 */
int resetDosDirSection(struct bootblock *, struct fatEntry *);
void finishDosDirSection(void);
int handleDirTree(int, struct bootblock *, struct fatEntry *);

/*
 * Cross-check routines run after everything is completely in memory
 */
/*
 * Check for lost cluster chains
 */
int checklost(int, struct bootblock *, struct fatEntry *);
/*
 * Try to reconnect a lost cluster chain
 */
int reconnect(int, struct bootblock *, struct fatEntry *, cl_t);
void finishlf(void);

/*
 * Small helper functions
 */
/*
 * Return the type of a reserved cluster as text
 */
const char *rsrvdcltype(cl_t);

/*
 * Clear a cluster chain in a FAT
 */
void clearchain(struct bootblock *, struct fatEntry *, cl_t);

#endif
