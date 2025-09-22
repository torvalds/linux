/*
 * hfsutils - tools for reading and writing Macintosh HFS volumes
 * Copyright (C) 1996, 1997 Robert Leslie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

# include <time.h>

# include "hfs.h"

# define ERROR(code, str)	(hfs_error = (str), errno = (code))

# define SIZE(type, n)		((size_t) (sizeof(type) * (n)))
# define ALLOC(type, n)		((type *) malloc(SIZE(type, n)))
# define ALLOCX(type, n)	((n) ? ALLOC(type, n) : (type *) 0)
# define FREE(ptr)		((ptr) ? (void) free((void *) ptr) : (void) 0)

# define REALLOC(ptr, type, n)  \
    ((type *) ((ptr) ? realloc(ptr, SIZE(type, n)) : malloc(SIZE(type, n))))
# define REALLOCX(ptr, type, n)  \
    ((n) ? REALLOC(type, n) : (FREE(ptr), (type *) 0))

# define BMTST(bm, num)  \
  (((char *) (bm))[(num) >> 3] & (0x80 >> ((num) & 0x07)))
# define BMSET(bm, num)  \
  (((char *) (bm))[(num) >> 3] |= (0x80 >> ((num) & 0x07)))
# define BMCLR(bm, num)  \
  (((char *) (bm))[(num) >> 3] &= ~(0x80 >> ((num) & 0x07)))

typedef unsigned char block[HFS_BLOCKSZ];

typedef signed char	Char;
typedef unsigned char	UChar;
typedef signed char	SignedByte;
typedef signed short	Integer;
typedef unsigned short	UInteger;
typedef signed long	LongInt;
typedef unsigned long	ULongInt;
typedef char		Str15[16];
typedef char		Str31[32];
typedef long		OSType;

typedef struct {
  UInteger	xdrStABN;	/* first allocation block */
  UInteger	xdrNumABlks;	/* number of allocation blocks */
} ExtDescriptor;

typedef ExtDescriptor ExtDataRec[3];

typedef struct {
  SignedByte	xkrKeyLen;	/* key length */
  SignedByte	xkrFkType;	/* fork type (0x00/0xff == data/resource */
  ULongInt	xkrFNum;	/* file number */
  UInteger	xkrFABN;	/* starting file allocation block */
} ExtKeyRec;

typedef struct {
  SignedByte	ckrKeyLen;	/* key length */
  SignedByte	ckrResrv1;	/* reserved */
  ULongInt	ckrParID;	/* parent directory ID */
  Str31		ckrCName;	/* catalog node name */
} CatKeyRec;

# define HFS_MAP1SZ  256
# define HFS_MAPXSZ  492

# define HFS_NODEREC(nd, rnum)	((nd).data + (nd).roff[rnum])

# define HFS_RECKEYLEN(ptr)	(*(unsigned char *) (ptr))
# define HFS_RECKEYSKIP(ptr)	((1 + HFS_RECKEYLEN(ptr) + 1) & ~1)
# define HFS_RECDATA(ptr)	((ptr) + HFS_RECKEYSKIP(ptr))

# define HFS_CATDATALEN		sizeof(CatDataRec)
# define HFS_EXTDATALEN		sizeof(ExtDataRec)

# define HFS_CATKEYLEN		sizeof(CatKeyRec)
# define HFS_EXTKEYLEN		sizeof(ExtKeyRec)

# define HFS_CATRECMAXLEN	(HFS_CATKEYLEN + HFS_CATDATALEN)
# define HFS_EXTRECMAXLEN	(HFS_EXTKEYLEN + HFS_EXTDATALEN)

# define HFS_MAXRECLEN		HFS_CATRECMAXLEN

typedef struct {
  Integer	v;		/* vertical coordinate */
  Integer	h;		/* horizontal coordinate */
} Point;

typedef struct {
  Integer	top;		/* top edge of rectangle */
  Integer	left;		/* left edge */
  Integer	bottom;		/* bottom edge */
  Integer	right;		/* rightmost edge */
} Rect;

typedef struct {
  Rect		frRect;		/* folder's rectangle */
  Integer	frFlags;	/* flags */
  Point		frLocation;	/* folder's location */
  Integer	frView;		/* folder's view */
} DInfo;

typedef struct {
  Point		frScroll;	/* scroll position */
  LongInt	frOpenChain;	/* directory ID chain of open folders */
  Integer	frUnused;	/* reserved */
  Integer	frComment;	/* comment ID */
  LongInt	frPutAway;	/* directory ID */
} DXInfo;

typedef struct {
  OSType	fdType;		/* file type */
  OSType	fdCreator;	/* file's creator */
  Integer	fdFlags;	/* flags */
  Point		fdLocation;	/* file's location */
  Integer	fdFldr;		/* file's window */
} FInfo;

