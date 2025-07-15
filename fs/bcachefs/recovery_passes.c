// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "alloc_background.h"
#include "backpointers.h"
#include "btree_gc.h"
#include "btree_node_scan.h"
#include "disk_accounting.h"
#include "ec.h"
#include "fsck.h"
#include "inode.h"
#include "journal.h"
#include "lru.h"
#include "logged_ops.h"
#include "movinggc.h"
#include "rebalance.h"
#include "recovery.h"
#include "recovery_passes.h"
#include "snapshot.h"
#include "subvolume.h"
#include "super.h"
#include "super-io.h"

const char * const bch2_recovery_passes[] = {
#define x(_fn, ...)	#_fn,
	BCH_RECOVERY_PASSES()
#undef x
	NULL
};

static const u8 passes_to_stable_map[] = {
#define x(n, id, ...)	[BCH_RECOVERY_PASS_##n] = BCH_RECOVERY_PASS_STABLE_##n,
	BCH_RECOVERY_PASSES()
#undef x
};

static const u8 passes_from_stable_map[] = {
#define x(n, id, ...)	[BCH_RECOVERY_PASS_STABLE_##n] = BCH_RECOVERY_PASS_##n,
	BCH_RECOVERY_PASSES()
#undef x
};

static enum bch_recovery_pass_stable bch2_recovery_pass_to_stable(enum bch_recovery_pass pass)
{
	return passes_to_stable_map[pass];
}

u64 bch2_recovery_passes_to_stable(u64 v)
{
	u64 ret = 0;
	for (unsigned i = 0; i < ARRAY_SIZE(passes_to_stable_map); i++)
		if (v & BIT_ULL(i))
			ret |= BIT_ULL(passes_to_stable_map[i]);
	return ret;
}

static enum bch_recovery_pass bch2_recovery_pass_from_stable(enum bch_recovery_pass_stable pass)
{
	return pass < ARRAY_SIZE(passes_from_stable_map)
		? passes_from_stable_map[pass]
		: 0;
}

u64 bch2_recovery_passes_from_stable(u64 v)
{
	u64 ret = 0;
	for (unsigned i = 0; i < ARRAY_SIZE(passes_from_stable_map); i++)
		if (v & BIT_ULL(i))
			ret |= BIT_ULL(passes_from_stable_map[i]);
	return ret;
}

static int bch2_sb_recovery_passes_validate(struct bch_sb *sb, struct bch_sb_field *f,
					    enum bch_validate_flags flags, struct printbuf *err)
{
	return 0;
}

static void bch2_sb_recovery_passes_to_text(struct printbuf *out,
					    struct bch_sb *sb,
					    struct bch_sb_field *f)
{
	struct bch_sb_field_recovery_passes *r =
		field_to_type(f, recovery_passes);
	unsigned nr = recovery_passes_nr_entries(r);

	if (out->nr_tabstops < 1)
		printbuf_tabstop_push(out, 32);
	if (out->nr_tabstops < 2)
		printbuf_tabstop_push(out, 16);

	prt_printf(out, "Pass\tLast run\tLast runtime\n");

	for (struct recovery_pass_entry *i = r->start; i < r->start + nr; i++) {
		if (!i->last_run)
			continue;

		unsigned idx = i - r->start;

		prt_printf(out, "%s\t", bch2_recovery_passes[bch2_recovery_pass_from_stable(idx)]);

		bch2_prt_datetime(out, le64_to_cpu(i->last_run));
		prt_tab(out);

		bch2_pr_time_units(out, le32_to_cpu(i->last_runtime) * NSEC_PER_SEC);

		if (BCH_RECOVERY_PASS_NO_RATELIMIT(i))
			prt_str(out, " (no ratelimit)");

		prt_newline(out);
	}
}

static struct recovery_pass_entry *bch2_sb_recovery_pass_entry(struct bch_fs *c,
							       enum bch_recovery_pass pass)
{
	enum bch_recovery_pass_stable stable = bch2_recovery_pass_to_stable(pass);

	lockdep_assert_held(&c->sb_lock);

	struct bch_sb_field_recovery_passes *r =
		bch2_sb_field_get(c->disk_sb.sb, recovery_passes);

	if (stable >= recovery_passes_nr_entries(r)) {
		unsigned u64s = struct_size(r, start, stable + 1) / sizeof(u64);

		r = bch2_sb_field_resize(&c->disk_sb, recovery_passes, u64s);
		if (!r) {
			bch_err(c, "error creating recovery_passes sb section");
			return NULL;
		}
	}

	return r->start + stable;
}

