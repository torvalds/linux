// SPDX-License-Identifier: GPL-1.0+
/*
 * Copyright (C) 2018 IBM Corporation
 */
#include <linux/efi.h>
#include <linux/secure_boot.h>
#include <asm/efi.h>

#ifndef arch_efi_boot_mode
#define arch_efi_boot_mode efi_secureboot_mode_unset
#endif

static enum efi_secureboot_mode get_sb_mode(void)
{
	enum efi_secureboot_mode mode;

	if (!efi_rt_services_supported(EFI_RT_SUPPORTED_GET_VARIABLE)) {
		pr_info("integrity: secureboot mode unknown, no efi\n");
		return efi_secureboot_mode_unknown;
	}

	mode = efi_get_secureboot_mode(efi.get_variable);
	if (mode == efi_secureboot_mode_disabled)
		pr_info("integrity: secureboot mode disabled\n");
	else if (mode == efi_secureboot_mode_unknown)
		pr_info("integrity: secureboot mode unknown\n");
	else
		pr_info("integrity: secureboot mode enabled\n");
	return mode;
}

/*
 * Query secure boot status
 *
 * Note don't call this function too early e.g. in __setup hook otherwise the
 * kernel may hang when calling efi_get_secureboot_mode.
 *
 */
bool arch_get_secureboot(void)
{
	static enum efi_secureboot_mode sb_mode;
	static bool initialized;

	if (!initialized && efi_enabled(EFI_BOOT)) {
		sb_mode = arch_efi_boot_mode;

		if (sb_mode == efi_secureboot_mode_unset)
			sb_mode = get_sb_mode();
		initialized = true;
	}

	if (sb_mode == efi_secureboot_mode_enabled)
		return true;
	else
		return false;
}
