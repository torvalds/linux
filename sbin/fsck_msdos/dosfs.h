/*	$OpenBSD: dosfs.h,v 1.6 2014/06/16 18:33:33 tobias Exp $	*/
/*	$NetBSD: dosfs.h,v 1.5 1997/10/17 11:19:41 ws Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997 Wolfgang Solfrank
 * Copyright (c) 1995 Martin Husemann
 * Some structure declaration borrowed from Paul Popelka
 * (paulp@uts.amdahl.com), see /sys/msdosfs/ for reference.
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
 */

#ifndef DOSFS_H
#define DOSFS_H

#define DOSBOOTBLOCKSIZE 512

typedef	u_int32_t	cl_t;	/* type holding a cluster number */

/*
 * architecture independent description of all the info stored in a
 * FAT boot block.
 */
struct bootblock {
	u_int	BytesPerSec;		/* bytes per sector */
	u_int	SecPerClust;		/* sectors per cluster */
	u_int	ResSectors;		/* number of reserved sectors */
	u_int	FATs;			/* number of FATs */
	u_int	RootDirEnts;		/* number of root directory entries */
	u_int	Media;			/* media descriptor */
	u_int	FATsmall;		/* number of sectors per FAT */
	u_int	SecPerTrack;		/* sectors per track */
	u_int	Heads;			/* number of heads */
	u_int32_t Sectors;		/* total number of sectors */
	u_int32_t HiddenSecs;		/* # of hidden sectors */
	u_int32_t HugeSectors;		/* # of sectors if bpbSectors == 0 */
	u_int	FSInfo;			/* FSInfo sector */
	u_int	Backup;			/* Backup of Bootblocks */
	cl_t	RootCl;			/* Start of Root Directory */
	cl_t	FSFree;			/* Number of free clusters acc. FSInfo */
	cl_t	FSNext;			/* Next free cluster acc. FSInfo */

	/* and some more calculated values */
	u_int	flags;			/* some flags: */
#define	FAT32		1		/* this is a FAT32 filesystem */
					/*
					 * Maybe, we should separate out
					 * various parts of FAT32?	XXX
					 */
	int	ValidFat;		/* valid fat if FAT32 non-mirrored */
	cl_t	ClustMask;		/* mask for entries in FAT */
	cl_t	NumClusters;		/* # of entries in a FAT */
	u_int32_t NumSectors;		/* how many sectors are there */
	u_int32_t FATsecs;		/* how many sectors are in FAT */
	u_int32_t NumFatEntries;	/* how many entries really are there */
	u_int	ClusterOffset;		/* at what sector would sector 0 start */
	u_int	ClusterSize;		/* Cluster size in bytes */

	/* Now some statistics: */
	u_int	NumFiles;		/* # of plain files */
	u_int	NumFree;		/* # of free clusters */
	u_int	NumBad;			/* # of bad clusters */
};

struct fatEntry {
	cl_t	next;			/* pointer to next cluster */
	cl_t	head;			/* pointer to start of chain */
	u_int32_t length;		/* number of clusters on chain */
	int	flags;			/* see below */
};

#define	CLUST_FREE	0		/* 0 means cluster is free */
#define	CLUST_FIRST	2		/* 2 is the minimum valid cluster number */
#define	CLUST_RSRVD	0xfffffff6	/* start of reserved clusters */
#define	CLUST_BAD	0xfffffff7	/* a cluster with a defect */
#define	CLUST_EOFS	0xfffffff8	/* start of EOF indicators */
#define	CLUST_EOF	0xffffffff	/* standard value for last cluster */

/*
 * Masks for cluster values
 */
#define	CLUST12_MASK	0xfff
#define	CLUST16_MASK	0xffff
#define	CLUST32_MASK	0xfffffff

#define	FAT_USED	1		/* This fat chain is used in a file */

#define	DOSLONGNAMELEN	256		/* long name maximal length */
#define LRFIRST		0x40		/* first long name record */
#define	LRNOMASK	0x1f		/* mask to extract long record
					 * sequence number */

/*
 * Architecture independent description of a directory entry
 */
struct dosDirEntry {
	struct dosDirEntry
		*parent,		/* previous tree level */
		*next,			/* next brother */
		*child;			/* if this is a directory */
	char name[8+1+3+1];		/* alias name first part */
	char lname[DOSLONGNAMELEN];	/* real name */
	uint flags;			/* attributes */
	cl_t head;			/* cluster no */
	u_int32_t size;			/* filesize in bytes */
	uint fsckflags;			/* flags during fsck */
};
/* Flags in fsckflags: */
#define	DIREMPTY	1
#define	DIREMPWARN	2

/*
 *  TODO-list of unread directories
 */
struct dirTodoNode {
	struct dosDirEntry *dir;
	struct dirTodoNode *next;
};

#endif
