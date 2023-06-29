/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#ifndef __GH_RM_DRV_H
#define __GH_RM_DRV_H

#include <linux/types.h>
#include <linux/notifier.h>
#include <linux/fwnode.h>
#include <linux/gunyah_rsc_mgr.h>
#include <linux/range.h>

#include "gh_common.h"

/* Notification type Message IDs */
/* Memory APIs */
#define GH_RM_NOTIF_MEM_SHARED		0x51100011
#define GH_RM_NOTIF_MEM_RELEASED	0x51100012
#define GH_RM_NOTIF_MEM_ACCEPTED	0x51100013

#define GH_RM_MEM_TYPE_NORMAL	0
#define GH_RM_MEM_TYPE_IO	1

#define GH_RM_TRANS_TYPE_DONATE	0
#define GH_RM_TRANS_TYPE_LEND	1
#define GH_RM_TRANS_TYPE_SHARE	2

#define GH_RM_ACL_X		BIT(0)
#define GH_RM_ACL_W		BIT(1)
#define GH_RM_ACL_R		BIT(2)

#define GH_RM_MEM_RELEASE_CLEAR BIT(0)
#define GH_RM_MEM_RECLAIM_CLEAR BIT(0)

#define GH_RM_MEM_ACCEPT_VALIDATE_SANITIZED	BIT(0)
#define GH_RM_MEM_ACCEPT_VALIDATE_ACL_ATTRS	BIT(1)
#define GH_RM_MEM_ACCEPT_VALIDATE_LABEL		BIT(2)
#define GH_RM_MEM_ACCEPT_MAP_IPA_CONTIGUOUS	BIT(4)
#define GH_RM_MEM_ACCEPT_DONE			BIT(7)

#define GH_RM_MEM_SHARE_SANITIZE		BIT(0)
#define GH_RM_MEM_SHARE_APPEND			BIT(1)
#define GH_RM_MEM_LEND_SANITIZE			BIT(0)
#define GH_RM_MEM_LEND_APPEND			BIT(1)
#define GH_RM_MEM_DONATE_SANITIZE		BIT(0)
#define GH_RM_MEM_DONATE_APPEND			BIT(1)

#define GH_RM_MEM_APPEND_END			BIT(0)

#define GH_RM_MEM_NOTIFY_RECIPIENT_SHARED	BIT(0)
#define GH_RM_MEM_NOTIFY_RECIPIENT	GH_RM_MEM_NOTIFY_RECIPIENT_SHARED
#define GH_RM_MEM_NOTIFY_OWNER_RELEASED		BIT(1)
#define GH_RM_MEM_NOTIFY_OWNER		GH_RM_MEM_NOTIFY_OWNER_RELEASED
#define GH_RM_MEM_NOTIFY_OWNER_ACCEPTED		BIT(2)

/* Support may vary across hardware platforms */
#define GH_RM_IPA_RESERVE_ECC			BIT(0)
#define GH_RM_IPA_RESERVE_MEMTAG		BIT(1)
#define GH_RM_IPA_RESERVE_NORMAL		BIT(2)
#define GH_RM_IPA_RESERVE_IO			BIT(3)
/* BIT(4) and BIT(5) reserved */
/* The calling VM's default memory type */
#define GH_RM_IPA_RESERVE_DEFAULT		BIT(6)
#define GH_RM_IPA_RESERVE_VALID_FLAGS		(GENMASK(3, 0) | BIT(6))

#define GH_RM_IPA_RESERVE_PLATFORM_ENCRYPTED		BIT(0)
#define GH_RM_IPA_RESERVE_PLATFORM_AUTHENTICATED	BIT(1)
#define GH_RM_IPA_RESERVE_PLATFORM_ANTI_ROLLBACK	BIT(2)
#define GH_RM_IPA_RESERVE_PLATFORM_VALID_FLAGS		GENMASK(2, 0)

#define MAX_EXIT_REASON_SIZE			4

struct gh_rm_mem_shared_acl_entry;
struct gh_rm_mem_shared_sgl_entry;
struct gh_rm_mem_shared_attr_entry;

