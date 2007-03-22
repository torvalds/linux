#ifndef __TRANSACTION__
#define __TRANSACTION__

struct btrfs_transaction {
	u64 transid;
	unsigned long num_writers;
	int in_commit;
	int use_count;
	int commit_done;
	wait_queue_head_t writer_wait;
	wait_queue_head_t commit_wait;
};

struct btrfs_trans_handle {
	u64 transid;
	unsigned long blocks_reserved;
	unsigned long blocks_used;
	struct btrfs_transaction *transaction;
};


int btrfs_end_transaction(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root);
struct btrfs_trans_handle *btrfs_start_transaction(struct btrfs_root *root,
						   int num_blocks);
int btrfs_write_and_wait_transaction(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root);
int btrfs_commit_tree_roots(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root);
#endif
