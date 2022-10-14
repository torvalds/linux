/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _GUNYAH_RSC_MGR_H
#define _GUNYAH_RSC_MGR_H

#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/gunyah.h>

#define GH_VMID_INVAL	U16_MAX

struct gh_rm;
int gh_rm_notifier_register(struct gh_rm *rm, struct notifier_block *nb);
int gh_rm_notifier_unregister(struct gh_rm *rm, struct notifier_block *nb);
struct device *gh_rm_get(struct gh_rm *rm);
void gh_rm_put(struct gh_rm *rm);

struct gh_rm_vm_exited_payload {
	__le16 vmid;
	__le16 exit_type;
	__le32 exit_reason_size;
	u8 exit_reason[];
} __packed;

#define GH_RM_NOTIFICATION_VM_EXITED		 0x56100001

enum gh_rm_vm_status {
	GH_RM_VM_STATUS_NO_STATE	= 0,
	GH_RM_VM_STATUS_INIT		= 1,
	GH_RM_VM_STATUS_READY		= 2,
	GH_RM_VM_STATUS_RUNNING		= 3,
	GH_RM_VM_STATUS_PAUSED		= 4,
	GH_RM_VM_STATUS_LOAD		= 5,
	GH_RM_VM_STATUS_AUTH		= 6,
	GH_RM_VM_STATUS_INIT_FAILED	= 8,
	GH_RM_VM_STATUS_EXITED		= 9,
	GH_RM_VM_STATUS_RESETTING	= 10,
	GH_RM_VM_STATUS_RESET		= 11,
};

struct gh_rm_vm_status_payload {
	__le16 vmid;
	u16 reserved;
	u8 vm_status;
	u8 os_status;
	__le16 app_status;
} __packed;

#define GH_RM_NOTIFICATION_VM_STATUS		 0x56100008

/* RPC Calls */
int gh_rm_alloc_vmid(struct gh_rm *rm, u16 vmid);
int gh_rm_dealloc_vmid(struct gh_rm *rm, u16 vmid);
int gh_rm_vm_reset(struct gh_rm *rm, u16 vmid);
int gh_rm_vm_start(struct gh_rm *rm, u16 vmid);
int gh_rm_vm_stop(struct gh_rm *rm, u16 vmid);

enum gh_rm_vm_auth_mechanism {
	GH_RM_VM_AUTH_NONE		= 0,
	GH_RM_VM_AUTH_QCOM_PIL_ELF	= 1,
	GH_RM_VM_AUTH_QCOM_ANDROID_PVM	= 2,
};

int gh_rm_vm_configure(struct gh_rm *rm, u16 vmid, enum gh_rm_vm_auth_mechanism auth_mechanism,
			u32 mem_handle, u64 image_offset, u64 image_size,
			u64 dtb_offset, u64 dtb_size);
int gh_rm_vm_init(struct gh_rm *rm, u16 vmid);

struct gh_rm_hyp_resource {
	u8 type;
	u8 reserved;
	__le16 partner_vmid;
	__le32 resource_handle;
	__le32 resource_label;
	__le64 cap_id;
	__le32 virq_handle;
	__le32 virq;
	__le64 base;
	__le64 size;
} __packed;

struct gh_rm_hyp_resources {
	__le32 n_entries;
	struct gh_rm_hyp_resource entries[];
} __packed;

int gh_rm_get_hyp_resources(struct gh_rm *rm, u16 vmid,
				struct gh_rm_hyp_resources **resources);
int gh_rm_get_vmid(struct gh_rm *rm, u16 *vmid);

#endif
