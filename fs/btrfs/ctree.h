#ifndef __CTREE__
#define __CTREE__

#define CTREE_BLOCKSIZE 256

struct key {
	u64 objectid;
	u32 flags;
	u64 offset;
} __attribute__ ((__packed__));

struct header {
	u64 fsid[2]; /* FS specific uuid */
	u64 blocknr;
	u64 parentid;
	u32 csum;
	u32 ham;
	u16 nritems;
	u16 flags;
} __attribute__ ((__packed__));

#define NODEPTRS_PER_BLOCK ((CTREE_BLOCKSIZE - sizeof(struct header)) / \
			    (sizeof(struct key) + sizeof(u64)))

#define MAX_LEVEL 8
#define node_level(f) ((f) & (MAX_LEVEL-1))
#define is_leaf(f) (node_level(f) == 0)

struct tree_buffer;

struct alloc_extent {
	u64 blocknr;
	u64 num_blocks;
	u64 num_used;
} __attribute__ ((__packed__));

struct ctree_root {
	struct tree_buffer *node;
	struct ctree_root *extent_root;
	struct alloc_extent *alloc_extent;
	struct alloc_extent *reserve_extent;
	int fp;
	struct radix_tree_root cache_radix;
	struct alloc_extent ai1;
	struct alloc_extent ai2;
};

struct ctree_root_info {
	u64 fsid[2]; /* FS specific uuid */
	u64 blocknr; /* blocknr of this block */
	u64 objectid; /* inode number of this root */
	u64 tree_root; /* the tree root */
	u32 csum;
	u32 ham;
	struct alloc_extent alloc_extent;
	struct alloc_extent reserve_extent;
	u64 snapuuid[2]; /* root specific uuid */
} __attribute__ ((__packed__));

struct ctree_super_block {
	struct ctree_root_info root_info;
	struct ctree_root_info extent_info;
} __attribute__ ((__packed__));

struct item {
	struct key key;
	u16 offset;
	u16 size;
} __attribute__ ((__packed__));

#define LEAF_DATA_SIZE (CTREE_BLOCKSIZE - sizeof(struct header))
struct leaf {
	struct header header;
	union {
		struct item items[LEAF_DATA_SIZE/sizeof(struct item)];
		u8 data[CTREE_BLOCKSIZE-sizeof(struct header)];
	};
} __attribute__ ((__packed__));

struct node {
	struct header header;
	struct key keys[NODEPTRS_PER_BLOCK];
	u64 blockptrs[NODEPTRS_PER_BLOCK];
} __attribute__ ((__packed__));

struct extent_item {
	u32 refs;
	u64 owner;
} __attribute__ ((__packed__));

struct ctree_path {
	struct tree_buffer *nodes[MAX_LEVEL];
	int slots[MAX_LEVEL];
};
#endif
