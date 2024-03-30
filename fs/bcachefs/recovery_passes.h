#ifndef _BCACHEFS_RECOVERY_PASSES_H
#define _BCACHEFS_RECOVERY_PASSES_H

extern const char * const bch2_recovery_passes[];

u64 bch2_recovery_passes_to_stable(u64 v);
u64 bch2_recovery_passes_from_stable(u64 v);

u64 bch2_fsck_recovery_passes(void);

int bch2_run_explicit_recovery_pass(struct bch_fs *, enum bch_recovery_pass);
int bch2_run_explicit_recovery_pass_persistent(struct bch_fs *, enum bch_recovery_pass);

int bch2_run_online_recovery_passes(struct bch_fs *);
int bch2_run_recovery_passes(struct bch_fs *);

#endif /* _BCACHEFS_RECOVERY_PASSES_H */
