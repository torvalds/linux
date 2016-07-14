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
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/libnvdimm.h>
#include <linux/vmalloc.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/ndctl.h>
#include <linux/sizes.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <nfit.h>
#include <nd.h>
#include "nfit_test.h"

/*
 * Generate an NFIT table to describe the following topology:
 *
 * BUS0: Interleaved PMEM regions, and aliasing with BLK regions
 *
 *                     (a)                       (b)            DIMM   BLK-REGION
 *           +----------+--------------+----------+---------+
 * +------+  |  blk2.0  |     pm0.0    |  blk2.1  |  pm1.0  |    0      region2
 * | imc0 +--+- - - - - region0 - - - -+----------+         +
 * +--+---+  |  blk3.0  |     pm0.0    |  blk3.1  |  pm1.0  |    1      region3
 *    |      +----------+--------------v----------v         v
 * +--+---+                            |                    |
 * | cpu0 |                                    region1
 * +--+---+                            |                    |
 *    |      +-------------------------^----------^         ^
 * +--+---+  |                 blk4.0             |  pm1.0  |    2      region4
 * | imc1 +--+-------------------------+----------+         +
 * +------+  |                 blk5.0             |  pm1.0  |    3      region5
 *           +-------------------------+----------+-+-------+
 *
 * +--+---+
 * | cpu1 |
 * +--+---+                   (Hotplug DIMM)
 *    |      +----------------------------------------------+
 * +--+---+  |                 blk6.0/pm7.0                 |    4      region6/7
 * | imc0 +--+----------------------------------------------+
 * +------+
 *
 *
 * *) In this layout we have four dimms and two memory controllers in one
 *    socket.  Each unique interface (BLK or PMEM) to DPA space
 *    is identified by a region device with a dynamically assigned id.
 *
 * *) The first portion of dimm0 and dimm1 are interleaved as REGION0.
 *    A single PMEM namespace "pm0.0" is created using half of the
 *    REGION0 SPA-range.  REGION0 spans dimm0 and dimm1.  PMEM namespace
 *    allocate from from the bottom of a region.  The unallocated
 *    portion of REGION0 aliases with REGION2 and REGION3.  That
 *    unallacted capacity is reclaimed as BLK namespaces ("blk2.0" and
 *    "blk3.0") starting at the base of each DIMM to offset (a) in those
 *    DIMMs.  "pm0.0", "blk2.0" and "blk3.0" are free-form readable
 *    names that can be assigned to a namespace.
 *
 * *) In the last portion of dimm0 and dimm1 we have an interleaved
 *    SPA range, REGION1, that spans those two dimms as well as dimm2
 *    and dimm3.  Some of REGION1 allocated to a PMEM namespace named
 *    "pm1.0" the rest is reclaimed in 4 BLK namespaces (for each
 *    dimm in the interleave set), "blk2.1", "blk3.1", "blk4.0", and
 *    "blk5.0".
 *
 * *) The portion of dimm2 and dimm3 that do not participate in the
 *    REGION1 interleaved SPA range (i.e. the DPA address below offset
 *    (b) are also included in the "blk4.0" and "blk5.0" namespaces.
 *    Note, that BLK namespaces need not be contiguous in DPA-space, and
 *    can consume aliased capacity from multiple interleave sets.
 *
 * BUS1: Legacy NVDIMM (single contiguous range)
 *
 *  region2
 * +---------------------+
 * |---------------------|
 * ||       pm2.0       ||
 * |---------------------|
 * +---------------------+
 *
 * *) A NFIT-table may describe a simple system-physical-address range
 *    with no BLK aliasing.  This type of region may optionally
 *    reference an NVDIMM.
 */
enum {
	NUM_PM  = 3,
	NUM_DCR = 5,
	NUM_HINTS = 8,
	NUM_BDW = NUM_DCR,
	NUM_SPA = NUM_PM + NUM_DCR + NUM_BDW,
	NUM_MEM = NUM_DCR + NUM_BDW + 2 /* spa0 iset */ + 4 /* spa1 iset */,
	DIMM_SIZE = SZ_32M,
	LABEL_SIZE = SZ_128K,
	SPA_VCD_SIZE = SZ_4M,
	SPA0_SIZE = DIMM_SIZE,
	SPA1_SIZE = DIMM_SIZE*2,
	SPA2_SIZE = DIMM_SIZE,
	BDW_SIZE = 64 << 8,
	DCR_SIZE = 12,
	NUM_NFITS = 2, /* permit testing multiple NFITs per system */
};

struct nfit_test_dcr {
	__le64 bdw_addr;
	__le32 bdw_status;
	__u8 aperature[BDW_SIZE];
};

#define NFIT_DIMM_HANDLE(node, socket, imc, chan, dimm) \
	(((node & 0xfff) << 16) | ((socket & 0xf) << 12) \
	 | ((imc & 0xf) << 8) | ((chan & 0xf) << 4) | (dimm & 0xf))

static u32 handle[NUM_DCR] = {
	[0] = NFIT_DIMM_HANDLE(0, 0, 0, 0, 0),
	[1] = NFIT_DIMM_HANDLE(0, 0, 0, 0, 1),
	[2] = NFIT_DIMM_HANDLE(0, 0, 1, 0, 0),
	[3] = NFIT_DIMM_HANDLE(0, 0, 1, 0, 1),
	[4] = NFIT_DIMM_HANDLE(0, 1, 0, 0, 0),
};

struct nfit_test {
	struct acpi_nfit_desc acpi_desc;
	struct platform_device pdev;
	struct list_head resources;
	void *nfit_buf;
	dma_addr_t nfit_dma;
	size_t nfit_size;
	int num_dcr;
	int num_pm;
	void **dimm;
	dma_addr_t *dimm_dma;
	void **flush;
	dma_addr_t *flush_dma;
	void **label;
	dma_addr_t *label_dma;
	void **spa_set;
	dma_addr_t *spa_set_dma;
	struct nfit_test_dcr **dcr;
	dma_addr_t *dcr_dma;
	int (*alloc)(struct nfit_test *t);
	void (*setup)(struct nfit_test *t);
	int setup_hotplug;
	struct ars_state {
		struct nd_cmd_ars_status *ars_status;
		unsigned long deadline;
		spinlock_t lock;
	} ars_state;
};

static struct nfit_test *to_nfit_test(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	return container_of(pdev, struct nfit_test, pdev);
}

static int nfit_test_cmd_get_config_size(struct nd_cmd_get_config_size *nd_cmd,
		unsigned int buf_len)
{
	if (buf_len < sizeof(*nd_cmd))
		return -EINVAL;

	nd_cmd->status = 0;
	nd_cmd->config_size = LABEL_SIZE;
	nd_cmd->max_xfer = SZ_4K;

	return 0;
}

static int nfit_test_cmd_get_config_data(struct nd_cmd_get_config_data_hdr
		*nd_cmd, unsigned int buf_len, void *label)
{
	unsigned int len, offset = nd_cmd->in_offset;
	int rc;

	if (buf_len < sizeof(*nd_cmd))
		return -EINVAL;
	if (offset >= LABEL_SIZE)
		return -EINVAL;
	if (nd_cmd->in_length + sizeof(*nd_cmd) > buf_len)
		return -EINVAL;

	nd_cmd->status = 0;
	len = min(nd_cmd->in_length, LABEL_SIZE - offset);
	memcpy(nd_cmd->out_buf, label + offset, len);
	rc = buf_len - sizeof(*nd_cmd) - len;

	return rc;
}

