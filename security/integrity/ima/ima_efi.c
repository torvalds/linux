/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 IBM Corporation
 */
#include <linux/efi.h>
#include <linux/module.h>
#include <linux/ima.h>
#include <asm/efi.h>

#ifndef arch_ima_efi_boot_mode
#define arch_ima_efi_boot_mode efi_secureboot_mode_unset
#endif

static enum efi_secureboot_mode get_sb_mode(void)
{
	enum efi_secureboot_mode mode;

	if (!efi_rt_services_supported(EFI_RT_SUPPORTED_GET_VARIABLE)) {
		pr_info("ima: secureboot mode unknown, no efi\n");
		return efi_secureboot_mode_unknown;
	}

	mode = efi_get_secureboot_mode(efi.get_variable);
	if (mode == efi_secureboot_mode_disabled)
		pr_info("ima: secureboot mode disabled\n");
	else if (mode == efi_secureboot_mode_unknown)
		pr_info("ima: secureboot mode unknown\n");
	else
		pr_info("ima: secureboot mode enabled\n");
	return mode;
}

bool arch_ima_get_secureboot(void)
{
	static enum efi_secureboot_mode sb_mode;
	static bool initialized;

	if (!initialized && efi_enabled(EFI_BOOT)) {
		sb_mode = arch_ima_efi_boot_mode;

		if (sb_mode == efi_secureboot_mode_unset)
			sb_mode = get_sb_mode();
		initialized = true;
	}

	if (sb_mode == efi_secureboot_mode_enabled)
		return true;
	else
		return false;
}

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
	if (IS_ENABLED(CONFIG_IMA_ARCH_POLICY) && arch_ima_get_secureboot()) {
		if (IS_ENABLED(CONFIG_MODULE_SIG))
			set_module_sig_enforced();
		if (IS_ENABLED(CONFIG_KEXEC_SIG))
			set_kexec_sig_enforced();
		return sb_arch_rules;
	}
	return NULL;
}
