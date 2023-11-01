/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SUPER_H
#define _BCACHEFS_SUPER_H

#include "extents.h"

#include "bcachefs_ioctl.h"

#include <linux/math64.h>

struct bch_fs *bch2_dev_to_fs(dev_t);
struct bch_fs *bch2_uuid_to_fs(__uuid_t);

bool bch2_dev_state_allowed(struct bch_fs *, struct bch_dev *,
			   enum bch_member_state, int);
int __bch2_dev_set_state(struct bch_fs *, struct bch_dev *,
			enum bch_member_state, int);
int bch2_dev_set_state(struct bch_fs *, struct bch_dev *,
		      enum bch_member_state, int);

int bch2_dev_fail(struct bch_dev *, int);
int bch2_dev_remove(struct bch_fs *, struct bch_dev *, int);
int bch2_dev_add(struct bch_fs *, const char *);
int bch2_dev_online(struct bch_fs *, const char *);
int bch2_dev_offline(struct bch_fs *, struct bch_dev *, int);
int bch2_dev_resize(struct bch_fs *, struct bch_dev *, u64);
struct bch_dev *bch2_dev_lookup(struct bch_fs *, const char *);

bool bch2_fs_emergency_read_only(struct bch_fs *);
void bch2_fs_read_only(struct bch_fs *);

int bch2_fs_read_write(struct bch_fs *);
int bch2_fs_read_write_early(struct bch_fs *);

/*
 * Only for use in the recovery/fsck path:
 */
static inline void bch2_fs_lazy_rw(struct bch_fs *c)
{
	if (!test_bit(BCH_FS_RW, &c->flags) &&
	    !test_bit(BCH_FS_WAS_RW, &c->flags))
		bch2_fs_read_write_early(c);
}

void __bch2_fs_stop(struct bch_fs *);
void bch2_fs_free(struct bch_fs *);
void bch2_fs_stop(struct bch_fs *);

int bch2_fs_start(struct bch_fs *);
struct bch_fs *bch2_fs_open(char * const *, unsigned, struct bch_opts);

#endif /* _BCACHEFS_SUPER_H */
