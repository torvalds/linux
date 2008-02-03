/*
 *  linux/fs/hfs/hfs.h
 *
 * Copyright (C) 1995-1997  Paul H. Hargrove
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 * This file may be distributed under the terms of the GNU General Public License.
 */

#ifndef _HFS_H
#define _HFS_H

/* offsets to various blocks */
#define HFS_DD_BLK		0 /* Driver Descriptor block */
#define HFS_PMAP_BLK		1 /* First block of partition map */
#define HFS_MDB_BLK		2 /* Block (w/i partition) of MDB */

/* magic numbers for various disk blocks */
#define HFS_DRVR_DESC_MAGIC	0x4552 /* "ER": driver descriptor map */
#define HFS_OLD_PMAP_MAGIC	0x5453 /* "TS": old-type partition map */
#define HFS_NEW_PMAP_MAGIC	0x504D /* "PM": new-type partition map */
#define HFS_SUPER_MAGIC		0x4244 /* "BD": HFS MDB (super block) */
#define HFS_MFS_SUPER_MAGIC	0xD2D7 /* MFS MDB (super block) */

/* various FIXED size parameters */
#define HFS_SECTOR_SIZE		512    /* size of an HFS sector */
#define HFS_SECTOR_SIZE_BITS	9      /* log_2(HFS_SECTOR_SIZE) */
#define HFS_NAMELEN		31     /* maximum length of an HFS filename */
#define HFS_MAX_NAMELEN		128
#define HFS_MAX_VALENCE		32767U

#define HFS_BAD_KEYLEN		0xFF

/* Meanings of the drAtrb field of the MDB,
 * Reference: _Inside Macintosh: Files_ p. 2-61
 */
#define HFS_SB_ATTRIB_HLOCK	(1 << 7)
#define HFS_SB_ATTRIB_UNMNT	(1 << 8)
#define HFS_SB_ATTRIB_SPARED	(1 << 9)
#define HFS_SB_ATTRIB_INCNSTNT	(1 << 11)
#define HFS_SB_ATTRIB_SLOCK	(1 << 15)

/* Some special File ID numbers */
#define HFS_POR_CNID		1	/* Parent Of the Root */
#define HFS_ROOT_CNID		2	/* ROOT directory */
#define HFS_EXT_CNID		3	/* EXTents B-tree */
#define HFS_CAT_CNID		4	/* CATalog B-tree */
#define HFS_BAD_CNID		5	/* BAD blocks file */
#define HFS_ALLOC_CNID		6	/* ALLOCation file (HFS+) */
#define HFS_START_CNID		7	/* STARTup file (HFS+) */
#define HFS_ATTR_CNID		8	/* ATTRibutes file (HFS+) */
#define HFS_EXCH_CNID		15	/* ExchangeFiles temp id */
#define HFS_FIRSTUSER_CNID	16

/* values for hfs_cat_rec.cdrType */
#define HFS_CDR_DIR    0x01    /* folder (directory) */
#define HFS_CDR_FIL    0x02    /* file */
#define HFS_CDR_THD    0x03    /* folder (directory) thread */
#define HFS_CDR_FTH    0x04    /* file thread */

/* legal values for hfs_ext_key.FkType and hfs_file.fork */
#define HFS_FK_DATA	0x00
#define HFS_FK_RSRC	0xFF

/* bits in hfs_fil_entry.Flags */
#define HFS_FIL_LOCK	0x01  /* locked */
#define HFS_FIL_THD	0x02  /* file thread */
#define HFS_FIL_DOPEN   0x04  /* data fork open */
#define HFS_FIL_ROPEN   0x08  /* resource fork open */
#define HFS_FIL_DIR     0x10  /* directory (always clear) */
#define HFS_FIL_NOCOPY  0x40  /* copy-protected file */
#define HFS_FIL_USED	0x80  /* open */

/* bits in hfs_dir_entry.Flags. dirflags is 16 bits. */
#define HFS_DIR_LOCK        0x01  /* locked */
#define HFS_DIR_THD         0x02  /* directory thread */
#define HFS_DIR_INEXPFOLDER 0x04  /* in a shared area */
#define HFS_DIR_MOUNTED     0x08  /* mounted */
#define HFS_DIR_DIR         0x10  /* directory (always set) */
#define HFS_DIR_EXPFOLDER   0x20  /* share point */

