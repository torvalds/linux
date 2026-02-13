/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 IBM Corporation
 */
#include <linux/module.h>
#include <linux/ima.h>
#include <linux/secure_boot.h>

/* secureboot arch rules */
static const char * const sb_arch_rules[] = {
#if !IS_ENABLED(CONFIG_KEXEC_SIG)
	"appraise func=KEXEC_KERNEL_CHECK appraise_type=imasig",
#endif /* CONFIG_KEXEC_SIG */
	"measure func=KEXEC_KERNEL_CHECK",
#if !IS_ENABLED(CONFIG_MODULE_SIG)
	"appraise func=MODULE_CHECK appraise_type=imasig",
#endif
#if IS_ENABLED(CONFIG_INTEGRITY_MACHINE_KEYRING) && IS_ENABLED(CONFIG_IMA_KEYRINGS_PERMIT_SIGNED_BY_BUILTIN_OR_SECONDARY)
	"appraise func=POLICY_CHECK appraise_type=imasig",
#endif
	"measure func=MODULE_CHECK",
	NULL
};

const char * const *arch_get_ima_policy(void)
{
	if (IS_ENABLED(CONFIG_IMA_ARCH_POLICY) && arch_get_secureboot()) {
		if (IS_ENABLED(CONFIG_MODULE_SIG))
			set_module_sig_enforced();
		if (IS_ENABLED(CONFIG_KEXEC_SIG))
			set_kexec_sig_enforced();
		return sb_arch_rules;
	}
	return NULL;
}
