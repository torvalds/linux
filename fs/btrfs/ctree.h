#ifndef __BTRFS__
#define __BTRFS__

#include "list.h"
#include "kerncompat.h"

struct btrfs_trans_handle;

#define BTRFS_MAGIC "_BtRfS_M"

#define BTRFS_ROOT_TREE_OBJECTID 1
#define BTRFS_EXTENT_TREE_OBJECTID 2
#define BTRFS_FS_TREE_OBJECTID 3

/*
 * the key defines the order in the tree, and so it also defines (optimal)
 * block layout.  objectid corresonds to the inode number.  The flags
 * tells us things about the object, and is a kind of stream selector.
 * so for a given inode, keys with flags of 1 might refer to the inode
 * data, flags of 2 may point to file data in the btree and flags == 3
 * may point to extents.
 *
 * offset is the starting byte offset for this key in the stream.
 *
 * btrfs_disk_key is in disk byte order.  struct btrfs_key is always
 * in cpu native order.  Otherwise they are identical and their sizes
 * should be the same (ie both packed)
 */
struct btrfs_disk_key {
	__le64 objectid;
	__le32 flags;
	__le64 offset;
} __attribute__ ((__packed__));

struct btrfs_key {
	u64 objectid;
	u32 flags;
	u64 offset;
} __attribute__ ((__packed__));

/*
 * every tree block (leaf or node) starts with this header.
 */
struct btrfs_header {
	u8 fsid[16]; /* FS specific uuid */
	__le64 blocknr; /* which block this node is supposed to live in */
	__le64 parentid; /* objectid of the tree root */
	__le32 csum;
	__le32 ham;
	__le16 nritems;
	__le16 flags;
	/* generation flags to be added */
} __attribute__ ((__packed__));

#define BTRFS_MAX_LEVEL 8
#define BTRFS_NODEPTRS_PER_BLOCK(r) (((r)->blocksize - \
			        sizeof(struct btrfs_header)) / \
			       (sizeof(struct btrfs_disk_key) + sizeof(u64)))
#define __BTRFS_LEAF_DATA_SIZE(bs) ((bs) - sizeof(struct btrfs_header))
#define BTRFS_LEAF_DATA_SIZE(r) (__BTRFS_LEAF_DATA_SIZE(r->blocksize))

struct btrfs_buffer;
/*
 * the super block basically lists the main trees of the FS
 * it currently lacks any block count etc etc
 */
struct btrfs_super_block {
	u8 fsid[16];    /* FS specific uuid */
	__le64 blocknr; /* this block number */
	__le32 csum;
	__le64 magic;
	__le32 blocksize;
	__le64 generation;
	__le64 root;
	__le64 total_blocks;
	__le64 blocks_used;
} __attribute__ ((__packed__));

/*
 * A leaf is full of items. offset and size tell us where to find
 * the item in the leaf (relative to the start of the data area)
 */
struct btrfs_item {
	struct btrfs_disk_key key;
	__le32 offset;
	__le16 size;
} __attribute__ ((__packed__));

/*
 * leaves have an item area and a data area:
 * [item0, item1....itemN] [free space] [dataN...data1, data0]
 *
 * The data is separate from the items to get the keys closer together
 * during searches.
 */
struct btrfs_leaf {
	struct btrfs_header header;
	struct btrfs_item items[];
} __attribute__ ((__packed__));

/*
 * all non-leaf blocks are nodes, they hold only keys and pointers to
 * other blocks
 */
struct btrfs_key_ptr {
	struct btrfs_disk_key key;
	__le64 blockptr;
} __attribute__ ((__packed__));

struct btrfs_node {
	struct btrfs_header header;
	struct btrfs_key_ptr ptrs[];
} __attribute__ ((__packed__));

/*
 * btrfs_paths remember the path taken from the root down to the leaf.
 * level 0 is always the leaf, and nodes[1...BTRFS_MAX_LEVEL] will point
 * to any other levels that are present.
 *
 * The slots array records the index of the item or block pointer
 * used while walking the tree.
 */
struct btrfs_path {
	struct btrfs_buffer *nodes[BTRFS_MAX_LEVEL];
	int slots[BTRFS_MAX_LEVEL];
};

/*
 * items in the extent btree are used to record the objectid of the
 * owner of the block and the number of references
 */
struct btrfs_extent_item {
	__le32 refs;
	__le64 owner;
} __attribute__ ((__packed__));

