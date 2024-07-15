// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "errcode.h"
#include "trace.h"

#include <linux/errname.h>

static const char * const bch2_errcode_strs[] = {
#define x(class, err) [BCH_ERR_##err - BCH_ERR_START] = #err,
	BCH_ERRCODES()
#undef x
	NULL
};

static unsigned bch2_errcode_parents[] = {
#define x(class, err) [BCH_ERR_##err - BCH_ERR_START] = class,
	BCH_ERRCODES()
#undef x
};

const char *bch2_err_str(int err)
{
	const char *errstr;

	err = abs(err);

	BUG_ON(err >= BCH_ERR_MAX);

	if (err >= BCH_ERR_START)
		errstr = bch2_errcode_strs[err - BCH_ERR_START];
	else if (err)
		errstr = errname(err);
	else
		errstr = "(No error)";
	return errstr ?: "(Invalid error)";
}

bool __bch2_err_matches(int err, int class)
{
	err	= abs(err);
	class	= abs(class);

	BUG_ON(err	>= BCH_ERR_MAX);
	BUG_ON(class	>= BCH_ERR_MAX);

	while (err >= BCH_ERR_START && err != class)
		err = bch2_errcode_parents[err - BCH_ERR_START];

	return err == class;
}

int __bch2_err_class(int bch_err)
{
	int std_err = -bch_err;
	BUG_ON((unsigned) std_err >= BCH_ERR_MAX);

	while (std_err >= BCH_ERR_START && bch2_errcode_parents[std_err - BCH_ERR_START])
		std_err = bch2_errcode_parents[std_err - BCH_ERR_START];

	trace_error_downcast(bch_err, std_err, _RET_IP_);

	return -std_err;
}

const char *bch2_blk_status_to_str(blk_status_t status)
{
	if (status == BLK_STS_REMOVED)
		return "device removed";
	return blk_status_to_str(status);
}
