// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "btree_cache.h"
#include "btree_iter.h"
#include "error.h"
#include "journal.h"
#include "namei.h"
#include "recovery_passes.h"
#include "super.h"
#include "thread_with_file.h"

#define FSCK_ERR_RATELIMIT_NR	10

void __bch2_log_msg_start(const char *fs_or_dev_name, struct printbuf *out)
{
	printbuf_indent_add_nextline(out, 2);

#ifdef BCACHEFS_LOG_PREFIX
	prt_printf(out, "bcachefs (%s): ", fs_or_dev_name);
#endif
}

bool __bch2_inconsistent_error(struct bch_fs *c, struct printbuf *out)
{
	set_bit(BCH_FS_error, &c->flags);

	switch (c->opts.errors) {
	case BCH_ON_ERROR_continue:
		return false;
	case BCH_ON_ERROR_fix_safe:
	case BCH_ON_ERROR_ro:
		bch2_fs_emergency_read_only2(c, out);
		return true;
	case BCH_ON_ERROR_panic:
		bch2_print_str(c, KERN_ERR, out->buf);
		panic(bch2_fmt(c, "panic after error"));
		return true;
	default:
		BUG();
	}
}

bool bch2_inconsistent_error(struct bch_fs *c)
{
	struct printbuf buf = PRINTBUF;
	buf.atomic++;

	printbuf_indent_add_nextline(&buf, 2);

	bool ret = __bch2_inconsistent_error(c, &buf);
	if (ret)
		bch_err(c, "%s", buf.buf);
	printbuf_exit(&buf);
	return ret;
}

__printf(3, 0)
static bool bch2_fs_trans_inconsistent(struct bch_fs *c, struct btree_trans *trans,
				       const char *fmt, va_list args)
{
	struct printbuf buf = PRINTBUF;
	buf.atomic++;

	bch2_log_msg_start(c, &buf);

	prt_vprintf(&buf, fmt, args);
	prt_newline(&buf);

	if (trans)
		bch2_trans_updates_to_text(&buf, trans);
	bool ret = __bch2_inconsistent_error(c, &buf);
	bch2_print_str(c, KERN_ERR, buf.buf);

	printbuf_exit(&buf);
	return ret;
}

bool bch2_fs_inconsistent(struct bch_fs *c, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	bool ret = bch2_fs_trans_inconsistent(c, NULL, fmt, args);
	va_end(args);
	return ret;
}

bool bch2_trans_inconsistent(struct btree_trans *trans, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	bool ret = bch2_fs_trans_inconsistent(trans->c, trans, fmt, args);
	va_end(args);
	return ret;
}

int __bch2_topology_error(struct bch_fs *c, struct printbuf *out)
{
	prt_printf(out, "btree topology error: ");

	set_bit(BCH_FS_topology_error, &c->flags);
	if (!test_bit(BCH_FS_in_recovery, &c->flags)) {
		__bch2_inconsistent_error(c, out);
		return bch_err_throw(c, btree_need_topology_repair);
	} else {
		return bch2_run_explicit_recovery_pass(c, out, BCH_RECOVERY_PASS_check_topology, 0) ?:
			bch_err_throw(c, btree_node_read_validate_error);
	}
}

int bch2_fs_topology_error(struct bch_fs *c, const char *fmt, ...)
{
	struct printbuf buf = PRINTBUF;

	bch2_log_msg_start(c, &buf);

	va_list args;
	va_start(args, fmt);
	prt_vprintf(&buf, fmt, args);
	va_end(args);

	int ret = __bch2_topology_error(c, &buf);
	bch2_print_str(c, KERN_ERR, buf.buf);

	printbuf_exit(&buf);
	return ret;
}

void bch2_fatal_error(struct bch_fs *c)
{
	if (bch2_fs_emergency_read_only(c))
		bch_err(c, "fatal error - emergency read only");
}

