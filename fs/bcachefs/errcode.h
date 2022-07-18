/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ERRCODE_H
#define _BCACHEFS_ERRCODE_H

#define BCH_ERRCODES()							\
	x(0,			open_buckets_empty)			\
	x(0,			freelist_empty)				\
	x(freelist_empty,	no_buckets_found)			\
	x(0,			insufficient_devices)			\
	x(0,			need_snapshot_cleanup)

enum bch_errcode {
	BCH_ERR_START		= 2048,
#define x(class, err) BCH_ERR_##err,
	BCH_ERRCODES()
#undef x
	BCH_ERR_MAX
};

const char *bch2_err_str(int);
bool __bch2_err_matches(int, int);

static inline bool _bch2_err_matches(int err, int class)
{
	return err && __bch2_err_matches(err, class);
}

#define bch2_err_matches(_err, _class)			\
({							\
	BUILD_BUG_ON(!__builtin_constant_p(_class));	\
	_bch2_err_matches(_err, _class);		\
})

#endif /* _BCACHFES_ERRCODE_H */