struct btrfs_inode_timespec {
	__le32 sec;
	__le32 nsec;
} __attribute__ ((__packed__));

/*
 * there is no padding here on purpose.  If you want to extent the inode,
 * make a new item type
 */
struct btrfs_inode_item {
	__le64 generation;
	__le64 size;
	__le64 nblocks;
	__le32 nlink;
	__le32 uid;
	__le32 gid;
	__le32 mode;
	__le32 rdev;
	__le16 flags;
	__le16 compat_flags;
	struct btrfs_inode_timespec atime;
	struct btrfs_inode_timespec ctime;
	struct btrfs_inode_timespec mtime;
	struct btrfs_inode_timespec otime;
} __attribute__ ((__packed__));

/* inline data is just a blob of bytes */
struct btrfs_inline_data_item {
	u8 data;
} __attribute__ ((__packed__));

struct btrfs_dir_item {
	__le64 objectid;
	__le16 flags;
	__le16 name_len;
	u8 type;
} __attribute__ ((__packed__));

struct btrfs_root_item {
	__le64 blocknr;
	__le32 flags;
	__le64 block_limit;
	__le64 blocks_used;
	__le32 refs;
};

/*
 * in ram representation of the tree.  extent_root is used for all allocations
 * and for the extent tree extent_root root.  current_insert is used
 * only for the extent tree.
 */
struct btrfs_root {
	struct btrfs_buffer *node;
	struct btrfs_buffer *commit_root;
	struct btrfs_root *extent_root;
	struct btrfs_root *tree_root;
	struct btrfs_key current_insert;
	struct btrfs_key last_insert;
	int fp;
	struct radix_tree_root cache_radix;
	struct radix_tree_root pinned_radix;
	struct list_head trans;
	struct list_head cache;
	int cache_size;
	int ref_cows;
	struct btrfs_root_item root_item;
	struct btrfs_key root_key;
	u32 blocksize;
	struct btrfs_trans_handle *running_transaction;
};

/* the lower bits in the key flags defines the item type */
#define BTRFS_KEY_TYPE_MAX	256
#define BTRFS_KEY_TYPE_MASK	(BTRFS_KEY_TYPE_MAX - 1)

/*
 * inode items have the data typically returned from stat and store other
 * info about object characteristics.  There is one for every file and dir in
 * the FS
 */
#define BTRFS_INODE_ITEM_KEY	1

/*
 * dir items are the name -> inode pointers in a directory.  There is one
 * for every name in a directory.
 */
#define BTRFS_DIR_ITEM_KEY	2
/*
 * inline data is file data that fits in the btree.
 */
#define BTRFS_INLINE_DATA_KEY	3
/*
 * extent data is for data that can't fit in the btree.  It points to
 * a (hopefully) huge chunk of disk
 */
#define BTRFS_EXTENT_DATA_KEY	4
/*
 * root items point to tree roots.  There are typically in the root
 * tree used by the super block to find all the other trees
 */
#define BTRFS_ROOT_ITEM_KEY	5
/*
 * extent items are in the extent map tree.  These record which blocks
 * are used, and how many references there are to each block
 */
#define BTRFS_EXTENT_ITEM_KEY	6
/*
 * string items are for debugging.  They just store a short string of
 * data in the FS
 */
#define BTRFS_STRING_ITEM_KEY	7

static inline u64 btrfs_inode_generation(struct btrfs_inode_item *i)
{
	return le64_to_cpu(i->generation);
}

static inline void btrfs_set_inode_generation(struct btrfs_inode_item *i,
					      u64 val)
{
	i->generation = cpu_to_le64(val);
}

static inline u64 btrfs_inode_size(struct btrfs_inode_item *i)
{
	return le64_to_cpu(i->size);
}

static inline void btrfs_set_inode_size(struct btrfs_inode_item *i, u64 val)
{
	i->size = cpu_to_le64(val);
}

static inline u64 btrfs_inode_nblocks(struct btrfs_inode_item *i)
{
	return le64_to_cpu(i->nblocks);
}

static inline void btrfs_set_inode_nblocks(struct btrfs_inode_item *i, u64 val)
{
	i->nblocks = cpu_to_le64(val);
}

static inline u32 btrfs_inode_nlink(struct btrfs_inode_item *i)
{
	return le32_to_cpu(i->nlink);
}

static inline void btrfs_set_inode_nlink(struct btrfs_inode_item *i, u32 val)
{
	i->nlink = cpu_to_le32(val);
}