static void bch2_sb_recovery_pass_complete(struct bch_fs *c,
					   enum bch_recovery_pass pass,
					   s64 start_time)
{
	guard(mutex)(&c->sb_lock);
	struct bch_sb_field_ext *ext = bch2_sb_field_get(c->disk_sb.sb, ext);
	__clear_bit_le64(bch2_recovery_pass_to_stable(pass),
			 ext->recovery_passes_required);

	struct recovery_pass_entry *e = bch2_sb_recovery_pass_entry(c, pass);
	if (e) {
		s64 end_time	= ktime_get_real_seconds();
		e->last_run	= cpu_to_le64(end_time);
		e->last_runtime	= cpu_to_le32(max(0, end_time - start_time));
		SET_BCH_RECOVERY_PASS_NO_RATELIMIT(e, false);
	}

	bch2_write_super(c);
}

void bch2_recovery_pass_set_no_ratelimit(struct bch_fs *c,
					 enum bch_recovery_pass pass)
{
	guard(mutex)(&c->sb_lock);

	struct recovery_pass_entry *e = bch2_sb_recovery_pass_entry(c, pass);
	if (e && !BCH_RECOVERY_PASS_NO_RATELIMIT(e)) {
		SET_BCH_RECOVERY_PASS_NO_RATELIMIT(e, false);
		bch2_write_super(c);
	}
}

static bool bch2_recovery_pass_want_ratelimit(struct bch_fs *c, enum bch_recovery_pass pass)
{
	enum bch_recovery_pass_stable stable = bch2_recovery_pass_to_stable(pass);
	bool ret = false;

	lockdep_assert_held(&c->sb_lock);

	struct bch_sb_field_recovery_passes *r =
		bch2_sb_field_get(c->disk_sb.sb, recovery_passes);

	if (stable < recovery_passes_nr_entries(r)) {
		struct recovery_pass_entry *i = r->start + stable;

		/*
		 * Ratelimit if the last runtime was more than 1% of the time
		 * since we last ran
		 */
		ret = (u64) le32_to_cpu(i->last_runtime) * 100 >
			ktime_get_real_seconds() - le64_to_cpu(i->last_run);

		if (BCH_RECOVERY_PASS_NO_RATELIMIT(i))
			ret = false;
	}

	return ret;
}

const struct bch_sb_field_ops bch_sb_field_ops_recovery_passes = {
	.validate	= bch2_sb_recovery_passes_validate,
	.to_text	= bch2_sb_recovery_passes_to_text
};

/* Fake recovery pass, so that scan_for_btree_nodes isn't 0: */
static int bch2_recovery_pass_empty(struct bch_fs *c)
{
	return 0;
}

static int bch2_set_may_go_rw(struct bch_fs *c)
{
	struct journal_keys *keys = &c->journal_keys;

	/*
	 * After we go RW, the journal keys buffer can't be modified (except for
	 * setting journal_key->overwritten: it will be accessed by multiple
	 * threads
	 */
	move_gap(keys, keys->nr);

	set_bit(BCH_FS_may_go_rw, &c->flags);

	if (keys->nr ||
	    !c->opts.read_only ||
	    !c->sb.clean ||
	    c->opts.recovery_passes ||
	    (c->opts.fsck && !(c->sb.features & BIT_ULL(BCH_FEATURE_no_alloc_info)))) {
		if (c->sb.features & BIT_ULL(BCH_FEATURE_no_alloc_info)) {
			bch_info(c, "mounting a filesystem with no alloc info read-write; will recreate");
			bch2_reconstruct_alloc(c);
		}

		return bch2_fs_read_write_early(c);
	}
	return 0;
}

/*
 * Make sure root inode is readable while we're still in recovery and can rewind
 * for repair:
 */
static int bch2_lookup_root_inode(struct bch_fs *c)
{
	subvol_inum inum = BCACHEFS_ROOT_SUBVOL_INUM;
	struct bch_inode_unpacked inode_u;
	struct bch_subvolume subvol;

	return bch2_trans_do(c,
		bch2_subvolume_get(trans, inum.subvol, true, &subvol) ?:
		bch2_inode_find_by_inum_trans(trans, inum, &inode_u));
}

struct recovery_pass_fn {
	int		(*fn)(struct bch_fs *);
	unsigned	when;
};

static struct recovery_pass_fn recovery_pass_fns[] = {
#define x(_fn, _id, _when)	{ .fn = bch2_##_fn, .when = _when },
	BCH_RECOVERY_PASSES()
#undef x
};