struct gh_rm_notif_mem_shared_payload {
	u32 mem_handle;
	u8 mem_type;
	u8 trans_type;
	u8 flags;
	u8 reserved1;
	u16 owner_vmid;
	u16 reserved2;
	u32 label;
	gh_label_t mem_info_tag;
	/* TODO: How to arrange multiple variable length struct arrays? */
} __packed;

struct gh_rm_mem_shared_acl_entry {
	u16 acl_vmid;
	u8 acl_rights;
	u8 reserved;
} __packed;

struct gh_rm_mem_shared_sgl_entry {
	u32 sgl_size_low;
	u32 sgl_size_high;
} __packed;

struct gh_rm_mem_shared_attr_entry {
	u16 attributes;
	u16 attributes_vmid;
} __packed;

struct gh_rm_notif_mem_released_payload {
	u32 mem_handle;
	u16 participant_vmid;
	u16 reserved;
	gh_label_t mem_info_tag;
} __packed;

struct gh_rm_notif_mem_accepted_payload {
	u32 mem_handle;
	u16 participant_vmid;
	u16 reserved;
	gh_label_t mem_info_tag;
} __packed;

struct gh_acl_entry {
	u16 vmid;
	u8 perms;
	u8 reserved;
} __packed;

struct gh_sgl_entry {
	u64 ipa_base;
	u64 size;
} __packed;

struct gh_mem_attr_entry {
	u16 attr;
	u16 vmid;
} __packed;

struct gh_acl_desc {
	u32 n_acl_entries;
	struct gh_acl_entry acl_entries[];
} __packed;

struct gh_sgl_desc {
	u16 n_sgl_entries;
	u16 reserved;
	struct gh_sgl_entry sgl_entries[];
} __packed;

struct gh_mem_attr_desc {
	u16 n_mem_attr_entries;
	u16 reserved;
	struct gh_mem_attr_entry attr_entries[];
} __packed;

struct gh_notify_vmid_entry {
	u16 vmid;
	u16 reserved;
} __packed;

struct gh_notify_vmid_desc {
	u16 n_vmid_entries;
	u16 reserved;
	struct gh_notify_vmid_entry vmid_entries[];
} __packed;

/* VM APIs */
#define GH_RM_NOTIF_VM_EXITED		0x56100001
#define GH_RM_NOTIF_VM_SHUTDOWN		0x56100002
#define GH_RM_NOTIF_VM_STATUS		0x56100008
#define GH_RM_NOTIF_VM_IRQ_LENT		0x56100011
#define GH_RM_NOTIF_VM_IRQ_RELEASED	0x56100012
#define GH_RM_NOTIF_VM_IRQ_ACCEPTED	0x56100013

/* AUTH mechanisms */
#define GH_VM_UNAUTH			0
#define GH_VM_AUTH_PIL_ELF		1
#define GH_VM_AUTH_ANDROID_PVM		2

/* AUTH_PARAM_TYPE mechanisms */
#define GH_VM_AUTH_PARAM_PAS_ID		0 /* Used to pass peripheral auth id */

#define GH_RM_VM_STATUS_NO_STATE	0
#define GH_RM_VM_STATUS_INIT		1
#define GH_RM_VM_STATUS_READY		2
#define GH_RM_VM_STATUS_RUNNING		3
#define GH_RM_VM_STATUS_PAUSED		4
#define GH_RM_VM_STATUS_LOAD		5
#define GH_RM_VM_STATUS_AUTH		6
/* 7 is reserved */
#define GH_RM_VM_STATUS_INIT_FAILED	8
#define GH_RM_VM_STATUS_EXITED		9
#define GH_RM_VM_STATUS_RESETTING	10
#define GH_RM_VM_STATUS_RESET		11

#define GH_RM_OS_STATUS_NONE		0
#define GH_RM_OS_STATUS_EARLY_BOOT	1
#define GH_RM_OS_STATUS_BOOT		2
#define GH_RM_OS_STATUS_INIT		3
#define GH_RM_OS_STATUS_RUN		4

#define GH_RM_APP_STATUS_TUI_SERVICE_BOOT	1

#define GH_RM_VM_STOP_FLAG_FORCE_STOP				0x01

