/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Intel Corporation. */
#ifndef _LINUX_CXL_EVENT_H
#define _LINUX_CXL_EVENT_H

#include <linux/types.h>
#include <linux/uuid.h>
#include <linux/workqueue_types.h>

/*
 * Common Event Record Format
 * CXL rev 3.0 section 8.2.9.2.1; Table 8-42
 */
struct cxl_event_record_hdr {
	u8 length;
	u8 flags[3];
	__le16 handle;
	__le16 related_handle;
	__le64 timestamp;
	u8 maint_op_class;
	u8 maint_op_sub_class;
	__le16 ld_id;
	u8 head_id;
	u8 reserved[11];
} __packed;

struct cxl_event_media_hdr {
	struct cxl_event_record_hdr hdr;
	__le64 phys_addr;
	u8 descriptor;
	u8 type;
	u8 transaction_type;
	/*
	 * The meaning of Validity Flags from bit 2 is
	 * different across DRAM and General Media records
	 */
	u8 validity_flags[2];
	u8 channel;
	u8 rank;
} __packed;

#define CXL_EVENT_RECORD_DATA_LENGTH 0x50
struct cxl_event_generic {
	struct cxl_event_record_hdr hdr;
	u8 data[CXL_EVENT_RECORD_DATA_LENGTH];
} __packed;

/*
 * General Media Event Record
 * CXL rev 3.1 Section 8.2.9.2.1.1; Table 8-45
 */
#define CXL_EVENT_GEN_MED_COMP_ID_SIZE	0x10
struct cxl_event_gen_media {
	struct cxl_event_media_hdr media_hdr;
	u8 device[3];
	u8 component_id[CXL_EVENT_GEN_MED_COMP_ID_SIZE];
	u8 cme_threshold_ev_flags;
	u8 cme_count[3];
	u8 sub_type;
	u8 reserved[41];
} __packed;

/*
 * DRAM Event Record - DER
 * CXL rev 3.1 section 8.2.9.2.1.2; Table 8-46
 */
#define CXL_EVENT_DER_CORRECTION_MASK_SIZE	0x20
struct cxl_event_dram {
	struct cxl_event_media_hdr media_hdr;
	u8 nibble_mask[3];
	u8 bank_group;
	u8 bank;
	u8 row[3];
	u8 column[2];
	u8 correction_mask[CXL_EVENT_DER_CORRECTION_MASK_SIZE];
	u8 component_id[CXL_EVENT_GEN_MED_COMP_ID_SIZE];
	u8 sub_channel;
	u8 cme_threshold_ev_flags;
	u8 cvme_count[3];
	u8 sub_type;
	u8 reserved;
} __packed;

/*
 * Get Health Info Record
 * CXL rev 3.1 section 8.2.9.9.3.1; Table 8-133
 */
struct cxl_get_health_info {
	u8 health_status;
	u8 media_status;
	u8 add_status;
	u8 life_used;
	u8 device_temp[2];
	u8 dirty_shutdown_cnt[4];
	u8 cor_vol_err_cnt[4];
	u8 cor_per_err_cnt[4];
} __packed;

/*
 * Memory Module Event Record
 * CXL rev 3.1 section 8.2.9.2.1.3; Table 8-47
 */
struct cxl_event_mem_module {
	struct cxl_event_record_hdr hdr;
	u8 event_type;
	struct cxl_get_health_info info;
	u8 validity_flags[2];
	u8 component_id[CXL_EVENT_GEN_MED_COMP_ID_SIZE];
	u8 event_sub_type;
	u8 reserved[0x2a];
} __packed;

/*
 * Memory Sparing Event Record - MSER
 * CXL rev 3.2 section 8.2.10.2.1.4; Table 8-60
 */
struct cxl_event_mem_sparing {
	struct cxl_event_record_hdr hdr;
	/*
	 * The fields maintenance operation class and maintenance operation
	 * subclass defined in the Memory Sparing Event Record are the
	 * duplication of the same in the common event record. Thus defined
	 * as reserved and to be removed after the spec correction.
	 */
	u8 rsv1;
	u8 rsv2;
	u8 flags;
	u8 result;
	__le16 validity_flags;
	u8 reserved1[6];
	__le16 res_avail;
	u8 channel;
	u8 rank;
	u8 nibble_mask[3];
	u8 bank_group;
	u8 bank;
	u8 row[3];
	__le16 column;
	u8 component_id[CXL_EVENT_GEN_MED_COMP_ID_SIZE];
	u8 sub_channel;
	u8 reserved2[0x25];
} __packed;

union cxl_event {
	struct cxl_event_generic generic;
	struct cxl_event_gen_media gen_media;
	struct cxl_event_dram dram;
	struct cxl_event_mem_module mem_module;
	struct cxl_event_mem_sparing mem_sparing;
	/* dram & gen_media event header */
	struct cxl_event_media_hdr media_hdr;
} __packed;

/*
 * Common Event Record Format; in event logs
 * CXL rev 3.0 section 8.2.9.2.1; Table 8-42
 */
struct cxl_event_record_raw {
	uuid_t id;
	union cxl_event event;
} __packed;