static int nfit_test_cmd_set_config_data(struct nd_cmd_set_config_hdr *nd_cmd,
		unsigned int buf_len, void *label)
{
	unsigned int len, offset = nd_cmd->in_offset;
	u32 *status;
	int rc;

	if (buf_len < sizeof(*nd_cmd))
		return -EINVAL;
	if (offset >= LABEL_SIZE)
		return -EINVAL;
	if (nd_cmd->in_length + sizeof(*nd_cmd) + 4 > buf_len)
		return -EINVAL;

	status = (void *)nd_cmd + nd_cmd->in_length + sizeof(*nd_cmd);
	*status = 0;
	len = min(nd_cmd->in_length, LABEL_SIZE - offset);
	memcpy(label + offset, nd_cmd->in_buf, len);
	rc = buf_len - sizeof(*nd_cmd) - (len + 4);

	return rc;
}

#define NFIT_TEST_ARS_RECORDS 4
#define NFIT_TEST_CLEAR_ERR_UNIT 256

static int nfit_test_cmd_ars_cap(struct nd_cmd_ars_cap *nd_cmd,
		unsigned int buf_len)
{
	if (buf_len < sizeof(*nd_cmd))
		return -EINVAL;

	nd_cmd->max_ars_out = sizeof(struct nd_cmd_ars_status)
		+ NFIT_TEST_ARS_RECORDS * sizeof(struct nd_ars_record);
	nd_cmd->status = (ND_ARS_PERSISTENT | ND_ARS_VOLATILE) << 16;
	nd_cmd->clear_err_unit = NFIT_TEST_CLEAR_ERR_UNIT;

	return 0;
}

/*
 * Initialize the ars_state to return an ars_result 1 second in the future with
 * a 4K error range in the middle of the requested address range.
 */
static void post_ars_status(struct ars_state *ars_state, u64 addr, u64 len)
{
	struct nd_cmd_ars_status *ars_status;
	struct nd_ars_record *ars_record;

	ars_state->deadline = jiffies + 1*HZ;
	ars_status = ars_state->ars_status;
	ars_status->status = 0;
	ars_status->out_length = sizeof(struct nd_cmd_ars_status)
		+ sizeof(struct nd_ars_record);
	ars_status->address = addr;
	ars_status->length = len;
	ars_status->type = ND_ARS_PERSISTENT;
	ars_status->num_records = 1;
	ars_record = &ars_status->records[0];
	ars_record->handle = 0;
	ars_record->err_address = addr + len / 2;
	ars_record->length = SZ_4K;
}

static int nfit_test_cmd_ars_start(struct ars_state *ars_state,
		struct nd_cmd_ars_start *ars_start, unsigned int buf_len,
		int *cmd_rc)
{
	if (buf_len < sizeof(*ars_start))
		return -EINVAL;

	spin_lock(&ars_state->lock);
	if (time_before(jiffies, ars_state->deadline)) {
		ars_start->status = NFIT_ARS_START_BUSY;
		*cmd_rc = -EBUSY;
	} else {
		ars_start->status = 0;
		ars_start->scrub_time = 1;
		post_ars_status(ars_state, ars_start->address,
				ars_start->length);
		*cmd_rc = 0;
	}
	spin_unlock(&ars_state->lock);

	return 0;
}

static int nfit_test_cmd_ars_status(struct ars_state *ars_state,
		struct nd_cmd_ars_status *ars_status, unsigned int buf_len,
		int *cmd_rc)
{
	if (buf_len < ars_state->ars_status->out_length)
		return -EINVAL;

	spin_lock(&ars_state->lock);
	if (time_before(jiffies, ars_state->deadline)) {
		memset(ars_status, 0, buf_len);
		ars_status->status = NFIT_ARS_STATUS_BUSY;
		ars_status->out_length = sizeof(*ars_status);
		*cmd_rc = -EBUSY;
	} else {
		memcpy(ars_status, ars_state->ars_status,
				ars_state->ars_status->out_length);
		*cmd_rc = 0;
	}
	spin_unlock(&ars_state->lock);
	return 0;
}

static int nfit_test_cmd_clear_error(struct nd_cmd_clear_error *clear_err,
		unsigned int buf_len, int *cmd_rc)
{
	const u64 mask = NFIT_TEST_CLEAR_ERR_UNIT - 1;
	if (buf_len < sizeof(*clear_err))
		return -EINVAL;

	if ((clear_err->address & mask) || (clear_err->length & mask))
		return -EINVAL;

	/*
	 * Report 'all clear' success for all commands even though a new
	 * scrub will find errors again.  This is enough to have the
	 * error removed from the 'badblocks' tracking in the pmem
	 * driver.
	 */
	clear_err->status = 0;
	clear_err->cleared = clear_err->length;
	*cmd_rc = 0;
	return 0;
}

static int nfit_test_cmd_smart(struct nd_cmd_smart *smart, unsigned int buf_len)
{
	static const struct nd_smart_payload smart_data = {
		.flags = ND_SMART_HEALTH_VALID | ND_SMART_TEMP_VALID
			| ND_SMART_SPARES_VALID | ND_SMART_ALARM_VALID
			| ND_SMART_USED_VALID | ND_SMART_SHUTDOWN_VALID,
		.health = ND_SMART_NON_CRITICAL_HEALTH,
		.temperature = 23 * 16,
		.spares = 75,
		.alarm_flags = ND_SMART_SPARE_TRIP | ND_SMART_TEMP_TRIP,
		.life_used = 5,
		.shutdown_state = 0,
		.vendor_size = 0,
	};

	if (buf_len < sizeof(*smart))
		return -EINVAL;
	memcpy(smart->data, &smart_data, sizeof(smart_data));
	return 0;
}

static int nfit_test_cmd_smart_threshold(struct nd_cmd_smart_threshold *smart_t,
		unsigned int buf_len)
{
	static const struct nd_smart_threshold_payload smart_t_data = {
		.alarm_control = ND_SMART_SPARE_TRIP | ND_SMART_TEMP_TRIP,
		.temperature = 40 * 16,
		.spares = 5,
	};

	if (buf_len < sizeof(*smart_t))
		return -EINVAL;
	memcpy(smart_t->data, &smart_t_data, sizeof(smart_t_data));
	return 0;
}