/* bits hfs_finfo.fdFlags */
#define HFS_FLG_INITED		0x0100
#define HFS_FLG_LOCKED		0x1000
#define HFS_FLG_INVISIBLE	0x4000

/*======== HFS structures as they appear on the disk ========*/

/* Pascal-style string of up to 31 characters */
struct hfs_name {
	u8 len;
	u8 name[HFS_NAMELEN];
} __packed;

struct hfs_point {
	__be16 v;
	__be16 h;
} __packed;

struct hfs_rect {
	__be16 top;
	__be16 left;
	__be16 bottom;
	__be16 right;
} __packed;

struct hfs_finfo {
	__be32 fdType;
	__be32 fdCreator;
	__be16 fdFlags;
	struct hfs_point fdLocation;
	__be16 fdFldr;
} __packed;

struct hfs_fxinfo {
	__be16 fdIconID;
	u8 fdUnused[8];
	__be16 fdComment;
	__be32 fdPutAway;
} __packed;

struct hfs_dinfo {
	struct hfs_rect frRect;
	__be16 frFlags;
	struct hfs_point frLocation;
	__be16 frView;
} __packed;

struct hfs_dxinfo {
	struct hfs_point frScroll;
	__be32 frOpenChain;
	__be16 frUnused;
	__be16 frComment;
	__be32 frPutAway;
} __packed;

union hfs_finder_info {
	struct {
		struct hfs_finfo finfo;
		struct hfs_fxinfo fxinfo;
	} file;
	struct {
		struct hfs_dinfo dinfo;
		struct hfs_dxinfo dxinfo;
	} dir;
} __packed;

/* Cast to a pointer to a generic bkey */
#define	HFS_BKEY(X)	(((void)((X)->KeyLen)), ((struct hfs_bkey *)(X)))

/* The key used in the catalog b-tree: */
struct hfs_cat_key {
	u8 key_len;		/* number of bytes in the key */
	u8 reserved;		/* padding */
	__be32 ParID;		/* CNID of the parent dir */
	struct hfs_name	CName;	/* The filename of the entry */
} __packed;

/* The key used in the extents b-tree: */
struct hfs_ext_key {
	u8 key_len;		/* number of bytes in the key */
	u8 FkType;		/* HFS_FK_{DATA,RSRC} */
	__be32 FNum;		/* The File ID of the file */
	__be16 FABN;		/* allocation blocks number*/
} __packed;

typedef union hfs_btree_key {
	u8 key_len;			/* number of bytes in the key */
	struct hfs_cat_key cat;
	struct hfs_ext_key ext;
} hfs_btree_key;

#define HFS_MAX_CAT_KEYLEN	(sizeof(struct hfs_cat_key) - sizeof(u8))
#define HFS_MAX_EXT_KEYLEN	(sizeof(struct hfs_ext_key) - sizeof(u8))

typedef union hfs_btree_key btree_key;

struct hfs_extent {
	__be16 block;
	__be16 count;
};
typedef struct hfs_extent hfs_extent_rec[3];

/* The catalog record for a file */
struct hfs_cat_file {
	s8 type;			/* The type of entry */
	u8 reserved;
	u8 Flags;			/* Flags such as read-only */
	s8 Typ;				/* file version number = 0 */
	struct hfs_finfo UsrWds;	/* data used by the Finder */
	__be32 FlNum;			/* The CNID */
	__be16 StBlk;			/* obsolete */
	__be32 LgLen;			/* The logical EOF of the data fork*/
	__be32 PyLen;			/* The physical EOF of the data fork */
	__be16 RStBlk;			/* obsolete */
	__be32 RLgLen;			/* The logical EOF of the rsrc fork */
	__be32 RPyLen;			/* The physical EOF of the rsrc fork */
	__be32 CrDat;			/* The creation date */
	__be32 MdDat;			/* The modified date */
	__be32 BkDat;			/* The last backup date */
	struct hfs_fxinfo FndrInfo;	/* more data for the Finder */
	__be16 ClpSize;			/* number of bytes to allocate
					   when extending files */
	hfs_extent_rec ExtRec;		/* first extent record
					   for the data fork */
	hfs_extent_rec RExtRec;		/* first extent record
					   for the resource fork */
	u32 Resrv;			/* reserved by Apple */
} __packed;