void bch2_io_error_work(struct work_struct *work)
{
	struct bch_dev *ca = container_of(work, struct bch_dev, io_error_work);
	struct bch_fs *c = ca->fs;

	/* XXX: if it's reads or checksums that are failing, set it to failed */

	down_write(&c->state_lock);
	unsigned long write_errors_start = READ_ONCE(ca->write_errors_start);

	if (write_errors_start &&
	    time_after(jiffies,
		       write_errors_start + c->opts.write_error_timeout * HZ)) {
		if (ca->mi.state >= BCH_MEMBER_STATE_ro)
			goto out;

		bool dev = !__bch2_dev_set_state(c, ca, BCH_MEMBER_STATE_ro,
						 BCH_FORCE_IF_DEGRADED);
		struct printbuf buf = PRINTBUF;
		__bch2_log_msg_start(ca->name, &buf);

		prt_printf(&buf, "writes erroring for %u seconds, setting %s ro",
			c->opts.write_error_timeout,
			dev ? "device" : "filesystem");
		if (!dev)
			bch2_fs_emergency_read_only2(c, &buf);

		bch2_print_str(c, KERN_ERR, buf.buf);
		printbuf_exit(&buf);
	}
out:
	up_write(&c->state_lock);
}

void bch2_io_error(struct bch_dev *ca, enum bch_member_error_type type)
{
	atomic64_inc(&ca->errors[type]);

	if (type == BCH_MEMBER_ERROR_write && !ca->write_errors_start)
		ca->write_errors_start = jiffies;

	queue_work(system_long_wq, &ca->io_error_work);
}

enum ask_yn {
	YN_NO,
	YN_YES,
	YN_ALLNO,
	YN_ALLYES,
};

static enum ask_yn parse_yn_response(char *buf)
{
	buf = strim(buf);

	if (strlen(buf) == 1)
		switch (buf[0]) {
		case 'n':
			return YN_NO;
		case 'y':
			return YN_YES;
		case 'N':
			return YN_ALLNO;
		case 'Y':
			return YN_ALLYES;
		}
	return -1;
}

#ifdef __KERNEL__
static enum ask_yn bch2_fsck_ask_yn(struct bch_fs *c, struct btree_trans *trans)
{
	struct stdio_redirect *stdio = c->stdio;

	if (c->stdio_filter && c->stdio_filter != current)
		stdio = NULL;

	if (!stdio)
		return YN_NO;

	if (trans)
		bch2_trans_unlock(trans);

	unsigned long unlock_long_at = trans ? jiffies + HZ * 2 : 0;
	darray_char line = {};
	int ret;

	do {
		unsigned long t;
		bch2_print(c, " (y,n, or Y,N for all errors of this type) ");
rewait:
		t = unlock_long_at
			? max_t(long, unlock_long_at - jiffies, 0)
			: MAX_SCHEDULE_TIMEOUT;

		int r = bch2_stdio_redirect_readline_timeout(stdio, &line, t);
		if (r == -ETIME) {
			bch2_trans_unlock_long(trans);
			unlock_long_at = 0;
			goto rewait;
		}

		if (r < 0) {
			ret = YN_NO;
			break;
		}

		darray_last(line) = '\0';
	} while ((ret = parse_yn_response(line.data)) < 0);

	darray_exit(&line);
	return ret;
}
#else

#include "tools-util.h"

static enum ask_yn bch2_fsck_ask_yn(struct bch_fs *c, struct btree_trans *trans)
{
	char *buf = NULL;
	size_t buflen = 0;
	int ret;

	do {
		fputs(" (y,n, or Y,N for all errors of this type) ", stdout);
		fflush(stdout);

		if (getline(&buf, &buflen, stdin) < 0)
			die("error reading from standard input");
	} while ((ret = parse_yn_response(buf)) < 0);

	free(buf);
	return ret;
}

#endif

static struct fsck_err_state *fsck_err_get(struct bch_fs *c,
					   enum bch_sb_error_id id)
{
	struct fsck_err_state *s;

	list_for_each_entry(s, &c->fsck_error_msgs, list)
		if (s->id == id) {
			/*
			 * move it to the head of the list: repeated fsck errors
			 * are common
			 */
			list_move(&s->list, &c->fsck_error_msgs);
			return s;
		}