static int nfit_test_ctl(struct nvdimm_bus_descriptor *nd_desc,
		struct nvdimm *nvdimm, unsigned int cmd, void *buf,
		unsigned int buf_len, int *cmd_rc)
{
	struct acpi_nfit_desc *acpi_desc = to_acpi_desc(nd_desc);
	struct nfit_test *t = container_of(acpi_desc, typeof(*t), acpi_desc);
	unsigned int func = cmd;
	int i, rc = 0, __cmd_rc;

	if (!cmd_rc)
		cmd_rc = &__cmd_rc;
	*cmd_rc = 0;

	if (nvdimm) {
		struct nfit_mem *nfit_mem = nvdimm_provider_data(nvdimm);
		unsigned long cmd_mask = nvdimm_cmd_mask(nvdimm);

		if (!nfit_mem)
			return -ENOTTY;

		if (cmd == ND_CMD_CALL) {
			struct nd_cmd_pkg *call_pkg = buf;

			buf_len = call_pkg->nd_size_in + call_pkg->nd_size_out;
			buf = (void *) call_pkg->nd_payload;
			func = call_pkg->nd_command;
			if (call_pkg->nd_family != nfit_mem->family)
				return -ENOTTY;
		}

		if (!test_bit(cmd, &cmd_mask)
				|| !test_bit(func, &nfit_mem->dsm_mask))
			return -ENOTTY;

		/* lookup label space for the given dimm */
		for (i = 0; i < ARRAY_SIZE(handle); i++)
			if (__to_nfit_memdev(nfit_mem)->device_handle ==
					handle[i])
				break;
		if (i >= ARRAY_SIZE(handle))
			return -ENXIO;

		switch (func) {
		case ND_CMD_GET_CONFIG_SIZE:
			rc = nfit_test_cmd_get_config_size(buf, buf_len);
			break;
		case ND_CMD_GET_CONFIG_DATA:
			rc = nfit_test_cmd_get_config_data(buf, buf_len,
				t->label[i]);
			break;
		case ND_CMD_SET_CONFIG_DATA:
			rc = nfit_test_cmd_set_config_data(buf, buf_len,
				t->label[i]);
			break;
		case ND_CMD_SMART:
			rc = nfit_test_cmd_smart(buf, buf_len);
			break;
		case ND_CMD_SMART_THRESHOLD:
			rc = nfit_test_cmd_smart_threshold(buf, buf_len);
			break;
		default:
			return -ENOTTY;
		}
	} else {
		struct ars_state *ars_state = &t->ars_state;

		if (!nd_desc || !test_bit(cmd, &nd_desc->cmd_mask))
			return -ENOTTY;

		switch (func) {
		case ND_CMD_ARS_CAP:
			rc = nfit_test_cmd_ars_cap(buf, buf_len);
			break;
		case ND_CMD_ARS_START:
			rc = nfit_test_cmd_ars_start(ars_state, buf, buf_len,
					cmd_rc);
			break;
		case ND_CMD_ARS_STATUS:
			rc = nfit_test_cmd_ars_status(ars_state, buf, buf_len,
					cmd_rc);
			break;
		case ND_CMD_CLEAR_ERROR:
			rc = nfit_test_cmd_clear_error(buf, buf_len, cmd_rc);
			break;
		default:
			return -ENOTTY;
		}
	}

	return rc;
}

static DEFINE_SPINLOCK(nfit_test_lock);
static struct nfit_test *instances[NUM_NFITS];

static void release_nfit_res(void *data)
{
	struct nfit_test_resource *nfit_res = data;
	struct resource *res = nfit_res->res;

	spin_lock(&nfit_test_lock);
	list_del(&nfit_res->list);
	spin_unlock(&nfit_test_lock);

	vfree(nfit_res->buf);
	kfree(res);
	kfree(nfit_res);
}

static void *__test_alloc(struct nfit_test *t, size_t size, dma_addr_t *dma,
		void *buf)
{
	struct device *dev = &t->pdev.dev;
	struct resource *res = kzalloc(sizeof(*res) * 2, GFP_KERNEL);
	struct nfit_test_resource *nfit_res = kzalloc(sizeof(*nfit_res),
			GFP_KERNEL);
	int rc;

	if (!res || !buf || !nfit_res)
		goto err;
	rc = devm_add_action(dev, release_nfit_res, nfit_res);
	if (rc)
		goto err;
	INIT_LIST_HEAD(&nfit_res->list);
	memset(buf, 0, size);
	nfit_res->dev = dev;
	nfit_res->buf = buf;
	nfit_res->res = res;
	res->start = *dma;
	res->end = *dma + size - 1;
	res->name = "NFIT";
	spin_lock(&nfit_test_lock);
	list_add(&nfit_res->list, &t->resources);
	spin_unlock(&nfit_test_lock);

	return nfit_res->buf;
 err:
	if (buf)
		vfree(buf);
	kfree(res);
	kfree(nfit_res);
	return NULL;
}

static void *test_alloc(struct nfit_test *t, size_t size, dma_addr_t *dma)
{
	void *buf = vmalloc(size);

	*dma = (unsigned long) buf;
	return __test_alloc(t, size, dma, buf);
}

static struct nfit_test_resource *nfit_test_lookup(resource_size_t addr)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(instances); i++) {
		struct nfit_test_resource *n, *nfit_res = NULL;
		struct nfit_test *t = instances[i];

		if (!t)
			continue;
		spin_lock(&nfit_test_lock);
		list_for_each_entry(n, &t->resources, list) {
			if (addr >= n->res->start && (addr < n->res->start
						+ resource_size(n->res))) {
				nfit_res = n;
				break;
			} else if (addr >= (unsigned long) n->buf
					&& (addr < (unsigned long) n->buf
						+ resource_size(n->res))) {
				nfit_res = n;
				break;
			}
		}
		spin_unlock(&nfit_test_lock);
		if (nfit_res)
			return nfit_res;
	}

	return NULL;
}

static int ars_state_init(struct device *dev, struct ars_state *ars_state)
{
	ars_state->ars_status = devm_kzalloc(dev,
			sizeof(struct nd_cmd_ars_status)
			+ sizeof(struct nd_ars_record) * NFIT_TEST_ARS_RECORDS,
			GFP_KERNEL);
	if (!ars_state->ars_status)
		return -ENOMEM;
	spin_lock_init(&ars_state->lock);
	return 0;
}

static int nfit_test0_alloc(struct nfit_test *t)
{
	size_t nfit_size = sizeof(struct acpi_nfit_system_address) * NUM_SPA
			+ sizeof(struct acpi_nfit_memory_map) * NUM_MEM
			+ sizeof(struct acpi_nfit_control_region) * NUM_DCR
			+ offsetof(struct acpi_nfit_control_region,
					window_size) * NUM_DCR
			+ sizeof(struct acpi_nfit_data_region) * NUM_BDW
			+ (sizeof(struct acpi_nfit_flush_address)
					+ sizeof(u64) * NUM_HINTS) * NUM_DCR;
	int i;

	t->nfit_buf = test_alloc(t, nfit_size, &t->nfit_dma);
	if (!t->nfit_buf)
		return -ENOMEM;
	t->nfit_size = nfit_size;

	t->spa_set[0] = test_alloc(t, SPA0_SIZE, &t->spa_set_dma[0]);
	if (!t->spa_set[0])
		return -ENOMEM;

	t->spa_set[1] = test_alloc(t, SPA1_SIZE, &t->spa_set_dma[1]);
	if (!t->spa_set[1])
		return -ENOMEM;

	t->spa_set[2] = test_alloc(t, SPA0_SIZE, &t->spa_set_dma[2]);
	if (!t->spa_set[2])
		return -ENOMEM;

	for (i = 0; i < NUM_DCR; i++) {
		t->dimm[i] = test_alloc(t, DIMM_SIZE, &t->dimm_dma[i]);
		if (!t->dimm[i])
			return -ENOMEM;

		t->label[i] = test_alloc(t, LABEL_SIZE, &t->label_dma[i]);
		if (!t->label[i])
			return -ENOMEM;
		sprintf(t->label[i], "label%d", i);

		t->flush[i] = test_alloc(t, sizeof(u64) * NUM_HINTS,
				&t->flush_dma[i]);
		if (!t->flush[i])
			return -ENOMEM;
	}

	for (i = 0; i < NUM_DCR; i++) {
		t->dcr[i] = test_alloc(t, LABEL_SIZE, &t->dcr_dma[i]);
		if (!t->dcr[i])
			return -ENOMEM;
	}

	return ars_state_init(&t->pdev.dev, &t->ars_state);
}

static int nfit_test1_alloc(struct nfit_test *t)
{
	size_t nfit_size = sizeof(struct acpi_nfit_system_address) * 2
		+ sizeof(struct acpi_nfit_memory_map)
		+ offsetof(struct acpi_nfit_control_region, window_size);

	t->nfit_buf = test_alloc(t, nfit_size, &t->nfit_dma);
	if (!t->nfit_buf)
		return -ENOMEM;
	t->nfit_size = nfit_size;

	t->spa_set[0] = test_alloc(t, SPA2_SIZE, &t->spa_set_dma[0]);
	if (!t->spa_set[0])
		return -ENOMEM;

	t->spa_set[1] = test_alloc(t, SPA_VCD_SIZE, &t->spa_set_dma[1]);
	if (!t->spa_set[1])
		return -ENOMEM;

	return ars_state_init(&t->pdev.dev, &t->ars_state);
}