#define GH_RM_VM_EXIT_TYPE_VM_EXIT				0
#define GH_RM_VM_EXIT_TYPE_SYSTEM_OFF		1
#define GH_RM_VM_EXIT_TYPE_SYSTEM_RESET		2
#define GH_RM_VM_EXIT_TYPE_SYSTEM_RESET2	3
#define GH_RM_VM_EXIT_TYPE_WDT_BITE				4
#define GH_RM_VM_EXIT_TYPE_HYP_ERROR			5
#define GH_RM_VM_EXIT_TYPE_ASYNC_EXT_ABORT		6
#define GH_RM_VM_EXIT_TYPE_VM_STOP_FORCED		7

/* GH_RM_VM_EXIT_TYPE_VM_EXIT */
struct gh_vm_exit_reason_vm_exit {
	u16 exit_flags;
	/* GH_VM_EXIT_EXIT_FLAG_* are bit representations */
#define GH_VM_EXIT_EXIT_FLAG_TYPE	0x1
#define GH_VM_EXIT_POWEROFF	0 /* Value at bit:0 */
#define GH_VM_EXIT_RESTART	1 /* Value at bit:0 */
#define GH_VM_EXIT_EXIT_FLAG_SYSTEM	0x2
#define GH_VM_EXIT_EXIT_FLAG_WARM	0x4
#define GH_VM_EXIT_EXIT_FLAG_DUMP	0x8

	u8 exit_code;
	/* Exit codes */
#define GH_VM_EXIT_CODE_NORMAL	0
#define GH_VM_EXIT_SOFTWARE_ERR	1
#define GH_VM_EXIT_BUS_ERR		2
#define GH_VM_EXIT_DEVICE_ERR	3

	u8 reserved;
} __packed;

/* Reasons for VM_STOP */
#define GH_VM_STOP_SHUTDOWN					0
#define GH_VM_STOP_RESTART					1
#define GH_VM_STOP_CRASH					2
#define GH_VM_STOP_FORCE_STOP					3
#define GH_VM_STOP_MAX						4
struct gh_rm_notif_vm_exited_payload {
	gh_vmid_t vmid;
	u16 exit_type;
	u32 exit_reason_size;
	u32 exit_reason[0];
} __packed;

struct gh_rm_notif_vm_shutdown_payload {
	u32 stop_reason;
} __packed;

struct gh_rm_notif_vm_status_payload {
	gh_vmid_t vmid;
	u16 reserved;
	u8 vm_status;
	u8 os_status;
	u16 app_status;
} __packed;

struct gh_rm_notif_vm_irq_lent_payload {
	gh_vmid_t owner_vmid;
	u16 reserved;
	gh_virq_handle_t virq_handle;
	gh_label_t virq_label;
} __packed;

struct gh_rm_notif_vm_irq_released_payload {
	gh_virq_handle_t virq_handle;
} __packed;

struct gh_rm_notif_vm_irq_accepted_payload {
	gh_virq_handle_t virq_handle;
} __packed;

struct gh_vm_auth_param_entry {
	u32 auth_param_type;
	u32 auth_param;
} __packed;

/* Arch specific APIs */
#if IS_ENABLED(CONFIG_GH_ARM64_DRV)
/* IRQ APIs */
int gh_get_irq(u32 virq, u32 type, struct fwnode_handle *handle);
int gh_put_irq(int irq);
int gh_get_virq(int base_virq, int virq);
int gh_put_virq(int irq);
int gh_arch_validate_vm_exited_notif(size_t payload_size,
	struct gh_rm_notif_vm_exited_payload *payload);
#else
static inline int gh_get_irq(u32 virq, u32 type,
					struct fwnode_handle *handle)
{
	return -EINVAL;
}
static inline int gh_put_irq(int irq)
{
	return -EINVAL;
}
static inline int gh_get_virq(int base_virq, int virq)
{
	return -EINVAL;
}
static inline int gh_put_virq(int irq)
{
	return -EINVAL;
}
static inline int gh_arch_validate_vm_exited_notif(size_t payload_size,
	struct gh_rm_notif_vm_exited_payload *payload)
{
	return -EINVAL;
}
#endif
/* VM Services */
#define GH_RM_NOTIF_VM_CONSOLE_CHARS	0X56100080

struct gh_rm_notif_vm_console_chars {
	gh_vmid_t vmid;
	u16 num_bytes;
	u8 bytes[0];
} __packed;