typedef struct {
  Integer	fdIconID;	/* icon ID */
  Integer	fdUnused[4];	/* reserved */
  Integer	fdComment;	/* comment ID */
  LongInt	fdPutAway;	/* home directory ID */
} FXInfo;

typedef struct {
  Integer	drSigWord;	/* volume signature (0x4244 for HFS) */
  LongInt	drCrDate;	/* date and time of volume creation */
  LongInt	drLsMod;	/* date and time of last modification */
  Integer	drAtrb;		/* volume attributes */
  UInteger	drNmFls;	/* number of files in root directory */
  UInteger	drVBMSt;	/* first block of volume bit map (always 3) */
  UInteger	drAllocPtr;	/* start of next allocation search */
  UInteger	drNmAlBlks;	/* number of allocation blocks in volume */
  ULongInt	drAlBlkSiz;	/* size (in bytes) of allocation blocks */
  ULongInt	drClpSiz;	/* default clump size */
  UInteger	drAlBlSt;	/* first allocation block in volume */
  LongInt	drNxtCNID;	/* next unused catalog node ID (dir/file ID) */
  UInteger	drFreeBks;	/* number of unused allocation blocks */
  char		drVN[28];	/* volume name (1-27 chars) */
  LongInt	drVolBkUp;	/* date and time of last backup */
  Integer	drVSeqNum;	/* volume backup sequence number */
  ULongInt	drWrCnt;	/* volume write count */
  ULongInt	drXTClpSiz;	/* clump size for extents overflow file */
  ULongInt	drCTClpSiz;	/* clump size for catalog file */
  UInteger	drNmRtDirs;	/* number of directories in root directory */
  ULongInt	drFilCnt;	/* number of files in volume */
  ULongInt	drDirCnt;	/* number of directories in volume */
  LongInt	drFndrInfo[8];	/* information used by the Finder */
  UInteger	drVCSize;	/* size (in blocks) of volume cache */
  UInteger	drVBMCSize;	/* size (in blocks) of volume bitmap cache */
  UInteger	drCtlCSize;	/* size (in blocks) of common volume cache */
  ULongInt	drXTFlSize;	/* size (in bytes) of extents overflow file */
  ExtDataRec	drXTExtRec;	/* first extent record for extents file */
  ULongInt	drCTFlSize;	/* size (in bytes) of catalog file */
  ExtDataRec	drCTExtRec;	/* first extent record for catalog file */
} MDB;

# define HFS_ATRB_BUSY		(1 <<  6)
# define HFS_ATRB_HLOCKED	(1 <<  7)
# define HFS_ATRB_UMOUNTED	(1 <<  8)
# define HFS_ATRB_BBSPARED	(1 <<  9)
# define HFS_ATRB_COPYPROT	(1 << 14)
# define HFS_ATRB_SLOCKED	(1 << 15)

typedef enum {
  cdrDirRec  = 1,
  cdrFilRec  = 2,
  cdrThdRec  = 3,
  cdrFThdRec = 4
} CatDataType;

typedef struct {
  SignedByte	cdrType;	/* record type */
  SignedByte	cdrResrv2;	/* reserved */
  union {
    struct {  /* cdrDirRec */
      Integer	dirFlags;	/* directory flags */
      UInteger	dirVal;		/* directory valence */
      ULongInt	dirDirID;	/* directory ID */
      LongInt	dirCrDat;	/* date and time of creation */
      LongInt	dirMdDat;	/* date and time of last modification */
      LongInt	dirBkDat;	/* date and time of last backup */
      DInfo	dirUsrInfo;	/* Finder information */
      DXInfo	dirFndrInfo;	/* additional Finder information */
      LongInt	dirResrv[4];	/* reserved */
    } dir;
    struct {  /* cdrFilRec */
      SignedByte
		filFlags;	/* file flags */
      SignedByte
		filTyp;		/* file type */
      FInfo	filUsrWds;	/* Finder information */
      ULongInt	filFlNum;	/* file ID */
      UInteger	filStBlk;	/* first alloc block of data fork */
      ULongInt	filLgLen;	/* logical EOF of data fork */
      ULongInt	filPyLen;	/* physical EOF of data fork */
      UInteger	filRStBlk;	/* first alloc block of resource fork */
      ULongInt	filRLgLen;	/* logical EOF of resource fork */
      ULongInt	filRPyLen;	/* physical EOF of resource fork */
      LongInt	filCrDat;	/* date and time of creation */
      LongInt	filMdDat;	/* date and time of last modification */
      LongInt	filBkDat;	/* date and time of last backup */
      FXInfo	filFndrInfo;	/* additional Finder information */
      UInteger	filClpSize;	/* file clump size */
      ExtDataRec
		filExtRec;	/* first data fork extent record */
      ExtDataRec
		filRExtRec;	/* first resource fork extent record */
      LongInt	filResrv;	/* reserved */
    } fil;
    struct {  /* cdrThdRec */
      LongInt	thdResrv[2];	/* reserved */
      ULongInt	thdParID;	/* parent ID for this directory */
      Str31	thdCName;	/* name of this directory */
    } dthd;
    struct {  /* cdrFThdRec */
      LongInt	fthdResrv[2];	/* reserved */
      ULongInt	fthdParID;	/* parent ID for this file */
      Str31	fthdCName;	/* name of this file */
    } fthd;
  } u;
} CatDataRec;

