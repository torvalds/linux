/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_RECOVERY_PASSES_TYPES_H
#define _BCACHEFS_RECOVERY_PASSES_TYPES_H

struct bch_fs_recovery {
	/*
	 * Two different uses:
	 * "Has this fsck pass?" - i.e. should this type of error be an
	 * emergency read-only
	 * And, in certain situations fsck will rewind to an earlier pass: used
	 * for signaling to the toplevel code which pass we want to run now.
	 */
	enum bch_recovery_pass	curr_pass;
	enum bch_recovery_pass	next_pass;
	/* never rewinds version of curr_pass */
	enum bch_recovery_pass	pass_done;
	u64			passes_to_run;
	/* bitmask of recovery passes that we actually ran */
	u64			passes_complete;
	u64			passes_failing;
	u64			passes_ratelimiting;
	spinlock_t		lock;
	struct semaphore	run_lock;
	struct work_struct	work;
};

#endif /* _BCACHEFS_RECOVERY_PASSES_TYPES_H */
