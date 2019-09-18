/*
 * Copyright(c) 2013-2015 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#ifndef __NFIT_TEST_H__
#define __NFIT_TEST_H__
#include <linux/acpi.h>
#include <linux/list.h>
#include <linux/uuid.h>
#include <linux/ioport.h>
#include <linux/spinlock_types.h>

struct nfit_test_request {
	struct list_head list;
	struct resource res;
};

struct nfit_test_resource {
	struct list_head requests;
	struct list_head list;
	struct resource res;
	struct device *dev;
	spinlock_t lock;
	int req_count;
	void *buf;
};

#define ND_TRANSLATE_SPA_STATUS_INVALID_SPA  2
#define NFIT_ARS_INJECT_INVALID 2

enum err_inj_options {
	ND_ARS_ERR_INJ_OPT_NOTIFY = 0,
};

/* nfit commands */
enum nfit_cmd_num {
	NFIT_CMD_TRANSLATE_SPA = 5,
	NFIT_CMD_ARS_INJECT_SET = 7,
	NFIT_CMD_ARS_INJECT_CLEAR = 8,
	NFIT_CMD_ARS_INJECT_GET = 9,
};

struct nd_cmd_translate_spa {
	__u64 spa;
	__u32 status;
	__u8  flags;
	__u8  _reserved[3];
	__u64 translate_length;
	__u32 num_nvdimms;
	struct nd_nvdimm_device {
		__u32 nfit_device_handle;
		__u32 _reserved;
		__u64 dpa;
	} __packed devices[0];

} __packed;

struct nd_cmd_ars_err_inj {
	__u64 err_inj_spa_range_base;
	__u64 err_inj_spa_range_length;
	__u8  err_inj_options;
	__u32 status;
} __packed;

struct nd_cmd_ars_err_inj_clr {
	__u64 err_inj_clr_spa_range_base;
	__u64 err_inj_clr_spa_range_length;
	__u32 status;
} __packed;

struct nd_cmd_ars_err_inj_stat {
	__u32 status;
	__u32 inj_err_rec_count;
	struct nd_error_stat_query_record {
		__u64 err_inj_stat_spa_range_base;
		__u64 err_inj_stat_spa_range_length;
	} __packed record[0];
} __packed;

#define ND_INTEL_SMART			 1
#define ND_INTEL_SMART_THRESHOLD	 2
#define ND_INTEL_ENABLE_LSS_STATUS	10
#define ND_INTEL_FW_GET_INFO		12
#define ND_INTEL_FW_START_UPDATE	13
#define ND_INTEL_FW_SEND_DATA		14
#define ND_INTEL_FW_FINISH_UPDATE	15
#define ND_INTEL_FW_FINISH_QUERY	16
#define ND_INTEL_SMART_SET_THRESHOLD	17
#define ND_INTEL_SMART_INJECT		18

#define ND_INTEL_SMART_HEALTH_VALID             (1 << 0)
#define ND_INTEL_SMART_SPARES_VALID             (1 << 1)
#define ND_INTEL_SMART_USED_VALID               (1 << 2)
#define ND_INTEL_SMART_MTEMP_VALID              (1 << 3)
#define ND_INTEL_SMART_CTEMP_VALID              (1 << 4)
#define ND_INTEL_SMART_SHUTDOWN_COUNT_VALID     (1 << 5)
#define ND_INTEL_SMART_AIT_STATUS_VALID         (1 << 6)
#define ND_INTEL_SMART_PTEMP_VALID              (1 << 7)
#define ND_INTEL_SMART_ALARM_VALID              (1 << 9)
#define ND_INTEL_SMART_SHUTDOWN_VALID           (1 << 10)
#define ND_INTEL_SMART_VENDOR_VALID             (1 << 11)
#define ND_INTEL_SMART_SPARE_TRIP               (1 << 0)
#define ND_INTEL_SMART_TEMP_TRIP                (1 << 1)
#define ND_INTEL_SMART_CTEMP_TRIP               (1 << 2)
#define ND_INTEL_SMART_NON_CRITICAL_HEALTH      (1 << 0)
#define ND_INTEL_SMART_CRITICAL_HEALTH          (1 << 1)
#define ND_INTEL_SMART_FATAL_HEALTH             (1 << 2)
#define ND_INTEL_SMART_INJECT_MTEMP		(1 << 0)
#define ND_INTEL_SMART_INJECT_SPARE		(1 << 1)
#define ND_INTEL_SMART_INJECT_FATAL		(1 << 2)
#define ND_INTEL_SMART_INJECT_SHUTDOWN		(1 << 3)

