/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  linux/fs/hfs/btree.h
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 */

#include "hfs_fs.h"

typedef int (*btree_keycmp)(const btree_key *, const btree_key *);

#define NODE_HASH_SIZE  256

/* B-tree mutex nested subclasses */
enum hfs_btree_mutex_classes {
	CATALOG_BTREE_MUTEX,
	EXTENTS_BTREE_MUTEX,
	ATTR_BTREE_MUTEX,
};

/* A HFS BTree held in memory */
struct hfs_btree {
	struct super_block *sb;
	struct inode *inode;
	btree_keycmp keycmp;

	u32 cnid;
	u32 root;
	u32 leaf_count;
	u32 leaf_head;
	u32 leaf_tail;
	u32 node_count;
	u32 free_nodes;
	u32 attributes;

	unsigned int node_size;
	unsigned int node_size_shift;
	unsigned int max_key_len;
	unsigned int depth;

	//unsigned int map1_size, map_size;
	struct mutex tree_lock;

	unsigned int pages_per_bnode;
	spinlock_t hash_lock;
	struct hfs_bnode *node_hash[NODE_HASH_SIZE];
	int node_hash_cnt;
};

/* A HFS BTree node in memory */
struct hfs_bnode {
	struct hfs_btree *tree;

	u32 prev;
	u32 this;
	u32 next;
	u32 parent;

	u16 num_recs;
	u8 type;
	u8 height;

	struct hfs_bnode *next_hash;
	unsigned long flags;
	wait_queue_head_t lock_wq;
	atomic_t refcnt;
	unsigned int page_offset;
	struct page *page[];
};

#define HFS_BNODE_ERROR		0
#define HFS_BNODE_NEW		1
#define HFS_BNODE_DELETED	2

struct hfs_find_data {
	btree_key *key;
	btree_key *search_key;
	struct hfs_btree *tree;
	struct hfs_bnode *bnode;
	int record;
	int keyoffset, keylength;
	int entryoffset, entrylength;
};


/* btree.c */
extern struct hfs_btree *hfs_btree_open(struct super_block *sb, u32 id,
					btree_keycmp keycmp);
extern void hfs_btree_close(struct hfs_btree *tree);
extern void hfs_btree_write(struct hfs_btree *tree);
extern int hfs_bmap_reserve(struct hfs_btree *tree, u32 rsvd_nodes);
extern struct hfs_bnode *hfs_bmap_alloc(struct hfs_btree *tree);
extern void hfs_bmap_free(struct hfs_bnode *node);

/* bnode.c */
extern void hfs_bnode_read(struct hfs_bnode *node, void *buf, u32 off, u32 len);
extern u16 hfs_bnode_read_u16(struct hfs_bnode *node, u32 off);
extern u8 hfs_bnode_read_u8(struct hfs_bnode *node, u32 off);
extern void hfs_bnode_read_key(struct hfs_bnode *node, void *key, u32 off);
extern void hfs_bnode_write(struct hfs_bnode *node, void *buf, u32 off, u32 len);
extern void hfs_bnode_write_u16(struct hfs_bnode *node, u32 off, u16 data);
extern void hfs_bnode_write_u8(struct hfs_bnode *node, u32 off, u8 data);
extern void hfs_bnode_clear(struct hfs_bnode *node, u32 off, u32 len);
extern void hfs_bnode_copy(struct hfs_bnode *dst_node, u32 dst,
			   struct hfs_bnode *src_node, u32 src, u32 len);
extern void hfs_bnode_move(struct hfs_bnode *node, u32 dst, u32 src, u32 len);
extern void hfs_bnode_dump(struct hfs_bnode *node);
extern void hfs_bnode_unlink(struct hfs_bnode *node);
extern struct hfs_bnode *hfs_bnode_findhash(struct hfs_btree *tree, u32 cnid);
extern struct hfs_bnode *hfs_bnode_find(struct hfs_btree *tree, u32 num);
extern void hfs_bnode_unhash(struct hfs_bnode *node);
extern void hfs_bnode_free(struct hfs_bnode *node);
extern struct hfs_bnode *hfs_bnode_create(struct hfs_btree *tree, u32 num);
extern void hfs_bnode_get(struct hfs_bnode *node);
extern void hfs_bnode_put(struct hfs_bnode *node);

/* brec.c */
extern u16 hfs_brec_lenoff(struct hfs_bnode *node, u16 rec, u16 *off);
extern u16 hfs_brec_keylen(struct hfs_bnode *node, u16 rec);
extern int hfs_brec_insert(struct hfs_find_data *fd, void *entry, u32 entry_len);
extern int hfs_brec_remove(struct hfs_find_data *fd);

/* bfind.c */
extern int hfs_find_init(struct hfs_btree *tree, struct hfs_find_data *fd);
extern void hfs_find_exit(struct hfs_find_data *fd);
extern int __hfs_brec_find(struct hfs_bnode *bnode, struct hfs_find_data *fd);
extern int hfs_brec_find(struct hfs_find_data *fd);
extern int hfs_brec_read(struct hfs_find_data *fd, void *rec, u32 rec_len);
extern int hfs_brec_goto(struct hfs_find_data *fd, int cnt);
