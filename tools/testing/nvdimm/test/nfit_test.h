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

union acpi_object;
typedef void *acpi_handle;

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