struct nd_intel_smart {
	__u32 status;
	union {
		struct {
			__u32 flags;
			__u8 reserved0[4];
			__u8 health;
			__u8 spares;
			__u8 life_used;
			__u8 alarm_flags;
			__u16 media_temperature;
			__u16 ctrl_temperature;
			__u32 shutdown_count;
			__u8 ait_status;
			__u16 pmic_temperature;
			__u8 reserved1[8];
			__u8 shutdown_state;
			__u32 vendor_size;
			__u8 vendor_data[92];
		} __packed;
		__u8 data[128];
	};
} __packed;

struct nd_intel_smart_threshold {
	__u32 status;
	union {
		struct {
			__u16 alarm_control;
			__u8 spares;
			__u16 media_temperature;
			__u16 ctrl_temperature;
			__u8 reserved[1];
		} __packed;
		__u8 data[8];
	};
} __packed;

struct nd_intel_smart_set_threshold {
	__u16 alarm_control;
	__u8 spares;
	__u16 media_temperature;
	__u16 ctrl_temperature;
	__u32 status;
} __packed;

struct nd_intel_smart_inject {
	__u64 flags;
	__u8 mtemp_enable;
	__u16 media_temperature;
	__u8 spare_enable;
	__u8 spares;
	__u8 fatal_enable;
	__u8 unsafe_shutdown_enable;
	__u32 status;
} __packed;

#define INTEL_FW_STORAGE_SIZE		0x100000
#define INTEL_FW_MAX_SEND_LEN		0xFFEC
#define INTEL_FW_QUERY_INTERVAL		250000
#define INTEL_FW_QUERY_MAX_TIME		3000000
#define INTEL_FW_FIS_VERSION		0x0105
#define INTEL_FW_FAKE_VERSION		0xffffffffabcd

enum intel_fw_update_state {
	FW_STATE_NEW = 0,
	FW_STATE_IN_PROGRESS,
	FW_STATE_VERIFY,
	FW_STATE_UPDATED,
};

struct nd_intel_fw_info {
	__u32 status;
	__u32 storage_size;
	__u32 max_send_len;
	__u32 query_interval;
	__u32 max_query_time;
	__u8 update_cap;
	__u8 reserved[3];
	__u32 fis_version;
	__u64 run_version;
	__u64 updated_version;
} __packed;

struct nd_intel_fw_start {
	__u32 status;
	__u32 context;
} __packed;

/* this one has the output first because the variable input data size */
struct nd_intel_fw_send_data {
	__u32 context;
	__u32 offset;
	__u32 length;
	__u8 data[0];
/* this field is not declared due ot variable data from input */
/*	__u32 status; */
} __packed;

struct nd_intel_fw_finish_update {
	__u8 ctrl_flags;
	__u8 reserved[3];
	__u32 context;
	__u32 status;
} __packed;

struct nd_intel_fw_finish_query {
	__u32 context;
	__u32 status;
	__u64 updated_fw_rev;
} __packed;

struct nd_intel_lss {
	__u8 enable;
	__u32 status;
} __packed;

typedef struct nfit_test_resource *(*nfit_test_lookup_fn)(resource_size_t);
typedef union acpi_object *(*nfit_test_evaluate_dsm_fn)(acpi_handle handle,
		 const guid_t *guid, u64 rev, u64 func,
		 union acpi_object *argv4);
void __iomem *__wrap_ioremap_nocache(resource_size_t offset,
		unsigned long size);
void __wrap_iounmap(volatile void __iomem *addr);
void nfit_test_setup(nfit_test_lookup_fn lookup,
		nfit_test_evaluate_dsm_fn evaluate);
void nfit_test_teardown(void);
struct nfit_test_resource *get_nfit_res(resource_size_t resource);
#endif
