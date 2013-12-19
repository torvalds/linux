/*
 *  linux/include/linux/hfsplus_raw.h
 *
 * Copyright (C) 1999
 * Brad Boyer (flar@pants.nu)
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 *
 * Format of structures on disk
 * Information taken from Apple Technote #1150 (HFS Plus Volume Format)
 *
 */

#ifndef _LINUX_HFSPLUS_RAW_H
#define _LINUX_HFSPLUS_RAW_H

#include <linux/types.h>

/* Some constants */
#define HFSPLUS_SECTOR_SIZE        512
#define HFSPLUS_SECTOR_SHIFT         9
#define HFSPLUS_VOLHEAD_SECTOR       2
#define HFSPLUS_VOLHEAD_SIG     0x482b
#define HFSPLUS_VOLHEAD_SIGX    0x4858
#define HFSPLUS_SUPER_MAGIC     0x482b
#define HFSPLUS_MIN_VERSION          4
#define HFSPLUS_CURRENT_VERSION      5

#define HFSP_WRAP_MAGIC         0x4244
#define HFSP_WRAP_ATTRIB_SLOCK  0x8000
#define HFSP_WRAP_ATTRIB_SPARED 0x0200

#define HFSP_WRAPOFF_SIG          0x00
#define HFSP_WRAPOFF_ATTRIB       0x0A
#define HFSP_WRAPOFF_ABLKSIZE     0x14
#define HFSP_WRAPOFF_ABLKSTART    0x1C
#define HFSP_WRAPOFF_EMBEDSIG     0x7C
#define HFSP_WRAPOFF_EMBEDEXT     0x7E

#define HFSP_HIDDENDIR_NAME \
	"\xe2\x90\x80\xe2\x90\x80\xe2\x90\x80\xe2\x90\x80HFS+ Private Data"

#define HFSP_HARDLINK_TYPE	0x686c6e6b	/* 'hlnk' */
#define HFSP_HFSPLUS_CREATOR	0x6866732b	/* 'hfs+' */

#define HFSP_SYMLINK_TYPE	0x736c6e6b	/* 'slnk' */
#define HFSP_SYMLINK_CREATOR	0x72686170	/* 'rhap' */

#define HFSP_MOUNT_VERSION	0x482b4c78	/* 'H+Lx' */

/* Structures used on disk */

typedef __be32 hfsplus_cnid;
typedef __be16 hfsplus_unichr;

#define HFSPLUS_MAX_STRLEN 255
#define HFSPLUS_ATTR_MAX_STRLEN 127

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

/* HFS+ BTree node descriptor */
struct hfs_bnode_desc {
	__be32 next;
	__be32 prev;
	s8 type;
	u8 height;
	__be16 num_recs;
	u16 reserved;
} __packed;

/* HFS+ BTree node types */
#define HFS_NODE_INDEX	0x00	/* An internal (index) node */
#define HFS_NODE_HEADER	0x01	/* The tree header node (node 0) */
#define HFS_NODE_MAP	0x02	/* Holds part of the bitmap of used nodes */
#define HFS_NODE_LEAF	0xFF	/* A leaf (ndNHeight==1) node */

/* HFS+ BTree header */
struct hfs_btree_header_rec {
	__be16 depth;
	__be32 root;
	__be32 leaf_count;
	__be32 leaf_head;
	__be32 leaf_tail;
	__be16 node_size;
	__be16 max_key_len;
	__be32 node_count;
	__be32 free_nodes;
	u16 reserved1;
	__be32 clump_size;
	u8 btree_type;
	u8 key_type;
	__be32 attributes;
	u32 reserved3[16];
} __packed;

/* BTree attributes */
#define HFS_TREE_BIGKEYS	2
#define HFS_TREE_VARIDXKEYS	4

/* HFS+ BTree misc info */
#define HFSPLUS_TREE_HEAD 0
#define HFSPLUS_NODE_MXSZ 32768
#define HFSPLUS_ATTR_TREE_NODE_SIZE		8192
#define HFSPLUS_BTREE_HDR_NODE_RECS_COUNT	3
#define HFSPLUS_BTREE_HDR_USER_BYTES		128

