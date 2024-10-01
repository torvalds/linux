// SPDX-License-Identifier: GPL-2.0-or-later
/* Module signature checker
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/module_signature.h>
#include <linux/string.h>
#include <linux/verification.h>
#include <linux/security.h>
#include <crypto/public_key.h>
#include <uapi/linux/module.h>
#include "internal.h"

#undef MODULE_PARAM_PREFIX
#define MODULE_PARAM_PREFIX "module."

/*
 * ANDROID: GKI:
 * Only enforce signature if SIG_PROTECT is not set
 */
#ifndef CONFIG_MODULE_SIG_PROTECT
static bool sig_enforce = IS_ENABLED(CONFIG_MODULE_SIG_FORCE);
module_param(sig_enforce, bool_enable_only, 0644);
void set_module_sig_enforced(void)
{
	sig_enforce = true;
}
#else
#define sig_enforce false
#endif

/*
 * Export sig_enforce kernel cmdline parameter to allow other subsystems rely
 * on that instead of directly to CONFIG_MODULE_SIG_FORCE config.
 */
bool is_module_sig_enforced(void)
{
	return sig_enforce;
}
EXPORT_SYMBOL(is_module_sig_enforced);

/*
 * Verify the signature on a module.
 */
int mod_verify_sig(const void *mod, struct load_info *info)
{
	struct module_signature ms;
	size_t sig_len, modlen = info->len;
	int ret;

	pr_devel("==>%s(,%zu)\n", __func__, modlen);

	if (modlen <= sizeof(ms))
		return -EBADMSG;

	memcpy(&ms, mod + (modlen - sizeof(ms)), sizeof(ms));

	ret = mod_check_sig(&ms, modlen, "module");
	if (ret)
		return ret;

	sig_len = be32_to_cpu(ms.sig_len);
	modlen -= sig_len + sizeof(ms);
	info->len = modlen;

	return verify_pkcs7_signature(mod, modlen, mod + modlen, sig_len,
				      VERIFY_USE_SECONDARY_KEYRING,
				      VERIFYING_MODULE_SIGNATURE,
				      NULL, NULL);
}

int module_sig_check(struct load_info *info, int flags)
{
	int err = -ENODATA;
	const unsigned long markerlen = sizeof(MODULE_SIG_STRING) - 1;
	const char *reason;
	const void *mod = info->hdr;
	bool mangled_module = flags & (MODULE_INIT_IGNORE_MODVERSIONS |
				       MODULE_INIT_IGNORE_VERMAGIC);
	/*
	 * Do not allow mangled modules as a module with version information
	 * removed is no longer the module that was signed.
	 */
	if (!mangled_module &&
	    info->len > markerlen &&
	    memcmp(mod + info->len - markerlen, MODULE_SIG_STRING, markerlen) == 0) {
		/* We truncate the module to discard the signature */
		info->len -= markerlen;
		err = mod_verify_sig(mod, info);
		if (!err) {
			info->sig_ok = true;
			return 0;
		}
	}

	/*
	 * We don't permit modules to be loaded into the trusted kernels
	 * without a valid signature on them, but if we're not enforcing,
	 * certain errors are non-fatal.
	 */
	switch (err) {
	case -ENODATA:
		reason = "unsigned module";
		break;
	case -ENOPKG:
		reason = "module with unsupported crypto";
		break;
	case -ENOKEY:
		reason = "module with unavailable key";
		break;

	default:
		/*
		 * All other errors are fatal, including lack of memory,
		 * unparseable signatures, and signature check failures --
		 * even if signatures aren't required.
		 */
		return err;
	}

	if (is_module_sig_enforced()) {
		pr_notice("Loading of %s is rejected\n", reason);
		return -EKEYREJECTED;
	}

/*
 * ANDROID: GKI: Do not prevent loading of unsigned modules;
 * as all modules except GKI modules are not signed.
 */
#ifndef CONFIG_MODULE_SIG_PROTECT
	return security_locked_down(LOCKDOWN_MODULE_SIGNATURE);
#else
	return 0;
#endif
}