static u64 bch2_recovery_passes_match(unsigned flags)
{
	u64 ret = 0;

	for (unsigned i = 0; i < ARRAY_SIZE(recovery_pass_fns); i++)
		if (recovery_pass_fns[i].when & flags)
			ret |= BIT_ULL(i);
	return ret;
}

u64 bch2_fsck_recovery_passes(void)
{
	return bch2_recovery_passes_match(PASS_FSCK);
}

static void bch2_run_async_recovery_passes(struct bch_fs *c)
{
	if (!down_trylock(&c->recovery.run_lock))
		return;

	if (!enumerated_ref_tryget(&c->writes, BCH_WRITE_REF_async_recovery_passes))
		goto unlock;

	if (queue_work(system_long_wq, &c->recovery.work))
		return;

	enumerated_ref_put(&c->writes, BCH_WRITE_REF_async_recovery_passes);
unlock:
	up(&c->recovery.run_lock);
}

static bool recovery_pass_needs_set(struct bch_fs *c,
				    enum bch_recovery_pass pass,
				    enum bch_run_recovery_pass_flags *flags)
{
	struct bch_fs_recovery *r = &c->recovery;

	/*
	 * Never run scan_for_btree_nodes persistently: check_topology will run
	 * it if required
	 */
	if (pass == BCH_RECOVERY_PASS_scan_for_btree_nodes)
		*flags |= RUN_RECOVERY_PASS_nopersistent;

	if ((*flags & RUN_RECOVERY_PASS_ratelimit) &&
	    !bch2_recovery_pass_want_ratelimit(c, pass))
		*flags &= ~RUN_RECOVERY_PASS_ratelimit;

	/*
	 * If RUN_RECOVERY_PASS_nopersistent is set, we don't want to do
	 * anything if the pass has already run: these mean we need a prior pass
	 * to run before we continue to repair, we don't expect that pass to fix
	 * the damage we encountered.
	 *
	 * Otherwise, we run run_explicit_recovery_pass when we find damage, so
	 * it should run again even if it's already run:
	 */
	bool in_recovery = test_bit(BCH_FS_in_recovery, &c->flags);
	bool persistent = !in_recovery || !(*flags & RUN_RECOVERY_PASS_nopersistent);

	if (persistent
	    ? !(c->sb.recovery_passes_required & BIT_ULL(pass))
	    : !((r->passes_to_run|r->passes_complete) & BIT_ULL(pass)))
		return true;

	if (!(*flags & RUN_RECOVERY_PASS_ratelimit) &&
	    (r->passes_ratelimiting & BIT_ULL(pass)))
		return true;

	return false;
}

/*
 * For when we need to rewind recovery passes and run a pass we skipped:
 */
int __bch2_run_explicit_recovery_pass(struct bch_fs *c,
				      struct printbuf *out,
				      enum bch_recovery_pass pass,
				      enum bch_run_recovery_pass_flags flags)
{
	struct bch_fs_recovery *r = &c->recovery;
	int ret = 0;


	lockdep_assert_held(&c->sb_lock);

	bch2_printbuf_make_room(out, 1024);
	out->atomic++;

	unsigned long lockflags;
	spin_lock_irqsave(&r->lock, lockflags);

	if (!recovery_pass_needs_set(c, pass, &flags))
		goto out;

	bool in_recovery = test_bit(BCH_FS_in_recovery, &c->flags);
	bool rewind = in_recovery &&
		r->curr_pass > pass &&
		!(r->passes_complete & BIT_ULL(pass));
	bool ratelimit = flags & RUN_RECOVERY_PASS_ratelimit;

	if (!(in_recovery && (flags & RUN_RECOVERY_PASS_nopersistent))) {
		struct bch_sb_field_ext *ext = bch2_sb_field_get(c->disk_sb.sb, ext);
		__set_bit_le64(bch2_recovery_pass_to_stable(pass), ext->recovery_passes_required);
	}

	if (pass < BCH_RECOVERY_PASS_set_may_go_rw &&
	    (!in_recovery || r->curr_pass >= BCH_RECOVERY_PASS_set_may_go_rw)) {
		prt_printf(out, "need recovery pass %s (%u), but already rw\n",
			   bch2_recovery_passes[pass], pass);
		ret = bch_err_throw(c, cannot_rewind_recovery);
		goto out;
	}

	if (ratelimit)
		r->passes_ratelimiting |= BIT_ULL(pass);
	else
		r->passes_ratelimiting &= ~BIT_ULL(pass);