/* the catalog record for a directory */
struct hfs_cat_dir {
	s8 type;			/* The type of entry */
	u8 reserved;
	__be16 Flags;			/* flags */
	__be16 Val;			/* Valence: number of files and
					   dirs in the directory */
	__be32 DirID;			/* The CNID */
	__be32 CrDat;			/* The creation date */
	__be32 MdDat;			/* The modification date */
	__be32 BkDat;			/* The last backup date */
	struct hfs_dinfo UsrInfo;	/* data used by the Finder */
	struct hfs_dxinfo FndrInfo;	/* more data used by Finder */
	u8 Resrv[16];			/* reserved by Apple */
} __packed;

/* the catalog record for a thread */
struct hfs_cat_thread {
	s8 type;			/* The type of entry */
	u8 reserved[9];			/* reserved by Apple */
	__be32 ParID;			/* CNID of parent directory */
	struct hfs_name CName;		/* The name of this entry */
}  __packed;

/* A catalog tree record */
typedef union hfs_cat_rec {
	s8 type;			/* The type of entry */
	struct hfs_cat_file file;
	struct hfs_cat_dir dir;
	struct hfs_cat_thread thread;
} hfs_cat_rec;

struct hfs_mdb {
	__be16 drSigWord;		/* Signature word indicating fs type */
	__be32 drCrDate;		/* fs creation date/time */
	__be32 drLsMod;			/* fs modification date/time */
	__be16 drAtrb;			/* fs attributes */
	__be16 drNmFls;			/* number of files in root directory */
	__be16 drVBMSt;			/* location (in 512-byte blocks)
					   of the volume bitmap */
	__be16 drAllocPtr;		/* location (in allocation blocks)
					   to begin next allocation search */
	__be16 drNmAlBlks;		/* number of allocation blocks */
	__be32 drAlBlkSiz;		/* bytes in an allocation block */
	__be32 drClpSiz;		/* clumpsize, the number of bytes to
					   allocate when extending a file */
	__be16 drAlBlSt;		/* location (in 512-byte blocks)
					   of the first allocation block */
	__be32 drNxtCNID;		/* CNID to assign to the next
					   file or directory created */
	__be16 drFreeBks;		/* number of free allocation blocks */
	u8 drVN[28];			/* the volume label */
	__be32 drVolBkUp;		/* fs backup date/time */
	__be16 drVSeqNum;		/* backup sequence number */
	__be32 drWrCnt;			/* fs write count */
	__be32 drXTClpSiz;		/* clumpsize for the extents B-tree */
	__be32 drCTClpSiz;		/* clumpsize for the catalog B-tree */
	__be16 drNmRtDirs;		/* number of directories in
					   the root directory */
	__be32 drFilCnt;		/* number of files in the fs */
	__be32 drDirCnt;		/* number of directories in the fs */
	u8 drFndrInfo[32];		/* data used by the Finder */
	__be16 drEmbedSigWord;		/* embedded volume signature */
	__be32 drEmbedExtent;		/* starting block number (xdrStABN)
					   and number of allocation blocks
					   (xdrNumABlks) occupied by embedded
					   volume */
	__be32 drXTFlSize;		/* bytes in the extents B-tree */
	hfs_extent_rec drXTExtRec;	/* extents B-tree's first 3 extents */
	__be32 drCTFlSize;		/* bytes in the catalog B-tree */
	hfs_extent_rec drCTExtRec;	/* catalog B-tree's first 3 extents */
} __packed;

/*======== Data structures kept in memory ========*/

struct hfs_readdir_data {
	struct list_head list;
	struct file *file;
	struct hfs_cat_key key;
};

#endif
