/* SPDX-License-Identifier: GPL-2.0 */
/*
 * HFS/HFS+ common definitions, inline functions,
 * and shared functionality.
 */

#ifndef _HFS_COMMON_H_
#define _HFS_COMMON_H_

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define hfs_dbg(fmt, ...)							\
	pr_debug("pid %d:%s:%d %s(): " fmt,					\
		 current->pid, __FILE__, __LINE__, __func__, ##__VA_ARGS__)	\

/*
 * Format of structures on disk
 * Information taken from Apple Technote #1150 (HFS Plus Volume Format)
 */

/* offsets to various blocks */
#define HFS_DD_BLK			0	/* Driver Descriptor block */
#define HFS_PMAP_BLK			1	/* First block of partition map */
#define HFS_MDB_BLK			2	/* Block (w/i partition) of MDB */

/* magic numbers for various disk blocks */
#define HFS_DRVR_DESC_MAGIC		0x4552	/* "ER": driver descriptor map */
#define HFS_OLD_PMAP_MAGIC		0x5453	/* "TS": old-type partition map */
#define HFS_NEW_PMAP_MAGIC		0x504D	/* "PM": new-type partition map */
#define HFS_SUPER_MAGIC			0x4244	/* "BD": HFS MDB (super block) */
#define HFS_MFS_SUPER_MAGIC		0xD2D7	/* MFS MDB (super block) */

#define HFSPLUS_VOLHEAD_SIG		0x482b
#define HFSPLUS_VOLHEAD_SIGX		0x4858
#define HFSPLUS_SUPER_MAGIC		0x482b

#define HFSP_WRAP_MAGIC			0x4244
#define HFSP_WRAP_ATTRIB_SLOCK		0x8000
#define HFSP_WRAP_ATTRIB_SPARED		0x0200

#define HFSP_WRAPOFF_SIG		0x00
#define HFSP_WRAPOFF_ATTRIB		0x0A
#define HFSP_WRAPOFF_ABLKSIZE		0x14
#define HFSP_WRAPOFF_ABLKSTART		0x1C
#define HFSP_WRAPOFF_EMBEDSIG		0x7C
#define HFSP_WRAPOFF_EMBEDEXT		0x7E

#define HFSP_HARDLINK_TYPE		0x686c6e6b	/* 'hlnk' */
#define HFSP_HFSPLUS_CREATOR		0x6866732b	/* 'hfs+' */

#define HFSP_SYMLINK_TYPE		0x736c6e6b	/* 'slnk' */
#define HFSP_SYMLINK_CREATOR		0x72686170	/* 'rhap' */

#define HFSP_MOUNT_VERSION		0x482b4c78	/* 'H+Lx' */

#define HFSP_HIDDENDIR_NAME \
	"\xe2\x90\x80\xe2\x90\x80\xe2\x90\x80\xe2\x90\x80HFS+ Private Data"

/* various FIXED size parameters */
#define HFS_SECTOR_SIZE			512	/* size of an HFS sector */
#define HFS_SECTOR_SIZE_BITS		9	/* log_2(HFS_SECTOR_SIZE) */
#define HFS_MAX_VALENCE			32767U

#define HFSPLUS_SECTOR_SIZE		HFS_SECTOR_SIZE
#define HFSPLUS_SECTOR_SHIFT		HFS_SECTOR_SIZE_BITS
#define HFSPLUS_VOLHEAD_SECTOR		2
#define HFSPLUS_MIN_VERSION		4
#define HFSPLUS_CURRENT_VERSION		5

#define HFS_NAMELEN			31	/* maximum length of an HFS filename */
#define HFS_MAX_NAMELEN			128

#define HFSPLUS_MAX_STRLEN		255
#define HFSPLUS_ATTR_MAX_STRLEN		127

/* Meanings of the drAtrb field of the MDB,
 * Reference: _Inside Macintosh: Files_ p. 2-61
 */
#define HFS_SB_ATTRIB_HLOCK	(1 << 7)
#define HFS_SB_ATTRIB_UNMNT	(1 << 8)
#define HFS_SB_ATTRIB_SPARED	(1 << 9)
#define HFS_SB_ATTRIB_INCNSTNT	(1 << 11)
#define HFS_SB_ATTRIB_SLOCK	(1 << 15)

/* values for hfs_cat_rec.cdrType */
#define HFS_CDR_DIR		0x01	/* folder (directory) */
#define HFS_CDR_FIL		0x02	/* file */
#define HFS_CDR_THD		0x03	/* folder (directory) thread */
#define HFS_CDR_FTH		0x04	/* file thread */

/* legal values for hfs_ext_key.FkType and hfs_file.fork */
#define HFS_FK_DATA		0x00
#define HFS_FK_RSRC		0xFF

/* bits in hfs_fil_entry.Flags */
#define HFS_FIL_LOCK		0x01	/* locked */
#define HFS_FIL_THD		0x02	/* file thread */
#define HFS_FIL_DOPEN		0x04	/* data fork open */
#define HFS_FIL_ROPEN		0x08	/* resource fork open */
#define HFS_FIL_DIR		0x10	/* directory (always clear) */
#define HFS_FIL_NOCOPY		0x40	/* copy-protected file */
#define HFS_FIL_USED		0x80	/* open */

/* bits in hfs_dir_entry.Flags. dirflags is 16 bits. */
#define HFS_DIR_LOCK		0x01	/* locked */
#define HFS_DIR_THD		0x02	/* directory thread */
#define HFS_DIR_INEXPFOLDER	0x04	/* in a shared area */
#define HFS_DIR_MOUNTED		0x08	/* mounted */
#define HFS_DIR_DIR		0x10	/* directory (always set) */
#define HFS_DIR_EXPFOLDER	0x20	/* share point */

/* bits hfs_finfo.fdFlags */
#define HFS_FLG_INITED		0x0100
#define HFS_FLG_LOCKED		0x1000
#define HFS_FLG_INVISIBLE	0x4000

/* Some special File ID numbers */
#define HFS_POR_CNID		1	/* Parent Of the Root */
#define HFSPLUS_POR_CNID	HFS_POR_CNID
#define HFS_ROOT_CNID		2	/* ROOT directory */
#define HFSPLUS_ROOT_CNID	HFS_ROOT_CNID
#define HFS_EXT_CNID		3	/* EXTents B-tree */
#define HFSPLUS_EXT_CNID	HFS_EXT_CNID
#define HFS_CAT_CNID		4	/* CATalog B-tree */
#define HFSPLUS_CAT_CNID	HFS_CAT_CNID
#define HFS_BAD_CNID		5	/* BAD blocks file */
#define HFSPLUS_BAD_CNID	HFS_BAD_CNID
#define HFS_ALLOC_CNID		6	/* ALLOCation file (HFS+) */
#define HFSPLUS_ALLOC_CNID	HFS_ALLOC_CNID
#define HFS_START_CNID		7	/* STARTup file (HFS+) */
#define HFSPLUS_START_CNID	HFS_START_CNID
#define HFS_ATTR_CNID		8	/* ATTRibutes file (HFS+) */
#define HFSPLUS_ATTR_CNID	HFS_ATTR_CNID
#define HFS_EXCH_CNID		15	/* ExchangeFiles temp id */
#define HFSPLUS_EXCH_CNID	HFS_EXCH_CNID
#define HFS_FIRSTUSER_CNID	16	/* first available user id */
#define HFSPLUS_FIRSTUSER_CNID	HFS_FIRSTUSER_CNID

/*======== HFS/HFS+ structures as they appear on the disk ========*/

typedef __be32 hfsplus_cnid;
typedef __be16 hfsplus_unichr;

/* Pascal-style string of up to 31 characters */
struct hfs_name {
	u8 len;
	u8 name[HFS_NAMELEN];
} __packed;

/* A "string" as used in filenames, etc. */
struct hfsplus_unistr {
	__be16 length;
	hfsplus_unichr unicode[HFSPLUS_MAX_STRLEN];
} __packed;

/*
 * A "string" is used in attributes file
 * for name of extended attribute
 */
struct hfsplus_attr_unistr {
	__be16 length;
	hfsplus_unichr unicode[HFSPLUS_ATTR_MAX_STRLEN];
} __packed;

struct hfs_extent {
	__be16 block;
	__be16 count;
};
typedef struct hfs_extent hfs_extent_rec[3];

/* A single contiguous area of a file */
struct hfsplus_extent {
	__be32 start_block;
	__be32 block_count;
} __packed;
typedef struct hfsplus_extent hfsplus_extent_rec[8];

/* Information for a "Fork" in a file */
struct hfsplus_fork_raw {
	__be64 total_size;
	__be32 clump_size;
	__be32 total_blocks;
	hfsplus_extent_rec extents;
} __packed;

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

/* HFS+ Volume Header */
struct hfsplus_vh {
	__be16 signature;
	__be16 version;
	__be32 attributes;
	__be32 last_mount_vers;
	u32 reserved;

	__be32 create_date;
	__be32 modify_date;
	__be32 backup_date;
	__be32 checked_date;

	__be32 file_count;
	__be32 folder_count;

	__be32 blocksize;
	__be32 total_blocks;
	__be32 free_blocks;

	__be32 next_alloc;
	__be32 rsrc_clump_sz;
	__be32 data_clump_sz;
	hfsplus_cnid next_cnid;

	__be32 write_count;
	__be64 encodings_bmp;

	u32 finder_info[8];

	struct hfsplus_fork_raw alloc_file;
	struct hfsplus_fork_raw ext_file;
	struct hfsplus_fork_raw cat_file;
	struct hfsplus_fork_raw attr_file;
	struct hfsplus_fork_raw start_file;
} __packed;

/* HFS+ volume attributes */
#define HFSPLUS_VOL_UNMNT		(1 << 8)
#define HFSPLUS_VOL_SPARE_BLK		(1 << 9)
#define HFSPLUS_VOL_NOCACHE		(1 << 10)
#define HFSPLUS_VOL_INCNSTNT		(1 << 11)
#define HFSPLUS_VOL_NODEID_REUSED	(1 << 12)
#define HFSPLUS_VOL_JOURNALED		(1 << 13)
#define HFSPLUS_VOL_SOFTLOCK		(1 << 15)
#define HFSPLUS_VOL_UNUSED_NODE_FIX	(1 << 31)

struct hfs_point {
	__be16 v;
	__be16 h;
} __packed;

typedef struct hfs_point hfsp_point;

struct hfs_rect {
	__be16 top;
	__be16 left;
	__be16 bottom;
	__be16 right;
} __packed;

typedef struct hfs_rect hfsp_rect;

struct hfs_finfo {
	__be32 fdType;
	__be32 fdCreator;
	__be16 fdFlags;
	struct hfs_point fdLocation;
	__be16 fdFldr;
} __packed;

typedef struct hfs_finfo FInfo;

struct hfs_fxinfo {
	__be16 fdIconID;
	u8 fdUnused[8];
	__be16 fdComment;
	__be32 fdPutAway;
} __packed;

typedef struct hfs_fxinfo FXInfo;

struct hfs_dinfo {
	struct hfs_rect frRect;
	__be16 frFlags;
	struct hfs_point frLocation;
	__be16 frView;
} __packed;

typedef struct hfs_dinfo DInfo;

struct hfs_dxinfo {
	struct hfs_point frScroll;
	__be32 frOpenChain;
	__be16 frUnused;
	__be16 frComment;
	__be32 frPutAway;
} __packed;

typedef struct hfs_dxinfo DXInfo;

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

/* The key used in the catalog b-tree: */
struct hfs_cat_key {
	u8 key_len;		/* number of bytes in the key */
	u8 reserved;		/* padding */
	__be32 ParID;		/* CNID of the parent dir */
	struct hfs_name	CName;	/* The filename of the entry */
} __packed;

/* HFS+ catalog entry key */
struct hfsplus_cat_key {
	__be16 key_len;
	hfsplus_cnid parent;
	struct hfsplus_unistr name;
} __packed;

#define HFSPLUS_CAT_KEYLEN	(sizeof(struct hfsplus_cat_key))

/* The key used in the extents b-tree: */
struct hfs_ext_key {
	u8 key_len;		/* number of bytes in the key */
	u8 FkType;		/* HFS_FK_{DATA,RSRC} */
	__be32 FNum;		/* The File ID of the file */
	__be16 FABN;		/* allocation blocks number*/
} __packed;

/* HFS+ extents tree key */
struct hfsplus_ext_key {
	__be16 key_len;
	u8 fork_type;
	u8 pad;
	hfsplus_cnid cnid;
	__be32 start_block;
} __packed;

#define HFSPLUS_EXT_KEYLEN	sizeof(struct hfsplus_ext_key)

typedef union hfs_btree_key {
	u8 key_len;			/* number of bytes in the key */
	struct hfs_cat_key cat;
	struct hfs_ext_key ext;
} hfs_btree_key;

#define HFS_MAX_CAT_KEYLEN	(sizeof(struct hfs_cat_key) - sizeof(u8))
#define HFS_MAX_EXT_KEYLEN	(sizeof(struct hfs_ext_key) - sizeof(u8))

typedef union hfs_btree_key btree_key;

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

/* POSIX permissions */
struct hfsplus_perm {
	__be32 owner;
	__be32 group;
	u8  rootflags;
	u8  userflags;
	__be16 mode;
	__be32 dev;
} __packed;

#define HFSPLUS_FLG_NODUMP	0x01
#define HFSPLUS_FLG_IMMUTABLE	0x02
#define HFSPLUS_FLG_APPEND	0x04

/* HFS/HFS+ BTree node descriptor */
struct hfs_bnode_desc {
	__be32 next;		/* (V) Number of the next node at this level */
	__be32 prev;		/* (V) Number of the prev node at this level */
	u8 type;		/* (F) The type of node */
	u8 height;		/* (F) The level of this node (leaves=1) */
	__be16 num_recs;	/* (V) The number of records in this node */
	u16 reserved;
} __packed;

/* HFS/HFS+ BTree node types */
#define HFS_NODE_INDEX	0x00	/* An internal (index) node */
#define HFS_NODE_HEADER	0x01	/* The tree header node (node 0) */
#define HFS_NODE_MAP	0x02	/* Holds part of the bitmap of used nodes */
#define HFS_NODE_LEAF	0xFF	/* A leaf (ndNHeight==1) node */

/* HFS/HFS+ BTree header */
struct hfs_btree_header_rec {
	__be16 depth;		/* (V) The number of levels in this B-tree */
	__be32 root;		/* (V) The node number of the root node */
	__be32 leaf_count;	/* (V) The number of leaf records */
	__be32 leaf_head;	/* (V) The number of the first leaf node */
	__be32 leaf_tail;	/* (V) The number of the last leaf node */
	__be16 node_size;	/* (F) The number of bytes in a node (=512) */
	__be16 max_key_len;	/* (F) The length of a key in an index node */
	__be32 node_count;	/* (V) The total number of nodes */
	__be32 free_nodes;	/* (V) The number of unused nodes */
	u16 reserved1;
	__be32 clump_size;	/* (F) clump size. not usually used. */
	u8 btree_type;		/* (F) BTree type */
	u8 key_type;
	__be32 attributes;	/* (F) attributes */
	u32 reserved3[16];
} __packed;

/* BTree attributes */
#define BTREE_ATTR_BADCLOSE	0x00000001	/* b-tree not closed properly. not
						   used by hfsplus. */
#define HFS_TREE_BIGKEYS	0x00000002	/* key length is u16 instead of u8.
						   used by hfsplus. */
#define HFS_TREE_VARIDXKEYS	0x00000004	/* variable key length instead of
						   max key length. use din catalog
						   b-tree but not in extents
						   b-tree (hfsplus). */

/* HFS+ BTree misc info */
#define HFSPLUS_TREE_HEAD			0
#define HFSPLUS_NODE_MXSZ			32768
#define HFSPLUS_ATTR_TREE_NODE_SIZE		8192
#define HFSPLUS_BTREE_HDR_NODE_RECS_COUNT	3
#define HFSPLUS_BTREE_HDR_USER_BYTES		128

/* btree key type */
#define HFSPLUS_KEY_CASEFOLDING		0xCF	/* case-insensitive */
#define HFSPLUS_KEY_BINARY		0xBC	/* case-sensitive */

/* HFS+ folder data (part of an hfsplus_cat_entry) */
struct hfsplus_cat_folder {
	__be16 type;
	__be16 flags;
	__be32 valence;
	hfsplus_cnid id;
	__be32 create_date;
	__be32 content_mod_date;
	__be32 attribute_mod_date;
	__be32 access_date;
	__be32 backup_date;
	struct hfsplus_perm permissions;
	struct_group_attr(info, __packed,
		DInfo user_info;
		DXInfo finder_info;
	);
	__be32 text_encoding;
	__be32 subfolders;	/* Subfolder count in HFSX. Reserved in HFS+. */
} __packed;

/* HFS+ file data (part of a cat_entry) */
struct hfsplus_cat_file {
	__be16 type;
	__be16 flags;
	u32 reserved1;
	hfsplus_cnid id;
	__be32 create_date;
	__be32 content_mod_date;
	__be32 attribute_mod_date;
	__be32 access_date;
	__be32 backup_date;
	struct hfsplus_perm permissions;
	struct_group_attr(info, __packed,
		FInfo user_info;
		FXInfo finder_info;
	);
	__be32 text_encoding;
	u32 reserved2;

	struct hfsplus_fork_raw data_fork;
	struct hfsplus_fork_raw rsrc_fork;
} __packed;

/* File and folder flag bits */
#define HFSPLUS_FILE_LOCKED		0x0001
#define HFSPLUS_FILE_THREAD_EXISTS	0x0002
#define HFSPLUS_XATTR_EXISTS		0x0004
#define HFSPLUS_ACL_EXISTS		0x0008
#define HFSPLUS_HAS_FOLDER_COUNT	0x0010	/* Folder has subfolder count
						 * (HFSX only) */

/* HFS+ catalog thread (part of a cat_entry) */
struct hfsplus_cat_thread {
	__be16 type;
	s16 reserved;
	hfsplus_cnid parentID;
	struct hfsplus_unistr nodeName;
} __packed;

#define HFSPLUS_MIN_THREAD_SZ		10

/* A data record in the catalog tree */
typedef union {
	__be16 type;
	struct hfsplus_cat_folder folder;
	struct hfsplus_cat_file file;
	struct hfsplus_cat_thread thread;
} __packed hfsplus_cat_entry;

/* HFS+ catalog entry type */
#define HFSPLUS_FOLDER		0x0001
#define HFSPLUS_FILE		0x0002
#define HFSPLUS_FOLDER_THREAD	0x0003
#define HFSPLUS_FILE_THREAD	0x0004

#define HFSPLUS_XATTR_FINDER_INFO_NAME	"com.apple.FinderInfo"
#define HFSPLUS_XATTR_ACL_NAME		"com.apple.system.Security"

#define HFSPLUS_ATTR_INLINE_DATA	0x10
#define HFSPLUS_ATTR_FORK_DATA		0x20
#define HFSPLUS_ATTR_EXTENTS		0x30

/* HFS+ attributes tree key */
struct hfsplus_attr_key {
	__be16 key_len;
	__be16 pad;
	hfsplus_cnid cnid;
	__be32 start_block;
	struct hfsplus_attr_unistr key_name;
} __packed;

#define HFSPLUS_ATTR_KEYLEN	sizeof(struct hfsplus_attr_key)

/* HFS+ fork data attribute */
struct hfsplus_attr_fork_data {
	__be32 record_type;
	__be32 reserved;
	struct hfsplus_fork_raw the_fork;
} __packed;

/* HFS+ extension attribute */
struct hfsplus_attr_extents {
	__be32 record_type;
	__be32 reserved;
	struct hfsplus_extent extents;
} __packed;

#define HFSPLUS_MAX_INLINE_DATA_SIZE	3802

/* HFS+ attribute inline data */
struct hfsplus_attr_inline_data {
	__be32 record_type;
	__be32 reserved1;
	u8 reserved2[6];
	__be16 length;
	u8 raw_bytes[HFSPLUS_MAX_INLINE_DATA_SIZE];
} __packed;

/* A data record in the attributes tree */
typedef union {
	__be32 record_type;
	struct hfsplus_attr_fork_data fork_data;
	struct hfsplus_attr_extents extents;
	struct hfsplus_attr_inline_data inline_data;
} __packed hfsplus_attr_entry;

/* HFS+ generic BTree key */
typedef union {
	__be16 key_len;
	struct hfsplus_cat_key cat;
	struct hfsplus_ext_key ext;
	struct hfsplus_attr_key attr;
} __packed hfsplus_btree_key;

#endif /* _HFS_COMMON_H_ */
