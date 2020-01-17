/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  linux/fs/hfs/btree.h
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Techyeslogies <roman@ardistech.com>
 */

#include "hfs_fs.h"

typedef int (*btree_keycmp)(const btree_key *, const btree_key *);

#define NODE_HASH_SIZE  256

/* A HFS BTree held in memory */
struct hfs_btree {
	struct super_block *sb;
	struct iyesde *iyesde;
	btree_keycmp keycmp;

	u32 cnid;
	u32 root;
	u32 leaf_count;
	u32 leaf_head;
	u32 leaf_tail;
	u32 yesde_count;
	u32 free_yesdes;
	u32 attributes;

	unsigned int yesde_size;
	unsigned int yesde_size_shift;
	unsigned int max_key_len;
	unsigned int depth;

	//unsigned int map1_size, map_size;
	struct mutex tree_lock;

	unsigned int pages_per_byesde;
	spinlock_t hash_lock;
	struct hfs_byesde *yesde_hash[NODE_HASH_SIZE];
	int yesde_hash_cnt;
};

/* A HFS BTree yesde in memory */
struct hfs_byesde {
	struct hfs_btree *tree;

	u32 prev;
	u32 this;
	u32 next;
	u32 parent;

	u16 num_recs;
	u8 type;
	u8 height;

	struct hfs_byesde *next_hash;
	unsigned long flags;
	wait_queue_head_t lock_wq;
	atomic_t refcnt;
	unsigned int page_offset;
	struct page *page[0];
};

#define HFS_BNODE_ERROR		0
#define HFS_BNODE_NEW		1
#define HFS_BNODE_DELETED	2

struct hfs_find_data {
	btree_key *key;
	btree_key *search_key;
	struct hfs_btree *tree;
	struct hfs_byesde *byesde;
	int record;
	int keyoffset, keylength;
	int entryoffset, entrylength;
};


/* btree.c */
extern struct hfs_btree *hfs_btree_open(struct super_block *, u32, btree_keycmp);
extern void hfs_btree_close(struct hfs_btree *);
extern void hfs_btree_write(struct hfs_btree *);
extern int hfs_bmap_reserve(struct hfs_btree *, int);
extern struct hfs_byesde * hfs_bmap_alloc(struct hfs_btree *);
extern void hfs_bmap_free(struct hfs_byesde *yesde);

/* byesde.c */
extern void hfs_byesde_read(struct hfs_byesde *, void *, int, int);
extern u16 hfs_byesde_read_u16(struct hfs_byesde *, int);
extern u8 hfs_byesde_read_u8(struct hfs_byesde *, int);
extern void hfs_byesde_read_key(struct hfs_byesde *, void *, int);
extern void hfs_byesde_write(struct hfs_byesde *, void *, int, int);
extern void hfs_byesde_write_u16(struct hfs_byesde *, int, u16);
extern void hfs_byesde_write_u8(struct hfs_byesde *, int, u8);
extern void hfs_byesde_clear(struct hfs_byesde *, int, int);
extern void hfs_byesde_copy(struct hfs_byesde *, int,
			   struct hfs_byesde *, int, int);
extern void hfs_byesde_move(struct hfs_byesde *, int, int, int);
extern void hfs_byesde_dump(struct hfs_byesde *);
extern void hfs_byesde_unlink(struct hfs_byesde *);
extern struct hfs_byesde *hfs_byesde_findhash(struct hfs_btree *, u32);
extern struct hfs_byesde *hfs_byesde_find(struct hfs_btree *, u32);
extern void hfs_byesde_unhash(struct hfs_byesde *);
extern void hfs_byesde_free(struct hfs_byesde *);
extern struct hfs_byesde *hfs_byesde_create(struct hfs_btree *, u32);
extern void hfs_byesde_get(struct hfs_byesde *);
extern void hfs_byesde_put(struct hfs_byesde *);

/* brec.c */
extern u16 hfs_brec_leyesff(struct hfs_byesde *, u16, u16 *);
extern u16 hfs_brec_keylen(struct hfs_byesde *, u16);
extern int hfs_brec_insert(struct hfs_find_data *, void *, int);
extern int hfs_brec_remove(struct hfs_find_data *);

/* bfind.c */
extern int hfs_find_init(struct hfs_btree *, struct hfs_find_data *);
extern void hfs_find_exit(struct hfs_find_data *);
extern int __hfs_brec_find(struct hfs_byesde *, struct hfs_find_data *);
extern int hfs_brec_find(struct hfs_find_data *);
extern int hfs_brec_read(struct hfs_find_data *, void *, int);
extern int hfs_brec_goto(struct hfs_find_data *, int);


struct hfs_byesde_desc {
	__be32 next;		/* (V) Number of the next yesde at this level */
	__be32 prev;		/* (V) Number of the prev yesde at this level */
	u8 type;		/* (F) The type of yesde */
	u8 height;		/* (F) The level of this yesde (leaves=1) */
	__be16 num_recs;	/* (V) The number of records in this yesde */
	u16 reserved;
} __packed;

#define HFS_NODE_INDEX	0x00	/* An internal (index) yesde */
#define HFS_NODE_HEADER	0x01	/* The tree header yesde (yesde 0) */
#define HFS_NODE_MAP	0x02	/* Holds part of the bitmap of used yesdes */
#define HFS_NODE_LEAF	0xFF	/* A leaf (ndNHeight==1) yesde */

struct hfs_btree_header_rec {
	__be16 depth;		/* (V) The number of levels in this B-tree */
	__be32 root;		/* (V) The yesde number of the root yesde */
	__be32 leaf_count;	/* (V) The number of leaf records */
	__be32 leaf_head;	/* (V) The number of the first leaf yesde */
	__be32 leaf_tail;	/* (V) The number of the last leaf yesde */
	__be16 yesde_size;	/* (F) The number of bytes in a yesde (=512) */
	__be16 max_key_len;	/* (F) The length of a key in an index yesde */
	__be32 yesde_count;	/* (V) The total number of yesdes */
	__be32 free_yesdes;	/* (V) The number of unused yesdes */
	u16 reserved1;
	__be32 clump_size;	/* (F) clump size. yest usually used. */
	u8 btree_type;		/* (F) BTree type */
	u8 reserved2;
	__be32 attributes;	/* (F) attributes */
	u32 reserved3[16];
} __packed;

#define BTREE_ATTR_BADCLOSE	0x00000001	/* b-tree yest closed properly. yest
						   used by hfsplus. */
#define HFS_TREE_BIGKEYS	0x00000002	/* key length is u16 instead of u8.
						   used by hfsplus. */
#define HFS_TREE_VARIDXKEYS	0x00000004	/* variable key length instead of
						   max key length. use din catalog
						   b-tree but yest in extents
						   b-tree (hfsplus). */
