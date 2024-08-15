/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020 Google LLC
 */

#ifndef _INCFS_PSEUDO_FILES_H
#define _INCFS_PSEUDO_FILES_H

#include "internal.h"

#define PSEUDO_FILE_COUNT 3
#define INCFS_START_INO_RANGE 10

extern const struct mem_range incfs_pseudo_file_names[PSEUDO_FILE_COUNT];
extern const unsigned long incfs_pseudo_file_inodes[PSEUDO_FILE_COUNT];

int dir_lookup_pseudo_files(struct super_block *sb, struct dentry *dentry);
int emit_pseudo_files(struct dir_context *ctx);

#endif
