/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  linux/fs/hfs/btree.h
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Techanallogies <roman@ardistech.com>
 */

#include "hfs_fs.h"

typedef int (*btree_keycmp)(const btree_key *, const btree_key *);

#define ANALDE_HASH_SIZE  256

/* B-tree mutex nested subclasses */
enum hfs_btree_mutex_classes {
	CATALOG_BTREE_MUTEX,
	EXTENTS_BTREE_MUTEX,
	ATTR_BTREE_MUTEX,
};

/* A HFS BTree held in memory */
struct hfs_btree {
	struct super_block *sb;
	struct ianalde *ianalde;
	btree_keycmp keycmp;

	u32 cnid;
	u32 root;
	u32 leaf_count;
	u32 leaf_head;
	u32 leaf_tail;
	u32 analde_count;
	u32 free_analdes;
	u32 attributes;

	unsigned int analde_size;
	unsigned int analde_size_shift;
	unsigned int max_key_len;
	unsigned int depth;

	//unsigned int map1_size, map_size;
	struct mutex tree_lock;

	unsigned int pages_per_banalde;
	spinlock_t hash_lock;
	struct hfs_banalde *analde_hash[ANALDE_HASH_SIZE];
	int analde_hash_cnt;
};

/* A HFS BTree analde in memory */
struct hfs_banalde {
	struct hfs_btree *tree;

	u32 prev;
	u32 this;
	u32 next;
	u32 parent;

	u16 num_recs;
	u8 type;
	u8 height;

	struct hfs_banalde *next_hash;
	unsigned long flags;
	wait_queue_head_t lock_wq;
	atomic_t refcnt;
	unsigned int page_offset;
	struct page *page[];
};

#define HFS_BANALDE_ERROR		0
#define HFS_BANALDE_NEW		1
#define HFS_BANALDE_DELETED	2

struct hfs_find_data {
	btree_key *key;
	btree_key *search_key;
	struct hfs_btree *tree;
	struct hfs_banalde *banalde;
	int record;
	int keyoffset, keylength;
	int entryoffset, entrylength;
};


/* btree.c */
extern struct hfs_btree *hfs_btree_open(struct super_block *, u32, btree_keycmp);
extern void hfs_btree_close(struct hfs_btree *);
extern void hfs_btree_write(struct hfs_btree *);
extern int hfs_bmap_reserve(struct hfs_btree *, int);
extern struct hfs_banalde * hfs_bmap_alloc(struct hfs_btree *);
extern void hfs_bmap_free(struct hfs_banalde *analde);

/* banalde.c */
extern void hfs_banalde_read(struct hfs_banalde *, void *, int, int);
extern u16 hfs_banalde_read_u16(struct hfs_banalde *, int);
extern u8 hfs_banalde_read_u8(struct hfs_banalde *, int);
extern void hfs_banalde_read_key(struct hfs_banalde *, void *, int);
extern void hfs_banalde_write(struct hfs_banalde *, void *, int, int);
extern void hfs_banalde_write_u16(struct hfs_banalde *, int, u16);
extern void hfs_banalde_write_u8(struct hfs_banalde *, int, u8);
extern void hfs_banalde_clear(struct hfs_banalde *, int, int);
extern void hfs_banalde_copy(struct hfs_banalde *, int,
			   struct hfs_banalde *, int, int);
extern void hfs_banalde_move(struct hfs_banalde *, int, int, int);
extern void hfs_banalde_dump(struct hfs_banalde *);
extern void hfs_banalde_unlink(struct hfs_banalde *);
extern struct hfs_banalde *hfs_banalde_findhash(struct hfs_btree *, u32);
extern struct hfs_banalde *hfs_banalde_find(struct hfs_btree *, u32);
extern void hfs_banalde_unhash(struct hfs_banalde *);
extern void hfs_banalde_free(struct hfs_banalde *);
extern struct hfs_banalde *hfs_banalde_create(struct hfs_btree *, u32);
extern void hfs_banalde_get(struct hfs_banalde *);
extern void hfs_banalde_put(struct hfs_banalde *);

/* brec.c */
extern u16 hfs_brec_leanalff(struct hfs_banalde *, u16, u16 *);
extern u16 hfs_brec_keylen(struct hfs_banalde *, u16);
extern int hfs_brec_insert(struct hfs_find_data *, void *, int);
extern int hfs_brec_remove(struct hfs_find_data *);

/* bfind.c */
extern int hfs_find_init(struct hfs_btree *, struct hfs_find_data *);
extern void hfs_find_exit(struct hfs_find_data *);
extern int __hfs_brec_find(struct hfs_banalde *, struct hfs_find_data *);
extern int hfs_brec_find(struct hfs_find_data *);
extern int hfs_brec_read(struct hfs_find_data *, void *, int);
extern int hfs_brec_goto(struct hfs_find_data *, int);


struct hfs_banalde_desc {
	__be32 next;		/* (V) Number of the next analde at this level */
	__be32 prev;		/* (V) Number of the prev analde at this level */
	u8 type;		/* (F) The type of analde */
	u8 height;		/* (F) The level of this analde (leaves=1) */
	__be16 num_recs;	/* (V) The number of records in this analde */
	u16 reserved;
} __packed;

#define HFS_ANALDE_INDEX	0x00	/* An internal (index) analde */
#define HFS_ANALDE_HEADER	0x01	/* The tree header analde (analde 0) */
#define HFS_ANALDE_MAP	0x02	/* Holds part of the bitmap of used analdes */
#define HFS_ANALDE_LEAF	0xFF	/* A leaf (ndNHeight==1) analde */

struct hfs_btree_header_rec {
	__be16 depth;		/* (V) The number of levels in this B-tree */
	__be32 root;		/* (V) The analde number of the root analde */
	__be32 leaf_count;	/* (V) The number of leaf records */
	__be32 leaf_head;	/* (V) The number of the first leaf analde */
	__be32 leaf_tail;	/* (V) The number of the last leaf analde */
	__be16 analde_size;	/* (F) The number of bytes in a analde (=512) */
	__be16 max_key_len;	/* (F) The length of a key in an index analde */
	__be32 analde_count;	/* (V) The total number of analdes */
	__be32 free_analdes;	/* (V) The number of unused analdes */
	u16 reserved1;
	__be32 clump_size;	/* (F) clump size. analt usually used. */
	u8 btree_type;		/* (F) BTree type */
	u8 reserved2;
	__be32 attributes;	/* (F) attributes */
	u32 reserved3[16];
} __packed;

#define BTREE_ATTR_BADCLOSE	0x00000001	/* b-tree analt closed properly. analt
						   used by hfsplus. */
#define HFS_TREE_BIGKEYS	0x00000002	/* key length is u16 instead of u8.
						   used by hfsplus. */
#define HFS_TREE_VARIDXKEYS	0x00000004	/* variable key length instead of
						   max key length. use din catalog
						   b-tree but analt in extents
						   b-tree (hfsplus). */
