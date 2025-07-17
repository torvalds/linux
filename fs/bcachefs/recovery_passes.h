#ifndef _BCACHEFS_RECOVERY_PASSES_H
#define _BCACHEFS_RECOVERY_PASSES_H

extern const char * const bch2_recovery_passes[];

extern const struct bch_sb_field_ops bch_sb_field_ops_recovery_passes;

u64 bch2_recovery_passes_to_stable(u64 v);
u64 bch2_recovery_passes_from_stable(u64 v);

u64 bch2_fsck_recovery_passes(void);

void bch2_recovery_pass_set_no_ratelimit(struct bch_fs *, enum bch_recovery_pass);

enum bch_run_recovery_pass_flags {
	RUN_RECOVERY_PASS_nopersistent	= BIT(0),
	RUN_RECOVERY_PASS_ratelimit	= BIT(1),
};

static inline bool go_rw_in_recovery(struct bch_fs *c)
{
	return (c->journal_keys.nr ||
		!c->opts.read_only ||
		!c->sb.clean ||
		c->opts.recovery_passes ||
		(c->opts.fsck && !(c->sb.features & BIT_ULL(BCH_FEATURE_no_alloc_info))));
}

int bch2_run_print_explicit_recovery_pass(struct bch_fs *, enum bch_recovery_pass);

int __bch2_run_explicit_recovery_pass(struct bch_fs *, struct printbuf *,
				      enum bch_recovery_pass,
				      enum bch_run_recovery_pass_flags);
int bch2_run_explicit_recovery_pass(struct bch_fs *, struct printbuf *,
				    enum bch_recovery_pass,
				    enum bch_run_recovery_pass_flags);

int bch2_require_recovery_pass(struct bch_fs *, struct printbuf *,
			       enum bch_recovery_pass);

int bch2_run_online_recovery_passes(struct bch_fs *, u64);
int bch2_run_recovery_passes(struct bch_fs *, enum bch_recovery_pass);

void bch2_recovery_pass_status_to_text(struct printbuf *, struct bch_fs *);

void bch2_fs_recovery_passes_init(struct bch_fs *);

#endif /* _BCACHEFS_RECOVERY_PASSES_H */
