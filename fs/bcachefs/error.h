/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ERROR_H
#define _BCACHEFS_ERROR_H

#include <linux/list.h>
#include <linux/printk.h>

struct bch_dev;
struct bch_fs;
struct work_struct;

/*
 * XXX: separate out errors that indicate on disk data is inconsistent, and flag
 * superblock as such
 */

/* Error messages: */

/*
 * Inconsistency errors: The on disk data is inconsistent. If these occur during
 * initial recovery, they don't indicate a bug in the running code - we walk all
 * the metadata before modifying anything. If they occur at runtime, they
 * indicate either a bug in the running code or (less likely) data is being
 * silently corrupted under us.
 *
 * XXX: audit all inconsistent errors and make sure they're all recoverable, in
 * BCH_ON_ERROR_CONTINUE mode
 */

bool bch2_inconsistent_error(struct bch_fs *);

#define bch2_fs_inconsistent(c, ...)					\
({									\
	bch_err(c, __VA_ARGS__);					\
	bch2_inconsistent_error(c);					\
})

#define bch2_fs_inconsistent_on(cond, c, ...)				\
({									\
	int _ret = !!(cond);						\
									\
	if (_ret)							\
		bch2_fs_inconsistent(c, __VA_ARGS__);			\
	_ret;								\
})

/*
 * Later we might want to mark only the particular device inconsistent, not the
 * entire filesystem:
 */

#define bch2_dev_inconsistent(ca, ...)					\
do {									\
	bch_err(ca, __VA_ARGS__);					\
	bch2_inconsistent_error((ca)->fs);				\
} while (0)

#define bch2_dev_inconsistent_on(cond, ca, ...)				\
({									\
	int _ret = !!(cond);						\
									\
	if (_ret)							\
		bch2_dev_inconsistent(ca, __VA_ARGS__);			\
	_ret;								\
})

/*
 * Fsck errors: inconsistency errors we detect at mount time, and should ideally
 * be able to repair:
 */

enum {
	BCH_FSCK_OK			= 0,
	BCH_FSCK_ERRORS_NOT_FIXED	= 1,
	BCH_FSCK_REPAIR_UNIMPLEMENTED	= 2,
	BCH_FSCK_REPAIR_IMPOSSIBLE	= 3,
	BCH_FSCK_UNKNOWN_VERSION	= 4,
};

enum fsck_err_opts {
	FSCK_OPT_EXIT,
	FSCK_OPT_YES,
	FSCK_OPT_NO,
	FSCK_OPT_ASK,
};

enum fsck_err_ret {
	FSCK_ERR_IGNORE	= 0,
	FSCK_ERR_FIX	= 1,
	FSCK_ERR_EXIT	= 2,
};

struct fsck_err_state {
	struct list_head	list;
	const char		*fmt;
	u64			nr;
	bool			ratelimited;
	char			buf[512];
};

#define FSCK_CAN_FIX		(1 << 0)
#define FSCK_CAN_IGNORE		(1 << 1)
#define FSCK_NEED_FSCK		(1 << 2)

__printf(3, 4) __cold
enum fsck_err_ret bch2_fsck_err(struct bch_fs *,
				unsigned, const char *, ...);
void bch2_flush_fsck_errs(struct bch_fs *);

#define __fsck_err(c, _flags, msg, ...)					\
({									\
	int _fix = bch2_fsck_err(c, _flags, msg, ##__VA_ARGS__);\
									\
	if (_fix == FSCK_ERR_EXIT) {					\
		bch_err(c, "Unable to continue, halting");		\
		ret = BCH_FSCK_ERRORS_NOT_FIXED;			\
		goto fsck_err;						\
	}								\
									\
	_fix;								\
})

/* These macros return true if error should be fixed: */

/* XXX: mark in superblock that filesystem contains errors, if we ignore: */

#define __fsck_err_on(cond, c, _flags, ...)				\
	((cond) ? __fsck_err(c, _flags,	##__VA_ARGS__) : false)

#define need_fsck_err_on(cond, c, ...)					\
	__fsck_err_on(cond, c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK, ##__VA_ARGS__)

#define need_fsck_err(c, ...)						\
	__fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK, ##__VA_ARGS__)

#define mustfix_fsck_err(c, ...)					\
	__fsck_err(c, FSCK_CAN_FIX, ##__VA_ARGS__)

#define mustfix_fsck_err_on(cond, c, ...)				\
	__fsck_err_on(cond, c, FSCK_CAN_FIX, ##__VA_ARGS__)

#define fsck_err(c, ...)						\
	__fsck_err(c, FSCK_CAN_FIX|FSCK_CAN_IGNORE, ##__VA_ARGS__)

#define fsck_err_on(cond, c, ...)					\
	__fsck_err_on(cond, c, FSCK_CAN_FIX|FSCK_CAN_IGNORE, ##__VA_ARGS__)

/*
 * Fatal errors: these don't indicate a bug, but we can't continue running in RW
 * mode - pretty much just due to metadata IO errors:
 */

void bch2_fatal_error(struct bch_fs *);

#define bch2_fs_fatal_error(c, ...)					\
do {									\
	bch_err(c, __VA_ARGS__);					\
	bch2_fatal_error(c);						\
} while (0)

#define bch2_fs_fatal_err_on(cond, c, ...)				\
({									\
	int _ret = !!(cond);						\
									\
	if (_ret)							\
		bch2_fs_fatal_error(c, __VA_ARGS__);			\
	_ret;								\
})

/*
 * IO errors: either recoverable metadata IO (because we have replicas), or data
 * IO - we need to log it and print out a message, but we don't (necessarily)
 * want to shut down the fs:
 */

void bch2_io_error_work(struct work_struct *);

/* Does the error handling without logging a message */
void bch2_io_error(struct bch_dev *);

/* Logs message and handles the error: */
#define bch2_dev_io_error(ca, fmt, ...)					\
do {									\
	printk_ratelimited(KERN_ERR bch2_fmt((ca)->fs,			\
		"IO error on %s for " fmt),				\
		(ca)->name, ##__VA_ARGS__);				\
	bch2_io_error(ca);						\
} while (0)

#define bch2_dev_io_err_on(cond, ca, ...)				\
({									\
	bool _ret = (cond);						\
									\
	if (_ret)							\
		bch2_dev_io_error(ca, __VA_ARGS__);			\
	_ret;								\
})

/* kill? */

#define __bcache_io_error(c, fmt, ...)					\
	printk_ratelimited(KERN_ERR bch2_fmt(c,				\
			"IO error: " fmt), ##__VA_ARGS__)

#define bcache_io_error(c, bio, fmt, ...)				\
do {									\
	__bcache_io_error(c, fmt, ##__VA_ARGS__);			\
	(bio)->bi_status = BLK_STS_IOERR;					\
} while (0)

#endif /* _BCACHEFS_ERROR_H */
