// SPDX-License-Identifier: GPL-2.0

#include <linux/cred.h>

const struct cred *rust_helper_get_cred(const struct cred *cred)
{
	return get_cred(cred);
}

void rust_helper_put_cred(const struct cred *cred)
{
	put_cred(cred);
}