struct _hfsfile_ {
  struct _hfsvol_ *vol;		/* pointer to volume descriptor */
  long parid;			/* parent directory ID of this file */
  char name[HFS_MAX_FLEN + 1];	/* catalog name of this file */
  CatDataRec cat;		/* catalog information */
  ExtDataRec ext;		/* current extent record */
  unsigned int fabn;		/* starting file allocation block number */
  int fork;			/* current selected fork for I/O */
  unsigned long pos;		/* current file seek pointer */
  unsigned long clump;		/* file's clump size, for allocation */
  int flags;			/* bit flags */

  struct _hfsfile_ *prev;
  struct _hfsfile_ *next;
};

# define HFS_UPDATE_CATREC	0x01

typedef struct {
  ULongInt	ndFLink;	/* forward link */
  ULongInt	ndBLink;	/* backward link */
  SignedByte	ndType;		/* node type */
  SignedByte	ndNHeight;	/* node level */
  UInteger	ndNRecs;	/* number of records in node */
  Integer	ndResv2;	/* reserved */
} NodeDescriptor;

# define HFS_MAXRECS	35	/* maximum based on minimum record size */

typedef struct _node_ {
  struct _btree_ *bt;		/* btree to which this node belongs */
  unsigned long nnum;		/* node index */
  NodeDescriptor nd;		/* node descriptor */
  int rnum;			/* current record index */
  UInteger roff[HFS_MAXRECS + 1];	/* record offsets */
  block data;			/* raw contents of node */
} node;

enum {
  ndIndxNode = 0x00,
  ndHdrNode  = 0x01,
  ndMapNode  = 0x02,
  ndLeafNode = 0xff
};

struct _hfsdir_ {
  struct _hfsvol_ *vol;		/* associated volume */
  long dirid;			/* directory ID of interest (or 0) */

  node n;			/* current B*-tree node */
  struct _hfsvol_ *vptr;	/* current volume pointer */

  struct _hfsdir_ *prev;
  struct _hfsdir_ *next;
};

typedef struct {
  UInteger	bthDepth;	/* current depth of tree */
  ULongInt	bthRoot;	/* number of root node */
  ULongInt	bthNRecs;	/* number of leaf records in tree */
  ULongInt	bthFNode;	/* number of first leaf node */
  ULongInt	bthLNode;	/* number of last leaf node */
  UInteger	bthNodeSize;	/* size of a node */
  UInteger	bthKeyLen;	/* maximum length of a key */
  ULongInt	bthNNodes;	/* total number of nodes in tree */
  ULongInt	bthFree;	/* number of free nodes */
  SignedByte	bthResv[76];	/* reserved */
} BTHdrRec;

typedef struct _btree_ {
  hfsfile f;			/* subset file information */
  node hdrnd;			/* header node */
  BTHdrRec hdr;			/* header record */
  char *map;			/* usage bitmap */
  unsigned long mapsz;		/* number of bytes in bitmap */
  int flags;			/* bit flags */

  int (*compare)(unsigned char *, unsigned char *);
				/* key comparison function */
} btree;

# define HFS_UPDATE_BTHDR	0x01

struct _hfsvol_ {
  int fd;		/* volume's open file descriptor */
  int flags;		/* bit flags */

#ifdef APPLE_HYB
  hce_mem *hce;		/* Extras needed by libhfs/mkisofs */
#endif /* APPLE_HYB */

  int pnum;		/* ordinal HFS partition number */
  unsigned long vstart;	/* logical block offset to start of volume */
  unsigned long vlen;	/* number of logical blocks in volume */
  unsigned int lpa;	/* number of logical blocks per allocation block */

  MDB mdb;		/* master directory block */
  block *vbm;		/* volume bit map */
  btree ext;		/* B*-tree control block for extents overflow file */
  btree cat;		/* B*-tree control block for catalog file */
  long cwd;		/* directory id of current working directory */

  int refs;		/* number of external references to this volume */
  hfsfile *files;	/* list of open files */
  hfsdir *dirs;		/* list of open directories */

  struct _hfsvol_ *prev;
  struct _hfsvol_ *next;
};

# define HFS_READONLY		0x01

# define HFS_UPDATE_MDB		0x10
# define HFS_UPDATE_ALTMDB	0x20
# define HFS_UPDATE_VBM		0x40

extern hfsvol *hfs_mounts;
extern hfsvol *hfs_curvol;
