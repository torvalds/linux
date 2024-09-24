/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ERROR_H
#define _BCACHEFS_ERROR_H

#include <linux/list.h>
#include <linux/printk.h>
#include "bkey_types.h"
#include "sb-errors.h"

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

int bch2_topology_error(struct bch_fs *);

#define bch2_fs_topology_error(c, ...)					\
({									\
	bch_err(c, "btree topology error: " __VA_ARGS__);		\
	bch2_topology_error(c);						\
})

#define bch2_fs_inconsistent(c, ...)					\
({									\
	bch_err(c, __VA_ARGS__);					\
	bch2_inconsistent_error(c);					\
})

#define bch2_fs_inconsistent_on(cond, c, ...)				\
({									\
	bool _ret = unlikely(!!(cond));					\
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
	bool _ret = unlikely(!!(cond));					\
									\
	if (_ret)							\
		bch2_dev_inconsistent(ca, __VA_ARGS__);			\
	_ret;								\
})

/*
 * When a transaction update discovers or is causing a fs inconsistency, it's
 * helpful to also dump the pending updates:
 */
#define bch2_trans_inconsistent(trans, ...)				\
({									\
	bch_err(trans->c, __VA_ARGS__);					\
	bch2_dump_trans_updates(trans);					\
	bch2_inconsistent_error(trans->c);				\
})

#define bch2_trans_inconsistent_on(cond, trans, ...)			\
({									\
	bool _ret = unlikely(!!(cond));					\
									\
	if (_ret)							\
		bch2_trans_inconsistent(trans, __VA_ARGS__);		\
	_ret;								\
})

/*
 * Fsck errors: inconsistency errors we detect at mount time, and should ideally
 * be able to repair:
 */

struct fsck_err_state {
	struct list_head	list;
	const char		*fmt;
	u64			nr;
	bool			ratelimited;
	int			ret;
	int			fix;
	char			*last_msg;
};

#define fsck_err_count(_c, _err)	bch2_sb_err_count(_c, BCH_FSCK_ERR_##_err)

__printf(5, 6) __cold
int __bch2_fsck_err(struct bch_fs *, struct btree_trans *,
		  enum bch_fsck_flags,
		  enum bch_sb_error_id,
		  const char *, ...);
#define bch2_fsck_err(c, _flags, _err_type, ...)				\
	__bch2_fsck_err(type_is(c, struct bch_fs *) ? (struct bch_fs *) c : NULL,\
			type_is(c, struct btree_trans *) ? (struct btree_trans *) c : NULL,\
			_flags, BCH_FSCK_ERR_##_err_type, __VA_ARGS__)

void bch2_flush_fsck_errs(struct bch_fs *);

#define __fsck_err(c, _flags, _err_type, ...)				\
({									\
	int _ret = bch2_fsck_err(c, _flags, _err_type, __VA_ARGS__);	\
	if (_ret != -BCH_ERR_fsck_fix &&				\
	    _ret != -BCH_ERR_fsck_ignore) {				\
		ret = _ret;						\
		goto fsck_err;						\
	}								\
									\
	_ret == -BCH_ERR_fsck_fix;					\
})

/* These macros return true if error should be fixed: */

/* XXX: mark in superblock that filesystem contains errors, if we ignore: */

#define __fsck_err_on(cond, c, _flags, _err_type, ...)			\
({									\
	might_sleep();							\
									\
	if (type_is(c, struct bch_fs *))				\
		WARN_ON(bch2_current_has_btree_trans((struct bch_fs *) c));\
									\
	(unlikely(cond) ? __fsck_err(c, _flags, _err_type, __VA_ARGS__) : false);\
})

#define need_fsck_err_on(cond, c, _err_type, ...)				\
	__fsck_err_on(cond, c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK, _err_type, __VA_ARGS__)

#define need_fsck_err(c, _err_type, ...)				\
	__fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK, _err_type, __VA_ARGS__)

#define mustfix_fsck_err(c, _err_type, ...)				\
	__fsck_err(c, FSCK_CAN_FIX, _err_type, __VA_ARGS__)

#define mustfix_fsck_err_on(cond, c, _err_type, ...)			\
	__fsck_err_on(cond, c, FSCK_CAN_FIX, _err_type, __VA_ARGS__)

#define fsck_err(c, _err_type, ...)					\
	__fsck_err(c, FSCK_CAN_FIX|FSCK_CAN_IGNORE, _err_type, __VA_ARGS__)

#define fsck_err_on(cond, c, _err_type, ...)				\
	__fsck_err_on(cond, c, FSCK_CAN_FIX|FSCK_CAN_IGNORE, _err_type, __VA_ARGS__)

__printf(5, 6)
int __bch2_bkey_fsck_err(struct bch_fs *,
			 struct bkey_s_c,
			 enum bch_fsck_flags,
			 enum bch_sb_error_id,
			 const char *, ...);

/*
 * for now, bkey fsck errors are always handled by deleting the entire key -
 * this will change at some point
 */
#define bkey_fsck_err(c, _err_type, _err_msg, ...)			\
do {									\
	if ((flags & BCH_VALIDATE_silent)) {				\
		ret = -BCH_ERR_fsck_delete_bkey;			\
		goto fsck_err;						\
	}								\
	int _ret = __bch2_bkey_fsck_err(c, k, FSCK_CAN_FIX,		\
				BCH_FSCK_ERR_##_err_type,		\
				_err_msg, ##__VA_ARGS__);		\
	if (_ret != -BCH_ERR_fsck_fix &&				\
	    _ret != -BCH_ERR_fsck_ignore)				\
		ret = _ret;						\
	ret = -BCH_ERR_fsck_delete_bkey;				\
	goto fsck_err;							\
} while (0)

#define bkey_fsck_err_on(cond, ...)					\
do {									\
	if (unlikely(cond))						\
		bkey_fsck_err(__VA_ARGS__);				\
} while (0)

/*
 * Fatal errors: these don't indicate a bug, but we can't continue running in RW
 * mode - pretty much just due to metadata IO errors:
 */

void bch2_fatal_error(struct bch_fs *);

#define bch2_fs_fatal_error(c, _msg, ...)				\
do {									\
	bch_err(c, "%s(): fatal error " _msg, __func__, ##__VA_ARGS__);	\
	bch2_fatal_error(c);						\
} while (0)

#define bch2_fs_fatal_err_on(cond, c, ...)				\
({									\
	bool _ret = unlikely(!!(cond));					\
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
void bch2_io_error(struct bch_dev *, enum bch_member_error_type);

#define bch2_dev_io_err_on(cond, ca, _type, ...)			\
({									\
	bool _ret = (cond);						\
									\
	if (_ret) {							\
		bch_err_dev_ratelimited(ca, __VA_ARGS__);		\
		bch2_io_error(ca, _type);				\
	}								\
	_ret;								\
})

#define bch2_dev_inum_io_err_on(cond, ca, _type, ...)			\
({									\
	bool _ret = (cond);						\
									\
	if (_ret) {							\
		bch_err_inum_offset_ratelimited(ca, __VA_ARGS__);	\
		bch2_io_error(ca, _type);				\
	}								\
	_ret;								\
})

#endif /* _BCACHEFS_ERROR_H */
