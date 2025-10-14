// SPDX-License-Identifier: GPL-2.0

#include <linux/security.h>

#ifndef CONFIG_SECURITY
void rust_helper_security_cred_getsecid(const struct cred *c, u32 *secid)
{
	security_cred_getsecid(c, secid);
}

int rust_helper_security_secid_to_secctx(u32 secid, struct lsm_context *cp)
{
	return security_secid_to_secctx(secid, cp);
}

void rust_helper_security_release_secctx(struct lsm_context *cp)
{
	security_release_secctx(cp);
}

int rust_helper_security_binder_set_context_mgr(const struct cred *mgr)
{
	return security_binder_set_context_mgr(mgr);
}

int rust_helper_security_binder_transaction(const struct cred *from,
					    const struct cred *to)
{
	return security_binder_transaction(from, to);
}

int rust_helper_security_binder_transfer_binder(const struct cred *from,
						const struct cred *to)
{
	return security_binder_transfer_binder(from, to);
}

int rust_helper_security_binder_transfer_file(const struct cred *from,
					      const struct cred *to,
					      const struct file *file)
{
	return security_binder_transfer_file(from, to, file);
}
#endif
