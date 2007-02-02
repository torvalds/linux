#ifndef __CTREE__
#define __CTREE__

#define CTREE_BLOCKSIZE 4096

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

#define LEVEL_BITS 3
#define MAX_LEVEL (1 << LEVEL_BITS)
#define node_level(f) ((f) & (MAX_LEVEL-1))
#define is_leaf(f) (node_level(f) == 0)

struct tree_buffer;
struct ctree_root {
	struct tree_buffer *node;
	int fp;
	struct radix_tree_root cache_radix;
};

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

struct ctree_path {
	struct tree_buffer *nodes[MAX_LEVEL];
	int slots[MAX_LEVEL];
};
#endif