/* Some special File ID numbers (stolen from hfs.h) */
#define HFSPLUS_POR_CNID		1	/* Parent Of the Root */
#define HFSPLUS_ROOT_CNID		2	/* ROOT directory */
#define HFSPLUS_EXT_CNID		3	/* EXTents B-tree */
#define HFSPLUS_CAT_CNID		4	/* CATalog B-tree */
#define HFSPLUS_BAD_CNID		5	/* BAD blocks file */
#define HFSPLUS_ALLOC_CNID		6	/* ALLOCation file */
#define HFSPLUS_START_CNID		7	/* STARTup file */
#define HFSPLUS_ATTR_CNID		8	/* ATTRibutes file */
#define HFSPLUS_EXCH_CNID		15	/* ExchangeFiles temp id */
#define HFSPLUS_FIRSTUSER_CNID		16	/* first available user id */

/* btree key type */
#define HFSPLUS_KEY_CASEFOLDING		0xCF	/* case-insensitive */
#define HFSPLUS_KEY_BINARY		0xBC	/* case-sensitive */

/* HFS+ catalog entry key */
struct hfsplus_cat_key {
	__be16 key_len;
	hfsplus_cnid parent;
	struct hfsplus_unistr name;
} __packed;

#define HFSPLUS_CAT_KEYLEN	(sizeof(struct hfsplus_cat_key))

/* Structs from hfs.h */
struct hfsp_point {
	__be16 v;
	__be16 h;
} __packed;

struct hfsp_rect {
	__be16 top;
	__be16 left;
	__be16 bottom;
	__be16 right;
} __packed;


/* HFS directory info (stolen from hfs.h */
struct DInfo {
	struct hfsp_rect frRect;
	__be16 frFlags;
	struct hfsp_point frLocation;
	__be16 frView;
} __packed;

struct DXInfo {
	struct hfsp_point frScroll;
	__be32 frOpenChain;
	__be16 frUnused;
	__be16 frComment;
	__be32 frPutAway;
} __packed;

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
	struct DInfo user_info;
	struct DXInfo finder_info;
	__be32 text_encoding;
	u32 reserved;
} __packed;

/* HFS file info (stolen from hfs.h) */
struct FInfo {
	__be32 fdType;
	__be32 fdCreator;
	__be16 fdFlags;
	struct hfsp_point fdLocation;
	__be16 fdFldr;
} __packed;

struct FXInfo {
	__be16 fdIconID;
	u8 fdUnused[8];
	__be16 fdComment;
	__be32 fdPutAway;
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
	struct FInfo user_info;
	struct FXInfo finder_info;
	__be32 text_encoding;
	u32 reserved2;

	struct hfsplus_fork_raw data_fork;
	struct hfsplus_fork_raw rsrc_fork;
} __packed;

/* File attribute bits */
#define HFSPLUS_FILE_LOCKED		0x0001
#define HFSPLUS_FILE_THREAD_EXISTS	0x0002
#define HFSPLUS_XATTR_EXISTS		0x0004
#define HFSPLUS_ACL_EXISTS		0x0008

/* HFS+ catalog thread (part of a cat_entry) */
struct hfsplus_cat_thread {
	__be16 type;
	s16 reserved;
	hfsplus_cnid parentID;
	struct hfsplus_unistr nodeName;
} __packed;

#define HFSPLUS_MIN_THREAD_SZ 10

/* A data record in the catalog tree */
typedef union {
	__be16 type;
	struct hfsplus_cat_folder folder;
	struct hfsplus_cat_file file;
	struct hfsplus_cat_thread thread;
} __packed hfsplus_cat_entry;

/* HFS+ catalog entry type */
#define HFSPLUS_FOLDER         0x0001
#define HFSPLUS_FILE           0x0002
#define HFSPLUS_FOLDER_THREAD  0x0003
#define HFSPLUS_FILE_THREAD    0x0004

/* HFS+ extents tree key */
struct hfsplus_ext_key {
	__be16 key_len;
	u8 fork_type;
	u8 pad;
	hfsplus_cnid cnid;
	__be32 start_block;
} __packed;

#define HFSPLUS_EXT_KEYLEN	sizeof(struct hfsplus_ext_key)

#define HFSPLUS_XATTR_FINDER_INFO_NAME "com.apple.FinderInfo"
#define HFSPLUS_XATTR_ACL_NAME "com.apple.system.Security"

#define HFSPLUS_ATTR_INLINE_DATA 0x10
#define HFSPLUS_ATTR_FORK_DATA   0x20
#define HFSPLUS_ATTR_EXTENTS     0x30

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

#define HFSPLUS_MAX_INLINE_DATA_SIZE 3802

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

#endif
