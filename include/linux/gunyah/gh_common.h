/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#ifndef __GH_COMMON_H
#define __GH_COMMON_H

#include <linux/types.h>

/* Common Gunyah types */
typedef u16 gh_vmid_t;
typedef u32 gh_rm_msgid_t;
typedef u32 gh_virq_handle_t;
typedef u32 gh_label_t;
typedef u32 gh_memparcel_handle_t;
typedef u64 gh_capid_t;
typedef u64 gh_dbl_flags_t;

struct gh_vminfo {
	u8 *guid;
	char *uri;
	char *name;
	char *sign_auth;
};

/* Common Gunyah macros */
#define GH_CAPID_INVAL	U64_MAX
#define GH_VMID_INVAL	U16_MAX

enum gh_vm_names {
	/*
	 * GH_SELF_VM is an alias for VMID 0. Useful for RM APIs which allow
	 * operations on current VM such as console
	 */
	GH_SELF_VM,
	GH_PRIMARY_VM,
	GH_TRUSTED_VM,
	GH_CPUSYS_VM,
	GH_OEM_VM,
	GH_AUTO_VM,
	GH_AUTO_VM_LV,
	GH_VM_MAX
};

#endif
