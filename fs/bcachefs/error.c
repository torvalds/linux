// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "error.h"
#include "io.h"
#include "super.h"

#define FSCK_ERR_RATELIMIT_NR	10

bool bch2_inconsistent_error(struct bch_fs *c)
{
	set_bit(BCH_FS_ERROR, &c->flags);

	switch (c->opts.errors) {
	case BCH_ON_ERROR_continue:
		return false;
	case BCH_ON_ERROR_ro:
		if (bch2_fs_emergency_read_only(c))
			bch_err(c, "inconsistency detected - emergency read only");
		return true;
	case BCH_ON_ERROR_panic:
		panic(bch2_fmt(c, "panic after error"));
		return true;
	default:
		BUG();
	}
}

void bch2_topology_error(struct bch_fs *c)
{
	set_bit(BCH_FS_TOPOLOGY_ERROR, &c->flags);
	if (test_bit(BCH_FS_FSCK_DONE, &c->flags))
		bch2_inconsistent_error(c);
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
	bool dev;

	down_write(&c->state_lock);
	dev = bch2_dev_state_allowed(c, ca, BCH_MEMBER_STATE_ro,
				    BCH_FORCE_IF_DEGRADED);
	if (dev
	    ? __bch2_dev_set_state(c, ca, BCH_MEMBER_STATE_ro,
				  BCH_FORCE_IF_DEGRADED)
	    : bch2_fs_emergency_read_only(c))
		bch_err(ca,
			"too many IO errors, setting %s RO",
			dev ? "device" : "filesystem");
	up_write(&c->state_lock);
}

void bch2_io_error(struct bch_dev *ca)
{
	//queue_work(system_long_wq, &ca->io_error_work);
}

enum ask_yn {
	YN_NO,
	YN_YES,
	YN_ALLNO,
	YN_ALLYES,
};

#ifdef __KERNEL__
#define bch2_fsck_ask_yn()	YN_NO
#else

#include "tools-util.h"

enum ask_yn bch2_fsck_ask_yn(void)
{
	char *buf = NULL;
	size_t buflen = 0;
	bool ret;

	while (true) {
		fputs(" (y,n, or Y,N for all errors of this type) ", stdout);
		fflush(stdout);

		if (getline(&buf, &buflen, stdin) < 0)
			die("error reading from standard input");

		strim(buf);
		if (strlen(buf) != 1)
			continue;

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
	}

	free(buf);
	return ret;
}

#endif

static struct fsck_err_state *fsck_err_get(struct bch_fs *c, const char *fmt)
{
	struct fsck_err_state *s;

	if (test_bit(BCH_FS_FSCK_DONE, &c->flags))
		return NULL;

	list_for_each_entry(s, &c->fsck_errors, list)
		if (s->fmt == fmt) {
			/*
			 * move it to the head of the list: repeated fsck errors
			 * are common
			 */
			list_move(&s->list, &c->fsck_errors);
			return s;
		}

	s = kzalloc(sizeof(*s), GFP_NOFS);
	if (!s) {
		if (!c->fsck_alloc_err)
			bch_err(c, "kmalloc err, cannot ratelimit fsck errs");
		c->fsck_alloc_err = true;
		return NULL;
	}

	INIT_LIST_HEAD(&s->list);
	s->fmt = fmt;
	list_add(&s->list, &c->fsck_errors);
	return s;
}

