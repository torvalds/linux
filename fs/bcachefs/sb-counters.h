/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SB_COUNTERS_H
#define _BCACHEFS_SB_COUNTERS_H

#include "bcachefs.h"
#include "super-io.h"

int bch2_sb_counters_to_cpu(struct bch_fs *);
int bch2_sb_counters_from_cpu(struct bch_fs *);

void bch2_fs_counters_exit(struct bch_fs *);
int bch2_fs_counters_init(struct bch_fs *);

extern const char * const bch2_counter_names[];
extern const struct bch_sb_field_ops bch_sb_field_ops_counters;

long bch2_ioctl_query_counters(struct bch_fs *,
			struct bch_ioctl_query_counters __user *);

#endif // _BCACHEFS_SB_COUNTERS_H
