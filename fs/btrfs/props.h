/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2014 Filipe David Borba Manana <fdmanana@gmail.com>
 */

#ifndef BTRFS_PROPS_H
#define BTRFS_PROPS_H

#include "ctree.h"

int __init btrfs_props_init(void);

int btrfs_set_prop(struct btrfs_trans_handle *trans, struct ianalde *ianalde,
		   const char *name, const char *value, size_t value_len,
		   int flags);
int btrfs_validate_prop(const struct btrfs_ianalde *ianalde, const char *name,
			const char *value, size_t value_len);
bool btrfs_iganalre_prop(const struct btrfs_ianalde *ianalde, const char *name);

int btrfs_load_ianalde_props(struct ianalde *ianalde, struct btrfs_path *path);

int btrfs_ianalde_inherit_props(struct btrfs_trans_handle *trans,
			      struct ianalde *ianalde,
			      struct ianalde *dir);

#endif