	s = kzalloc(sizeof(*s), GFP_NOFS);
	if (!s) {
		if (!c->fsck_alloc_msgs_err)
			bch_err(c, "kmalloc err, cannot ratelimit fsck errs");
		c->fsck_alloc_msgs_err = true;
		return NULL;
	}

	INIT_LIST_HEAD(&s->list);
	s->id = id;
	list_add(&s->list, &c->fsck_error_msgs);
	return s;
}

/* s/fix?/fixing/ s/recreate?/recreating/ */
static void prt_actioning(struct printbuf *out, const char *action)
{
	unsigned len = strlen(action);

	BUG_ON(action[len - 1] != '?');
	--len;

	if (action[len - 1] == 'e')
		--len;

	prt_bytes(out, action, len);
	prt_str(out, "ing");
}

static const u8 fsck_flags_extra[] = {
#define x(t, n, flags)		[BCH_FSCK_ERR_##t] = flags,
	BCH_SB_ERRS()
#undef x
};

static int do_fsck_ask_yn(struct bch_fs *c,
			  struct btree_trans *trans,
			  struct printbuf *question,
			  const char *action)
{
	prt_str(question, ", ");
	prt_str(question, action);

	if (bch2_fs_stdio_redirect(c))
		bch2_print(c, "%s", question->buf);
	else
		bch2_print_str(c, KERN_ERR, question->buf);

	int ask = bch2_fsck_ask_yn(c, trans);

	if (trans) {
		int ret = bch2_trans_relock(trans);
		if (ret)
			return ret;
	}

	return ask;
}

static struct fsck_err_state *count_fsck_err_locked(struct bch_fs *c,
			  enum bch_sb_error_id id, const char *msg,
			  bool *repeat, bool *print, bool *suppress)
{
	bch2_sb_error_count(c, id);

	struct fsck_err_state *s = fsck_err_get(c, id);
	if (s) {
		/*
		 * We may be called multiple times for the same error on
		 * transaction restart - this memoizes instead of asking the user
		 * multiple times for the same error:
		 */
		if (s->last_msg && !strcmp(msg, s->last_msg)) {
			*repeat = true;
			*print = false;
			return s;
		}

		kfree(s->last_msg);
		s->last_msg = kstrdup(msg, GFP_KERNEL);

		if (c->opts.ratelimit_errors &&
		    s->nr >= FSCK_ERR_RATELIMIT_NR) {
			if (s->nr == FSCK_ERR_RATELIMIT_NR)
				*suppress = true;
			else
				*print = false;
		}

		s->nr++;
	}
	return s;
}

bool __bch2_count_fsck_err(struct bch_fs *c,
			   enum bch_sb_error_id id, struct printbuf *msg)
{
	bch2_sb_error_count(c, id);

	mutex_lock(&c->fsck_error_msgs_lock);
	bool print = true, repeat = false, suppress = false;

	count_fsck_err_locked(c, id, msg->buf, &repeat, &print, &suppress);
	mutex_unlock(&c->fsck_error_msgs_lock);

	if (suppress)
		prt_printf(msg, "Ratelimiting new instances of previous error\n");

	return print && !repeat;
}

int bch2_fsck_err_opt(struct bch_fs *c,
		      enum bch_fsck_flags flags,
		      enum bch_sb_error_id err)
{
	if (!WARN_ON(err >= ARRAY_SIZE(fsck_flags_extra)))
		flags |= fsck_flags_extra[err];

	if (test_bit(BCH_FS_in_fsck, &c->flags)) {
		if (!(flags & (FSCK_CAN_FIX|FSCK_CAN_IGNORE)))
			return bch_err_throw(c, fsck_repair_unimplemented);

		switch (c->opts.fix_errors) {
		case FSCK_FIX_exit:
			return bch_err_throw(c, fsck_errors_not_fixed);
		case FSCK_FIX_yes:
			if (flags & FSCK_CAN_FIX)
				return bch_err_throw(c, fsck_fix);
			fallthrough;
		case FSCK_FIX_no:
			if (flags & FSCK_CAN_IGNORE)
				return bch_err_throw(c, fsck_ignore);
			return bch_err_throw(c, fsck_errors_not_fixed);
		case FSCK_FIX_ask:
			if (flags & FSCK_AUTOFIX)
				return bch_err_throw(c, fsck_fix);
			return bch_err_throw(c, fsck_ask);
		default:
			BUG();
		}
	} else {
		if ((flags & FSCK_AUTOFIX) &&
		    (c->opts.errors == BCH_ON_ERROR_continue ||
		     c->opts.errors == BCH_ON_ERROR_fix_safe))
			return bch_err_throw(c, fsck_fix);

		if (c->opts.errors == BCH_ON_ERROR_continue &&
		    (flags & FSCK_CAN_IGNORE))
			return bch_err_throw(c, fsck_ignore);
		return bch_err_throw(c, fsck_errors_not_fixed);
	}
}

