/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_ROOT_TREE_H
#define BTRFS_ROOT_TREE_H

int btrfs_subvolume_reserve_metadata(struct btrfs_root *root,
				     struct btrfs_block_rsv *rsv,
				     int nitems, bool use_global_rsv);
void btrfs_subvolume_release_metadata(struct btrfs_root *root,
				      struct btrfs_block_rsv *rsv);
int btrfs_add_root_ref(struct btrfs_trans_handle *trans, u64 root_id,
		       u64 ref_id, u64 dirid, u64 sequence,
		       const struct fscrypt_str *name);
int btrfs_del_root_ref(struct btrfs_trans_handle *trans, u64 root_id,
		       u64 ref_id, u64 dirid, u64 *sequence,
		       const struct fscrypt_str *name);
int btrfs_del_root(struct btrfs_trans_handle *trans, const struct btrfs_key *key);
int btrfs_insert_root(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		      const struct btrfs_key *key,
		      struct btrfs_root_item *item);
int __must_check btrfs_update_root(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct btrfs_key *key,
				   struct btrfs_root_item *item);
int btrfs_find_root(struct btrfs_root *root, const struct btrfs_key *search_key,
		    struct btrfs_path *path, struct btrfs_root_item *root_item,
		    struct btrfs_key *root_key);
int btrfs_find_orphan_roots(struct btrfs_fs_info *fs_info);
void btrfs_set_root_node(struct btrfs_root_item *item,
			 struct extent_buffer *node);
void btrfs_check_and_init_root_item(struct btrfs_root_item *item);
void btrfs_update_root_times(struct btrfs_trans_handle *trans, struct btrfs_root *root);

#endif