	if (in_recovery && !ratelimit) {
		prt_printf(out, "running recovery pass %s (%u), currently at %s (%u)%s\n",
			   bch2_recovery_passes[pass], pass,
			   bch2_recovery_passes[r->curr_pass], r->curr_pass,
			   rewind ? " - rewinding" : "");

		r->passes_to_run |= BIT_ULL(pass);

		if (rewind) {
			r->next_pass = pass;
			r->passes_complete &= (1ULL << pass) >> 1;
			ret = bch_err_throw(c, restart_recovery);
		}
	} else {
		prt_printf(out, "scheduling recovery pass %s (%u)%s\n",
			   bch2_recovery_passes[pass], pass,
			   ratelimit ? " - ratelimiting" : "");

		struct recovery_pass_fn *p = recovery_pass_fns + pass;
		if (p->when & PASS_ONLINE)
			bch2_run_async_recovery_passes(c);
	}
out:
	spin_unlock_irqrestore(&r->lock, lockflags);
	--out->atomic;
	return ret;
}

int bch2_run_explicit_recovery_pass(struct bch_fs *c,
				    struct printbuf *out,
				    enum bch_recovery_pass pass,
				    enum bch_run_recovery_pass_flags flags)
{
	int ret = 0;

	scoped_guard(mutex, &c->sb_lock) {
		if (!recovery_pass_needs_set(c, pass, &flags))
			return 0;

		ret = __bch2_run_explicit_recovery_pass(c, out, pass, flags);
		bch2_write_super(c);
	}

	return ret;
}

/*
 * Returns 0 if @pass has run recently, otherwise one of
 * -BCH_ERR_restart_recovery
 * -BCH_ERR_recovery_pass_will_run
 */
int bch2_require_recovery_pass(struct bch_fs *c,
			       struct printbuf *out,
			       enum bch_recovery_pass pass)
{
	if (test_bit(BCH_FS_in_recovery, &c->flags) &&
	    c->recovery.passes_complete & BIT_ULL(pass))
		return 0;

	guard(mutex)(&c->sb_lock);

	if (bch2_recovery_pass_want_ratelimit(c, pass))
		return 0;

	enum bch_run_recovery_pass_flags flags = 0;
	int ret = 0;

	if (recovery_pass_needs_set(c, pass, &flags)) {
		ret = __bch2_run_explicit_recovery_pass(c, out, pass, flags);
		bch2_write_super(c);
	}

	return ret ?: bch_err_throw(c, recovery_pass_will_run);
}

int bch2_run_print_explicit_recovery_pass(struct bch_fs *c, enum bch_recovery_pass pass)
{
	enum bch_run_recovery_pass_flags flags = 0;

	if (!recovery_pass_needs_set(c, pass, &flags))
		return 0;

	struct printbuf buf = PRINTBUF;
	bch2_log_msg_start(c, &buf);

	mutex_lock(&c->sb_lock);
	int ret = __bch2_run_explicit_recovery_pass(c, &buf, pass,
						RUN_RECOVERY_PASS_nopersistent);
	mutex_unlock(&c->sb_lock);

	bch2_print_str(c, KERN_NOTICE, buf.buf);
	printbuf_exit(&buf);
	return ret;
}

static int bch2_run_recovery_pass(struct bch_fs *c, enum bch_recovery_pass pass)
{
	struct bch_fs_recovery *r = &c->recovery;
	struct recovery_pass_fn *p = recovery_pass_fns + pass;

	if (!(p->when & PASS_SILENT))
		bch2_print(c, KERN_INFO bch2_log_msg(c, "%s..."),
			   bch2_recovery_passes[pass]);

	s64 start_time = ktime_get_real_seconds();
	int ret = p->fn(c);

	r->passes_to_run &= ~BIT_ULL(pass);

	if (ret) {
		r->passes_failing |= BIT_ULL(pass);
		return ret;
	}

	r->passes_failing = 0;

	if (!test_bit(BCH_FS_error, &c->flags))
		bch2_sb_recovery_pass_complete(c, pass, start_time);

	if (!(p->when & PASS_SILENT))
		bch2_print(c, KERN_CONT " done\n");

	return 0;
}