static void dcr_common_init(struct acpi_nfit_control_region *dcr)
{
	dcr->vendor_id = 0xabcd;
	dcr->device_id = 0;
	dcr->revision_id = 1;
	dcr->valid_fields = 1;
	dcr->manufacturing_location = 0xa;
	dcr->manufacturing_date = cpu_to_be16(2016);
}

static void nfit_test0_setup(struct nfit_test *t)
{
	const int flush_hint_size = sizeof(struct acpi_nfit_flush_address)
		+ (sizeof(u64) * NUM_HINTS);
	struct acpi_nfit_desc *acpi_desc;
	struct acpi_nfit_memory_map *memdev;
	void *nfit_buf = t->nfit_buf;
	struct acpi_nfit_system_address *spa;
	struct acpi_nfit_control_region *dcr;
	struct acpi_nfit_data_region *bdw;
	struct acpi_nfit_flush_address *flush;
	unsigned int offset, i;

	/*
	 * spa0 (interleave first half of dimm0 and dimm1, note storage
	 * does not actually alias the related block-data-window
	 * regions)
	 */
	spa = nfit_buf;
	spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
	spa->header.length = sizeof(*spa);
	memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_PM), 16);
	spa->range_index = 0+1;
	spa->address = t->spa_set_dma[0];
	spa->length = SPA0_SIZE;

	/*
	 * spa1 (interleave last half of the 4 DIMMS, note storage
	 * does not actually alias the related block-data-window
	 * regions)
	 */
	spa = nfit_buf + sizeof(*spa);
	spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
	spa->header.length = sizeof(*spa);
	memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_PM), 16);
	spa->range_index = 1+1;
	spa->address = t->spa_set_dma[1];
	spa->length = SPA1_SIZE;

	/* spa2 (dcr0) dimm0 */
	spa = nfit_buf + sizeof(*spa) * 2;
	spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
	spa->header.length = sizeof(*spa);
	memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_DCR), 16);
	spa->range_index = 2+1;
	spa->address = t->dcr_dma[0];
	spa->length = DCR_SIZE;

	/* spa3 (dcr1) dimm1 */
	spa = nfit_buf + sizeof(*spa) * 3;
	spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
	spa->header.length = sizeof(*spa);
	memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_DCR), 16);
	spa->range_index = 3+1;
	spa->address = t->dcr_dma[1];
	spa->length = DCR_SIZE;

	/* spa4 (dcr2) dimm2 */
	spa = nfit_buf + sizeof(*spa) * 4;
	spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
	spa->header.length = sizeof(*spa);
	memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_DCR), 16);
	spa->range_index = 4+1;
	spa->address = t->dcr_dma[2];
	spa->length = DCR_SIZE;

	/* spa5 (dcr3) dimm3 */
	spa = nfit_buf + sizeof(*spa) * 5;
	spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
	spa->header.length = sizeof(*spa);
	memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_DCR), 16);
	spa->range_index = 5+1;
	spa->address = t->dcr_dma[3];
	spa->length = DCR_SIZE;

	/* spa6 (bdw for dcr0) dimm0 */
	spa = nfit_buf + sizeof(*spa) * 6;
	spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
	spa->header.length = sizeof(*spa);
	memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_BDW), 16);
	spa->range_index = 6+1;
	spa->address = t->dimm_dma[0];
	spa->length = DIMM_SIZE;

	/* spa7 (bdw for dcr1) dimm1 */
	spa = nfit_buf + sizeof(*spa) * 7;
	spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
	spa->header.length = sizeof(*spa);
	memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_BDW), 16);
	spa->range_index = 7+1;
	spa->address = t->dimm_dma[1];
	spa->length = DIMM_SIZE;

	/* spa8 (bdw for dcr2) dimm2 */
	spa = nfit_buf + sizeof(*spa) * 8;
	spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
	spa->header.length = sizeof(*spa);
	memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_BDW), 16);
	spa->range_index = 8+1;
	spa->address = t->dimm_dma[2];
	spa->length = DIMM_SIZE;

	/* spa9 (bdw for dcr3) dimm3 */
	spa = nfit_buf + sizeof(*spa) * 9;
	spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
	spa->header.length = sizeof(*spa);
	memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_BDW), 16);
	spa->range_index = 9+1;
	spa->address = t->dimm_dma[3];
	spa->length = DIMM_SIZE;

	offset = sizeof(*spa) * 10;
	/* mem-region0 (spa0, dimm0) */
	memdev = nfit_buf + offset;
	memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
	memdev->header.length = sizeof(*memdev);
	memdev->device_handle = handle[0];
	memdev->physical_id = 0;
	memdev->region_id = 0;
	memdev->range_index = 0+1;
	memdev->region_index = 4+1;
	memdev->region_size = SPA0_SIZE/2;
	memdev->region_offset = t->spa_set_dma[0];
	memdev->address = 0;
	memdev->interleave_index = 0;
	memdev->interleave_ways = 2;

	/* mem-region1 (spa0, dimm1) */
	memdev = nfit_buf + offset + sizeof(struct acpi_nfit_memory_map);
	memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
	memdev->header.length = sizeof(*memdev);
	memdev->device_handle = handle[1];
	memdev->physical_id = 1;
	memdev->region_id = 0;
	memdev->range_index = 0+1;
	memdev->region_index = 5+1;
	memdev->region_size = SPA0_SIZE/2;
	memdev->region_offset = t->spa_set_dma[0] + SPA0_SIZE/2;
	memdev->address = 0;
	memdev->interleave_index = 0;
	memdev->interleave_ways = 2;

	/* mem-region2 (spa1, dimm0) */
	memdev = nfit_buf + offset + sizeof(struct acpi_nfit_memory_map) * 2;
	memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
	memdev->header.length = sizeof(*memdev);
	memdev->device_handle = handle[0];
	memdev->physical_id = 0;
	memdev->region_id = 1;
	memdev->range_index = 1+1;
	memdev->region_index = 4+1;
	memdev->region_size = SPA1_SIZE/4;
	memdev->region_offset = t->spa_set_dma[1];
	memdev->address = SPA0_SIZE/2;
	memdev->interleave_index = 0;
	memdev->interleave_ways = 4;

	/* mem-region3 (spa1, dimm1) */
	memdev = nfit_buf + offset + sizeof(struct acpi_nfit_memory_map) * 3;
	memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
	memdev->header.length = sizeof(*memdev);
	memdev->device_handle = handle[1];
	memdev->physical_id = 1;
	memdev->region_id = 1;
	memdev->range_index = 1+1;
	memdev->region_index = 5+1;
	memdev->region_size = SPA1_SIZE/4;
	memdev->region_offset = t->spa_set_dma[1] + SPA1_SIZE/4;
	memdev->address = SPA0_SIZE/2;
	memdev->interleave_index = 0;
	memdev->interleave_ways = 4;

	/* mem-region4 (spa1, dimm2) */
	memdev = nfit_buf + offset + sizeof(struct acpi_nfit_memory_map) * 4;
	memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
	memdev->header.length = sizeof(*memdev);
	memdev->device_handle = handle[2];
	memdev->physical_id = 2;
	memdev->region_id = 0;
	memdev->range_index = 1+1;
	memdev->region_index = 6+1;
	memdev->region_size = SPA1_SIZE/4;
	memdev->region_offset = t->spa_set_dma[1] + 2*SPA1_SIZE/4;
	memdev->address = SPA0_SIZE/2;
	memdev->interleave_index = 0;
	memdev->interleave_ways = 4;

	/* mem-region5 (spa1, dimm3) */
	memdev = nfit_buf + offset + sizeof(struct acpi_nfit_memory_map) * 5;
	memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
	memdev->header.length = sizeof(*memdev);
	memdev->device_handle = handle[3];
	memdev->physical_id = 3;
	memdev->region_id = 0;
	memdev->range_index = 1+1;
	memdev->region_index = 7+1;
	memdev->region_size = SPA1_SIZE/4;
	memdev->region_offset = t->spa_set_dma[1] + 3*SPA1_SIZE/4;
	memdev->address = SPA0_SIZE/2;
	memdev->interleave_index = 0;
	memdev->interleave_ways = 4;

	/* mem-region6 (spa/dcr0, dimm0) */
	memdev = nfit_buf + offset + sizeof(struct acpi_nfit_memory_map) * 6;
	memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
	memdev->header.length = sizeof(*memdev);
	memdev->device_handle = handle[0];
	memdev->physical_id = 0;
	memdev->region_id = 0;
	memdev->range_index = 2+1;
	memdev->region_index = 0+1;
	memdev->region_size = 0;
	memdev->region_offset = 0;
	memdev->address = 0;
	memdev->interleave_index = 0;
	memdev->interleave_ways = 1;

	/* mem-region7 (spa/dcr1, dimm1) */
	memdev = nfit_buf + offset + sizeof(struct acpi_nfit_memory_map) * 7;
	memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
	memdev->header.length = sizeof(*memdev);
	memdev->device_handle = handle[1];
	memdev->physical_id = 1;
	memdev->region_id = 0;
	memdev->range_index = 3+1;
	memdev->region_index = 1+1;
	memdev->region_size = 0;
	memdev->region_offset = 0;
	memdev->address = 0;
	memdev->interleave_index = 0;
	memdev->interleave_ways = 1;

	/* mem-region8 (spa/dcr2, dimm2) */
	memdev = nfit_buf + offset + sizeof(struct acpi_nfit_memory_map) * 8;
	memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
	memdev->header.length = sizeof(*memdev);
	memdev->device_handle = handle[2];
	memdev->physical_id = 2;
	memdev->region_id = 0;
	memdev->range_index = 4+1;
	memdev->region_index = 2+1;
	memdev->region_size = 0;
	memdev->region_offset = 0;
	memdev->address = 0;
	memdev->interleave_index = 0;
	memdev->interleave_ways = 1;

	/* mem-region9 (spa/dcr3, dimm3) */
	memdev = nfit_buf + offset + sizeof(struct acpi_nfit_memory_map) * 9;
	memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
	memdev->header.length = sizeof(*memdev);
	memdev->device_handle = handle[3];
	memdev->physical_id = 3;
	memdev->region_id = 0;
	memdev->range_index = 5+1;
	memdev->region_index = 3+1;
	memdev->region_size = 0;
	memdev->region_offset = 0;
	memdev->address = 0;
	memdev->interleave_index = 0;
	memdev->interleave_ways = 1;

	/* mem-region10 (spa/bdw0, dimm0) */
	memdev = nfit_buf + offset + sizeof(struct acpi_nfit_memory_map) * 10;
	memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
	memdev->header.length = sizeof(*memdev);
	memdev->device_handle = handle[0];
	memdev->physical_id = 0;
	memdev->region_id = 0;
	memdev->range_index = 6+1;
	memdev->region_index = 0+1;
	memdev->region_size = 0;
	memdev->region_offset = 0;
	memdev->address = 0;
	memdev->interleave_index = 0;
	memdev->interleave_ways = 1;

	/* mem-region11 (spa/bdw1, dimm1) */
	memdev = nfit_buf + offset + sizeof(struct acpi_nfit_memory_map) * 11;
	memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
	memdev->header.length = sizeof(*memdev);
	memdev->device_handle = handle[1];
	memdev->physical_id = 1;
	memdev->region_id = 0;
	memdev->range_index = 7+1;
	memdev->region_index = 1+1;
	memdev->region_size = 0;
	memdev->region_offset = 0;
	memdev->address = 0;
	memdev->interleave_index = 0;
	memdev->interleave_ways = 1;

	/* mem-region12 (spa/bdw2, dimm2) */
	memdev = nfit_buf + offset + sizeof(struct acpi_nfit_memory_map) * 12;
	memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
	memdev->header.length = sizeof(*memdev);
	memdev->device_handle = handle[2];
	memdev->physical_id = 2;
	memdev->region_id = 0;
	memdev->range_index = 8+1;
	memdev->region_index = 2+1;
	memdev->region_size = 0;
	memdev->region_offset = 0;
	memdev->address = 0;
	memdev->interleave_index = 0;
	memdev->interleave_ways = 1;

	/* mem-region13 (spa/dcr3, dimm3) */
	memdev = nfit_buf + offset + sizeof(struct acpi_nfit_memory_map) * 13;
	memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
	memdev->header.length = sizeof(*memdev);
	memdev->device_handle = handle[3];
	memdev->physical_id = 3;
	memdev->region_id = 0;
	memdev->range_index = 9+1;
	memdev->region_index = 3+1;
	memdev->region_size = 0;
	memdev->region_offset = 0;
	memdev->address = 0;
	memdev->interleave_index = 0;
	memdev->interleave_ways = 1;

	offset = offset + sizeof(struct acpi_nfit_memory_map) * 14;
	/* dcr-descriptor0: blk */
	dcr = nfit_buf + offset;
	dcr->header.type = ACPI_NFIT_TYPE_CONTROL_REGION;
	dcr->header.length = sizeof(struct acpi_nfit_control_region);
	dcr->region_index = 0+1;
	dcr_common_init(dcr);
	dcr->serial_number = ~handle[0];
	dcr->code = NFIT_FIC_BLK;
	dcr->windows = 1;
	dcr->window_size = DCR_SIZE;
	dcr->command_offset = 0;
	dcr->command_size = 8;
	dcr->status_offset = 8;
	dcr->status_size = 4;

	/* dcr-descriptor1: blk */
	dcr = nfit_buf + offset + sizeof(struct acpi_nfit_control_region);
	dcr->header.type = ACPI_NFIT_TYPE_CONTROL_REGION;
	dcr->header.length = sizeof(struct acpi_nfit_control_region);
	dcr->region_index = 1+1;
	dcr_common_init(dcr);
	dcr->serial_number = ~handle[1];
	dcr->code = NFIT_FIC_BLK;
	dcr->windows = 1;
	dcr->window_size = DCR_SIZE;
	dcr->command_offset = 0;
	dcr->command_size = 8;
	dcr->status_offset = 8;
	dcr->status_size = 4;

	/* dcr-descriptor2: blk */
	dcr = nfit_buf + offset + sizeof(struct acpi_nfit_control_region) * 2;
	dcr->header.type = ACPI_NFIT_TYPE_CONTROL_REGION;
	dcr->header.length = sizeof(struct acpi_nfit_control_region);
	dcr->region_index = 2+1;
	dcr_common_init(dcr);
	dcr->serial_number = ~handle[2];
	dcr->code = NFIT_FIC_BLK;
	dcr->windows = 1;
	dcr->window_size = DCR_SIZE;
	dcr->command_offset = 0;
	dcr->command_size = 8;
	dcr->status_offset = 8;
	dcr->status_size = 4;

	/* dcr-descriptor3: blk */
	dcr = nfit_buf + offset + sizeof(struct acpi_nfit_control_region) * 3;
	dcr->header.type = ACPI_NFIT_TYPE_CONTROL_REGION;
	dcr->header.length = sizeof(struct acpi_nfit_control_region);
	dcr->region_index = 3+1;
	dcr_common_init(dcr);
	dcr->serial_number = ~handle[3];
	dcr->code = NFIT_FIC_BLK;
	dcr->windows = 1;
	dcr->window_size = DCR_SIZE;
	dcr->command_offset = 0;
	dcr->command_size = 8;
	dcr->status_offset = 8;
	dcr->status_size = 4;

	offset = offset + sizeof(struct acpi_nfit_control_region) * 4;
	/* dcr-descriptor0: pmem */
	dcr = nfit_buf + offset;
	dcr->header.type = ACPI_NFIT_TYPE_CONTROL_REGION;
	dcr->header.length = offsetof(struct acpi_nfit_control_region,
			window_size);
	dcr->region_index = 4+1;
	dcr_common_init(dcr);
	dcr->serial_number = ~handle[0];
	dcr->code = NFIT_FIC_BYTEN;
	dcr->windows = 0;

	/* dcr-descriptor1: pmem */
	dcr = nfit_buf + offset + offsetof(struct acpi_nfit_control_region,
			window_size);
	dcr->header.type = ACPI_NFIT_TYPE_CONTROL_REGION;
	dcr->header.length = offsetof(struct acpi_nfit_control_region,
			window_size);
	dcr->region_index = 5+1;
	dcr_common_init(dcr);
	dcr->serial_number = ~handle[1];
	dcr->code = NFIT_FIC_BYTEN;
	dcr->windows = 0;

	/* dcr-descriptor2: pmem */
	dcr = nfit_buf + offset + offsetof(struct acpi_nfit_control_region,
			window_size) * 2;
	dcr->header.type = ACPI_NFIT_TYPE_CONTROL_REGION;
	dcr->header.length = offsetof(struct acpi_nfit_control_region,
			window_size);
	dcr->region_index = 6+1;
	dcr_common_init(dcr);
	dcr->serial_number = ~handle[2];
	dcr->code = NFIT_FIC_BYTEN;
	dcr->windows = 0;

	/* dcr-descriptor3: pmem */
	dcr = nfit_buf + offset + offsetof(struct acpi_nfit_control_region,
			window_size) * 3;
	dcr->header.type = ACPI_NFIT_TYPE_CONTROL_REGION;
	dcr->header.length = offsetof(struct acpi_nfit_control_region,
			window_size);
	dcr->region_index = 7+1;
	dcr_common_init(dcr);
	dcr->serial_number = ~handle[3];
	dcr->code = NFIT_FIC_BYTEN;
	dcr->windows = 0;

	offset = offset + offsetof(struct acpi_nfit_control_region,
			window_size) * 4;
	/* bdw0 (spa/dcr0, dimm0) */
	bdw = nfit_buf + offset;
	bdw->header.type = ACPI_NFIT_TYPE_DATA_REGION;
	bdw->header.length = sizeof(struct acpi_nfit_data_region);
	bdw->region_index = 0+1;
	bdw->windows = 1;
	bdw->offset = 0;
	bdw->size = BDW_SIZE;
	bdw->capacity = DIMM_SIZE;
	bdw->start_address = 0;

	/* bdw1 (spa/dcr1, dimm1) */
	bdw = nfit_buf + offset + sizeof(struct acpi_nfit_data_region);
	bdw->header.type = ACPI_NFIT_TYPE_DATA_REGION;
	bdw->header.length = sizeof(struct acpi_nfit_data_region);
	bdw->region_index = 1+1;
	bdw->windows = 1;
	bdw->offset = 0;
	bdw->size = BDW_SIZE;
	bdw->capacity = DIMM_SIZE;
	bdw->start_address = 0;

	/* bdw2 (spa/dcr2, dimm2) */
	bdw = nfit_buf + offset + sizeof(struct acpi_nfit_data_region) * 2;
	bdw->header.type = ACPI_NFIT_TYPE_DATA_REGION;
	bdw->header.length = sizeof(struct acpi_nfit_data_region);
	bdw->region_index = 2+1;
	bdw->windows = 1;
	bdw->offset = 0;
	bdw->size = BDW_SIZE;
	bdw->capacity = DIMM_SIZE;
	bdw->start_address = 0;

	/* bdw3 (spa/dcr3, dimm3) */
	bdw = nfit_buf + offset + sizeof(struct acpi_nfit_data_region) * 3;
	bdw->header.type = ACPI_NFIT_TYPE_DATA_REGION;
	bdw->header.length = sizeof(struct acpi_nfit_data_region);
	bdw->region_index = 3+1;
	bdw->windows = 1;
	bdw->offset = 0;
	bdw->size = BDW_SIZE;
	bdw->capacity = DIMM_SIZE;
	bdw->start_address = 0;

	offset = offset + sizeof(struct acpi_nfit_data_region) * 4;
	/* flush0 (dimm0) */
	flush = nfit_buf + offset;
	flush->header.type = ACPI_NFIT_TYPE_FLUSH_ADDRESS;
	flush->header.length = flush_hint_size;
	flush->device_handle = handle[0];
	flush->hint_count = NUM_HINTS;
	for (i = 0; i < NUM_HINTS; i++)
		flush->hint_address[i] = t->flush_dma[0] + i * sizeof(u64);

	/* flush1 (dimm1) */
	flush = nfit_buf + offset + flush_hint_size * 1;
	flush->header.type = ACPI_NFIT_TYPE_FLUSH_ADDRESS;
	flush->header.length = flush_hint_size;
	flush->device_handle = handle[1];
	flush->hint_count = NUM_HINTS;
	for (i = 0; i < NUM_HINTS; i++)
		flush->hint_address[i] = t->flush_dma[1] + i * sizeof(u64);

	/* flush2 (dimm2) */
	flush = nfit_buf + offset + flush_hint_size  * 2;
	flush->header.type = ACPI_NFIT_TYPE_FLUSH_ADDRESS;
	flush->header.length = flush_hint_size;
	flush->device_handle = handle[2];
	flush->hint_count = NUM_HINTS;
	for (i = 0; i < NUM_HINTS; i++)
		flush->hint_address[i] = t->flush_dma[2] + i * sizeof(u64);

	/* flush3 (dimm3) */
	flush = nfit_buf + offset + flush_hint_size * 3;
	flush->header.type = ACPI_NFIT_TYPE_FLUSH_ADDRESS;
	flush->header.length = flush_hint_size;
	flush->device_handle = handle[3];
	flush->hint_count = NUM_HINTS;
	for (i = 0; i < NUM_HINTS; i++)
		flush->hint_address[i] = t->flush_dma[3] + i * sizeof(u64);

	if (t->setup_hotplug) {
		offset = offset + flush_hint_size * 4;
		/* dcr-descriptor4: blk */
		dcr = nfit_buf + offset;
		dcr->header.type = ACPI_NFIT_TYPE_CONTROL_REGION;
		dcr->header.length = sizeof(struct acpi_nfit_control_region);
		dcr->region_index = 8+1;
		dcr_common_init(dcr);
		dcr->serial_number = ~handle[4];
		dcr->code = NFIT_FIC_BLK;
		dcr->windows = 1;
		dcr->window_size = DCR_SIZE;
		dcr->command_offset = 0;
		dcr->command_size = 8;
		dcr->status_offset = 8;
		dcr->status_size = 4;

		offset = offset + sizeof(struct acpi_nfit_control_region);
		/* dcr-descriptor4: pmem */
		dcr = nfit_buf + offset;
		dcr->header.type = ACPI_NFIT_TYPE_CONTROL_REGION;
		dcr->header.length = offsetof(struct acpi_nfit_control_region,
				window_size);
		dcr->region_index = 9+1;
		dcr_common_init(dcr);
		dcr->serial_number = ~handle[4];
		dcr->code = NFIT_FIC_BYTEN;
		dcr->windows = 0;

		offset = offset + offsetof(struct acpi_nfit_control_region,
				window_size);
		/* bdw4 (spa/dcr4, dimm4) */
		bdw = nfit_buf + offset;
		bdw->header.type = ACPI_NFIT_TYPE_DATA_REGION;
		bdw->header.length = sizeof(struct acpi_nfit_data_region);
		bdw->region_index = 8+1;
		bdw->windows = 1;
		bdw->offset = 0;
		bdw->size = BDW_SIZE;
		bdw->capacity = DIMM_SIZE;
		bdw->start_address = 0;

		offset = offset + sizeof(struct acpi_nfit_data_region);
		/* spa10 (dcr4) dimm4 */
		spa = nfit_buf + offset;
		spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
		spa->header.length = sizeof(*spa);
		memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_DCR), 16);
		spa->range_index = 10+1;
		spa->address = t->dcr_dma[4];
		spa->length = DCR_SIZE;

		/*
		 * spa11 (single-dimm interleave for hotplug, note storage
		 * does not actually alias the related block-data-window
		 * regions)
		 */
		spa = nfit_buf + offset + sizeof(*spa);
		spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
		spa->header.length = sizeof(*spa);
		memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_PM), 16);
		spa->range_index = 11+1;
		spa->address = t->spa_set_dma[2];
		spa->length = SPA0_SIZE;

		/* spa12 (bdw for dcr4) dimm4 */
		spa = nfit_buf + offset + sizeof(*spa) * 2;
		spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
		spa->header.length = sizeof(*spa);
		memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_BDW), 16);
		spa->range_index = 12+1;
		spa->address = t->dimm_dma[4];
		spa->length = DIMM_SIZE;

		offset = offset + sizeof(*spa) * 3;
		/* mem-region14 (spa/dcr4, dimm4) */
		memdev = nfit_buf + offset;
		memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
		memdev->header.length = sizeof(*memdev);
		memdev->device_handle = handle[4];
		memdev->physical_id = 4;
		memdev->region_id = 0;
		memdev->range_index = 10+1;
		memdev->region_index = 8+1;
		memdev->region_size = 0;
		memdev->region_offset = 0;
		memdev->address = 0;
		memdev->interleave_index = 0;
		memdev->interleave_ways = 1;

		/* mem-region15 (spa0, dimm4) */
		memdev = nfit_buf + offset +
				sizeof(struct acpi_nfit_memory_map);
		memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
		memdev->header.length = sizeof(*memdev);
		memdev->device_handle = handle[4];
		memdev->physical_id = 4;
		memdev->region_id = 0;
		memdev->range_index = 11+1;
		memdev->region_index = 9+1;
		memdev->region_size = SPA0_SIZE;
		memdev->region_offset = t->spa_set_dma[2];
		memdev->address = 0;
		memdev->interleave_index = 0;
		memdev->interleave_ways = 1;

		/* mem-region16 (spa/bdw4, dimm4) */
		memdev = nfit_buf + offset +
				sizeof(struct acpi_nfit_memory_map) * 2;
		memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
		memdev->header.length = sizeof(*memdev);
		memdev->device_handle = handle[4];
		memdev->physical_id = 4;
		memdev->region_id = 0;
		memdev->range_index = 12+1;
		memdev->region_index = 8+1;
		memdev->region_size = 0;
		memdev->region_offset = 0;
		memdev->address = 0;
		memdev->interleave_index = 0;
		memdev->interleave_ways = 1;

		offset = offset + sizeof(struct acpi_nfit_memory_map) * 3;
		/* flush3 (dimm4) */
		flush = nfit_buf + offset;
		flush->header.type = ACPI_NFIT_TYPE_FLUSH_ADDRESS;
		flush->header.length = flush_hint_size;
		flush->device_handle = handle[4];
		flush->hint_count = NUM_HINTS;
		for (i = 0; i < NUM_HINTS; i++)
			flush->hint_address[i] = t->flush_dma[4]
				+ i * sizeof(u64);
	}

	post_ars_status(&t->ars_state, t->spa_set_dma[0], SPA0_SIZE);

	acpi_desc = &t->acpi_desc;
	set_bit(ND_CMD_GET_CONFIG_SIZE, &acpi_desc->dimm_cmd_force_en);
	set_bit(ND_CMD_GET_CONFIG_DATA, &acpi_desc->dimm_cmd_force_en);
	set_bit(ND_CMD_SET_CONFIG_DATA, &acpi_desc->dimm_cmd_force_en);
	set_bit(ND_CMD_SMART, &acpi_desc->dimm_cmd_force_en);
	set_bit(ND_CMD_ARS_CAP, &acpi_desc->bus_cmd_force_en);
	set_bit(ND_CMD_ARS_START, &acpi_desc->bus_cmd_force_en);
	set_bit(ND_CMD_ARS_STATUS, &acpi_desc->bus_cmd_force_en);
	set_bit(ND_CMD_CLEAR_ERROR, &acpi_desc->bus_cmd_force_en);
	set_bit(ND_CMD_SMART_THRESHOLD, &acpi_desc->dimm_cmd_force_en);
}