struct gh_vm_status {
	u8 vm_status;
	u8 os_status;
	u16 app_status;
} __packed;

struct notifier_block;

typedef int (*gh_virtio_mmio_cb_t)(gh_vmid_t peer, const char *vm_name,
	gh_label_t label, gh_capid_t cap_id, int linux_irq, u64 base, u64 size);
typedef int (*gh_wdog_manage_cb_t)(gh_vmid_t vmid, gh_capid_t cap_id, bool populate);
typedef int (*gh_vcpu_affinity_set_cb_t)(gh_vmid_t vmid, gh_label_t label,
						gh_capid_t cap_id, int linux_irq);
typedef int (*gh_vcpu_affinity_reset_cb_t)(gh_vmid_t vmid, gh_label_t label,
						gh_capid_t cap_id, int *linux_irq);
typedef int (*gh_vpm_grp_set_cb_t)(gh_vmid_t vmid, gh_capid_t cap_id, int linux_irq);
typedef int (*gh_vpm_grp_reset_cb_t)(gh_vmid_t vmid, int *linux_irq);
typedef void (*gh_all_res_populated_cb_t)(gh_vmid_t vmid, bool res_populated);

#if IS_ENABLED(CONFIG_GH_RM_DRV)
/* RM client registration APIs */
int gh_rm_register_notifier(struct notifier_block *nb);
int gh_rm_unregister_notifier(struct notifier_block *nb);

/* Client APIs for IRQ management */
int gh_rm_virq_to_irq(u32 virq, u32 type);
int gh_rm_irq_to_virq(int irq, u32 *virq);

int gh_rm_vm_irq_lend(gh_vmid_t vmid,
		      int virq,
		      int label,
		      gh_virq_handle_t *virq_handle);
int gh_rm_vm_irq_lend_notify(gh_vmid_t vmid, gh_virq_handle_t virq_handle);
int gh_rm_vm_irq_accept(gh_virq_handle_t virq_handle, int virq);
int gh_rm_vm_irq_accept_notify(gh_vmid_t vmid, gh_virq_handle_t virq_handle);
int gh_rm_vm_irq_release(gh_virq_handle_t virq_handle);
int gh_rm_vm_irq_release_notify(gh_vmid_t vmid, gh_virq_handle_t virq_handle);


int gh_rm_vm_irq_reclaim(gh_virq_handle_t virq_handle);

int gh_rm_set_virtio_mmio_cb(gh_virtio_mmio_cb_t fnptr);
void gh_rm_unset_virtio_mmio_cb(void);
int gh_rm_set_wdog_manage_cb(gh_wdog_manage_cb_t fnptr);
int gh_rm_set_vcpu_affinity_cb(gh_vcpu_affinity_set_cb_t fnptr);
int gh_rm_reset_vcpu_affinity_cb(gh_vcpu_affinity_reset_cb_t fnptr);
int gh_rm_set_vpm_grp_cb(gh_vpm_grp_set_cb_t fnptr);
int gh_rm_reset_vpm_grp_cb(gh_vpm_grp_reset_cb_t fnptr);
int gh_rm_all_res_populated_cb(gh_all_res_populated_cb_t fnptr);

/* Client APIs for VM management */
int gh_rm_vm_alloc_vmid(enum gh_vm_names vm_name, int *vmid);
int gh_rm_vm_dealloc_vmid(gh_vmid_t vmid);
int gh_rm_vm_config_image(gh_vmid_t vmid, u16 auth_mech, u32 mem_handle,
	u64 image_offset, u64 image_size, u64 dtb_offset, u64 dtb_size);
int gh_rm_vm_auth_image(gh_vmid_t vmid, ssize_t n_entries,
				struct gh_vm_auth_param_entry *entry);
int ghd_rm_vm_init(gh_vmid_t vmid);
int ghd_rm_get_vmid(enum gh_vm_names vm_name, gh_vmid_t *vmid);
int gh_rm_get_vm_id_info(gh_vmid_t vmid);
int gh_rm_get_vm_name(gh_vmid_t vmid, enum gh_vm_names *vm_name);
int gh_rm_get_vminfo(enum gh_vm_names vm_name, struct gh_vminfo *vminfo);
int ghd_rm_vm_start(int vmid);
enum gh_vm_names gh_get_image_name(const char *str);
enum gh_vm_names gh_get_vm_name(const char *str);
int gh_rm_get_this_vmid(gh_vmid_t *vmid);
int ghd_rm_vm_stop(gh_vmid_t vmid, u32 stop_reason, u8 flags);
int ghd_rm_vm_reset(gh_vmid_t vmid);

