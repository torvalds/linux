/*	$OpenBSD: fsck.h,v 1.35 2024/02/03 18:51:57 beck Exp $	*/
/*	$NetBSD: fsck.h,v 1.13 1996/10/11 20:15:46 thorpej Exp $	*/

/*
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Marshall
 * Kirk McKusick and Network Associates Laboratories, the Security
 * Research Division of Network Associates, Inc. under DARPA/SPAWAR
 * contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA CHATS
 * research program.
 *
 * Copyright (c) 1980, 1986, 1993
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
 *	@(#)fsck.h	8.1 (Berkeley) 6/5/93
 */

#define	MAXDUP		10	/* limit on dup blks (per inode) */
#define	MAXBAD		10	/* limit on bad blks (per inode) */
#define	MAXBUFSPACE	40*1024	/* maximum space to allocate to buffers */
#define	INOBUFSIZE	56*1024	/* size of buffer to read inodes in pass1 */

union dinode {
	struct ufs1_dinode dp1;
	struct ufs2_dinode dp2;
};

#define	DIP(dp, field)				\
	((sblock.fs_magic == FS_UFS1_MAGIC) ?	\
	(dp)->dp1.field : (dp)->dp2.field)

#define	DIP_SET(dp, field, val) do { \
	if (sblock.fs_magic == FS_UFS1_MAGIC)	\
		(dp)->dp1.field = (val);	\
	else					\
		(dp)->dp2.field = (val);	\
	} while (0)

/*
 * Each inode on the file system is described by the following structure.
 * The linkcnt is initially set to the value in the inode. Each time it
 * is found during the descent in passes 2, 3, and 4 the count is
 * decremented. Any inodes whose count is non-zero after pass 4 needs to
 * have its link count adjusted by the value remaining in ino_linkcnt.
 */
struct inostat {
	char    ino_state;      /* state of inode, see below */
	char    ino_type;       /* type of inode */
	short   ino_linkcnt;    /* number of links not found */
};

#define	USTATE	01		/* inode not allocated */
#define	FSTATE	02		/* inode is file */
#define	DSTATE	03		/* inode is directory */
#define	DFOUND	04		/* directory found during descent */
#define	DCLEAR	05		/* directory is to be cleared */
#define	FCLEAR	06		/* file is to be cleared */

/*
 * Inode state information is contained on per cylinder group lists
 * which are described by the following structure.
 */
extern struct inostatlist {
	long    il_numalloced;  /* number of inodes allocated in this cg */
	struct inostat *il_stat;/* inostat info for this cylinder group */
} *inostathead;

#define GET_ISTATE(ino)		(inoinfo(ino)->ino_state)
#define GET_ITYPE(ino)		(inoinfo(ino)->ino_type)
#define SET_ISTATE(ino, v)	do { GET_ISTATE(ino) = (v); } while (0)
#define SET_ITYPE(ino, v)	do { GET_ITYPE(ino) = (v); } while (0)
#define ILNCOUNT(ino)		(inoinfo(ino)->ino_linkcnt)

/*
 * buffer cache structure.
 */
struct bufarea {
	daddr_t	b_bno;
	struct bufarea	*b_next;		/* free list queue */
	struct bufarea	*b_prev;		/* free list queue */
	int	b_size;
	int	b_errs;
	int	b_flags;
	union {
		char	*b_buf;			/* buffer space */
		int32_t	*b_indir1;		/* FFS1 indirect block */
		int64_t	*b_indir2;		/* FFS2 indirect block */
		struct	fs *b_fs;		/* super block */
		struct	cg *b_cg;		/* cylinder group */
		struct	ufs1_dinode *b_dinode1;	/* FFS1 inode block */
		struct	ufs2_dinode *b_dinode2;	/* FFS2 inode block */
	} b_un;
	char	b_dirty;
};

#define IBLK(bp, i)				\
	((sblock.fs_magic == FS_UFS1_MAGIC) ?	\
	(bp)->b_un.b_indir1[i] : (bp)->b_un.b_indir2[i])

#define IBLK_SET(bp, i, val) do {		\
	if (sblock.fs_magic == FS_UFS1_MAGIC)	\
		(bp)->b_un.b_indir1[i] = (val);	\
	else					\
		(bp)->b_un.b_indir2[i] = (val);	\
	} while (0)

#define	B_INUSE 1

#define	MINBUFS		5	/* minimum number of buffers required */
extern struct bufarea bufhead;		/* head of list of other blks in filesys */
extern struct bufarea sblk;		/* file system superblock */
extern struct bufarea asblk;		/* alternate file system superblock */
extern struct bufarea *pdirbp;		/* current directory contents */
extern struct bufarea *pbp;		/* current inode block */
extern struct bufarea *getdatablk(daddr_t, long);

#define	dirty(bp)	(bp)->b_dirty = 1
#define	initbarea(bp) \
	(bp)->b_dirty = 0; \
	(bp)->b_bno = -1; \
	(bp)->b_flags = 0;

#define	sbdirty()	sblk.b_dirty = 1
#define	sblock		(*sblk.b_un.b_fs)

enum fixstate {DONTKNOW, NOFIX, FIX, IGNORE};