static void nfit_test1_setup(struct nfit_test *t)
{
	size_t offset;
	void *nfit_buf = t->nfit_buf;
	struct acpi_nfit_memory_map *memdev;
	struct acpi_nfit_control_region *dcr;
	struct acpi_nfit_system_address *spa;
	struct acpi_nfit_desc *acpi_desc;

	offset = 0;
	/* spa0 (flat range with no bdw aliasing) */
	spa = nfit_buf + offset;
	spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
	spa->header.length = sizeof(*spa);
	memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_PM), 16);
	spa->range_index = 0+1;
	spa->address = t->spa_set_dma[0];
	spa->length = SPA2_SIZE;

	/* virtual cd region */
	spa = nfit_buf + sizeof(*spa);
	spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
	spa->header.length = sizeof(*spa);
	memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_VCD), 16);
	spa->range_index = 0;
	spa->address = t->spa_set_dma[1];
	spa->length = SPA_VCD_SIZE;

	offset += sizeof(*spa) * 2;
	/* mem-region0 (spa0, dimm0) */
	memdev = nfit_buf + offset;
	memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
	memdev->header.length = sizeof(*memdev);
	memdev->device_handle = 0;
	memdev->physical_id = 0;
	memdev->region_id = 0;
	memdev->range_index = 0+1;
	memdev->region_index = 0+1;
	memdev->region_size = SPA2_SIZE;
	memdev->region_offset = 0;
	memdev->address = 0;
	memdev->interleave_index = 0;
	memdev->interleave_ways = 1;
	memdev->flags = ACPI_NFIT_MEM_SAVE_FAILED | ACPI_NFIT_MEM_RESTORE_FAILED
		| ACPI_NFIT_MEM_FLUSH_FAILED | ACPI_NFIT_MEM_HEALTH_OBSERVED
		| ACPI_NFIT_MEM_NOT_ARMED;

	offset += sizeof(*memdev);
	/* dcr-descriptor0 */
	dcr = nfit_buf + offset;
	dcr->header.type = ACPI_NFIT_TYPE_CONTROL_REGION;
	dcr->header.length = offsetof(struct acpi_nfit_control_region,
			window_size);
	dcr->region_index = 0+1;
	dcr_common_init(dcr);
	dcr->serial_number = ~0;
	dcr->code = NFIT_FIC_BYTE;
	dcr->windows = 0;

	post_ars_status(&t->ars_state, t->spa_set_dma[0], SPA2_SIZE);

	acpi_desc = &t->acpi_desc;
	set_bit(ND_CMD_ARS_CAP, &acpi_desc->bus_cmd_force_en);
	set_bit(ND_CMD_ARS_START, &acpi_desc->bus_cmd_force_en);
	set_bit(ND_CMD_ARS_STATUS, &acpi_desc->bus_cmd_force_en);
	set_bit(ND_CMD_CLEAR_ERROR, &acpi_desc->bus_cmd_force_en);
}

