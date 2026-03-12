// SPDX-License-Identifier: GPL-2.0

#include <linux/errname.h>

__rust_helper const char *rust_helper_errname(int err)
{
	return errname(err);
}
