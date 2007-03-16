#ifndef __TRANSACTION__
#define __TRANSACTION__

struct btrfs_trans_handle {
	u64 transid;
	unsigned long blocks_reserved;
	unsigned long blocks_used;
};

static inline struct btrfs_trans_handle *
btrfs_start_transaction(struct btrfs_root *root, int num_blocks)
{
	struct btrfs_trans_handle *h = malloc(sizeof(*h));
	h->transid = root->root_key.offset;
	h->blocks_reserved = num_blocks;
	h->blocks_used = 0;
	return h;
}

static inline void btrfs_free_transaction(struct btrfs_root *root,
					  struct btrfs_trans_handle *handle)
{
	memset(handle, 0, sizeof(*handle));
	free(handle);
}

#endif
