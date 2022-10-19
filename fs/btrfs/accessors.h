/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_ACCESSORS_H
#define BTRFS_ACCESSORS_H

struct btrfs_map_token {
	struct extent_buffer *eb;
	char *kaddr;
	unsigned long offset;
};

void btrfs_init_map_token(struct btrfs_map_token *token, struct extent_buffer *eb);

#endif
