// SPDX-License-Identifier: GPL-2.0

#include <linux/cred.h>

__rust_helper const struct cred *rust_helper_get_cred(const struct cred *cred)
{
	return get_cred(cred);
}

__rust_helper void rust_helper_put_cred(const struct cred *cred)
{
	put_cred(cred);
}
