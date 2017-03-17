// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "error.h"
#include "io.h"
#include "super.h"

bool bch2_inconsistent_error(struct bch_fs *c)
{
	set_bit(BCH_FS_ERROR, &c->flags);

	switch (c->opts.errors) {
	case BCH_ON_ERROR_CONTINUE:
		return false;
	case BCH_ON_ERROR_RO:
		if (bch2_fs_emergency_read_only(c))
			bch_err(c, "emergency read only");
		return true;
	case BCH_ON_ERROR_PANIC:
		panic(bch2_fmt(c, "panic after error"));
		return true;
	default:
		BUG();
	}
}

void bch2_fatal_error(struct bch_fs *c)
{
	if (bch2_fs_emergency_read_only(c))
		bch_err(c, "emergency read only");
}

void bch2_io_error_work(struct work_struct *work)
{
	struct bch_dev *ca = container_of(work, struct bch_dev, io_error_work);
	struct bch_fs *c = ca->fs;
	bool dev;

	mutex_lock(&c->state_lock);
	dev = bch2_dev_state_allowed(c, ca, BCH_MEMBER_STATE_RO,
				    BCH_FORCE_IF_DEGRADED);
	if (dev
	    ? __bch2_dev_set_state(c, ca, BCH_MEMBER_STATE_RO,
				  BCH_FORCE_IF_DEGRADED)
	    : bch2_fs_emergency_read_only(c))
		bch_err(ca,
			"too many IO errors, setting %s RO",
			dev ? "device" : "filesystem");
	mutex_unlock(&c->state_lock);
}

void bch2_io_error(struct bch_dev *ca)
{
	//queue_work(system_long_wq, &ca->io_error_work);
}

#ifdef __KERNEL__
#define ask_yn()	false
#else
#include "tools-util.h"
#endif

enum fsck_err_ret bch2_fsck_err(struct bch_fs *c, unsigned flags,
				const char *fmt, ...)
{
	struct fsck_err_state *s;
	va_list args;
	bool fix = false, print = true, suppressing = false;
	char _buf[sizeof(s->buf)], *buf = _buf;

	mutex_lock(&c->fsck_error_lock);

	if (test_bit(BCH_FS_FSCK_DONE, &c->flags))
		goto print;

	list_for_each_entry(s, &c->fsck_errors, list)
		if (s->fmt == fmt)
			goto found;

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s) {
		if (!c->fsck_alloc_err)
			bch_err(c, "kmalloc err, cannot ratelimit fsck errs");
		c->fsck_alloc_err = true;
		buf = _buf;
		goto print;
	}

	INIT_LIST_HEAD(&s->list);
	s->fmt = fmt;
found:
	list_move(&s->list, &c->fsck_errors);
	s->nr++;
	suppressing	= s->nr == 10;
	print		= s->nr <= 10;
	buf		= s->buf;
print:
	va_start(args, fmt);
	vscnprintf(buf, sizeof(_buf), fmt, args);
	va_end(args);

	if (c->opts.fix_errors == FSCK_OPT_EXIT) {
		bch_err(c, "%s, exiting", buf);
		mutex_unlock(&c->fsck_error_lock);
		return FSCK_ERR_EXIT;
	}

	if (flags & FSCK_CAN_FIX) {
		if (c->opts.fix_errors == FSCK_OPT_ASK) {
			printk(KERN_ERR "%s: fix?", buf);
			fix = ask_yn();
		} else if (c->opts.fix_errors == FSCK_OPT_YES ||
			   (c->opts.nochanges &&
			    !(flags & FSCK_CAN_IGNORE))) {
			if (print)
				bch_err(c, "%s, fixing", buf);
			fix = true;
		} else {
			if (print)
				bch_err(c, "%s, not fixing", buf);
			fix = false;
		}
	} else if (flags & FSCK_NEED_FSCK) {
		if (print)
			bch_err(c, "%s (run fsck to correct)", buf);
	} else {
		if (print)
			bch_err(c, "%s (repair unimplemented)", buf);
	}

	if (suppressing)
		bch_err(c, "Ratelimiting new instances of previous error");

	mutex_unlock(&c->fsck_error_lock);

	if (fix)
		set_bit(BCH_FS_FSCK_FIXED_ERRORS, &c->flags);

	return fix				? FSCK_ERR_FIX
		: flags & FSCK_CAN_IGNORE	? FSCK_ERR_IGNORE
						: FSCK_ERR_EXIT;
}

void bch2_flush_fsck_errs(struct bch_fs *c)
{
	struct fsck_err_state *s, *n;

	mutex_lock(&c->fsck_error_lock);
	set_bit(BCH_FS_FSCK_DONE, &c->flags);

	list_for_each_entry_safe(s, n, &c->fsck_errors, list) {
		if (s->nr > 10)
			bch_err(c, "Saw %llu errors like:\n    %s", s->nr, s->buf);

		list_del(&s->list);
		kfree(s);
	}

	mutex_unlock(&c->fsck_error_lock);
}
