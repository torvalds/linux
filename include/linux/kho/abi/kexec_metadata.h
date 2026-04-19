/* SPDX-License-Identifier: GPL-2.0-only */

/**
 * DOC: Kexec Metadata ABI
 *
 * The "kexec-metadata" subtree stores optional metadata about the kexec chain.
 * It is registered via kho_add_subtree(), keeping it independent from the core
 * KHO ABI. This allows the metadata format to evolve without affecting other
 * KHO consumers.
 *
 * The metadata is stored as a plain C struct rather than FDT format for
 * simplicity and direct field access.
 *
 * Copyright (c) 2026 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2026 Breno Leitao <leitao@debian.org>
 */

#ifndef _LINUX_KHO_ABI_KEXEC_METADATA_H
#define _LINUX_KHO_ABI_KEXEC_METADATA_H

#include <linux/types.h>
#include <linux/utsname.h>

#define KHO_KEXEC_METADATA_VERSION 1

/**
 * struct kho_kexec_metadata - Kexec metadata passed between kernels
 * @version: ABI version of this struct (must be first field)
 * @previous_release: Kernel version string that initiated the kexec
 * @kexec_count: Number of kexec boots since last cold boot
 *
 * This structure is preserved across kexec and allows the new kernel to
 * identify which kernel it was booted from and how many kexec reboots
 * have occurred.
 *
 * __NEW_UTS_LEN is part of uABI, so it safe to use it in here.
 */
struct kho_kexec_metadata {
	u32 version;
	char previous_release[__NEW_UTS_LEN + 1];
	u32 kexec_count;
} __packed;

#define KHO_METADATA_NODE_NAME "kexec-metadata"

#endif /* _LINUX_KHO_ABI_KEXEC_METADATA_H */