struct inodesc {
	daddr_t id_blkno;	/* current block number being examined */
	quad_t id_filesize;	/* for DATA nodes, the size of the directory */
	u_int64_t id_entryno;	/* for DATA nodes, current entry number */
	ino_t id_number;	/* inode number described */
	ino_t id_parent;	/* for DATA nodes, their parent */
	int (*id_func)		/* function to be applied to blocks of inode */
(struct inodesc *);
	struct direct *id_dirp;	/* for DATA nodes, ptr to current entry */
	char *id_name;		/* for DATA nodes, name to find or enter */
	int id_numfrags;	/* number of frags contained in block */
	int id_loc;		/* for DATA nodes, current location in dir */
	enum fixstate id_fix;	/* policy on fixing errors */
	char id_type;		/* type of descriptor, DATA or ADDR */
};
/* file types */
#define	DATA	1
#define	ADDR	2

/*
 * Linked list of duplicate blocks.
 *
 * The list is composed of two parts. The first part of the
 * list (from duplist through the node pointed to by muldup)
 * contains a single copy of each duplicate block that has been
 * found. The second part of the list (from muldup to the end)
 * contains duplicate blocks that have been found more than once.
 * To check if a block has been found as a duplicate it is only
 * necessary to search from duplist through muldup. To find the
 * total number of times that a block has been found as a duplicate
 * the entire list must be searched for occurrences of the block
 * in question. The following diagram shows a sample list where
 * w (found twice), x (found once), y (found three times), and z
 * (found once) are duplicate block numbers:
 *
 *    w -> y -> x -> z -> y -> w -> y
 *    ^		     ^
 *    |		     |
 * duplist	  muldup
 */
struct dups {
	struct dups *next;
	daddr_t dup;
};
extern struct dups *duplist;		/* head of dup list */
extern struct dups *muldup;		/* end of unique duplicate dup block numbers */

/*
 * Linked list of inodes with zero link counts.
 */
struct zlncnt {
	struct zlncnt *next;
	ino_t zlncnt;
};
extern struct zlncnt *zlnhead;		/* head of zero link count list */

/*
 * Inode cache data structures.
 */
extern struct inoinfo {
	struct	inoinfo *i_nexthash;	/* next entry in hash chain */
	struct	inoinfo	*i_child, *i_sibling;
	size_t	i_isize;		/* size of inode */
	ino_t	i_number;		/* inode number of this entry */
	ino_t	i_parent;		/* inode number of parent */
	ino_t	i_dotdot;		/* inode number of `..' */
	u_int	i_numblks;		/* size of block array in bytes */
	daddr_t	i_blks[1];		/* actually longer */
} **inphead, **inpsort;

extern long numdirs, listmax, inplast;

extern long	secsize;		/* actual disk sector size */
extern char	nflag;			/* assume a no response */
extern char	yflag;			/* assume a yes response */
extern daddr_t	bflag;			/* location of alternate super block */
extern int	debug;			/* output debugging info */
extern int	cvtlevel;		/* convert to newer file system format */
extern int	preen;			/* just fix normal inconsistencies */
extern char    resolved;               /* cleared if unresolved changes => not clean */
extern char	havesb;			/* superblock has been read */
extern char	skipclean;		/* skip clean file systems if preening */
extern int	fsmodified;		/* 1 => write done to file system */
extern int	fsreadfd;		/* file descriptor for reading file system */
extern int	fswritefd;		/* file descriptor for writing file system */
extern int	rerun;			/* rerun fsck.  Only used in non-preen mode */

extern daddr_t	maxfsblock;		/* number of blocks in the file system */
extern char	*blockmap;		/* ptr to primary blk allocation map */
extern ino_t	maxino;			/* number of inodes in file system */
extern ino_t	lastino;		/* last inode in use */

extern ino_t	lfdir;			/* lost & found directory inode number */
extern char	*lfname;		/* lost & found directory name */
extern int	lfmode;			/* lost & found directory creation mode */

extern daddr_t	n_blks;			/* number of blocks in use */
extern int64_t	n_files;		/* number of files in use */

#define	clearinode(dp)	\
	if (sblock.fs_magic == FS_UFS1_MAGIC) {	\
		(dp)->dp1 = ufs1_zino;		\
	} else {				\
		(dp)->dp2 = ufs2_zino;		\
	}

extern struct ufs1_dinode ufs1_zino;
extern struct ufs2_dinode ufs2_zino;

#define	setbmap(blkno)	setbit(blockmap, blkno)
#define	testbmap(blkno)	isset(blockmap, blkno)
#define	clrbmap(blkno)	clrbit(blockmap, blkno)

#define	STOP	0x01
#define	SKIP	0x02
#define	KEEPON	0x04
#define	ALTERED	0x08
#define	FOUND	0x10

union dinode *ginode(ino_t);
struct bufarea *cglookup(u_int cg);
struct inoinfo *getinoinfo(ino_t);
void getblk(struct bufarea *, daddr_t, long);
ino_t allocino(ino_t, int);
void *Malloc(size_t);
void *Calloc(size_t, size_t);
void *Reallocarray(void *, size_t, size_t);

extern int	(*info_fn)(char *, size_t);
extern char	*info_filesys;