int bch2_fsck_err(struct bch_fs *c, unsigned flags, const char *fmt, ...)
{
	struct fsck_err_state *s = NULL;
	va_list args;
	bool print = true, suppressing = false, inconsistent = false;
	struct printbuf buf = PRINTBUF, *out = &buf;
	int ret = -BCH_ERR_fsck_ignore;

	va_start(args, fmt);
	prt_vprintf(out, fmt, args);
	va_end(args);

	mutex_lock(&c->fsck_error_lock);
	s = fsck_err_get(c, fmt);
	if (s) {
		/*
		 * We may be called multiple times for the same error on
		 * transaction restart - this memoizes instead of asking the user
		 * multiple times for the same error:
		 */
		if (s->last_msg && !strcmp(buf.buf, s->last_msg)) {
			ret = s->ret;
			mutex_unlock(&c->fsck_error_lock);
			printbuf_exit(&buf);
			return ret;
		}

		kfree(s->last_msg);
		s->last_msg = kstrdup(buf.buf, GFP_KERNEL);

		if (c->opts.ratelimit_errors &&
		    !(flags & FSCK_NO_RATELIMIT) &&
		    s->nr >= FSCK_ERR_RATELIMIT_NR) {
			if (s->nr == FSCK_ERR_RATELIMIT_NR)
				suppressing = true;
			else
				print = false;
		}

		s->nr++;
	}

#ifdef BCACHEFS_LOG_PREFIX
	if (!strncmp(fmt, "bcachefs:", 9))
		prt_printf(out, bch2_log_msg(c, ""));
#endif

	if (test_bit(BCH_FS_FSCK_DONE, &c->flags)) {
		if (c->opts.errors != BCH_ON_ERROR_continue ||
		    !(flags & (FSCK_CAN_FIX|FSCK_CAN_IGNORE))) {
			prt_str(out, ", shutting down");
			inconsistent = true;
			ret = -BCH_ERR_fsck_errors_not_fixed;
		} else if (flags & FSCK_CAN_FIX) {
			prt_str(out, ", fixing");
			ret = -BCH_ERR_fsck_fix;
		} else {
			prt_str(out, ", continuing");
			ret = -BCH_ERR_fsck_ignore;
		}
	} else if (c->opts.fix_errors == FSCK_FIX_exit) {
		prt_str(out, ", exiting");
		ret = -BCH_ERR_fsck_errors_not_fixed;
	} else if (flags & FSCK_CAN_FIX) {
		int fix = s && s->fix
			? s->fix
			: c->opts.fix_errors;

		if (fix == FSCK_FIX_ask) {
			int ask;

			prt_str(out, ": fix?");
			bch2_print_string_as_lines(KERN_ERR, out->buf);
			print = false;

			ask = bch2_fsck_ask_yn();

			if (ask >= YN_ALLNO && s)
				s->fix = ask == YN_ALLNO
					? FSCK_FIX_no
					: FSCK_FIX_yes;

			ret = ask & 1
				? -BCH_ERR_fsck_fix
				: -BCH_ERR_fsck_ignore;
		} else if (fix == FSCK_FIX_yes ||
			   (c->opts.nochanges &&
			    !(flags & FSCK_CAN_IGNORE))) {
			prt_str(out, ", fixing");
			ret = -BCH_ERR_fsck_fix;
		} else {
			prt_str(out, ", not fixing");
		}
	} else if (flags & FSCK_NEED_FSCK) {
		prt_str(out, " (run fsck to correct)");
	} else {
		prt_str(out, " (repair unimplemented)");
	}

	if (ret == -BCH_ERR_fsck_ignore &&
	    (c->opts.fix_errors == FSCK_FIX_exit ||
	     !(flags & FSCK_CAN_IGNORE)))
		ret = -BCH_ERR_fsck_errors_not_fixed;

	if (print)
		bch2_print_string_as_lines(KERN_ERR, out->buf);

	if (!test_bit(BCH_FS_FSCK_DONE, &c->flags) &&
	    (ret != -BCH_ERR_fsck_fix &&
	     ret != -BCH_ERR_fsck_ignore))
		bch_err(c, "Unable to continue, halting");
	else if (suppressing)
		bch_err(c, "Ratelimiting new instances of previous error");

	if (s)
		s->ret = ret;

	mutex_unlock(&c->fsck_error_lock);

	printbuf_exit(&buf);

	if (inconsistent)
		bch2_inconsistent_error(c);

	if (ret == -BCH_ERR_fsck_fix) {
		set_bit(BCH_FS_ERRORS_FIXED, &c->flags);
	} else {
		set_bit(BCH_FS_ERRORS_NOT_FIXED, &c->flags);
		set_bit(BCH_FS_ERROR, &c->flags);
	}

	return ret;
}

void bch2_flush_fsck_errs(struct bch_fs *c)
{
	struct fsck_err_state *s, *n;

	mutex_lock(&c->fsck_error_lock);

	list_for_each_entry_safe(s, n, &c->fsck_errors, list) {
		if (s->ratelimited && s->last_msg)
			bch_err(c, "Saw %llu errors like:\n    %s", s->nr, s->last_msg);

		list_del(&s->list);
		kfree(s->last_msg);
		kfree(s);
	}

	mutex_unlock(&c->fsck_error_lock);
}
