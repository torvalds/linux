/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Intel Corporation. */
#ifndef _LINUX_CXL_EVENT_H
#define _LINUX_CXL_EVENT_H

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
	u8 reserved[15];
} __packed;

/*
 * Common Event Record Format
 * CXL rev 3.0 section 8.2.9.2.1; Table 8-42
 */
#define CXL_EVENT_RECORD_DATA_LENGTH 0x50
struct cxl_event_record_raw {
	uuid_t id;
	struct cxl_event_record_hdr hdr;
	u8 data[CXL_EVENT_RECORD_DATA_LENGTH];
} __packed;

/*
 * General Media Event Record
 * CXL rev 3.0 Section 8.2.9.2.1.1; Table 8-43
 */
#define CXL_EVENT_GEN_MED_COMP_ID_SIZE	0x10
struct cxl_event_gen_media {
	struct cxl_event_record_hdr hdr;
	__le64 phys_addr;
	u8 descriptor;
	u8 type;
	u8 transaction_type;
	u8 validity_flags[2];
	u8 channel;
	u8 rank;
	u8 device[3];
	u8 component_id[CXL_EVENT_GEN_MED_COMP_ID_SIZE];
	u8 reserved[46];
} __packed;

/*
 * DRAM Event Record - DER
 * CXL rev 3.0 section 8.2.9.2.1.2; Table 3-44
 */
#define CXL_EVENT_DER_CORRECTION_MASK_SIZE	0x20
struct cxl_event_dram {
	struct cxl_event_record_hdr hdr;
	__le64 phys_addr;
	u8 descriptor;
	u8 type;
	u8 transaction_type;
	u8 validity_flags[2];
	u8 channel;
	u8 rank;
	u8 nibble_mask[3];
	u8 bank_group;
	u8 bank;
	u8 row[3];
	u8 column[2];
	u8 correction_mask[CXL_EVENT_DER_CORRECTION_MASK_SIZE];
	u8 reserved[0x17];
} __packed;

/*
 * Get Health Info Record
 * CXL rev 3.0 section 8.2.9.8.3.1; Table 8-100
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
 * CXL rev 3.0 section 8.2.9.2.1.3; Table 8-45
 */
struct cxl_event_mem_module {
	struct cxl_event_record_hdr hdr;
	u8 event_type;
	struct cxl_get_health_info info;
	u8 reserved[0x3d];
} __packed;

#endif /* _LINUX_CXL_EVENT_H */