int __bch2_fsck_err(struct bch_fs *c,
		  struct btree_trans *trans,
		  enum bch_fsck_flags flags,
		  enum bch_sb_error_id err,
		  const char *fmt, ...)
{
	va_list args;
	struct printbuf buf = PRINTBUF, *out = &buf;
	int ret = 0;
	const char *action_orig = "fix?", *action = action_orig;

	might_sleep();

	if (!WARN_ON(err >= ARRAY_SIZE(fsck_flags_extra)))
		flags |= fsck_flags_extra[err];

	if (!c)
		c = trans->c;

	/*
	 * Ugly: if there's a transaction in the current task it has to be
	 * passed in to unlock if we prompt for user input.
	 *
	 * But, plumbing a transaction and transaction restarts into
	 * bkey_validate() is problematic.
	 *
	 * So:
	 * - make all bkey errors AUTOFIX, they're simple anyways (we just
	 *   delete the key)
	 * - and we don't need to warn if we're not prompting
	 */
	WARN_ON((flags & FSCK_CAN_FIX) &&
		!(flags & FSCK_AUTOFIX) &&
		!trans &&
		bch2_current_has_btree_trans(c));

	if (test_bit(err, c->sb.errors_silent))
		return flags & FSCK_CAN_FIX
			? bch_err_throw(c, fsck_fix)
			: bch_err_throw(c, fsck_ignore);

	printbuf_indent_add_nextline(out, 2);

#ifdef BCACHEFS_LOG_PREFIX
	if (strncmp(fmt, "bcachefs", 8))
		prt_printf(out, bch2_log_msg(c, ""));
#endif

	va_start(args, fmt);
	prt_vprintf(out, fmt, args);
	va_end(args);

	/* Custom fix/continue/recreate/etc.? */
	if (out->buf[out->pos - 1] == '?') {
		const char *p = strrchr(out->buf, ',');
		if (p) {
			out->pos = p - out->buf;
			action = kstrdup(p + 2, GFP_KERNEL);
			if (!action) {
				ret = -ENOMEM;
				goto err;
			}
		}
	}

	mutex_lock(&c->fsck_error_msgs_lock);
	bool repeat = false, print = true, suppress = false;
	bool inconsistent = false, exiting = false;
	struct fsck_err_state *s =
		count_fsck_err_locked(c, err, buf.buf, &repeat, &print, &suppress);
	if (repeat) {
		ret = s->ret;
		goto err_unlock;
	}

	if ((flags & FSCK_AUTOFIX) &&
	    (c->opts.errors == BCH_ON_ERROR_continue ||
	     c->opts.errors == BCH_ON_ERROR_fix_safe)) {
		prt_str(out, ", ");
		if (flags & FSCK_CAN_FIX) {
			prt_actioning(out, action);
			ret = bch_err_throw(c, fsck_fix);
		} else {
			prt_str(out, ", continuing");
			ret = bch_err_throw(c, fsck_ignore);
		}

		goto print;
	} else if (!test_bit(BCH_FS_in_fsck, &c->flags)) {
		if (c->opts.errors != BCH_ON_ERROR_continue ||
		    !(flags & (FSCK_CAN_FIX|FSCK_CAN_IGNORE))) {
			prt_str_indented(out, ", shutting down\n"
					 "error not marked as autofix and not in fsck\n"
					 "run fsck, and forward to devs so error can be marked for self-healing");
			inconsistent = true;
			print = true;
			ret = bch_err_throw(c, fsck_errors_not_fixed);
		} else if (flags & FSCK_CAN_FIX) {
			prt_str(out, ", ");
			prt_actioning(out, action);
			ret = bch_err_throw(c, fsck_fix);
		} else {
			prt_str(out, ", continuing");
			ret = bch_err_throw(c, fsck_ignore);
		}
	} else if (c->opts.fix_errors == FSCK_FIX_exit) {
		prt_str(out, ", exiting");
		ret = bch_err_throw(c, fsck_errors_not_fixed);
	} else if (flags & FSCK_CAN_FIX) {
		int fix = s && s->fix
			? s->fix
			: c->opts.fix_errors;

		if (fix == FSCK_FIX_ask) {
			print = false;

			ret = do_fsck_ask_yn(c, trans, out, action);
			if (ret < 0)
				goto err_unlock;

			if (ret >= YN_ALLNO && s)
				s->fix = ret == YN_ALLNO
					? FSCK_FIX_no
					: FSCK_FIX_yes;

			ret = ret & 1
				? bch_err_throw(c, fsck_fix)
				: bch_err_throw(c, fsck_ignore);
		} else if (fix == FSCK_FIX_yes ||
			   (c->opts.nochanges &&
			    !(flags & FSCK_CAN_IGNORE))) {
			prt_str(out, ", ");
			prt_actioning(out, action);
			ret = bch_err_throw(c, fsck_fix);
		} else {
			prt_str(out, ", not ");
			prt_actioning(out, action);
			ret = bch_err_throw(c, fsck_ignore);
		}
	} else {
		if (flags & FSCK_CAN_IGNORE) {
			prt_str(out, ", continuing");
			ret = bch_err_throw(c, fsck_ignore);
		} else {
			prt_str(out, " (repair unimplemented)");
			ret = bch_err_throw(c, fsck_repair_unimplemented);
		}
	}

	if (bch2_err_matches(ret, BCH_ERR_fsck_ignore) &&
	    (c->opts.fix_errors == FSCK_FIX_exit ||
	     !(flags & FSCK_CAN_IGNORE)))
		ret = bch_err_throw(c, fsck_errors_not_fixed);

	if (test_bit(BCH_FS_in_fsck, &c->flags) &&
	    (!bch2_err_matches(ret, BCH_ERR_fsck_fix) &&
	     !bch2_err_matches(ret, BCH_ERR_fsck_ignore))) {
		exiting = true;
		print = true;
	}
print:
	prt_newline(out);

	if (inconsistent)
		__bch2_inconsistent_error(c, out);
	else if (exiting)
		prt_printf(out, "Unable to continue, halting\n");
	else if (suppress)
		prt_printf(out, "Ratelimiting new instances of previous error\n");

	if (print) {
		/* possibly strip an empty line, from printbuf_indent_add */
		while (out->pos && out->buf[out->pos - 1] == ' ')
			--out->pos;
		printbuf_nul_terminate(out);

		if (bch2_fs_stdio_redirect(c))
			bch2_print(c, "%s", out->buf);
		else
			bch2_print_str(c, KERN_ERR, out->buf);
	}

	if (s)
		s->ret = ret;

	if (trans &&
	    !(flags & FSCK_ERR_NO_LOG) &&
	    ret == -BCH_ERR_fsck_fix)
		ret = bch2_trans_log_str(trans, bch2_sb_error_strs[err]) ?: ret;
err_unlock:
	mutex_unlock(&c->fsck_error_msgs_lock);
err:
	/*
	 * We don't yet track whether the filesystem currently has errors, for
	 * log_fsck_err()s: that would require us to track for every error type
	 * which recovery pass corrects it, to get the fsck exit status correct:
	 */
	if (bch2_err_matches(ret, BCH_ERR_fsck_fix)) {
		set_bit(BCH_FS_errors_fixed, &c->flags);
	} else {
		set_bit(BCH_FS_errors_not_fixed, &c->flags);
		set_bit(BCH_FS_error, &c->flags);
	}

	if (action != action_orig)
		kfree(action);
	printbuf_exit(&buf);

	BUG_ON(!ret);
	return ret;
}

