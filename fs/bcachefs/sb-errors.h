/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SB_ERRORS_H
#define _BCACHEFS_SB_ERRORS_H

#include "sb-errors_types.h"

#define BCH_SB_ERRS()

enum bch_sb_error_id {
#define x(t, n) BCH_FSCK_ERR_##t = n,
	BCH_SB_ERRS()
#undef x
	BCH_SB_ERR_MAX
};

extern const struct bch_sb_field_ops bch_sb_field_ops_errors;

void bch2_sb_error_count(struct bch_fs *, enum bch_sb_error_id);

void bch2_sb_errors_from_cpu(struct bch_fs *);

void bch2_fs_sb_errors_exit(struct bch_fs *);
void bch2_fs_sb_errors_init_early(struct bch_fs *);
int bch2_fs_sb_errors_init(struct bch_fs *);

#endif /* _BCACHEFS_SB_ERRORS_H */