static int nfit_test_blk_do_io(struct nd_blk_region *ndbr, resource_size_t dpa,
		void *iobuf, u64 len, int rw)
{
	struct nfit_blk *nfit_blk = ndbr->blk_provider_data;
	struct nfit_blk_mmio *mmio = &nfit_blk->mmio[BDW];
	struct nd_region *nd_region = &ndbr->nd_region;
	unsigned int lane;

	lane = nd_region_acquire_lane(nd_region);
	if (rw)
		memcpy(mmio->addr.base + dpa, iobuf, len);
	else {
		memcpy(iobuf, mmio->addr.base + dpa, len);

		/* give us some some coverage of the mmio_flush_range() API */
		mmio_flush_range(mmio->addr.base + dpa, len);
	}
	nd_region_release_lane(nd_region, lane);

	return 0;
}

static int nfit_test_probe(struct platform_device *pdev)
{
	struct nvdimm_bus_descriptor *nd_desc;
	struct acpi_nfit_desc *acpi_desc;
	struct device *dev = &pdev->dev;
	struct nfit_test *nfit_test;
	int rc;

	nfit_test = to_nfit_test(&pdev->dev);

	/* common alloc */
	if (nfit_test->num_dcr) {
		int num = nfit_test->num_dcr;

		nfit_test->dimm = devm_kcalloc(dev, num, sizeof(void *),
				GFP_KERNEL);
		nfit_test->dimm_dma = devm_kcalloc(dev, num, sizeof(dma_addr_t),
				GFP_KERNEL);
		nfit_test->flush = devm_kcalloc(dev, num, sizeof(void *),
				GFP_KERNEL);
		nfit_test->flush_dma = devm_kcalloc(dev, num, sizeof(dma_addr_t),
				GFP_KERNEL);
		nfit_test->label = devm_kcalloc(dev, num, sizeof(void *),
				GFP_KERNEL);
		nfit_test->label_dma = devm_kcalloc(dev, num,
				sizeof(dma_addr_t), GFP_KERNEL);
		nfit_test->dcr = devm_kcalloc(dev, num,
				sizeof(struct nfit_test_dcr *), GFP_KERNEL);
		nfit_test->dcr_dma = devm_kcalloc(dev, num,
				sizeof(dma_addr_t), GFP_KERNEL);
		if (nfit_test->dimm && nfit_test->dimm_dma && nfit_test->label
				&& nfit_test->label_dma && nfit_test->dcr
				&& nfit_test->dcr_dma && nfit_test->flush
				&& nfit_test->flush_dma)
			/* pass */;
		else
			return -ENOMEM;
	}

	if (nfit_test->num_pm) {
		int num = nfit_test->num_pm;

		nfit_test->spa_set = devm_kcalloc(dev, num, sizeof(void *),
				GFP_KERNEL);
		nfit_test->spa_set_dma = devm_kcalloc(dev, num,
				sizeof(dma_addr_t), GFP_KERNEL);
		if (nfit_test->spa_set && nfit_test->spa_set_dma)
			/* pass */;
		else
			return -ENOMEM;
	}

	/* per-nfit specific alloc */
	if (nfit_test->alloc(nfit_test))
		return -ENOMEM;

	nfit_test->setup(nfit_test);
	acpi_desc = &nfit_test->acpi_desc;
	acpi_nfit_desc_init(acpi_desc, &pdev->dev);
	acpi_desc->blk_do_io = nfit_test_blk_do_io;
	nd_desc = &acpi_desc->nd_desc;
	nd_desc->provider_name = NULL;
	nd_desc->ndctl = nfit_test_ctl;
	acpi_desc->nvdimm_bus = nvdimm_bus_register(&pdev->dev, nd_desc);
	if (!acpi_desc->nvdimm_bus)
		return -ENXIO;

	rc = acpi_nfit_init(acpi_desc, nfit_test->nfit_buf,
			nfit_test->nfit_size);
	if (rc) {
		nvdimm_bus_unregister(acpi_desc->nvdimm_bus);
		return rc;
	}

	if (nfit_test->setup != nfit_test0_setup)
		return 0;

	nfit_test->setup_hotplug = 1;
	nfit_test->setup(nfit_test);

	rc = acpi_nfit_init(acpi_desc, nfit_test->nfit_buf,
			nfit_test->nfit_size);
	if (rc) {
		nvdimm_bus_unregister(acpi_desc->nvdimm_bus);
		return rc;
	}

	return 0;
}