static inline u32 btrfs_inode_uid(struct btrfs_inode_item *i)
{
	return le32_to_cpu(i->uid);
}

static inline void btrfs_set_inode_uid(struct btrfs_inode_item *i, u32 val)
{
	i->uid = cpu_to_le32(val);
}

static inline u32 btrfs_inode_gid(struct btrfs_inode_item *i)
{
	return le32_to_cpu(i->gid);
}

static inline void btrfs_set_inode_gid(struct btrfs_inode_item *i, u32 val)
{
	i->gid = cpu_to_le32(val);
}

static inline u32 btrfs_inode_mode(struct btrfs_inode_item *i)
{
	return le32_to_cpu(i->mode);
}

static inline void btrfs_set_inode_mode(struct btrfs_inode_item *i, u32 val)
{
	i->mode = cpu_to_le32(val);
}

static inline u32 btrfs_inode_rdev(struct btrfs_inode_item *i)
{
	return le32_to_cpu(i->rdev);
}

static inline void btrfs_set_inode_rdev(struct btrfs_inode_item *i, u32 val)
{
	i->rdev = cpu_to_le32(val);
}

static inline u16 btrfs_inode_flags(struct btrfs_inode_item *i)
{
	return le16_to_cpu(i->flags);
}

static inline void btrfs_set_inode_flags(struct btrfs_inode_item *i, u16 val)
{
	i->flags = cpu_to_le16(val);
}

static inline u16 btrfs_inode_compat_flags(struct btrfs_inode_item *i)
{
	return le16_to_cpu(i->compat_flags);
}

static inline void btrfs_set_inode_compat_flags(struct btrfs_inode_item *i,
						u16 val)
{
	i->compat_flags = cpu_to_le16(val);
}


static inline u64 btrfs_extent_owner(struct btrfs_extent_item *ei)
{
	return le64_to_cpu(ei->owner);
}

static inline void btrfs_set_extent_owner(struct btrfs_extent_item *ei, u64 val)
{
	ei->owner = cpu_to_le64(val);
}

static inline u32 btrfs_extent_refs(struct btrfs_extent_item *ei)
{
	return le32_to_cpu(ei->refs);
}

static inline void btrfs_set_extent_refs(struct btrfs_extent_item *ei, u32 val)
{
	ei->refs = cpu_to_le32(val);
}

static inline u64 btrfs_node_blockptr(struct btrfs_node *n, int nr)
{
	return le64_to_cpu(n->ptrs[nr].blockptr);
}

static inline void btrfs_set_node_blockptr(struct btrfs_node *n, int nr,
					   u64 val)
{
	n->ptrs[nr].blockptr = cpu_to_le64(val);
}

static inline u32 btrfs_item_offset(struct btrfs_item *item)
{
	return le32_to_cpu(item->offset);
}

static inline void btrfs_set_item_offset(struct btrfs_item *item, u32 val)
{
	item->offset = cpu_to_le32(val);
}

static inline u32 btrfs_item_end(struct btrfs_item *item)
{
	return le32_to_cpu(item->offset) + le16_to_cpu(item->size);
}

static inline u16 btrfs_item_size(struct btrfs_item *item)
{
	return le16_to_cpu(item->size);
}

static inline void btrfs_set_item_size(struct btrfs_item *item, u16 val)
{
	item->size = cpu_to_le16(val);
}

static inline u64 btrfs_dir_objectid(struct btrfs_dir_item *d)
{
	return le64_to_cpu(d->objectid);
}

static inline void btrfs_set_dir_objectid(struct btrfs_dir_item *d, u64 val)
{
	d->objectid = cpu_to_le64(val);
}

static inline u16 btrfs_dir_flags(struct btrfs_dir_item *d)
{
	return le16_to_cpu(d->flags);
}

static inline void btrfs_set_dir_flags(struct btrfs_dir_item *d, u16 val)
{
	d->flags = cpu_to_le16(val);
}

static inline u8 btrfs_dir_type(struct btrfs_dir_item *d)
{
	return d->type;
}

static inline void btrfs_set_dir_type(struct btrfs_dir_item *d, u8 val)
{
	d->type = val;
}

static inline u16 btrfs_dir_name_len(struct btrfs_dir_item *d)
{
	return le16_to_cpu(d->name_len);
}

static inline void btrfs_set_dir_name_len(struct btrfs_dir_item *d, u16 val)
{
	d->name_len = cpu_to_le16(val);
}