/* Client APIs for VM query */
int gh_rm_populate_hyp_res(gh_vmid_t vmid, const char *vm_name);
int gh_rm_unpopulate_hyp_res(gh_vmid_t vmid, const char *vm_name);

/* Client APIs for VM Services */
struct gh_vm_status *gh_rm_vm_get_status(gh_vmid_t vmid);
int gh_rm_vm_set_status(struct gh_vm_status gh_vm_status);
int gh_rm_vm_set_vm_status(u8 vm_status);
int gh_rm_vm_set_os_status(u8 os_status);
int gh_rm_vm_set_app_status(u16 app_status);
int gh_rm_console_open(gh_vmid_t vmid);
int gh_rm_console_close(gh_vmid_t vmid);
int gh_rm_console_write(gh_vmid_t vmid, const char *buf, size_t size);
int gh_rm_console_flush(gh_vmid_t vmid);
int gh_rm_mem_qcom_lookup_sgl(u8 mem_type, gh_label_t label,
			      struct gh_acl_desc *acl_desc,
			      struct gh_sgl_desc *sgl_desc,
			      struct gh_mem_attr_desc *mem_attr_desc,
			      gh_memparcel_handle_t *handle);
int gh_rm_mem_release(gh_memparcel_handle_t handle, u8 flags);
int ghd_rm_mem_reclaim(gh_memparcel_handle_t handle, u8 flags);
struct gh_sgl_desc *gh_rm_mem_accept(gh_memparcel_handle_t handle, u8 mem_type,
				     u8 trans_type, u8 flags, gh_label_t label,
				     struct gh_acl_desc *acl_desc,
				     struct gh_sgl_desc *sgl_desc,
				     struct gh_mem_attr_desc *mem_attr_desc,
				     u16 map_vmid);
int ghd_rm_mem_share(u8 mem_type, u8 flags, gh_label_t label,
		    struct gh_acl_desc *acl_desc, struct gh_sgl_desc *sgl_desc,
		    struct gh_mem_attr_desc *mem_attr_desc,
		    gh_memparcel_handle_t *handle);
int ghd_rm_mem_lend(u8 mem_type, u8 flags, gh_label_t label,
		   struct gh_acl_desc *acl_desc, struct gh_sgl_desc *sgl_desc,
		   struct gh_mem_attr_desc *mem_attr_desc,
		   gh_memparcel_handle_t *handle);
int gh_rm_mem_donate(u8 mem_type, u8 flags, gh_label_t label,
		   struct gh_acl_desc *acl_desc, struct gh_sgl_desc *sgl_desc,
		   struct gh_mem_attr_desc *mem_attr_desc,
		   gh_memparcel_handle_t *handle);
int gh_rm_mem_notify(gh_memparcel_handle_t handle, u8 flags,
		     gh_label_t mem_info_tag,
		     struct gh_notify_vmid_desc *vmid_desc);
int gh_rm_ipa_reserve(u64 size, u64 align, struct range limits, u32 generic_constraints,
		      u32 platform_constraints, u64 *ipa);

/* API to set time base */
int gh_rm_vm_set_time_base(gh_vmid_t vmid);

/* API for minidump support */
int gh_rm_minidump_get_info(void);
int gh_rm_minidump_register_range(phys_addr_t base_ipa, size_t region_size,
				  const char *name, size_t name_size);
int gh_rm_minidump_deregister_slot(uint16_t slot_num);
int gh_rm_minidump_get_slot_from_name(uint16_t starting_slot, const char *name,
				      size_t name_size);

#else
/* RM client register notifications APIs */
static inline int gh_rm_register_notifier(struct notifier_block *nb)
{
	return -ENODEV;
}

static inline int gh_rm_unregister_notifier(struct notifier_block *nb)
{
	return -ENODEV;
}

/* Client APIs for IRQ management */
static inline int gh_rm_virq_to_irq(u32 virq)
{
	return -EINVAL;
}