static const char * const bch2_bkey_validate_contexts[] = {
#define x(n) #n,
	BKEY_VALIDATE_CONTEXTS()
#undef x
	NULL
};

int __bch2_bkey_fsck_err(struct bch_fs *c,
			 struct bkey_s_c k,
			 struct bkey_validate_context from,
			 enum bch_sb_error_id err,
			 const char *fmt, ...)
{
	if (from.flags & BCH_VALIDATE_silent)
		return bch_err_throw(c, fsck_delete_bkey);

	unsigned fsck_flags = 0;
	if (!(from.flags & (BCH_VALIDATE_write|BCH_VALIDATE_commit))) {
		if (test_bit(err, c->sb.errors_silent))
			return bch_err_throw(c, fsck_delete_bkey);

		fsck_flags |= FSCK_AUTOFIX|FSCK_CAN_FIX;
	}
	if (!WARN_ON(err >= ARRAY_SIZE(fsck_flags_extra)))
		fsck_flags |= fsck_flags_extra[err];

	struct printbuf buf = PRINTBUF;
	prt_printf(&buf, "invalid bkey in %s",
		   bch2_bkey_validate_contexts[from.from]);

	if (from.from == BKEY_VALIDATE_journal)
		prt_printf(&buf, " journal seq=%llu offset=%u",
			   from.journal_seq, from.journal_offset);

	prt_str(&buf, " btree=");
	bch2_btree_id_to_text(&buf, from.btree);
	prt_printf(&buf, " level=%u: ", from.level);

	bch2_bkey_val_to_text(&buf, c, k);
	prt_newline(&buf);

	va_list args;
	va_start(args, fmt);
	prt_vprintf(&buf, fmt, args);
	va_end(args);

	int ret = __bch2_fsck_err(c, NULL, fsck_flags, err, "%s, delete?", buf.buf);
	printbuf_exit(&buf);
	return ret;
}