static inline void btrfs_disk_key_to_cpu(struct btrfs_key *cpu,
					 struct btrfs_disk_key *disk)
{
	cpu->offset = le64_to_cpu(disk->offset);
	cpu->flags = le32_to_cpu(disk->flags);
	cpu->objectid = le64_to_cpu(disk->objectid);
}

static inline void btrfs_cpu_key_to_disk(struct btrfs_disk_key *disk,
					 struct btrfs_key *cpu)
{
	disk->offset = cpu_to_le64(cpu->offset);
	disk->flags = cpu_to_le32(cpu->flags);
	disk->objectid = cpu_to_le64(cpu->objectid);
}

static inline u64 btrfs_disk_key_objectid(struct btrfs_disk_key *disk)
{
	return le64_to_cpu(disk->objectid);
}

static inline void btrfs_set_disk_key_objectid(struct btrfs_disk_key *disk,
					       u64 val)
{
	disk->objectid = cpu_to_le64(val);
}

static inline u64 btrfs_disk_key_offset(struct btrfs_disk_key *disk)
{
	return le64_to_cpu(disk->offset);
}

static inline void btrfs_set_disk_key_offset(struct btrfs_disk_key *disk,
					     u64 val)
{
	disk->offset = cpu_to_le64(val);
}

static inline u32 btrfs_disk_key_flags(struct btrfs_disk_key *disk)
{
	return le32_to_cpu(disk->flags);
}

static inline void btrfs_set_disk_key_flags(struct btrfs_disk_key *disk,
					    u32 val)
{
	disk->flags = cpu_to_le32(val);
}

static inline u32 btrfs_key_type(struct btrfs_key *key)
{
	return key->flags & BTRFS_KEY_TYPE_MASK;
}

static inline u32 btrfs_disk_key_type(struct btrfs_disk_key *key)
{
	return le32_to_cpu(key->flags) & BTRFS_KEY_TYPE_MASK;
}

static inline void btrfs_set_key_type(struct btrfs_key *key, u32 type)
{
	BUG_ON(type >= BTRFS_KEY_TYPE_MAX);
	key->flags = (key->flags & ~((u64)BTRFS_KEY_TYPE_MASK)) | type;
}

static inline void btrfs_set_disk_key_type(struct btrfs_disk_key *key, u32 type)
{
	u32 flags = btrfs_disk_key_flags(key);
	BUG_ON(type >= BTRFS_KEY_TYPE_MAX);
	flags = (flags & ~((u64)BTRFS_KEY_TYPE_MASK)) | type;
	btrfs_set_disk_key_flags(key, flags);
}

static inline u64 btrfs_header_blocknr(struct btrfs_header *h)
{
	return le64_to_cpu(h->blocknr);
}

static inline void btrfs_set_header_blocknr(struct btrfs_header *h, u64 blocknr)
{
	h->blocknr = cpu_to_le64(blocknr);
}

static inline u64 btrfs_header_parentid(struct btrfs_header *h)
{
	return le64_to_cpu(h->parentid);
}

static inline void btrfs_set_header_parentid(struct btrfs_header *h,
					     u64 parentid)
{
	h->parentid = cpu_to_le64(parentid);
}

static inline u16 btrfs_header_nritems(struct btrfs_header *h)
{
	return le16_to_cpu(h->nritems);
}

static inline void btrfs_set_header_nritems(struct btrfs_header *h, u16 val)
{
	h->nritems = cpu_to_le16(val);
}

static inline u16 btrfs_header_flags(struct btrfs_header *h)
{
	return le16_to_cpu(h->flags);
}

static inline void btrfs_set_header_flags(struct btrfs_header *h, u16 val)
{
	h->flags = cpu_to_le16(val);
}

static inline int btrfs_header_level(struct btrfs_header *h)
{
	return btrfs_header_flags(h) & (BTRFS_MAX_LEVEL - 1);
}

static inline void btrfs_set_header_level(struct btrfs_header *h, int level)
{
	u16 flags;
	BUG_ON(level > BTRFS_MAX_LEVEL);
	flags = btrfs_header_flags(h) & ~(BTRFS_MAX_LEVEL - 1);
	btrfs_set_header_flags(h, flags | level);
}

static inline int btrfs_is_leaf(struct btrfs_node *n)
{
	return (btrfs_header_level(&n->header) == 0);
}

static inline u64 btrfs_root_blocknr(struct btrfs_root_item *item)
{
	return le64_to_cpu(item->blocknr);
}