static inline int gh_rm_vm_irq_lend(gh_vmid_t vmid,
				    int virq,
				    int label,
				    gh_virq_handle_t *virq_handle)
{
	return -EINVAL;
}

static inline int gh_rm_irq_to_virq(int irq, u32 *virq)
{
	return -EINVAL;
}

static inline int gh_rm_vm_irq_lend_notify(gh_vmid_t vmid,
					   gh_virq_handle_t virq_handle)
{
	return -EINVAL;
}

static inline int gh_rm_vm_irq_accept(gh_virq_handle_t virq_handle, int virq)
{
	return -EINVAL;

}

static inline int gh_rm_vm_irq_accept_notify(gh_vmid_t vmid,
					     gh_virq_handle_t virq_handle)
{
	return -EINVAL;
}

static inline int gh_rm_vm_irq_release(gh_virq_handle_t virq_handle)
{
	return -EINVAL;
}

static inline int gh_rm_vm_irq_release_notify(gh_vmid_t vmid,
					      gh_virq_handle_t virq_handle)
{
	return -EINVAL;
}

static inline int gh_rm_vm_irq_reclaim(gh_virq_handle_t virq_handle)
{
	return -EINVAL;
}

/* Client APIs for VM management */
static inline int gh_rm_vm_alloc_vmid(enum gh_vm_names vm_name, int *vmid)
{
	return -EINVAL;
}

static inline int gh_rm_vm_dealloc_vmid(gh_vmid_t vmid)
{
	return -EINVAL;
}

static inline int gh_rm_vm_config_image(gh_vmid_t vmid, u16 auth_mech,
		u32 mem_handle, u64 image_offset, u64 image_size,
		u64 dtb_offset, u64 dtb_size)
{
	return -EINVAL;
}

static inline int gh_rm_vm_auth_image(gh_vmid_t vmid, ssize_t n_entries,
				struct gh_vm_auth_param_entry *entry)
{
	return -EINVAL;
}

static inline int ghd_rm_vm_init(gh_vmid_t vmid)
{
	return -EINVAL;
}

static inline int ghd_rm_get_vmid(enum gh_vm_names vm_name, gh_vmid_t *vmid)
{
	return -EINVAL;
}

static inline int gh_rm_get_vm_name(gh_vmid_t vmid, enum gh_vm_names *vm_name)
{
	return -EINVAL;
}

static inline int gh_rm_get_this_vmid(gh_vmid_t *vmid)
{
	return -EINVAL;
}

static inline int gh_rm_get_vminfo(enum gh_vm_names vm_name, struct gh_vminfo *vminfo)
{
	return -EINVAL;
}

static inline int ghd_rm_vm_start(int vmid)
{
	return -EINVAL;
}

static inline int gh_rm_get_vm_id_info(gh_vmid_t vmid)
{
	return -EINVAL;
}

static inline int ghd_rm_vm_stop(gh_vmid_t vmid, u32 stop_reason, u8 flags)
{
	return -EINVAL;
}

static inline int ghd_rm_vm_reset(gh_vmid_t vmid)
{
	return -EINVAL;
}

/* Client APIs for VM query */
static inline int gh_rm_populate_hyp_res(gh_vmid_t vmid, const char *vm_name)
{
	return -EINVAL;
}

/* Client APIs for VM Services */
static inline struct gh_vm_status *gh_rm_vm_get_status(gh_vmid_t vmid)
{
	return ERR_PTR(-EINVAL);
}

static inline int gh_rm_vm_set_status(struct gh_vm_status gh_vm_status)
{
	return -EINVAL;
}

static inline int gh_rm_vm_set_vm_status(u8 vm_status)
{
	return -EINVAL;
}

static inline int gh_rm_vm_set_os_status(u8 os_status)
{
	return -EINVAL;
}

static inline int gh_rm_vm_set_app_status(u16 app_status)
{
	return -EINVAL;
}

static inline int gh_rm_console_open(gh_vmid_t vmid)
{
	return -EINVAL;
}

static inline int gh_rm_console_close(gh_vmid_t vmid)
{
	return -EINVAL;
}

static inline int gh_rm_console_write(gh_vmid_t vmid, const char *buf,
					size_t size)
{
	return -EINVAL;
}