enum cxl_event_type {
	CXL_CPER_EVENT_GENERIC,
	CXL_CPER_EVENT_GEN_MEDIA,
	CXL_CPER_EVENT_DRAM,
	CXL_CPER_EVENT_MEM_MODULE,
	CXL_CPER_EVENT_MEM_SPARING,
};

#define CPER_CXL_DEVICE_ID_VALID		BIT(0)
#define CPER_CXL_DEVICE_SN_VALID		BIT(1)
#define CPER_CXL_COMP_EVENT_LOG_VALID		BIT(2)
struct cxl_cper_event_rec {
	struct {
		u32 length;
		u64 validation_bits;
		struct cper_cxl_event_devid {
			u16 vendor_id;
			u16 device_id;
			u8 func_num;
			u8 device_num;
			u8 bus_num;
			u16 segment_num;
			u16 slot_num; /* bits 2:0 reserved */
			u8 reserved;
		} __packed device_id;
		struct cper_cxl_event_sn {
			u32 lower_dw;
			u32 upper_dw;
		} __packed dev_serial_num;
	} __packed hdr;

	union cxl_event event;
} __packed;

struct cxl_cper_work_data {
	enum cxl_event_type event_type;
	struct cxl_cper_event_rec rec;
};

#define PROT_ERR_VALID_AGENT_TYPE		BIT_ULL(0)
#define PROT_ERR_VALID_AGENT_ADDRESS		BIT_ULL(1)
#define PROT_ERR_VALID_DEVICE_ID		BIT_ULL(2)
#define PROT_ERR_VALID_SERIAL_NUMBER		BIT_ULL(3)
#define PROT_ERR_VALID_CAPABILITY		BIT_ULL(4)
#define PROT_ERR_VALID_DVSEC			BIT_ULL(5)
#define PROT_ERR_VALID_ERROR_LOG		BIT_ULL(6)

/*
 * The layout of the enumeration and the values matches CXL Agent Type
 * field in the UEFI 2.10 Section N.2.13,
 */
enum {
	RCD,	/* Restricted CXL Device */
	RCH_DP,	/* Restricted CXL Host Downstream Port */
	DEVICE,	/* CXL Device */
	LD,	/* CXL Logical Device */
	FMLD,	/* CXL Fabric Manager managed Logical Device */
	RP,	/* CXL Root Port */
	DSP,	/* CXL Downstream Switch Port */
	USP,	/* CXL Upstream Switch Port */
};

#pragma pack(1)

/* Compute Express Link Protocol Error Section, UEFI v2.10 sec N.2.13 */
struct cxl_cper_sec_prot_err {
	u64 valid_bits;
	u8 agent_type;
	u8 reserved[7];

	/*
	 * Except for RCH Downstream Port, all the remaining CXL Agent
	 * types are uniquely identified by the PCIe compatible SBDF number.
	 */
	union {
		u64 rcrb_base_addr;
		struct {
			u8 function;
			u8 device;
			u8 bus;
			u16 segment;
			u8 reserved_1[3];
		};
	} agent_addr;

	struct {
		u16 vendor_id;
		u16 device_id;
		u16 subsystem_vendor_id;
		u16 subsystem_id;
		u8 class_code[2];
		u16 slot;
		u8 reserved_1[4];
	} device_id;

	struct {
		u32 lower_dw;
		u32 upper_dw;
	} dev_serial_num;

	u8 capability[60];
	u16 dvsec_len;
	u16 err_len;
	u8 reserved_2[4];
};

#pragma pack()

/* CXL RAS Capability Structure, CXL v3.0 sec 8.2.4.16 */
struct cxl_ras_capability_regs {
	u32 uncor_status;
	u32 uncor_mask;
	u32 uncor_severity;
	u32 cor_status;
	u32 cor_mask;
	u32 cap_control;
	u32 header_log[16];
};

struct cxl_cper_prot_err_work_data {
	struct cxl_cper_sec_prot_err prot_err;
	struct cxl_ras_capability_regs ras_cap;
	int severity;
};

#ifdef CONFIG_ACPI_APEI_GHES
int cxl_cper_register_work(struct work_struct *work);
int cxl_cper_unregister_work(struct work_struct *work);
int cxl_cper_kfifo_get(struct cxl_cper_work_data *wd);
int cxl_cper_register_prot_err_work(struct work_struct *work);
int cxl_cper_unregister_prot_err_work(struct work_struct *work);
int cxl_cper_prot_err_kfifo_get(struct cxl_cper_prot_err_work_data *wd);
#else
static inline int cxl_cper_register_work(struct work_struct *work)
{
	return 0;
}

static inline int cxl_cper_unregister_work(struct work_struct *work)
{
	return 0;
}
static inline int cxl_cper_kfifo_get(struct cxl_cper_work_data *wd)
{
	return 0;
}
static inline int cxl_cper_register_prot_err_work(struct work_struct *work)
{
	return 0;
}
static inline int cxl_cper_unregister_prot_err_work(struct work_struct *work)
{
	return 0;
}
static inline int cxl_cper_prot_err_kfifo_get(struct cxl_cper_prot_err_work_data *wd)
{
	return 0;
}
#endif

#endif /* _LINUX_CXL_EVENT_H */