static void __bch2_flush_fsck_errs(struct bch_fs *c, bool print)
{
	struct fsck_err_state *s, *n;

	mutex_lock(&c->fsck_error_msgs_lock);

	list_for_each_entry_safe(s, n, &c->fsck_error_msgs, list) {
		if (print && s->ratelimited && s->last_msg)
			bch_err(c, "Saw %llu errors like:\n  %s", s->nr, s->last_msg);

		list_del(&s->list);
		kfree(s->last_msg);
		kfree(s);
	}

	mutex_unlock(&c->fsck_error_msgs_lock);
}

void bch2_flush_fsck_errs(struct bch_fs *c)
{
	__bch2_flush_fsck_errs(c, true);
}

void bch2_free_fsck_errs(struct bch_fs *c)
{
	__bch2_flush_fsck_errs(c, false);
}

int bch2_inum_offset_err_msg_trans(struct btree_trans *trans, struct printbuf *out,
				    subvol_inum inum, u64 offset)
{
	u32 restart_count = trans->restart_count;
	int ret = 0;

	if (inum.subvol) {
		ret = bch2_inum_to_path(trans, inum, out);
		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			return ret;
	}
	if (!inum.subvol || ret)
		prt_printf(out, "inum %llu:%llu", inum.subvol, inum.inum);
	prt_printf(out, " offset %llu: ", offset);

	return trans_was_restarted(trans, restart_count);
}

void bch2_inum_offset_err_msg(struct bch_fs *c, struct printbuf *out,
			      subvol_inum inum, u64 offset)
{
	bch2_trans_do(c, bch2_inum_offset_err_msg_trans(trans, out, inum, offset));
}

int bch2_inum_snap_offset_err_msg_trans(struct btree_trans *trans, struct printbuf *out,
					struct bpos pos)
{
	int ret = bch2_inum_snapshot_to_path(trans, pos.inode, pos.snapshot, NULL, out);
	if (ret)
		return ret;

	prt_printf(out, " offset %llu: ", pos.offset << 8);
	return 0;
}

void bch2_inum_snap_offset_err_msg(struct bch_fs *c, struct printbuf *out,
				  struct bpos pos)
{
	bch2_trans_do(c, bch2_inum_snap_offset_err_msg_trans(trans, out, pos));
}