static inline int gh_rm_console_flush(gh_vmid_t vmid)
{
	return -EINVAL;
}

static inline int gh_rm_mem_qcom_lookup_sgl(u8 mem_type, gh_label_t label,
			      struct gh_acl_desc *acl_desc,
			      struct gh_sgl_desc *sgl_desc,
			      struct gh_mem_attr_desc *mem_attr_desc,
			      gh_memparcel_handle_t *handle)
{
	return -EINVAL;
}

static inline int gh_rm_mem_release(gh_memparcel_handle_t handle, u8 flags)
{
	return -EINVAL;
}

static inline int ghd_rm_mem_reclaim(gh_memparcel_handle_t handle, u8 flags)
{
	return -EINVAL;
}

static inline struct gh_sgl_desc *gh_rm_mem_accept(gh_memparcel_handle_t handle,
				     u8 mem_type,
				     u8 trans_type, u8 flags, gh_label_t label,
				     struct gh_acl_desc *acl_desc,
				     struct gh_sgl_desc *sgl_desc,
				     struct gh_mem_attr_desc *mem_attr_desc,
				     u16 map_vmid)
{
	return ERR_PTR(-EINVAL);
}

static inline int ghd_rm_mem_share(u8 mem_type, u8 flags, gh_label_t label,
		    struct gh_acl_desc *acl_desc, struct gh_sgl_desc *sgl_desc,
		    struct gh_mem_attr_desc *mem_attr_desc,
		    gh_memparcel_handle_t *handle)
{
	return -EINVAL;
}

static inline int ghd_rm_mem_lend(u8 mem_type, u8 flags, gh_label_t label,
		   struct gh_acl_desc *acl_desc, struct gh_sgl_desc *sgl_desc,
		   struct gh_mem_attr_desc *mem_attr_desc,
		   gh_memparcel_handle_t *handle)
{
	return -EINVAL;
}

static inline int gh_rm_mem_notify(gh_memparcel_handle_t handle, u8 flags,
				   gh_label_t mem_info_tag,
				   struct gh_notify_vmid_desc *vmid_desc)
{
	return -EINVAL;
}

static inline int gh_rm_set_virtio_mmio_cb(gh_virtio_mmio_cb_t fnptr)
{
	return -EINVAL;
}

static inline void gh_rm_unset_virtio_mmio_cb(void)
{

}

static inline int gh_rm_set_wdog_manage_cb(gh_wdog_manage_cb_t fnptr)
{
	return -EINVAL;
}

static inline int gh_rm_set_vcpu_affinity_cb(gh_vcpu_affinity_set_cb_t fnptr)
{
	return -EINVAL;
}

static inline int gh_rm_reset_vcpu_affinity_cb(gh_vcpu_affinity_reset_cb_t fnptr)
{
	return -EINVAL;
}

static inline int gh_rm_set_vpm_grp_cb(gh_vpm_grp_set_cb_t fnptr)
{
	return -EINVAL;
}

static inline int gh_rm_reset_vpm_grp_cb(gh_vpm_grp_reset_cb_t fnptr)
{
	return -EINVAL;
}

static inline int gh_rm_all_res_populated_cb(gh_all_res_populated_cb_t fnptr)
{
	return -EINVAL;
}

/* API to set time base */
static inline int gh_rm_vm_set_time_base(gh_vmid_t vmid)
{
	return -EINVAL;
}

/* API for minidump support */
static inline int gh_rm_minidump_get_info(void)
{
	return -EINVAL;
}

static inline int gh_rm_minidump_register_range(phys_addr_t base_ipa,
					 size_t region_size, const char *name,
					 size_t name_size)
{
	return -EINVAL;
}

static inline int gh_rm_minidump_deregister_slot(uint16_t slot_num)
{
	return -EINVAL;
}

static inline int gh_rm_minidump_get_slot_from_name(uint16_t starting_slot,
						    const char *name,
						    size_t name_size)
{
	return -EINVAL;
}

static inline int gh_rm_ipa_reserve(u64 size, u64 align, struct range limits,
				    u32 generic_constraints, u32 platform_constraints,
				    u64 *ipa)
{
	return -EINVAL;
}
#endif
#endif
