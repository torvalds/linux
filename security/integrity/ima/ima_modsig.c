// SPDX-License-Identifier: GPL-2.0+
/*
 * IMA support for appraising module-style appended signatures.
 *
 * Copyright (C) 2019  IBM Corporation
 *
 * Author:
 * Thiago Jung Bauermann <bauerman@linux.ibm.com>
 */

#include "ima.h"

/**
 * ima_hook_supports_modsig - can the policy allow modsig for this hook?
 *
 * modsig is only supported by hooks using ima_post_read_file(), because only
 * they preload the contents of the file in a buffer. FILE_CHECK does that in
 * some cases, but not when reached from vfs_open(). POLICY_CHECK can support
 * it, but it's not useful in practice because it's a text file so deny.
 */
bool ima_hook_supports_modsig(enum ima_hooks func)
{
	switch (func) {
	case KEXEC_KERNEL_CHECK:
	case KEXEC_INITRAMFS_CHECK:
	case MODULE_CHECK:
		return true;
	default:
		return false;
	}
}
