/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_REFLINK_H
#define BTRFS_REFLINK_H

#include <linux/types.h>

struct file;

loff_t btrfs_remap_file_range(struct file *file_in, loff_t pos_in,
			      struct file *file_out, loff_t pos_out,
			      loff_t len, unsigned int remap_flags);

#endif /* BTRFS_REFLINK_H */
