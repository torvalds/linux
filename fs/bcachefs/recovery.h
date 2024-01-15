/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_RECOVERY_H
#define _BCACHEFS_RECOVERY_H

extern const char * const bch2_recovery_passes[];

/*
 * For when we need to rewind recovery passes and run a pass we skipped:
 */
static inline int bch2_run_explicit_recovery_pass(struct bch_fs *c,
						  enum bch_recovery_pass pass)
{
	bch_info(c, "running explicit recovery pass %s (%u), currently at %s (%u)",
		 bch2_recovery_passes[pass], pass,
		 bch2_recovery_passes[c->curr_recovery_pass], c->curr_recovery_pass);

	c->recovery_passes_explicit |= BIT_ULL(pass);

	if (c->curr_recovery_pass >= pass) {
		c->curr_recovery_pass = pass;
		c->recovery_passes_complete &= (1ULL << pass) >> 1;
		return -BCH_ERR_restart_recovery;
	} else {
		return 0;
	}
}

u64 bch2_fsck_recovery_passes(void);

int bch2_fs_recovery(struct bch_fs *);
int bch2_fs_initialize(struct bch_fs *);

#endif /* _BCACHEFS_RECOVERY_H */
