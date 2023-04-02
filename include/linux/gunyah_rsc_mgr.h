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
#define GH_MEM_HANDLE_INVAL	U32_MAX

struct gh_rm;
int gh_rm_call(struct gh_rm *rm, u32 message_id, void *req_buff, size_t req_buff_size,
		void **resp_buf, size_t *resp_buff_size);
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
	/**
	 * RM doesn't have a state where load partially failed because
	 * only Linux
	 */
	GH_RM_VM_STATUS_LOAD_FAILED	= -1,

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

#define GH_RM_ACL_X		BIT(0)
#define GH_RM_ACL_W		BIT(1)
#define GH_RM_ACL_R		BIT(2)

struct gh_rm_mem_acl_entry {
	__le16 vmid;
	u8 perms;
	u8 reserved;
} __packed;

struct gh_rm_mem_entry {
	__le64 ipa_base;
	__le64 size;
} __packed;

enum gh_rm_mem_type {
	GH_RM_MEM_TYPE_NORMAL	= 0,
	GH_RM_MEM_TYPE_IO	= 1,
};

/*
 * struct gh_rm_mem_parcel - Package info about memory to be lent/shared/donated/reclaimed
 * @mem_type: The type of memory: normal (DDR) or IO
 * @label: An client-specified identifier which can be used by the other VMs to identify the purpose
 *         of the memory parcel.
 * @acl_entries: An array of access control entries. Each entry specifies a VM and what access
 *               is allowed for the memory parcel.
 * @n_acl_entries: Count of the number of entries in the `acl_entries` array.
 * @mem_entries: An list of regions to be associated with the memory parcel. Addresses should be
 *               (intermediate) physical addresses from Linux's perspective.
 * @n_mem_entries: Count of the number of entries in the `mem_entries` array.
 * @mem_handle: On success, filled with memory handle that RM allocates for this memory parcel
 */
struct gh_rm_mem_parcel {
	enum gh_rm_mem_type mem_type;
	u32 label;
	size_t n_acl_entries;
	struct gh_rm_mem_acl_entry *acl_entries;
	size_t n_mem_entries;
	struct gh_rm_mem_entry *mem_entries;
	u32 mem_handle;
};

/* RPC Calls */
int gh_rm_mem_lend(struct gh_rm *rm, struct gh_rm_mem_parcel *parcel);
int gh_rm_mem_share(struct gh_rm *rm, struct gh_rm_mem_parcel *parcel);
int gh_rm_mem_reclaim(struct gh_rm *rm, struct gh_rm_mem_parcel *parcel);

int gh_rm_alloc_vmid(struct gh_rm *rm, u16 vmid);
int gh_rm_dealloc_vmid(struct gh_rm *rm, u16 vmid);
int gh_rm_vm_reset(struct gh_rm *rm, u16 vmid);
int gh_rm_vm_start(struct gh_rm *rm, u16 vmid);
int gh_rm_vm_stop(struct gh_rm *rm, u16 vmid);
int gh_rm_vm_set_firmware_mem(struct gh_rm *rm, u16 vmid, struct gh_rm_mem_parcel *parcel,
				u64 fw_offset, u64 fw_size);

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

struct gh_resource *gh_rm_alloc_resource(struct gh_rm *rm, struct gh_rm_hyp_resource *hyp_resource);
void gh_rm_free_resource(struct gh_resource *ghrsc);

struct gh_rm_platform_ops {
	int (*pre_mem_share)(struct gh_rm *rm, struct gh_rm_mem_parcel *mem_parcel);
	int (*post_mem_reclaim)(struct gh_rm *rm, struct gh_rm_mem_parcel *mem_parcel);
};

#if IS_ENABLED(CONFIG_GUNYAH_PLATFORM_HOOKS)
int gh_rm_register_platform_ops(struct gh_rm_platform_ops *platform_ops);
void gh_rm_unregister_platform_ops(struct gh_rm_platform_ops *platform_ops);
int devm_gh_rm_register_platform_ops(struct device *dev, struct gh_rm_platform_ops *ops);
#else
static inline int gh_rm_register_platform_ops(struct gh_rm_platform_ops *platform_ops)
	{ return 0; }
static inline void gh_rm_unregister_platform_ops(struct gh_rm_platform_ops *platform_ops) { }
static inline int devm_gh_rm_register_platform_ops(struct device *dev,
	struct gh_rm_platform_ops *ops) { return 0; }
#endif

#endif