static inline void btrfs_set_root_blocknr(struct btrfs_root_item *item, u64 val)
{
	item->blocknr = cpu_to_le64(val);
}

static inline u32 btrfs_root_refs(struct btrfs_root_item *item)
{
	return le32_to_cpu(item->refs);
}

static inline void btrfs_set_root_refs(struct btrfs_root_item *item, u32 val)
{
	item->refs = cpu_to_le32(val);
}

static inline u64 btrfs_super_blocknr(struct btrfs_super_block *s)
{
	return le64_to_cpu(s->blocknr);
}

static inline void btrfs_set_super_blocknr(struct btrfs_super_block *s, u64 val)
{
	s->blocknr = cpu_to_le64(val);
}

static inline u64 btrfs_super_root(struct btrfs_super_block *s)
{
	return le64_to_cpu(s->root);
}

static inline void btrfs_set_super_root(struct btrfs_super_block *s, u64 val)
{
	s->root = cpu_to_le64(val);
}

static inline u64 btrfs_super_total_blocks(struct btrfs_super_block *s)
{
	return le64_to_cpu(s->total_blocks);
}

static inline void btrfs_set_super_total_blocks(struct btrfs_super_block *s,
						u64 val)
{
	s->total_blocks = cpu_to_le64(val);
}

static inline u64 btrfs_super_blocks_used(struct btrfs_super_block *s)
{
	return le64_to_cpu(s->blocks_used);
}

static inline void btrfs_set_super_blocks_used(struct btrfs_super_block *s,
						u64 val)
{
	s->blocks_used = cpu_to_le64(val);
}

static inline u32 btrfs_super_blocksize(struct btrfs_super_block *s)
{
	return le32_to_cpu(s->blocksize);
}

static inline void btrfs_set_super_blocksize(struct btrfs_super_block *s,
						u32 val)
{
	s->blocksize = cpu_to_le32(val);
}

static inline u8 *btrfs_leaf_data(struct btrfs_leaf *l)
{
	return (u8 *)l->items;
}
/* helper function to cast into the data area of the leaf. */
#define btrfs_item_ptr(leaf, slot, type) \
	((type *)(btrfs_leaf_data(leaf) + \
	btrfs_item_offset((leaf)->items + (slot))))

struct btrfs_buffer *btrfs_alloc_free_block(struct btrfs_trans_handle *trans,
					    struct btrfs_root *root);
int btrfs_inc_ref(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		  struct btrfs_buffer *buf);
int btrfs_free_extent(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, u64 blocknr, u64 num_blocks, int pin);
int btrfs_search_slot(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *key, struct btrfs_path *p, int
		      ins_len, int cow);
void btrfs_release_path(struct btrfs_root *root, struct btrfs_path *p);
void btrfs_init_path(struct btrfs_path *p);
int btrfs_del_item(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		   struct btrfs_path *path);
int btrfs_insert_item(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *key, void *data, u32 data_size);
int btrfs_insert_empty_item(struct btrfs_trans_handle *trans, struct btrfs_root
			    *root, struct btrfs_path *path, struct btrfs_key
			    *cpu_key, u32 data_size);
int btrfs_next_leaf(struct btrfs_root *root, struct btrfs_path *path);
int btrfs_leaf_free_space(struct btrfs_root *root, struct btrfs_leaf *leaf);
int btrfs_drop_snapshot(struct btrfs_trans_handle *trans, struct btrfs_root
			*root, struct btrfs_buffer *snap);
int btrfs_finish_extent_commit(struct btrfs_trans_handle *trans, struct
			       btrfs_root *root);
int btrfs_del_root(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		   struct btrfs_key *key);
int btrfs_insert_root(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *key, struct btrfs_root_item
		      *item);
int btrfs_update_root(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *key, struct btrfs_root_item
		      *item);
int btrfs_find_last_root(struct btrfs_root *root, u64 objectid, struct
			 btrfs_root_item *item, struct btrfs_key *key);
int btrfs_insert_dir_item(struct btrfs_trans_handle *trans, struct btrfs_root
			  *root, char *name, int name_len, u64 dir, u64
			  objectid, u8 type);
int btrfs_lookup_dir_item(struct btrfs_trans_handle *trans, struct btrfs_root
			  *root, struct btrfs_path *path, u64 dir, char *name,
			  int name_len, int mod);
int btrfs_match_dir_item_name(struct btrfs_root *root, struct btrfs_path *path,
			      char *name, int name_len);
#endif
