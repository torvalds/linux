// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "errcode.h"

#include <linux/errname.h>

static const char * const bch2_errcode_strs[] = {
#define x(class, err) [BCH_ERR_##err - BCH_ERR_START] = #err,
	BCH_ERRCODES()
#undef x
	NULL
};

#define BCH_ERR_0	0

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

int __bch2_err_class(int err)
{
	err = -err;
	BUG_ON((unsigned) err >= BCH_ERR_MAX);

	while (err >= BCH_ERR_START && bch2_errcode_parents[err - BCH_ERR_START])
		err = bch2_errcode_parents[err - BCH_ERR_START];

	return -err;
}
