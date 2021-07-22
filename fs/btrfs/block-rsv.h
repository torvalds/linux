/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_BLOCK_RSV_H
#define BTRFS_BLOCK_RSV_H

struct btrfs_trans_handle;
enum btrfs_reserve_flush_enum;

/*
 * Types of block reserves
 */
enum {
	BTRFS_BLOCK_RSV_GLOBAL,
	BTRFS_BLOCK_RSV_DELALLOC,
	BTRFS_BLOCK_RSV_TRANS,
	BTRFS_BLOCK_RSV_CHUNK,
	BTRFS_BLOCK_RSV_DELOPS,
	BTRFS_BLOCK_RSV_DELREFS,
	BTRFS_BLOCK_RSV_EMPTY,
	BTRFS_BLOCK_RSV_TEMP,
};

struct btrfs_block_rsv {
	u64 size;
	u64 reserved;
	struct btrfs_space_info *space_info;
	spinlock_t lock;
	unsigned short full;
	unsigned short type;
	unsigned short failfast;

	/*
	 * Qgroup equivalent for @size @reserved
	 *
	 * Unlike normal @size/@reserved for inode rsv, qgroup doesn't care
	 * about things like csum size nor how many tree blocks it will need to
	 * reserve.
	 *
	 * Qgroup cares more about net change of the extent usage.
	 *
	 * So for one newly inserted file extent, in worst case it will cause
	 * leaf split and level increase, nodesize for each file extent is
	 * already too much.
	 *
	 * In short, qgroup_size/reserved is the upper limit of possible needed
	 * qgroup metadata reservation.
	 */
	u64 qgroup_rsv_size;
	u64 qgroup_rsv_reserved;
};

void btrfs_init_block_rsv(struct btrfs_block_rsv *rsv, unsigned short type);
struct btrfs_block_rsv *btrfs_alloc_block_rsv(struct btrfs_fs_info *fs_info,
					      unsigned short type);
void btrfs_init_metadata_block_rsv(struct btrfs_fs_info *fs_info,
				   struct btrfs_block_rsv *rsv,
				   unsigned short type);
void btrfs_free_block_rsv(struct btrfs_fs_info *fs_info,
			  struct btrfs_block_rsv *rsv);
int btrfs_block_rsv_add(struct btrfs_root *root,
			struct btrfs_block_rsv *block_rsv, u64 num_bytes,
			enum btrfs_reserve_flush_enum flush);
int btrfs_block_rsv_check(struct btrfs_block_rsv *block_rsv, int min_factor);
int btrfs_block_rsv_refill(struct btrfs_root *root,
			   struct btrfs_block_rsv *block_rsv, u64 min_reserved,
			   enum btrfs_reserve_flush_enum flush);
int btrfs_block_rsv_migrate(struct btrfs_block_rsv *src_rsv,
			    struct btrfs_block_rsv *dst_rsv, u64 num_bytes,
			    bool update_size);
int btrfs_block_rsv_use_bytes(struct btrfs_block_rsv *block_rsv, u64 num_bytes);
int btrfs_cond_migrate_bytes(struct btrfs_fs_info *fs_info,
			     struct btrfs_block_rsv *dest, u64 num_bytes,
			     int min_factor);
void btrfs_block_rsv_add_bytes(struct btrfs_block_rsv *block_rsv,
			       u64 num_bytes, bool update_size);
u64 btrfs_block_rsv_release(struct btrfs_fs_info *fs_info,
			      struct btrfs_block_rsv *block_rsv,
			      u64 num_bytes, u64 *qgroup_to_release);
void btrfs_update_global_block_rsv(struct btrfs_fs_info *fs_info);
void btrfs_init_global_block_rsv(struct btrfs_fs_info *fs_info);
void btrfs_release_global_block_rsv(struct btrfs_fs_info *fs_info);
struct btrfs_block_rsv *btrfs_use_block_rsv(struct btrfs_trans_handle *trans,
					    struct btrfs_root *root,
					    u32 blocksize);
static inline void btrfs_unuse_block_rsv(struct btrfs_fs_info *fs_info,
					 struct btrfs_block_rsv *block_rsv,
					 u32 blocksize)
{
	btrfs_block_rsv_add_bytes(block_rsv, blocksize, false);
	btrfs_block_rsv_release(fs_info, block_rsv, 0, NULL);
}

#endif /* BTRFS_BLOCK_RSV_H */