static int __bch2_run_recovery_passes(struct bch_fs *c, u64 orig_passes_to_run,
				      bool online)
{
	struct bch_fs_recovery *r = &c->recovery;
	int ret = 0;

	spin_lock_irq(&r->lock);

	if (online)
		orig_passes_to_run &= bch2_recovery_passes_match(PASS_ONLINE);

	if (c->sb.features & BIT_ULL(BCH_FEATURE_no_alloc_info))
		orig_passes_to_run &= ~bch2_recovery_passes_match(PASS_ALLOC);

	/*
	 * A failed recovery pass will be retried after another pass succeeds -
	 * but not this iteration.
	 *
	 * This is because some passes depend on repair done by other passes: we
	 * may want to retry, but we don't want to loop on failing passes.
	 */

	orig_passes_to_run &= ~r->passes_failing;

	r->passes_to_run = orig_passes_to_run;

	while (r->passes_to_run) {
		unsigned prev_done = r->pass_done;
		unsigned pass = __ffs64(r->passes_to_run);
		r->curr_pass = pass;
		r->next_pass = r->curr_pass + 1;
		r->passes_to_run &= ~BIT_ULL(pass);

		spin_unlock_irq(&r->lock);

		int ret2 = bch2_run_recovery_pass(c, pass) ?:
			bch2_journal_flush(&c->journal);

		spin_lock_irq(&r->lock);

		if (r->next_pass < r->curr_pass) {
			/* Rewind: */
			r->passes_to_run |= orig_passes_to_run & (~0ULL << r->next_pass);
		} else if (!ret2) {
			r->pass_done = max(r->pass_done, pass);
			r->passes_complete |= BIT_ULL(pass);
		} else {
			ret = ret2;
		}

		if (ret && !online)
			break;

		if (prev_done <= BCH_RECOVERY_PASS_check_snapshots &&
		    r->pass_done > BCH_RECOVERY_PASS_check_snapshots) {
			bch2_copygc_wakeup(c);
			bch2_rebalance_wakeup(c);
		}
	}

	clear_bit(BCH_FS_in_recovery, &c->flags);
	spin_unlock_irq(&r->lock);

	return ret;
}

static void bch2_async_recovery_passes_work(struct work_struct *work)
{
	struct bch_fs *c = container_of(work, struct bch_fs, recovery.work);
	struct bch_fs_recovery *r = &c->recovery;

	__bch2_run_recovery_passes(c,
		c->sb.recovery_passes_required & ~r->passes_ratelimiting,
		true);

	up(&r->run_lock);
	enumerated_ref_put(&c->writes, BCH_WRITE_REF_async_recovery_passes);
}

int bch2_run_online_recovery_passes(struct bch_fs *c, u64 passes)
{
	return __bch2_run_recovery_passes(c, c->sb.recovery_passes_required|passes, true);
}

int bch2_run_recovery_passes(struct bch_fs *c, enum bch_recovery_pass from)
{
	u64 passes =
		bch2_recovery_passes_match(PASS_ALWAYS) |
		(!c->sb.clean ? bch2_recovery_passes_match(PASS_UNCLEAN) : 0) |
		(c->opts.fsck ? bch2_recovery_passes_match(PASS_FSCK) : 0) |
		c->opts.recovery_passes |
		c->sb.recovery_passes_required;

	if (c->opts.recovery_pass_last)
		passes &= BIT_ULL(c->opts.recovery_pass_last + 1) - 1;

	/*
	 * We can't allow set_may_go_rw to be excluded; that would cause us to
	 * use the journal replay keys for updates where it's not expected.
	 */
	c->opts.recovery_passes_exclude &= ~BCH_RECOVERY_PASS_set_may_go_rw;
	passes &= ~c->opts.recovery_passes_exclude;

	passes &= ~(BIT_ULL(from) - 1);

	down(&c->recovery.run_lock);
	int ret = __bch2_run_recovery_passes(c, passes, false);
	up(&c->recovery.run_lock);

	return ret;
}

static void prt_passes(struct printbuf *out, const char *msg, u64 passes)
{
	prt_printf(out, "%s:\t", msg);
	prt_bitflags(out, bch2_recovery_passes, passes);
	prt_newline(out);
}

void bch2_recovery_pass_status_to_text(struct printbuf *out, struct bch_fs *c)
{
	struct bch_fs_recovery *r = &c->recovery;

	printbuf_tabstop_push(out, 32);
	prt_passes(out, "Scheduled passes", c->sb.recovery_passes_required);
	prt_passes(out, "Scheduled online passes", c->sb.recovery_passes_required &
		   bch2_recovery_passes_match(PASS_ONLINE));
	prt_passes(out, "Complete passes", r->passes_complete);
	prt_passes(out, "Failing passes", r->passes_failing);

	if (r->curr_pass) {
		prt_printf(out, "Current pass:\t%s\n", bch2_recovery_passes[r->curr_pass]);
		prt_passes(out, "Current passes", r->passes_to_run);
	}
}

void bch2_fs_recovery_passes_init(struct bch_fs *c)
{
	spin_lock_init(&c->recovery.lock);
	sema_init(&c->recovery.run_lock, 1);

	INIT_WORK(&c->recovery.work, bch2_async_recovery_passes_work);
}