static int nfit_test_remove(struct platform_device *pdev)
{
	struct nfit_test *nfit_test = to_nfit_test(&pdev->dev);
	struct acpi_nfit_desc *acpi_desc = &nfit_test->acpi_desc;

	nvdimm_bus_unregister(acpi_desc->nvdimm_bus);

	return 0;
}

static void nfit_test_release(struct device *dev)
{
	struct nfit_test *nfit_test = to_nfit_test(dev);

	kfree(nfit_test);
}

static const struct platform_device_id nfit_test_id[] = {
	{ KBUILD_MODNAME },
	{ },
};

static struct platform_driver nfit_test_driver = {
	.probe = nfit_test_probe,
	.remove = nfit_test_remove,
	.driver = {
		.name = KBUILD_MODNAME,
	},
	.id_table = nfit_test_id,
};

static __init int nfit_test_init(void)
{
	int rc, i;

	nfit_test_setup(nfit_test_lookup);

	for (i = 0; i < NUM_NFITS; i++) {
		struct nfit_test *nfit_test;
		struct platform_device *pdev;

		nfit_test = kzalloc(sizeof(*nfit_test), GFP_KERNEL);
		if (!nfit_test) {
			rc = -ENOMEM;
			goto err_register;
		}
		INIT_LIST_HEAD(&nfit_test->resources);
		switch (i) {
		case 0:
			nfit_test->num_pm = NUM_PM;
			nfit_test->num_dcr = NUM_DCR;
			nfit_test->alloc = nfit_test0_alloc;
			nfit_test->setup = nfit_test0_setup;
			break;
		case 1:
			nfit_test->num_pm = 1;
			nfit_test->alloc = nfit_test1_alloc;
			nfit_test->setup = nfit_test1_setup;
			break;
		default:
			rc = -EINVAL;
			goto err_register;
		}
		pdev = &nfit_test->pdev;
		pdev->name = KBUILD_MODNAME;
		pdev->id = i;
		pdev->dev.release = nfit_test_release;
		rc = platform_device_register(pdev);
		if (rc) {
			put_device(&pdev->dev);
			goto err_register;
		}

		rc = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
		if (rc)
			goto err_register;

		instances[i] = nfit_test;
	}

	rc = platform_driver_register(&nfit_test_driver);
	if (rc)
		goto err_register;
	return 0;

 err_register:
	for (i = 0; i < NUM_NFITS; i++)
		if (instances[i])
			platform_device_unregister(&instances[i]->pdev);
	nfit_test_teardown();
	return rc;
}

static __exit void nfit_test_exit(void)
{
	int i;

	platform_driver_unregister(&nfit_test_driver);
	for (i = 0; i < NUM_NFITS; i++)
		platform_device_unregister(&instances[i]->pdev);
	nfit_test_teardown();
}

module_init(nfit_test_init);
module_exit(nfit_test_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Intel Corporation");
