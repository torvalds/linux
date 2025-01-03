// SPDX-License-Identifier: GPL-2.0

#include <linux/security.h>

#ifndef CONFIG_SECURITY
void rust_helper_security_cred_getsecid(const struct cred *c, u32 *secid)
{
	security_cred_getsecid(c, secid);
}

int rust_helper_security_secid_to_secctx(u32 secid, char **secdata, u32 *seclen)
{
	return security_secid_to_secctx(secid, secdata, seclen);
}

void rust_helper_security_release_secctx(char *secdata, u32 seclen)
{
	security_release_secctx(secdata, seclen);
}
#endif
