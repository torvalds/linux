// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright(c) 2013-2015 Intel Corporation. All rights reserved.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/workqueue.h>
#include <linux/libnvdimm.h>
#include <linux/genalloc.h>
#include <linux/vmalloc.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/ndctl.h>
#include <linux/sizes.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <nd-core.h>
#include <intel.h>
#include <nfit.h>
#include <nd.h>
#include "nfit_test.h"
#include "../watermark.h"

#include <asm/mcsafe_test.h>

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
	NUM_MEM = NUM_DCR + NUM_BDW + 2 /* spa0 iset */
		+ 4 /* spa1 iset */ + 1 /* spa11 iset */,
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

static u32 handle[] = {
	[0] = NFIT_DIMM_HANDLE(0, 0, 0, 0, 0),
	[1] = NFIT_DIMM_HANDLE(0, 0, 0, 0, 1),
	[2] = NFIT_DIMM_HANDLE(0, 0, 1, 0, 0),
	[3] = NFIT_DIMM_HANDLE(0, 0, 1, 0, 1),
	[4] = NFIT_DIMM_HANDLE(0, 1, 0, 0, 0),
	[5] = NFIT_DIMM_HANDLE(1, 0, 0, 0, 0),
	[6] = NFIT_DIMM_HANDLE(1, 0, 0, 0, 1),
};

static unsigned long dimm_fail_cmd_flags[ARRAY_SIZE(handle)];
static int dimm_fail_cmd_code[ARRAY_SIZE(handle)];
struct nfit_test_sec {
	u8 state;
	u8 ext_state;
	u8 old_state;
	u8 passphrase[32];
	u8 master_passphrase[32];
	u64 overwrite_end_time;
} dimm_sec_info[NUM_DCR];

static const struct nd_intel_smart smart_def = {
	.flags = ND_INTEL_SMART_HEALTH_VALID
		| ND_INTEL_SMART_SPARES_VALID
		| ND_INTEL_SMART_ALARM_VALID
		| ND_INTEL_SMART_USED_VALID
		| ND_INTEL_SMART_SHUTDOWN_VALID
		| ND_INTEL_SMART_SHUTDOWN_COUNT_VALID
		| ND_INTEL_SMART_MTEMP_VALID
		| ND_INTEL_SMART_CTEMP_VALID,
	.health = ND_INTEL_SMART_NON_CRITICAL_HEALTH,
	.media_temperature = 23 * 16,
	.ctrl_temperature = 25 * 16,
	.pmic_temperature = 40 * 16,
	.spares = 75,
	.alarm_flags = ND_INTEL_SMART_SPARE_TRIP
		| ND_INTEL_SMART_TEMP_TRIP,
	.ait_status = 1,
	.life_used = 5,
	.shutdown_state = 0,
	.shutdown_count = 42,
	.vendor_size = 0,
};

struct nfit_test_fw {
	enum intel_fw_update_state state;
	u32 context;
	u64 version;
	u32 size_received;
	u64 end_time;
};

struct nfit_test {
	struct acpi_nfit_desc acpi_desc;
	struct platform_device pdev;
	struct list_head resources;
	void *nfit_buf;
	dma_addr_t nfit_dma;
	size_t nfit_size;
	size_t nfit_filled;
	int dcr_idx;
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
	union acpi_object **_fit;
	dma_addr_t _fit_dma;
	struct ars_state {
		struct nd_cmd_ars_status *ars_status;
		unsigned long deadline;
		spinlock_t lock;
	} ars_state;
	struct device *dimm_dev[ARRAY_SIZE(handle)];
	struct nd_intel_smart *smart;
	struct nd_intel_smart_threshold *smart_threshold;
	struct badrange badrange;
	struct work_struct work;
	struct nfit_test_fw *fw;
};

static struct workqueue_struct *nfit_wq;

static struct gen_pool *nfit_pool;

static const char zero_key[NVDIMM_PASSPHRASE_LEN];

static struct nfit_test *to_nfit_test(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	return container_of(pdev, struct nfit_test, pdev);
}

static int nd_intel_test_get_fw_info(struct nfit_test *t,
		struct nd_intel_fw_info *nd_cmd, unsigned int buf_len,
		int idx)
{
	struct device *dev = &t->pdev.dev;
	struct nfit_test_fw *fw = &t->fw[idx];

	dev_dbg(dev, "%s(nfit_test: %p nd_cmd: %p, buf_len: %u, idx: %d\n",
			__func__, t, nd_cmd, buf_len, idx);

	if (buf_len < sizeof(*nd_cmd))
		return -EINVAL;

	nd_cmd->status = 0;
	nd_cmd->storage_size = INTEL_FW_STORAGE_SIZE;
	nd_cmd->max_send_len = INTEL_FW_MAX_SEND_LEN;
	nd_cmd->query_interval = INTEL_FW_QUERY_INTERVAL;
	nd_cmd->max_query_time = INTEL_FW_QUERY_MAX_TIME;
	nd_cmd->update_cap = 0;
	nd_cmd->fis_version = INTEL_FW_FIS_VERSION;
	nd_cmd->run_version = 0;
	nd_cmd->updated_version = fw->version;

	return 0;
}

static int nd_intel_test_start_update(struct nfit_test *t,
		struct nd_intel_fw_start *nd_cmd, unsigned int buf_len,
		int idx)
{
	struct device *dev = &t->pdev.dev;
	struct nfit_test_fw *fw = &t->fw[idx];

	dev_dbg(dev, "%s(nfit_test: %p nd_cmd: %p buf_len: %u idx: %d)\n",
			__func__, t, nd_cmd, buf_len, idx);

	if (buf_len < sizeof(*nd_cmd))
		return -EINVAL;

	if (fw->state != FW_STATE_NEW) {
		/* extended status, FW update in progress */
		nd_cmd->status = 0x10007;
		return 0;
	}

	fw->state = FW_STATE_IN_PROGRESS;
	fw->context++;
	fw->size_received = 0;
	nd_cmd->status = 0;
	nd_cmd->context = fw->context;

	dev_dbg(dev, "%s: context issued: %#x\n", __func__, nd_cmd->context);

	return 0;
}

static int nd_intel_test_send_data(struct nfit_test *t,
		struct nd_intel_fw_send_data *nd_cmd, unsigned int buf_len,
		int idx)
{
	struct device *dev = &t->pdev.dev;
	struct nfit_test_fw *fw = &t->fw[idx];
	u32 *status = (u32 *)&nd_cmd->data[nd_cmd->length];

	dev_dbg(dev, "%s(nfit_test: %p nd_cmd: %p buf_len: %u idx: %d)\n",
			__func__, t, nd_cmd, buf_len, idx);

	if (buf_len < sizeof(*nd_cmd))
		return -EINVAL;


	dev_dbg(dev, "%s: cmd->status: %#x\n", __func__, *status);
	dev_dbg(dev, "%s: cmd->data[0]: %#x\n", __func__, nd_cmd->data[0]);
	dev_dbg(dev, "%s: cmd->data[%u]: %#x\n", __func__, nd_cmd->length-1,
			nd_cmd->data[nd_cmd->length-1]);

	if (fw->state != FW_STATE_IN_PROGRESS) {
		dev_dbg(dev, "%s: not in IN_PROGRESS state\n", __func__);
		*status = 0x5;
		return 0;
	}

	if (nd_cmd->context != fw->context) {
		dev_dbg(dev, "%s: incorrect context: in: %#x correct: %#x\n",
				__func__, nd_cmd->context, fw->context);
		*status = 0x10007;
		return 0;
	}

	/*
	 * check offset + len > size of fw storage
	 * check length is > max send length
	 */
	if (nd_cmd->offset + nd_cmd->length > INTEL_FW_STORAGE_SIZE ||
			nd_cmd->length > INTEL_FW_MAX_SEND_LEN) {
		*status = 0x3;
		dev_dbg(dev, "%s: buffer boundary violation\n", __func__);
		return 0;
	}

	fw->size_received += nd_cmd->length;
	dev_dbg(dev, "%s: copying %u bytes, %u bytes so far\n",
			__func__, nd_cmd->length, fw->size_received);
	*status = 0;
	return 0;
}

static int nd_intel_test_finish_fw(struct nfit_test *t,
		struct nd_intel_fw_finish_update *nd_cmd,
		unsigned int buf_len, int idx)
{
	struct device *dev = &t->pdev.dev;
	struct nfit_test_fw *fw = &t->fw[idx];

	dev_dbg(dev, "%s(nfit_test: %p nd_cmd: %p buf_len: %u idx: %d)\n",
			__func__, t, nd_cmd, buf_len, idx);

	if (fw->state == FW_STATE_UPDATED) {
		/* update already done, need cold boot */
		nd_cmd->status = 0x20007;
		return 0;
	}

	dev_dbg(dev, "%s: context: %#x  ctrl_flags: %#x\n",
			__func__, nd_cmd->context, nd_cmd->ctrl_flags);

	switch (nd_cmd->ctrl_flags) {
	case 0: /* finish */
		if (nd_cmd->context != fw->context) {
			dev_dbg(dev, "%s: incorrect context: in: %#x correct: %#x\n",
					__func__, nd_cmd->context,
					fw->context);
			nd_cmd->status = 0x10007;
			return 0;
		}
		nd_cmd->status = 0;
		fw->state = FW_STATE_VERIFY;
		/* set 1 second of time for firmware "update" */
		fw->end_time = jiffies + HZ;
		break;

	case 1: /* abort */
		fw->size_received = 0;
		/* successfully aborted status */
		nd_cmd->status = 0x40007;
		fw->state = FW_STATE_NEW;
		dev_dbg(dev, "%s: abort successful\n", __func__);
		break;

	default: /* bad control flag */
		dev_warn(dev, "%s: unknown control flag: %#x\n",
				__func__, nd_cmd->ctrl_flags);
		return -EINVAL;
	}

	return 0;
}

static int nd_intel_test_finish_query(struct nfit_test *t,
		struct nd_intel_fw_finish_query *nd_cmd,
		unsigned int buf_len, int idx)
{
	struct device *dev = &t->pdev.dev;
	struct nfit_test_fw *fw = &t->fw[idx];

	dev_dbg(dev, "%s(nfit_test: %p nd_cmd: %p buf_len: %u idx: %d)\n",
			__func__, t, nd_cmd, buf_len, idx);

	if (buf_len < sizeof(*nd_cmd))
		return -EINVAL;

	if (nd_cmd->context != fw->context) {
		dev_dbg(dev, "%s: incorrect context: in: %#x correct: %#x\n",
				__func__, nd_cmd->context, fw->context);
		nd_cmd->status = 0x10007;
		return 0;
	}

	dev_dbg(dev, "%s context: %#x\n", __func__, nd_cmd->context);

	switch (fw->state) {
	case FW_STATE_NEW:
		nd_cmd->updated_fw_rev = 0;
		nd_cmd->status = 0;
		dev_dbg(dev, "%s: new state\n", __func__);
		break;

	case FW_STATE_IN_PROGRESS:
		/* sequencing error */
		nd_cmd->status = 0x40007;
		nd_cmd->updated_fw_rev = 0;
		dev_dbg(dev, "%s: sequence error\n", __func__);
		break;

	case FW_STATE_VERIFY:
		if (time_is_after_jiffies64(fw->end_time)) {
			nd_cmd->updated_fw_rev = 0;
			nd_cmd->status = 0x20007;
			dev_dbg(dev, "%s: still verifying\n", __func__);
			break;
		}
		dev_dbg(dev, "%s: transition out verify\n", __func__);
		fw->state = FW_STATE_UPDATED;
		/* fall through */
	case FW_STATE_UPDATED:
		nd_cmd->status = 0;
		/* bogus test version */
		fw->version = nd_cmd->updated_fw_rev =
			INTEL_FW_FAKE_VERSION;
		dev_dbg(dev, "%s: updated\n", __func__);
		break;

	default: /* we should never get here */
		return -EINVAL;
	}

	return 0;
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

#define NFIT_TEST_CLEAR_ERR_UNIT 256

static int nfit_test_cmd_ars_cap(struct nd_cmd_ars_cap *nd_cmd,
		unsigned int buf_len)
{
	int ars_recs;

	if (buf_len < sizeof(*nd_cmd))
		return -EINVAL;

	/* for testing, only store up to n records that fit within 4k */
	ars_recs = SZ_4K / sizeof(struct nd_ars_record);

	nd_cmd->max_ars_out = sizeof(struct nd_cmd_ars_status)
		+ ars_recs * sizeof(struct nd_ars_record);
	nd_cmd->status = (ND_ARS_PERSISTENT | ND_ARS_VOLATILE) << 16;
	nd_cmd->clear_err_unit = NFIT_TEST_CLEAR_ERR_UNIT;

	return 0;
}

static void post_ars_status(struct ars_state *ars_state,
		struct badrange *badrange, u64 addr, u64 len)
{
	struct nd_cmd_ars_status *ars_status;
	struct nd_ars_record *ars_record;
	struct badrange_entry *be;
	u64 end = addr + len - 1;
	int i = 0;

	ars_state->deadline = jiffies + 1*HZ;
	ars_status = ars_state->ars_status;
	ars_status->status = 0;
	ars_status->address = addr;
	ars_status->length = len;
	ars_status->type = ND_ARS_PERSISTENT;

	spin_lock(&badrange->lock);
	list_for_each_entry(be, &badrange->list, list) {
		u64 be_end = be->start + be->length - 1;
		u64 rstart, rend;

		/* skip entries outside the range */
		if (be_end < addr || be->start > end)
			continue;

		rstart = (be->start < addr) ? addr : be->start;
		rend = (be_end < end) ? be_end : end;
		ars_record = &ars_status->records[i];
		ars_record->handle = 0;
		ars_record->err_address = rstart;
		ars_record->length = rend - rstart + 1;
		i++;
	}
	spin_unlock(&badrange->lock);
	ars_status->num_records = i;
	ars_status->out_length = sizeof(struct nd_cmd_ars_status)
		+ i * sizeof(struct nd_ars_record);
}

static int nfit_test_cmd_ars_start(struct nfit_test *t,
		struct ars_state *ars_state,
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
		post_ars_status(ars_state, &t->badrange, ars_start->address,
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

static int nfit_test_cmd_clear_error(struct nfit_test *t,
		struct nd_cmd_clear_error *clear_err,
		unsigned int buf_len, int *cmd_rc)
{
	const u64 mask = NFIT_TEST_CLEAR_ERR_UNIT - 1;
	if (buf_len < sizeof(*clear_err))
		return -EINVAL;

	if ((clear_err->address & mask) || (clear_err->length & mask))
		return -EINVAL;

	badrange_forget(&t->badrange, clear_err->address, clear_err->length);
	clear_err->status = 0;
	clear_err->cleared = clear_err->length;
	*cmd_rc = 0;
	return 0;
}

struct region_search_spa {
	u64 addr;
	struct nd_region *region;
};

static int is_region_device(struct device *dev)
{
	return !strncmp(dev->kobj.name, "region", 6);
}

static int nfit_test_search_region_spa(struct device *dev, void *data)
{
	struct region_search_spa *ctx = data;
	struct nd_region *nd_region;
	resource_size_t ndr_end;

	if (!is_region_device(dev))
		return 0;

	nd_region = to_nd_region(dev);
	ndr_end = nd_region->ndr_start + nd_region->ndr_size;

	if (ctx->addr >= nd_region->ndr_start && ctx->addr < ndr_end) {
		ctx->region = nd_region;
		return 1;
	}

	return 0;
}

static int nfit_test_search_spa(struct nvdimm_bus *bus,
		struct nd_cmd_translate_spa *spa)
{
	int ret;
	struct nd_region *nd_region = NULL;
	struct nvdimm *nvdimm = NULL;
	struct nd_mapping *nd_mapping = NULL;
	struct region_search_spa ctx = {
		.addr = spa->spa,
		.region = NULL,
	};
	u64 dpa;

	ret = device_for_each_child(&bus->dev, &ctx,
				nfit_test_search_region_spa);

	if (!ret)
		return -ENODEV;

	nd_region = ctx.region;

	dpa = ctx.addr - nd_region->ndr_start;

	/*
	 * last dimm is selected for test
	 */
	nd_mapping = &nd_region->mapping[nd_region->ndr_mappings - 1];
	nvdimm = nd_mapping->nvdimm;

	spa->devices[0].nfit_device_handle = handle[nvdimm->id];
	spa->num_nvdimms = 1;
	spa->devices[0].dpa = dpa;

	return 0;
}

static int nfit_test_cmd_translate_spa(struct nvdimm_bus *bus,
		struct nd_cmd_translate_spa *spa, unsigned int buf_len)
{
	if (buf_len < spa->translate_length)
		return -EINVAL;

	if (nfit_test_search_spa(bus, spa) < 0 || !spa->num_nvdimms)
		spa->status = 2;

	return 0;
}

static int nfit_test_cmd_smart(struct nd_intel_smart *smart, unsigned int buf_len,
		struct nd_intel_smart *smart_data)
{
	if (buf_len < sizeof(*smart))
		return -EINVAL;
	memcpy(smart, smart_data, sizeof(*smart));
	return 0;
}

static int nfit_test_cmd_smart_threshold(
		struct nd_intel_smart_threshold *out,
		unsigned int buf_len,
		struct nd_intel_smart_threshold *smart_t)
{
	if (buf_len < sizeof(*smart_t))
		return -EINVAL;
	memcpy(out, smart_t, sizeof(*smart_t));
	return 0;
}

static void smart_notify(struct device *bus_dev,
		struct device *dimm_dev, struct nd_intel_smart *smart,
		struct nd_intel_smart_threshold *thresh)
{
	dev_dbg(dimm_dev, "%s: alarm: %#x spares: %d (%d) mtemp: %d (%d) ctemp: %d (%d)\n",
			__func__, thresh->alarm_control, thresh->spares,
			smart->spares, thresh->media_temperature,
			smart->media_temperature, thresh->ctrl_temperature,
			smart->ctrl_temperature);
	if (((thresh->alarm_control & ND_INTEL_SMART_SPARE_TRIP)
				&& smart->spares
				<= thresh->spares)
			|| ((thresh->alarm_control & ND_INTEL_SMART_TEMP_TRIP)
				&& smart->media_temperature
				>= thresh->media_temperature)
			|| ((thresh->alarm_control & ND_INTEL_SMART_CTEMP_TRIP)
				&& smart->ctrl_temperature
				>= thresh->ctrl_temperature)
			|| (smart->health != ND_INTEL_SMART_NON_CRITICAL_HEALTH)
			|| (smart->shutdown_state != 0)) {
		device_lock(bus_dev);
		__acpi_nvdimm_notify(dimm_dev, 0x81);
		device_unlock(bus_dev);
	}
}

static int nfit_test_cmd_smart_set_threshold(
		struct nd_intel_smart_set_threshold *in,
		unsigned int buf_len,
		struct nd_intel_smart_threshold *thresh,
		struct nd_intel_smart *smart,
		struct device *bus_dev, struct device *dimm_dev)
{
	unsigned int size;

	size = sizeof(*in) - 4;
	if (buf_len < size)
		return -EINVAL;
	memcpy(thresh->data, in, size);
	in->status = 0;
	smart_notify(bus_dev, dimm_dev, smart, thresh);

	return 0;
}

static int nfit_test_cmd_smart_inject(
		struct nd_intel_smart_inject *inj,
		unsigned int buf_len,
		struct nd_intel_smart_threshold *thresh,
		struct nd_intel_smart *smart,
		struct device *bus_dev, struct device *dimm_dev)
{
	if (buf_len != sizeof(*inj))
		return -EINVAL;

	if (inj->flags & ND_INTEL_SMART_INJECT_MTEMP) {
		if (inj->mtemp_enable)
			smart->media_temperature = inj->media_temperature;
		else
			smart->media_temperature = smart_def.media_temperature;
	}
	if (inj->flags & ND_INTEL_SMART_INJECT_SPARE) {
		if (inj->spare_enable)
			smart->spares = inj->spares;
		else
			smart->spares = smart_def.spares;
	}
	if (inj->flags & ND_INTEL_SMART_INJECT_FATAL) {
		if (inj->fatal_enable)
			smart->health = ND_INTEL_SMART_FATAL_HEALTH;
		else
			smart->health = ND_INTEL_SMART_NON_CRITICAL_HEALTH;
	}
	if (inj->flags & ND_INTEL_SMART_INJECT_SHUTDOWN) {
		if (inj->unsafe_shutdown_enable) {
			smart->shutdown_state = 1;
			smart->shutdown_count++;
		} else
			smart->shutdown_state = 0;
	}
	inj->status = 0;
	smart_notify(bus_dev, dimm_dev, smart, thresh);

	return 0;
}

static void uc_error_notify(struct work_struct *work)
{
	struct nfit_test *t = container_of(work, typeof(*t), work);

	__acpi_nfit_notify(&t->pdev.dev, t, NFIT_NOTIFY_UC_MEMORY_ERROR);
}

static int nfit_test_cmd_ars_error_inject(struct nfit_test *t,
		struct nd_cmd_ars_err_inj *err_inj, unsigned int buf_len)
{
	int rc;

	if (buf_len != sizeof(*err_inj)) {
		rc = -EINVAL;
		goto err;
	}

	if (err_inj->err_inj_spa_range_length <= 0) {
		rc = -EINVAL;
		goto err;
	}

	rc =  badrange_add(&t->badrange, err_inj->err_inj_spa_range_base,
			err_inj->err_inj_spa_range_length);
	if (rc < 0)
		goto err;

	if (err_inj->err_inj_options & (1 << ND_ARS_ERR_INJ_OPT_NOTIFY))
		queue_work(nfit_wq, &t->work);

	err_inj->status = 0;
	return 0;

err:
	err_inj->status = NFIT_ARS_INJECT_INVALID;
	return rc;
}

static int nfit_test_cmd_ars_inject_clear(struct nfit_test *t,
		struct nd_cmd_ars_err_inj_clr *err_clr, unsigned int buf_len)
{
	int rc;

	if (buf_len != sizeof(*err_clr)) {
		rc = -EINVAL;
		goto err;
	}

	if (err_clr->err_inj_clr_spa_range_length <= 0) {
		rc = -EINVAL;
		goto err;
	}

	badrange_forget(&t->badrange, err_clr->err_inj_clr_spa_range_base,
			err_clr->err_inj_clr_spa_range_length);

	err_clr->status = 0;
	return 0;

err:
	err_clr->status = NFIT_ARS_INJECT_INVALID;
	return rc;
}

static int nfit_test_cmd_ars_inject_status(struct nfit_test *t,
		struct nd_cmd_ars_err_inj_stat *err_stat,
		unsigned int buf_len)
{
	struct badrange_entry *be;
	int max = SZ_4K / sizeof(struct nd_error_stat_query_record);
	int i = 0;

	err_stat->status = 0;
	spin_lock(&t->badrange.lock);
	list_for_each_entry(be, &t->badrange.list, list) {
		err_stat->record[i].err_inj_stat_spa_range_base = be->start;
		err_stat->record[i].err_inj_stat_spa_range_length = be->length;
		i++;
		if (i > max)
			break;
	}
	spin_unlock(&t->badrange.lock);
	err_stat->inj_err_rec_count = i;

	return 0;
}

static int nd_intel_test_cmd_set_lss_status(struct nfit_test *t,
		struct nd_intel_lss *nd_cmd, unsigned int buf_len)
{
	struct device *dev = &t->pdev.dev;

	if (buf_len < sizeof(*nd_cmd))
		return -EINVAL;

	switch (nd_cmd->enable) {
	case 0:
		nd_cmd->status = 0;
		dev_dbg(dev, "%s: Latch System Shutdown Status disabled\n",
				__func__);
		break;
	case 1:
		nd_cmd->status = 0;
		dev_dbg(dev, "%s: Latch System Shutdown Status enabled\n",
				__func__);
		break;
	default:
		dev_warn(dev, "Unknown enable value: %#x\n", nd_cmd->enable);
		nd_cmd->status = 0x3;
		break;
	}


	return 0;
}

static int override_return_code(int dimm, unsigned int func, int rc)
{
	if ((1 << func) & dimm_fail_cmd_flags[dimm]) {
		if (dimm_fail_cmd_code[dimm])
			return dimm_fail_cmd_code[dimm];
		return -EIO;
	}
	return rc;
}

static int nd_intel_test_cmd_security_status(struct nfit_test *t,
		struct nd_intel_get_security_state *nd_cmd,
		unsigned int buf_len, int dimm)
{
	struct device *dev = &t->pdev.dev;
	struct nfit_test_sec *sec = &dimm_sec_info[dimm];

	nd_cmd->status = 0;
	nd_cmd->state = sec->state;
	nd_cmd->extended_state = sec->ext_state;
	dev_dbg(dev, "security state (%#x) returned\n", nd_cmd->state);

	return 0;
}

static int nd_intel_test_cmd_unlock_unit(struct nfit_test *t,
		struct nd_intel_unlock_unit *nd_cmd,
		unsigned int buf_len, int dimm)
{
	struct device *dev = &t->pdev.dev;
	struct nfit_test_sec *sec = &dimm_sec_info[dimm];

	if (!(sec->state & ND_INTEL_SEC_STATE_LOCKED) ||
			(sec->state & ND_INTEL_SEC_STATE_FROZEN)) {
		nd_cmd->status = ND_INTEL_STATUS_INVALID_STATE;
		dev_dbg(dev, "unlock unit: invalid state: %#x\n",
				sec->state);
	} else if (memcmp(nd_cmd->passphrase, sec->passphrase,
				ND_INTEL_PASSPHRASE_SIZE) != 0) {
		nd_cmd->status = ND_INTEL_STATUS_INVALID_PASS;
		dev_dbg(dev, "unlock unit: invalid passphrase\n");
	} else {
		nd_cmd->status = 0;
		sec->state = ND_INTEL_SEC_STATE_ENABLED;
		dev_dbg(dev, "Unit unlocked\n");
	}

	dev_dbg(dev, "unlocking status returned: %#x\n", nd_cmd->status);
	return 0;
}

static int nd_intel_test_cmd_set_pass(struct nfit_test *t,
		struct nd_intel_set_passphrase *nd_cmd,
		unsigned int buf_len, int dimm)
{
	struct device *dev = &t->pdev.dev;
	struct nfit_test_sec *sec = &dimm_sec_info[dimm];

	if (sec->state & ND_INTEL_SEC_STATE_FROZEN) {
		nd_cmd->status = ND_INTEL_STATUS_INVALID_STATE;
		dev_dbg(dev, "set passphrase: wrong security state\n");
	} else if (memcmp(nd_cmd->old_pass, sec->passphrase,
				ND_INTEL_PASSPHRASE_SIZE) != 0) {
		nd_cmd->status = ND_INTEL_STATUS_INVALID_PASS;
		dev_dbg(dev, "set passphrase: wrong passphrase\n");
	} else {
		memcpy(sec->passphrase, nd_cmd->new_pass,
				ND_INTEL_PASSPHRASE_SIZE);
		sec->state |= ND_INTEL_SEC_STATE_ENABLED;
		nd_cmd->status = 0;
		dev_dbg(dev, "passphrase updated\n");
	}

	return 0;
}

static int nd_intel_test_cmd_freeze_lock(struct nfit_test *t,
		struct nd_intel_freeze_lock *nd_cmd,
		unsigned int buf_len, int dimm)
{
	struct device *dev = &t->pdev.dev;
	struct nfit_test_sec *sec = &dimm_sec_info[dimm];

	if (!(sec->state & ND_INTEL_SEC_STATE_ENABLED)) {
		nd_cmd->status = ND_INTEL_STATUS_INVALID_STATE;
		dev_dbg(dev, "freeze lock: wrong security state\n");
	} else {
		sec->state |= ND_INTEL_SEC_STATE_FROZEN;
		nd_cmd->status = 0;
		dev_dbg(dev, "security frozen\n");
	}

	return 0;
}

static int nd_intel_test_cmd_disable_pass(struct nfit_test *t,
		struct nd_intel_disable_passphrase *nd_cmd,
		unsigned int buf_len, int dimm)
{
	struct device *dev = &t->pdev.dev;
	struct nfit_test_sec *sec = &dimm_sec_info[dimm];

	if (!(sec->state & ND_INTEL_SEC_STATE_ENABLED) ||
			(sec->state & ND_INTEL_SEC_STATE_FROZEN)) {
		nd_cmd->status = ND_INTEL_STATUS_INVALID_STATE;
		dev_dbg(dev, "disable passphrase: wrong security state\n");
	} else if (memcmp(nd_cmd->passphrase, sec->passphrase,
				ND_INTEL_PASSPHRASE_SIZE) != 0) {
		nd_cmd->status = ND_INTEL_STATUS_INVALID_PASS;
		dev_dbg(dev, "disable passphrase: wrong passphrase\n");
	} else {
		memset(sec->passphrase, 0, ND_INTEL_PASSPHRASE_SIZE);
		sec->state = 0;
		dev_dbg(dev, "disable passphrase: done\n");
	}

	return 0;
}

static int nd_intel_test_cmd_secure_erase(struct nfit_test *t,
		struct nd_intel_secure_erase *nd_cmd,
		unsigned int buf_len, int dimm)
{
	struct device *dev = &t->pdev.dev;
	struct nfit_test_sec *sec = &dimm_sec_info[dimm];

	if (sec->state & ND_INTEL_SEC_STATE_FROZEN) {
		nd_cmd->status = ND_INTEL_STATUS_INVALID_STATE;
		dev_dbg(dev, "secure erase: wrong security state\n");
	} else if (memcmp(nd_cmd->passphrase, sec->passphrase,
				ND_INTEL_PASSPHRASE_SIZE) != 0) {
		nd_cmd->status = ND_INTEL_STATUS_INVALID_PASS;
		dev_dbg(dev, "secure erase: wrong passphrase\n");
	} else {
		if (!(sec->state & ND_INTEL_SEC_STATE_ENABLED)
				&& (memcmp(nd_cmd->passphrase, zero_key,
					ND_INTEL_PASSPHRASE_SIZE) != 0)) {
			dev_dbg(dev, "invalid zero key\n");
			return 0;
		}
		memset(sec->passphrase, 0, ND_INTEL_PASSPHRASE_SIZE);
		memset(sec->master_passphrase, 0, ND_INTEL_PASSPHRASE_SIZE);
		sec->state = 0;
		sec->ext_state = ND_INTEL_SEC_ESTATE_ENABLED;
		dev_dbg(dev, "secure erase: done\n");
	}

	return 0;
}

static int nd_intel_test_cmd_overwrite(struct nfit_test *t,
		struct nd_intel_overwrite *nd_cmd,
		unsigned int buf_len, int dimm)
{
	struct device *dev = &t->pdev.dev;
	struct nfit_test_sec *sec = &dimm_sec_info[dimm];

	if ((sec->state & ND_INTEL_SEC_STATE_ENABLED) &&
			memcmp(nd_cmd->passphrase, sec->passphrase,
				ND_INTEL_PASSPHRASE_SIZE) != 0) {
		nd_cmd->status = ND_INTEL_STATUS_INVALID_PASS;
		dev_dbg(dev, "overwrite: wrong passphrase\n");
		return 0;
	}

	sec->old_state = sec->state;
	sec->state = ND_INTEL_SEC_STATE_OVERWRITE;
	dev_dbg(dev, "overwrite progressing.\n");
	sec->overwrite_end_time = get_jiffies_64() + 5 * HZ;

	return 0;
}

static int nd_intel_test_cmd_query_overwrite(struct nfit_test *t,
		struct nd_intel_query_overwrite *nd_cmd,
		unsigned int buf_len, int dimm)
{
	struct device *dev = &t->pdev.dev;
	struct nfit_test_sec *sec = &dimm_sec_info[dimm];

	if (!(sec->state & ND_INTEL_SEC_STATE_OVERWRITE)) {
		nd_cmd->status = ND_INTEL_STATUS_OQUERY_SEQUENCE_ERR;
		return 0;
	}

	if (time_is_before_jiffies64(sec->overwrite_end_time)) {
		sec->overwrite_end_time = 0;
		sec->state = sec->old_state;
		sec->old_state = 0;
		sec->ext_state = ND_INTEL_SEC_ESTATE_ENABLED;
		dev_dbg(dev, "overwrite is complete\n");
	} else
		nd_cmd->status = ND_INTEL_STATUS_OQUERY_INPROGRESS;
	return 0;
}

static int nd_intel_test_cmd_master_set_pass(struct nfit_test *t,
		struct nd_intel_set_master_passphrase *nd_cmd,
		unsigned int buf_len, int dimm)
{
	struct device *dev = &t->pdev.dev;
	struct nfit_test_sec *sec = &dimm_sec_info[dimm];

	if (!(sec->ext_state & ND_INTEL_SEC_ESTATE_ENABLED)) {
		nd_cmd->status = ND_INTEL_STATUS_NOT_SUPPORTED;
		dev_dbg(dev, "master set passphrase: in wrong state\n");
	} else if (sec->ext_state & ND_INTEL_SEC_ESTATE_PLIMIT) {
		nd_cmd->status = ND_INTEL_STATUS_INVALID_STATE;
		dev_dbg(dev, "master set passphrase: in wrong security state\n");
	} else if (memcmp(nd_cmd->old_pass, sec->master_passphrase,
				ND_INTEL_PASSPHRASE_SIZE) != 0) {
		nd_cmd->status = ND_INTEL_STATUS_INVALID_PASS;
		dev_dbg(dev, "master set passphrase: wrong passphrase\n");
	} else {
		memcpy(sec->master_passphrase, nd_cmd->new_pass,
				ND_INTEL_PASSPHRASE_SIZE);
		sec->ext_state = ND_INTEL_SEC_ESTATE_ENABLED;
		dev_dbg(dev, "master passphrase: updated\n");
	}

	return 0;
}

static int nd_intel_test_cmd_master_secure_erase(struct nfit_test *t,
		struct nd_intel_master_secure_erase *nd_cmd,
		unsigned int buf_len, int dimm)
{
	struct device *dev = &t->pdev.dev;
	struct nfit_test_sec *sec = &dimm_sec_info[dimm];

	if (!(sec->ext_state & ND_INTEL_SEC_ESTATE_ENABLED)) {
		nd_cmd->status = ND_INTEL_STATUS_NOT_SUPPORTED;
		dev_dbg(dev, "master secure erase: in wrong state\n");
	} else if (sec->ext_state & ND_INTEL_SEC_ESTATE_PLIMIT) {
		nd_cmd->status = ND_INTEL_STATUS_INVALID_STATE;
		dev_dbg(dev, "master secure erase: in wrong security state\n");
	} else if (memcmp(nd_cmd->passphrase, sec->master_passphrase,
				ND_INTEL_PASSPHRASE_SIZE) != 0) {
		nd_cmd->status = ND_INTEL_STATUS_INVALID_PASS;
		dev_dbg(dev, "master secure erase: wrong passphrase\n");
	} else {
		/* we do not erase master state passphrase ever */
		sec->ext_state = ND_INTEL_SEC_ESTATE_ENABLED;
		memset(sec->passphrase, 0, ND_INTEL_PASSPHRASE_SIZE);
		sec->state = 0;
		dev_dbg(dev, "master secure erase: done\n");
	}

	return 0;
}


static int get_dimm(struct nfit_mem *nfit_mem, unsigned int func)
{
	int i;

	/* lookup per-dimm data */
	for (i = 0; i < ARRAY_SIZE(handle); i++)
		if (__to_nfit_memdev(nfit_mem)->device_handle == handle[i])
			break;
	if (i >= ARRAY_SIZE(handle))
		return -ENXIO;
	return i;
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

			i = get_dimm(nfit_mem, func);
			if (i < 0)
				return i;

			switch (func) {
			case NVDIMM_INTEL_GET_SECURITY_STATE:
				rc = nd_intel_test_cmd_security_status(t,
						buf, buf_len, i);
				break;
			case NVDIMM_INTEL_UNLOCK_UNIT:
				rc = nd_intel_test_cmd_unlock_unit(t,
						buf, buf_len, i);
				break;
			case NVDIMM_INTEL_SET_PASSPHRASE:
				rc = nd_intel_test_cmd_set_pass(t,
						buf, buf_len, i);
				break;
			case NVDIMM_INTEL_DISABLE_PASSPHRASE:
				rc = nd_intel_test_cmd_disable_pass(t,
						buf, buf_len, i);
				break;
			case NVDIMM_INTEL_FREEZE_LOCK:
				rc = nd_intel_test_cmd_freeze_lock(t,
						buf, buf_len, i);
				break;
			case NVDIMM_INTEL_SECURE_ERASE:
				rc = nd_intel_test_cmd_secure_erase(t,
						buf, buf_len, i);
				break;
			case NVDIMM_INTEL_OVERWRITE:
				rc = nd_intel_test_cmd_overwrite(t,
						buf, buf_len, i - t->dcr_idx);
				break;
			case NVDIMM_INTEL_QUERY_OVERWRITE:
				rc = nd_intel_test_cmd_query_overwrite(t,
						buf, buf_len, i - t->dcr_idx);
				break;
			case NVDIMM_INTEL_SET_MASTER_PASSPHRASE:
				rc = nd_intel_test_cmd_master_set_pass(t,
						buf, buf_len, i);
				break;
			case NVDIMM_INTEL_MASTER_SECURE_ERASE:
				rc = nd_intel_test_cmd_master_secure_erase(t,
						buf, buf_len, i);
				break;
			case ND_INTEL_ENABLE_LSS_STATUS:
				rc = nd_intel_test_cmd_set_lss_status(t,
						buf, buf_len);
				break;
			case ND_INTEL_FW_GET_INFO:
				rc = nd_intel_test_get_fw_info(t, buf,
						buf_len, i - t->dcr_idx);
				break;
			case ND_INTEL_FW_START_UPDATE:
				rc = nd_intel_test_start_update(t, buf,
						buf_len, i - t->dcr_idx);
				break;
			case ND_INTEL_FW_SEND_DATA:
				rc = nd_intel_test_send_data(t, buf,
						buf_len, i - t->dcr_idx);
				break;
			case ND_INTEL_FW_FINISH_UPDATE:
				rc = nd_intel_test_finish_fw(t, buf,
						buf_len, i - t->dcr_idx);
				break;
			case ND_INTEL_FW_FINISH_QUERY:
				rc = nd_intel_test_finish_query(t, buf,
						buf_len, i - t->dcr_idx);
				break;
			case ND_INTEL_SMART:
				rc = nfit_test_cmd_smart(buf, buf_len,
						&t->smart[i - t->dcr_idx]);
				break;
			case ND_INTEL_SMART_THRESHOLD:
				rc = nfit_test_cmd_smart_threshold(buf,
						buf_len,
						&t->smart_threshold[i -
							t->dcr_idx]);
				break;
			case ND_INTEL_SMART_SET_THRESHOLD:
				rc = nfit_test_cmd_smart_set_threshold(buf,
						buf_len,
						&t->smart_threshold[i -
							t->dcr_idx],
						&t->smart[i - t->dcr_idx],
						&t->pdev.dev, t->dimm_dev[i]);
				break;
			case ND_INTEL_SMART_INJECT:
				rc = nfit_test_cmd_smart_inject(buf,
						buf_len,
						&t->smart_threshold[i -
							t->dcr_idx],
						&t->smart[i - t->dcr_idx],
						&t->pdev.dev, t->dimm_dev[i]);
				break;
			default:
				return -ENOTTY;
			}
			return override_return_code(i, func, rc);
		}

		if (!test_bit(cmd, &cmd_mask)
				|| !test_bit(func, &nfit_mem->dsm_mask))
			return -ENOTTY;

		i = get_dimm(nfit_mem, func);
		if (i < 0)
			return i;

		switch (func) {
		case ND_CMD_GET_CONFIG_SIZE:
			rc = nfit_test_cmd_get_config_size(buf, buf_len);
			break;
		case ND_CMD_GET_CONFIG_DATA:
			rc = nfit_test_cmd_get_config_data(buf, buf_len,
				t->label[i - t->dcr_idx]);
			break;
		case ND_CMD_SET_CONFIG_DATA:
			rc = nfit_test_cmd_set_config_data(buf, buf_len,
				t->label[i - t->dcr_idx]);
			break;
		default:
			return -ENOTTY;
		}
		return override_return_code(i, func, rc);
	} else {
		struct ars_state *ars_state = &t->ars_state;
		struct nd_cmd_pkg *call_pkg = buf;

		if (!nd_desc)
			return -ENOTTY;

		if (cmd == ND_CMD_CALL) {
			func = call_pkg->nd_command;

			buf_len = call_pkg->nd_size_in + call_pkg->nd_size_out;
			buf = (void *) call_pkg->nd_payload;

			switch (func) {
			case NFIT_CMD_TRANSLATE_SPA:
				rc = nfit_test_cmd_translate_spa(
					acpi_desc->nvdimm_bus, buf, buf_len);
				return rc;
			case NFIT_CMD_ARS_INJECT_SET:
				rc = nfit_test_cmd_ars_error_inject(t, buf,
					buf_len);
				return rc;
			case NFIT_CMD_ARS_INJECT_CLEAR:
				rc = nfit_test_cmd_ars_inject_clear(t, buf,
					buf_len);
				return rc;
			case NFIT_CMD_ARS_INJECT_GET:
				rc = nfit_test_cmd_ars_inject_status(t, buf,
					buf_len);
				return rc;
			default:
				return -ENOTTY;
			}
		}

		if (!nd_desc || !test_bit(cmd, &nd_desc->cmd_mask))
			return -ENOTTY;

		switch (func) {
		case ND_CMD_ARS_CAP:
			rc = nfit_test_cmd_ars_cap(buf, buf_len);
			break;
		case ND_CMD_ARS_START:
			rc = nfit_test_cmd_ars_start(t, ars_state, buf,
					buf_len, cmd_rc);
			break;
		case ND_CMD_ARS_STATUS:
			rc = nfit_test_cmd_ars_status(ars_state, buf, buf_len,
					cmd_rc);
			break;
		case ND_CMD_CLEAR_ERROR:
			rc = nfit_test_cmd_clear_error(t, buf, buf_len, cmd_rc);
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

	spin_lock(&nfit_test_lock);
	list_del(&nfit_res->list);
	spin_unlock(&nfit_test_lock);

	if (resource_size(&nfit_res->res) >= DIMM_SIZE)
		gen_pool_free(nfit_pool, nfit_res->res.start,
				resource_size(&nfit_res->res));
	vfree(nfit_res->buf);
	kfree(nfit_res);
}

static void *__test_alloc(struct nfit_test *t, size_t size, dma_addr_t *dma,
		void *buf)
{
	struct device *dev = &t->pdev.dev;
	struct nfit_test_resource *nfit_res = kzalloc(sizeof(*nfit_res),
			GFP_KERNEL);
	int rc;

	if (!buf || !nfit_res || !*dma)
		goto err;
	rc = devm_add_action(dev, release_nfit_res, nfit_res);
	if (rc)
		goto err;
	INIT_LIST_HEAD(&nfit_res->list);
	memset(buf, 0, size);
	nfit_res->dev = dev;
	nfit_res->buf = buf;
	nfit_res->res.start = *dma;
	nfit_res->res.end = *dma + size - 1;
	nfit_res->res.name = "NFIT";
	spin_lock_init(&nfit_res->lock);
	INIT_LIST_HEAD(&nfit_res->requests);
	spin_lock(&nfit_test_lock);
	list_add(&nfit_res->list, &t->resources);
	spin_unlock(&nfit_test_lock);

	return nfit_res->buf;
 err:
	if (*dma && size >= DIMM_SIZE)
		gen_pool_free(nfit_pool, *dma, size);
	if (buf)
		vfree(buf);
	kfree(nfit_res);
	return NULL;
}

static void *test_alloc(struct nfit_test *t, size_t size, dma_addr_t *dma)
{
	struct genpool_data_align data = {
		.align = SZ_128M,
	};
	void *buf = vmalloc(size);

	if (size >= DIMM_SIZE)
		*dma = gen_pool_alloc_algo(nfit_pool, size,
				gen_pool_first_fit_align, &data);
	else
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
			if (addr >= n->res.start && (addr < n->res.start
						+ resource_size(&n->res))) {
				nfit_res = n;
				break;
			} else if (addr >= (unsigned long) n->buf
					&& (addr < (unsigned long) n->buf
						+ resource_size(&n->res))) {
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
	/* for testing, only store up to n records that fit within 4k */
	ars_state->ars_status = devm_kzalloc(dev,
			sizeof(struct nd_cmd_ars_status) + SZ_4K, GFP_KERNEL);
	if (!ars_state->ars_status)
		return -ENOMEM;
	spin_lock_init(&ars_state->lock);
	return 0;
}

static void put_dimms(void *data)
{
	struct nfit_test *t = data;
	int i;

	for (i = 0; i < t->num_dcr; i++)
		if (t->dimm_dev[i])
			device_unregister(t->dimm_dev[i]);
}

static struct class *nfit_test_dimm;

static int dimm_name_to_id(struct device *dev)
{
	int dimm;

	if (sscanf(dev_name(dev), "test_dimm%d", &dimm) != 1)
		return -ENXIO;
	return dimm;
}

static ssize_t handle_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	int dimm = dimm_name_to_id(dev);

	if (dimm < 0)
		return dimm;

	return sprintf(buf, "%#x\n", handle[dimm]);
}
DEVICE_ATTR_RO(handle);

static ssize_t fail_cmd_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	int dimm = dimm_name_to_id(dev);

	if (dimm < 0)
		return dimm;

	return sprintf(buf, "%#lx\n", dimm_fail_cmd_flags[dimm]);
}

static ssize_t fail_cmd_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	int dimm = dimm_name_to_id(dev);
	unsigned long val;
	ssize_t rc;

	if (dimm < 0)
		return dimm;

	rc = kstrtol(buf, 0, &val);
	if (rc)
		return rc;

	dimm_fail_cmd_flags[dimm] = val;
	return size;
}
static DEVICE_ATTR_RW(fail_cmd);

static ssize_t fail_cmd_code_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	int dimm = dimm_name_to_id(dev);

	if (dimm < 0)
		return dimm;

	return sprintf(buf, "%d\n", dimm_fail_cmd_code[dimm]);
}

static ssize_t fail_cmd_code_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	int dimm = dimm_name_to_id(dev);
	unsigned long val;
	ssize_t rc;

	if (dimm < 0)
		return dimm;

	rc = kstrtol(buf, 0, &val);
	if (rc)
		return rc;

	dimm_fail_cmd_code[dimm] = val;
	return size;
}
static DEVICE_ATTR_RW(fail_cmd_code);

static ssize_t lock_dimm_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int dimm = dimm_name_to_id(dev);
	struct nfit_test_sec *sec = &dimm_sec_info[dimm];

	sec->state = ND_INTEL_SEC_STATE_ENABLED | ND_INTEL_SEC_STATE_LOCKED;
	return size;
}
static DEVICE_ATTR_WO(lock_dimm);

static struct attribute *nfit_test_dimm_attributes[] = {
	&dev_attr_fail_cmd.attr,
	&dev_attr_fail_cmd_code.attr,
	&dev_attr_handle.attr,
	&dev_attr_lock_dimm.attr,
	NULL,
};

static struct attribute_group nfit_test_dimm_attribute_group = {
	.attrs = nfit_test_dimm_attributes,
};

static const struct attribute_group *nfit_test_dimm_attribute_groups[] = {
	&nfit_test_dimm_attribute_group,
	NULL,
};

static int nfit_test_dimm_init(struct nfit_test *t)
{
	int i;

	if (devm_add_action_or_reset(&t->pdev.dev, put_dimms, t))
		return -ENOMEM;
	for (i = 0; i < t->num_dcr; i++) {
		t->dimm_dev[i] = device_create_with_groups(nfit_test_dimm,
				&t->pdev.dev, 0, NULL,
				nfit_test_dimm_attribute_groups,
				"test_dimm%d", i + t->dcr_idx);
		if (!t->dimm_dev[i])
			return -ENOMEM;
	}
	return 0;
}

static void security_init(struct nfit_test *t)
{
	int i;

	for (i = 0; i < t->num_dcr; i++) {
		struct nfit_test_sec *sec = &dimm_sec_info[i];

		sec->ext_state = ND_INTEL_SEC_ESTATE_ENABLED;
	}
}

static void smart_init(struct nfit_test *t)
{
	int i;
	const struct nd_intel_smart_threshold smart_t_data = {
		.alarm_control = ND_INTEL_SMART_SPARE_TRIP
			| ND_INTEL_SMART_TEMP_TRIP,
		.media_temperature = 40 * 16,
		.ctrl_temperature = 30 * 16,
		.spares = 5,
	};

	for (i = 0; i < t->num_dcr; i++) {
		memcpy(&t->smart[i], &smart_def, sizeof(smart_def));
		memcpy(&t->smart_threshold[i], &smart_t_data,
				sizeof(smart_t_data));
	}
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
					+ sizeof(u64) * NUM_HINTS) * NUM_DCR
			+ sizeof(struct acpi_nfit_capabilities);
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

	for (i = 0; i < t->num_dcr; i++) {
		t->dimm[i] = test_alloc(t, DIMM_SIZE, &t->dimm_dma[i]);
		if (!t->dimm[i])
			return -ENOMEM;

		t->label[i] = test_alloc(t, LABEL_SIZE, &t->label_dma[i]);
		if (!t->label[i])
			return -ENOMEM;
		sprintf(t->label[i], "label%d", i);

		t->flush[i] = test_alloc(t, max(PAGE_SIZE,
					sizeof(u64) * NUM_HINTS),
				&t->flush_dma[i]);
		if (!t->flush[i])
			return -ENOMEM;
	}

	for (i = 0; i < t->num_dcr; i++) {
		t->dcr[i] = test_alloc(t, LABEL_SIZE, &t->dcr_dma[i]);
		if (!t->dcr[i])
			return -ENOMEM;
	}

	t->_fit = test_alloc(t, sizeof(union acpi_object **), &t->_fit_dma);
	if (!t->_fit)
		return -ENOMEM;

	if (nfit_test_dimm_init(t))
		return -ENOMEM;
	smart_init(t);
	security_init(t);
	return ars_state_init(&t->pdev.dev, &t->ars_state);
}

static int nfit_test1_alloc(struct nfit_test *t)
{
	size_t nfit_size = sizeof(struct acpi_nfit_system_address) * 2
		+ sizeof(struct acpi_nfit_memory_map) * 2
		+ offsetof(struct acpi_nfit_control_region, window_size) * 2;
	int i;

	t->nfit_buf = test_alloc(t, nfit_size, &t->nfit_dma);
	if (!t->nfit_buf)
		return -ENOMEM;
	t->nfit_size = nfit_size;

	t->spa_set[0] = test_alloc(t, SPA2_SIZE, &t->spa_set_dma[0]);
	if (!t->spa_set[0])
		return -ENOMEM;

	for (i = 0; i < t->num_dcr; i++) {
		t->label[i] = test_alloc(t, LABEL_SIZE, &t->label_dma[i]);
		if (!t->label[i])
			return -ENOMEM;
		sprintf(t->label[i], "label%d", i);
	}

	t->spa_set[1] = test_alloc(t, SPA_VCD_SIZE, &t->spa_set_dma[1]);
	if (!t->spa_set[1])
		return -ENOMEM;

	if (nfit_test_dimm_init(t))
		return -ENOMEM;
	smart_init(t);
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
	struct acpi_nfit_capabilities *pcap;
	unsigned int offset = 0, i;

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
	offset += spa->header.length;

	/*
	 * spa1 (interleave last half of the 4 DIMMS, note storage
	 * does not actually alias the related block-data-window
	 * regions)
	 */
	spa = nfit_buf + offset;
	spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
	spa->header.length = sizeof(*spa);
	memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_PM), 16);
	spa->range_index = 1+1;
	spa->address = t->spa_set_dma[1];
	spa->length = SPA1_SIZE;
	offset += spa->header.length;

	/* spa2 (dcr0) dimm0 */
	spa = nfit_buf + offset;
	spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
	spa->header.length = sizeof(*spa);
	memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_DCR), 16);
	spa->range_index = 2+1;
	spa->address = t->dcr_dma[0];
	spa->length = DCR_SIZE;
	offset += spa->header.length;

	/* spa3 (dcr1) dimm1 */
	spa = nfit_buf + offset;
	spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
	spa->header.length = sizeof(*spa);
	memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_DCR), 16);
	spa->range_index = 3+1;
	spa->address = t->dcr_dma[1];
	spa->length = DCR_SIZE;
	offset += spa->header.length;

	/* spa4 (dcr2) dimm2 */
	spa = nfit_buf + offset;
	spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
	spa->header.length = sizeof(*spa);
	memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_DCR), 16);
	spa->range_index = 4+1;
	spa->address = t->dcr_dma[2];
	spa->length = DCR_SIZE;
	offset += spa->header.length;

	/* spa5 (dcr3) dimm3 */
	spa = nfit_buf + offset;
	spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
	spa->header.length = sizeof(*spa);
	memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_DCR), 16);
	spa->range_index = 5+1;
	spa->address = t->dcr_dma[3];
	spa->length = DCR_SIZE;
	offset += spa->header.length;

	/* spa6 (bdw for dcr0) dimm0 */
	spa = nfit_buf + offset;
	spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
	spa->header.length = sizeof(*spa);
	memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_BDW), 16);
	spa->range_index = 6+1;
	spa->address = t->dimm_dma[0];
	spa->length = DIMM_SIZE;
	offset += spa->header.length;

	/* spa7 (bdw for dcr1) dimm1 */
	spa = nfit_buf + offset;
	spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
	spa->header.length = sizeof(*spa);
	memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_BDW), 16);
	spa->range_index = 7+1;
	spa->address = t->dimm_dma[1];
	spa->length = DIMM_SIZE;
	offset += spa->header.length;

	/* spa8 (bdw for dcr2) dimm2 */
	spa = nfit_buf + offset;
	spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
	spa->header.length = sizeof(*spa);
	memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_BDW), 16);
	spa->range_index = 8+1;
	spa->address = t->dimm_dma[2];
	spa->length = DIMM_SIZE;
	offset += spa->header.length;

	/* spa9 (bdw for dcr3) dimm3 */
	spa = nfit_buf + offset;
	spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
	spa->header.length = sizeof(*spa);
	memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_BDW), 16);
	spa->range_index = 9+1;
	spa->address = t->dimm_dma[3];
	spa->length = DIMM_SIZE;
	offset += spa->header.length;

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
	memdev->region_offset = 1;
	memdev->address = 0;
	memdev->interleave_index = 0;
	memdev->interleave_ways = 2;
	offset += memdev->header.length;

	/* mem-region1 (spa0, dimm1) */
	memdev = nfit_buf + offset;
	memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
	memdev->header.length = sizeof(*memdev);
	memdev->device_handle = handle[1];
	memdev->physical_id = 1;
	memdev->region_id = 0;
	memdev->range_index = 0+1;
	memdev->region_index = 5+1;
	memdev->region_size = SPA0_SIZE/2;
	memdev->region_offset = (1 << 8);
	memdev->address = 0;
	memdev->interleave_index = 0;
	memdev->interleave_ways = 2;
	memdev->flags = ACPI_NFIT_MEM_HEALTH_ENABLED;
	offset += memdev->header.length;

	/* mem-region2 (spa1, dimm0) */
	memdev = nfit_buf + offset;
	memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
	memdev->header.length = sizeof(*memdev);
	memdev->device_handle = handle[0];
	memdev->physical_id = 0;
	memdev->region_id = 1;
	memdev->range_index = 1+1;
	memdev->region_index = 4+1;
	memdev->region_size = SPA1_SIZE/4;
	memdev->region_offset = (1 << 16);
	memdev->address = SPA0_SIZE/2;
	memdev->interleave_index = 0;
	memdev->interleave_ways = 4;
	memdev->flags = ACPI_NFIT_MEM_HEALTH_ENABLED;
	offset += memdev->header.length;

	/* mem-region3 (spa1, dimm1) */
	memdev = nfit_buf + offset;
	memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
	memdev->header.length = sizeof(*memdev);
	memdev->device_handle = handle[1];
	memdev->physical_id = 1;
	memdev->region_id = 1;
	memdev->range_index = 1+1;
	memdev->region_index = 5+1;
	memdev->region_size = SPA1_SIZE/4;
	memdev->region_offset = (1 << 24);
	memdev->address = SPA0_SIZE/2;
	memdev->interleave_index = 0;
	memdev->interleave_ways = 4;
	offset += memdev->header.length;

	/* mem-region4 (spa1, dimm2) */
	memdev = nfit_buf + offset;
	memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
	memdev->header.length = sizeof(*memdev);
	memdev->device_handle = handle[2];
	memdev->physical_id = 2;
	memdev->region_id = 0;
	memdev->range_index = 1+1;
	memdev->region_index = 6+1;
	memdev->region_size = SPA1_SIZE/4;
	memdev->region_offset = (1ULL << 32);
	memdev->address = SPA0_SIZE/2;
	memdev->interleave_index = 0;
	memdev->interleave_ways = 4;
	memdev->flags = ACPI_NFIT_MEM_HEALTH_ENABLED;
	offset += memdev->header.length;

	/* mem-region5 (spa1, dimm3) */
	memdev = nfit_buf + offset;
	memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
	memdev->header.length = sizeof(*memdev);
	memdev->device_handle = handle[3];
	memdev->physical_id = 3;
	memdev->region_id = 0;
	memdev->range_index = 1+1;
	memdev->region_index = 7+1;
	memdev->region_size = SPA1_SIZE/4;
	memdev->region_offset = (1ULL << 40);
	memdev->address = SPA0_SIZE/2;
	memdev->interleave_index = 0;
	memdev->interleave_ways = 4;
	offset += memdev->header.length;

	/* mem-region6 (spa/dcr0, dimm0) */
	memdev = nfit_buf + offset;
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
	offset += memdev->header.length;

	/* mem-region7 (spa/dcr1, dimm1) */
	memdev = nfit_buf + offset;
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
	offset += memdev->header.length;

	/* mem-region8 (spa/dcr2, dimm2) */
	memdev = nfit_buf + offset;
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
	offset += memdev->header.length;

	/* mem-region9 (spa/dcr3, dimm3) */
	memdev = nfit_buf + offset;
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
	offset += memdev->header.length;

	/* mem-region10 (spa/bdw0, dimm0) */
	memdev = nfit_buf + offset;
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
	offset += memdev->header.length;

	/* mem-region11 (spa/bdw1, dimm1) */
	memdev = nfit_buf + offset;
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
	offset += memdev->header.length;

	/* mem-region12 (spa/bdw2, dimm2) */
	memdev = nfit_buf + offset;
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
	offset += memdev->header.length;

	/* mem-region13 (spa/dcr3, dimm3) */
	memdev = nfit_buf + offset;
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
	memdev->flags = ACPI_NFIT_MEM_HEALTH_ENABLED;
	offset += memdev->header.length;

	/* dcr-descriptor0: blk */
	dcr = nfit_buf + offset;
	dcr->header.type = ACPI_NFIT_TYPE_CONTROL_REGION;
	dcr->header.length = sizeof(*dcr);
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
	offset += dcr->header.length;

	/* dcr-descriptor1: blk */
	dcr = nfit_buf + offset;
	dcr->header.type = ACPI_NFIT_TYPE_CONTROL_REGION;
	dcr->header.length = sizeof(*dcr);
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
	offset += dcr->header.length;

	/* dcr-descriptor2: blk */
	dcr = nfit_buf + offset;
	dcr->header.type = ACPI_NFIT_TYPE_CONTROL_REGION;
	dcr->header.length = sizeof(*dcr);
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
	offset += dcr->header.length;

	/* dcr-descriptor3: blk */
	dcr = nfit_buf + offset;
	dcr->header.type = ACPI_NFIT_TYPE_CONTROL_REGION;
	dcr->header.length = sizeof(*dcr);
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
	offset += dcr->header.length;

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
	offset += dcr->header.length;

	/* dcr-descriptor1: pmem */
	dcr = nfit_buf + offset;
	dcr->header.type = ACPI_NFIT_TYPE_CONTROL_REGION;
	dcr->header.length = offsetof(struct acpi_nfit_control_region,
			window_size);
	dcr->region_index = 5+1;
	dcr_common_init(dcr);
	dcr->serial_number = ~handle[1];
	dcr->code = NFIT_FIC_BYTEN;
	dcr->windows = 0;
	offset += dcr->header.length;

	/* dcr-descriptor2: pmem */
	dcr = nfit_buf + offset;
	dcr->header.type = ACPI_NFIT_TYPE_CONTROL_REGION;
	dcr->header.length = offsetof(struct acpi_nfit_control_region,
			window_size);
	dcr->region_index = 6+1;
	dcr_common_init(dcr);
	dcr->serial_number = ~handle[2];
	dcr->code = NFIT_FIC_BYTEN;
	dcr->windows = 0;
	offset += dcr->header.length;

	/* dcr-descriptor3: pmem */
	dcr = nfit_buf + offset;
	dcr->header.type = ACPI_NFIT_TYPE_CONTROL_REGION;
	dcr->header.length = offsetof(struct acpi_nfit_control_region,
			window_size);
	dcr->region_index = 7+1;
	dcr_common_init(dcr);
	dcr->serial_number = ~handle[3];
	dcr->code = NFIT_FIC_BYTEN;
	dcr->windows = 0;
	offset += dcr->header.length;

	/* bdw0 (spa/dcr0, dimm0) */
	bdw = nfit_buf + offset;
	bdw->header.type = ACPI_NFIT_TYPE_DATA_REGION;
	bdw->header.length = sizeof(*bdw);
	bdw->region_index = 0+1;
	bdw->windows = 1;
	bdw->offset = 0;
	bdw->size = BDW_SIZE;
	bdw->capacity = DIMM_SIZE;
	bdw->start_address = 0;
	offset += bdw->header.length;

	/* bdw1 (spa/dcr1, dimm1) */
	bdw = nfit_buf + offset;
	bdw->header.type = ACPI_NFIT_TYPE_DATA_REGION;
	bdw->header.length = sizeof(*bdw);
	bdw->region_index = 1+1;
	bdw->windows = 1;
	bdw->offset = 0;
	bdw->size = BDW_SIZE;
	bdw->capacity = DIMM_SIZE;
	bdw->start_address = 0;
	offset += bdw->header.length;

	/* bdw2 (spa/dcr2, dimm2) */
	bdw = nfit_buf + offset;
	bdw->header.type = ACPI_NFIT_TYPE_DATA_REGION;
	bdw->header.length = sizeof(*bdw);
	bdw->region_index = 2+1;
	bdw->windows = 1;
	bdw->offset = 0;
	bdw->size = BDW_SIZE;
	bdw->capacity = DIMM_SIZE;
	bdw->start_address = 0;
	offset += bdw->header.length;

	/* bdw3 (spa/dcr3, dimm3) */
	bdw = nfit_buf + offset;
	bdw->header.type = ACPI_NFIT_TYPE_DATA_REGION;
	bdw->header.length = sizeof(*bdw);
	bdw->region_index = 3+1;
	bdw->windows = 1;
	bdw->offset = 0;
	bdw->size = BDW_SIZE;
	bdw->capacity = DIMM_SIZE;
	bdw->start_address = 0;
	offset += bdw->header.length;

	/* flush0 (dimm0) */
	flush = nfit_buf + offset;
	flush->header.type = ACPI_NFIT_TYPE_FLUSH_ADDRESS;
	flush->header.length = flush_hint_size;
	flush->device_handle = handle[0];
	flush->hint_count = NUM_HINTS;
	for (i = 0; i < NUM_HINTS; i++)
		flush->hint_address[i] = t->flush_dma[0] + i * sizeof(u64);
	offset += flush->header.length;

	/* flush1 (dimm1) */
	flush = nfit_buf + offset;
	flush->header.type = ACPI_NFIT_TYPE_FLUSH_ADDRESS;
	flush->header.length = flush_hint_size;
	flush->device_handle = handle[1];
	flush->hint_count = NUM_HINTS;
	for (i = 0; i < NUM_HINTS; i++)
		flush->hint_address[i] = t->flush_dma[1] + i * sizeof(u64);
	offset += flush->header.length;

	/* flush2 (dimm2) */
	flush = nfit_buf + offset;
	flush->header.type = ACPI_NFIT_TYPE_FLUSH_ADDRESS;
	flush->header.length = flush_hint_size;
	flush->device_handle = handle[2];
	flush->hint_count = NUM_HINTS;
	for (i = 0; i < NUM_HINTS; i++)
		flush->hint_address[i] = t->flush_dma[2] + i * sizeof(u64);
	offset += flush->header.length;

	/* flush3 (dimm3) */
	flush = nfit_buf + offset;
	flush->header.type = ACPI_NFIT_TYPE_FLUSH_ADDRESS;
	flush->header.length = flush_hint_size;
	flush->device_handle = handle[3];
	flush->hint_count = NUM_HINTS;
	for (i = 0; i < NUM_HINTS; i++)
		flush->hint_address[i] = t->flush_dma[3] + i * sizeof(u64);
	offset += flush->header.length;

	/* platform capabilities */
	pcap = nfit_buf + offset;
	pcap->header.type = ACPI_NFIT_TYPE_CAPABILITIES;
	pcap->header.length = sizeof(*pcap);
	pcap->highest_capability = 1;
	pcap->capabilities = ACPI_NFIT_CAPABILITY_MEM_FLUSH;
	offset += pcap->header.length;

	if (t->setup_hotplug) {
		/* dcr-descriptor4: blk */
		dcr = nfit_buf + offset;
		dcr->header.type = ACPI_NFIT_TYPE_CONTROL_REGION;
		dcr->header.length = sizeof(*dcr);
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
		offset += dcr->header.length;

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
		offset += dcr->header.length;

		/* bdw4 (spa/dcr4, dimm4) */
		bdw = nfit_buf + offset;
		bdw->header.type = ACPI_NFIT_TYPE_DATA_REGION;
		bdw->header.length = sizeof(*bdw);
		bdw->region_index = 8+1;
		bdw->windows = 1;
		bdw->offset = 0;
		bdw->size = BDW_SIZE;
		bdw->capacity = DIMM_SIZE;
		bdw->start_address = 0;
		offset += bdw->header.length;

		/* spa10 (dcr4) dimm4 */
		spa = nfit_buf + offset;
		spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
		spa->header.length = sizeof(*spa);
		memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_DCR), 16);
		spa->range_index = 10+1;
		spa->address = t->dcr_dma[4];
		spa->length = DCR_SIZE;
		offset += spa->header.length;

		/*
		 * spa11 (single-dimm interleave for hotplug, note storage
		 * does not actually alias the related block-data-window
		 * regions)
		 */
		spa = nfit_buf + offset;
		spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
		spa->header.length = sizeof(*spa);
		memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_PM), 16);
		spa->range_index = 11+1;
		spa->address = t->spa_set_dma[2];
		spa->length = SPA0_SIZE;
		offset += spa->header.length;

		/* spa12 (bdw for dcr4) dimm4 */
		spa = nfit_buf + offset;
		spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
		spa->header.length = sizeof(*spa);
		memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_BDW), 16);
		spa->range_index = 12+1;
		spa->address = t->dimm_dma[4];
		spa->length = DIMM_SIZE;
		offset += spa->header.length;

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
		offset += memdev->header.length;

		/* mem-region15 (spa11, dimm4) */
		memdev = nfit_buf + offset;
		memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
		memdev->header.length = sizeof(*memdev);
		memdev->device_handle = handle[4];
		memdev->physical_id = 4;
		memdev->region_id = 0;
		memdev->range_index = 11+1;
		memdev->region_index = 9+1;
		memdev->region_size = SPA0_SIZE;
		memdev->region_offset = (1ULL << 48);
		memdev->address = 0;
		memdev->interleave_index = 0;
		memdev->interleave_ways = 1;
		memdev->flags = ACPI_NFIT_MEM_HEALTH_ENABLED;
		offset += memdev->header.length;

		/* mem-region16 (spa/bdw4, dimm4) */
		memdev = nfit_buf + offset;
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
		offset += memdev->header.length;

		/* flush3 (dimm4) */
		flush = nfit_buf + offset;
		flush->header.type = ACPI_NFIT_TYPE_FLUSH_ADDRESS;
		flush->header.length = flush_hint_size;
		flush->device_handle = handle[4];
		flush->hint_count = NUM_HINTS;
		for (i = 0; i < NUM_HINTS; i++)
			flush->hint_address[i] = t->flush_dma[4]
				+ i * sizeof(u64);
		offset += flush->header.length;

		/* sanity check to make sure we've filled the buffer */
		WARN_ON(offset != t->nfit_size);
	}

	t->nfit_filled = offset;

	post_ars_status(&t->ars_state, &t->badrange, t->spa_set_dma[0],
			SPA0_SIZE);

	acpi_desc = &t->acpi_desc;
	set_bit(ND_CMD_GET_CONFIG_SIZE, &acpi_desc->dimm_cmd_force_en);
	set_bit(ND_CMD_GET_CONFIG_DATA, &acpi_desc->dimm_cmd_force_en);
	set_bit(ND_CMD_SET_CONFIG_DATA, &acpi_desc->dimm_cmd_force_en);
	set_bit(ND_INTEL_SMART, &acpi_desc->dimm_cmd_force_en);
	set_bit(ND_INTEL_SMART_THRESHOLD, &acpi_desc->dimm_cmd_force_en);
	set_bit(ND_INTEL_SMART_SET_THRESHOLD, &acpi_desc->dimm_cmd_force_en);
	set_bit(ND_INTEL_SMART_INJECT, &acpi_desc->dimm_cmd_force_en);
	set_bit(ND_CMD_ARS_CAP, &acpi_desc->bus_cmd_force_en);
	set_bit(ND_CMD_ARS_START, &acpi_desc->bus_cmd_force_en);
	set_bit(ND_CMD_ARS_STATUS, &acpi_desc->bus_cmd_force_en);
	set_bit(ND_CMD_CLEAR_ERROR, &acpi_desc->bus_cmd_force_en);
	set_bit(ND_CMD_CALL, &acpi_desc->bus_cmd_force_en);
	set_bit(NFIT_CMD_TRANSLATE_SPA, &acpi_desc->bus_nfit_cmd_force_en);
	set_bit(NFIT_CMD_ARS_INJECT_SET, &acpi_desc->bus_nfit_cmd_force_en);
	set_bit(NFIT_CMD_ARS_INJECT_CLEAR, &acpi_desc->bus_nfit_cmd_force_en);
	set_bit(NFIT_CMD_ARS_INJECT_GET, &acpi_desc->bus_nfit_cmd_force_en);
	set_bit(ND_INTEL_FW_GET_INFO, &acpi_desc->dimm_cmd_force_en);
	set_bit(ND_INTEL_FW_START_UPDATE, &acpi_desc->dimm_cmd_force_en);
	set_bit(ND_INTEL_FW_SEND_DATA, &acpi_desc->dimm_cmd_force_en);
	set_bit(ND_INTEL_FW_FINISH_UPDATE, &acpi_desc->dimm_cmd_force_en);
	set_bit(ND_INTEL_FW_FINISH_QUERY, &acpi_desc->dimm_cmd_force_en);
	set_bit(ND_INTEL_ENABLE_LSS_STATUS, &acpi_desc->dimm_cmd_force_en);
	set_bit(NVDIMM_INTEL_GET_SECURITY_STATE,
			&acpi_desc->dimm_cmd_force_en);
	set_bit(NVDIMM_INTEL_SET_PASSPHRASE, &acpi_desc->dimm_cmd_force_en);
	set_bit(NVDIMM_INTEL_DISABLE_PASSPHRASE,
			&acpi_desc->dimm_cmd_force_en);
	set_bit(NVDIMM_INTEL_UNLOCK_UNIT, &acpi_desc->dimm_cmd_force_en);
	set_bit(NVDIMM_INTEL_FREEZE_LOCK, &acpi_desc->dimm_cmd_force_en);
	set_bit(NVDIMM_INTEL_SECURE_ERASE, &acpi_desc->dimm_cmd_force_en);
	set_bit(NVDIMM_INTEL_OVERWRITE, &acpi_desc->dimm_cmd_force_en);
	set_bit(NVDIMM_INTEL_QUERY_OVERWRITE, &acpi_desc->dimm_cmd_force_en);
	set_bit(NVDIMM_INTEL_SET_MASTER_PASSPHRASE,
			&acpi_desc->dimm_cmd_force_en);
	set_bit(NVDIMM_INTEL_MASTER_SECURE_ERASE,
			&acpi_desc->dimm_cmd_force_en);
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
	offset += spa->header.length;

	/* virtual cd region */
	spa = nfit_buf + offset;
	spa->header.type = ACPI_NFIT_TYPE_SYSTEM_ADDRESS;
	spa->header.length = sizeof(*spa);
	memcpy(spa->range_guid, to_nfit_uuid(NFIT_SPA_VCD), 16);
	spa->range_index = 0;
	spa->address = t->spa_set_dma[1];
	spa->length = SPA_VCD_SIZE;
	offset += spa->header.length;

	/* mem-region0 (spa0, dimm0) */
	memdev = nfit_buf + offset;
	memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
	memdev->header.length = sizeof(*memdev);
	memdev->device_handle = handle[5];
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
	offset += memdev->header.length;

	/* dcr-descriptor0 */
	dcr = nfit_buf + offset;
	dcr->header.type = ACPI_NFIT_TYPE_CONTROL_REGION;
	dcr->header.length = offsetof(struct acpi_nfit_control_region,
			window_size);
	dcr->region_index = 0+1;
	dcr_common_init(dcr);
	dcr->serial_number = ~handle[5];
	dcr->code = NFIT_FIC_BYTE;
	dcr->windows = 0;
	offset += dcr->header.length;

	memdev = nfit_buf + offset;
	memdev->header.type = ACPI_NFIT_TYPE_MEMORY_MAP;
	memdev->header.length = sizeof(*memdev);
	memdev->device_handle = handle[6];
	memdev->physical_id = 0;
	memdev->region_id = 0;
	memdev->range_index = 0;
	memdev->region_index = 0+2;
	memdev->region_size = SPA2_SIZE;
	memdev->region_offset = 0;
	memdev->address = 0;
	memdev->interleave_index = 0;
	memdev->interleave_ways = 1;
	memdev->flags = ACPI_NFIT_MEM_MAP_FAILED;
	offset += memdev->header.length;

	/* dcr-descriptor1 */
	dcr = nfit_buf + offset;
	dcr->header.type = ACPI_NFIT_TYPE_CONTROL_REGION;
	dcr->header.length = offsetof(struct acpi_nfit_control_region,
			window_size);
	dcr->region_index = 0+2;
	dcr_common_init(dcr);
	dcr->serial_number = ~handle[6];
	dcr->code = NFIT_FIC_BYTE;
	dcr->windows = 0;
	offset += dcr->header.length;

	/* sanity check to make sure we've filled the buffer */
	WARN_ON(offset != t->nfit_size);

	t->nfit_filled = offset;

	post_ars_status(&t->ars_state, &t->badrange, t->spa_set_dma[0],
			SPA2_SIZE);

	acpi_desc = &t->acpi_desc;
	set_bit(ND_CMD_ARS_CAP, &acpi_desc->bus_cmd_force_en);
	set_bit(ND_CMD_ARS_START, &acpi_desc->bus_cmd_force_en);
	set_bit(ND_CMD_ARS_STATUS, &acpi_desc->bus_cmd_force_en);
	set_bit(ND_CMD_CLEAR_ERROR, &acpi_desc->bus_cmd_force_en);
	set_bit(ND_INTEL_ENABLE_LSS_STATUS, &acpi_desc->dimm_cmd_force_en);
	set_bit(ND_CMD_GET_CONFIG_SIZE, &acpi_desc->dimm_cmd_force_en);
	set_bit(ND_CMD_GET_CONFIG_DATA, &acpi_desc->dimm_cmd_force_en);
	set_bit(ND_CMD_SET_CONFIG_DATA, &acpi_desc->dimm_cmd_force_en);
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

		/* give us some some coverage of the arch_invalidate_pmem() API */
		arch_invalidate_pmem(mmio->addr.base + dpa, len);
	}
	nd_region_release_lane(nd_region, lane);

	return 0;
}

static unsigned long nfit_ctl_handle;

union acpi_object *result;

static union acpi_object *nfit_test_evaluate_dsm(acpi_handle handle,
		const guid_t *guid, u64 rev, u64 func, union acpi_object *argv4)
{
	if (handle != &nfit_ctl_handle)
		return ERR_PTR(-ENXIO);

	return result;
}

static int setup_result(void *buf, size_t size)
{
	result = kmalloc(sizeof(union acpi_object) + size, GFP_KERNEL);
	if (!result)
		return -ENOMEM;
	result->package.type = ACPI_TYPE_BUFFER,
	result->buffer.pointer = (void *) (result + 1);
	result->buffer.length = size;
	memcpy(result->buffer.pointer, buf, size);
	memset(buf, 0, size);
	return 0;
}

static int nfit_ctl_test(struct device *dev)
{
	int rc, cmd_rc;
	struct nvdimm *nvdimm;
	struct acpi_device *adev;
	struct nfit_mem *nfit_mem;
	struct nd_ars_record *record;
	struct acpi_nfit_desc *acpi_desc;
	const u64 test_val = 0x0123456789abcdefULL;
	unsigned long mask, cmd_size, offset;
	union {
		struct nd_cmd_get_config_size cfg_size;
		struct nd_cmd_clear_error clear_err;
		struct nd_cmd_ars_status ars_stat;
		struct nd_cmd_ars_cap ars_cap;
		char buf[sizeof(struct nd_cmd_ars_status)
			+ sizeof(struct nd_ars_record)];
	} cmds;

	adev = devm_kzalloc(dev, sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return -ENOMEM;
	*adev = (struct acpi_device) {
		.handle = &nfit_ctl_handle,
		.dev = {
			.init_name = "test-adev",
		},
	};

	acpi_desc = devm_kzalloc(dev, sizeof(*acpi_desc), GFP_KERNEL);
	if (!acpi_desc)
		return -ENOMEM;
	*acpi_desc = (struct acpi_nfit_desc) {
		.nd_desc = {
			.cmd_mask = 1UL << ND_CMD_ARS_CAP
				| 1UL << ND_CMD_ARS_START
				| 1UL << ND_CMD_ARS_STATUS
				| 1UL << ND_CMD_CLEAR_ERROR
				| 1UL << ND_CMD_CALL,
			.module = THIS_MODULE,
			.provider_name = "ACPI.NFIT",
			.ndctl = acpi_nfit_ctl,
			.bus_dsm_mask = 1UL << NFIT_CMD_TRANSLATE_SPA
				| 1UL << NFIT_CMD_ARS_INJECT_SET
				| 1UL << NFIT_CMD_ARS_INJECT_CLEAR
				| 1UL << NFIT_CMD_ARS_INJECT_GET,
		},
		.dev = &adev->dev,
	};

	nfit_mem = devm_kzalloc(dev, sizeof(*nfit_mem), GFP_KERNEL);
	if (!nfit_mem)
		return -ENOMEM;

	mask = 1UL << ND_CMD_SMART | 1UL << ND_CMD_SMART_THRESHOLD
		| 1UL << ND_CMD_DIMM_FLAGS | 1UL << ND_CMD_GET_CONFIG_SIZE
		| 1UL << ND_CMD_GET_CONFIG_DATA | 1UL << ND_CMD_SET_CONFIG_DATA
		| 1UL << ND_CMD_VENDOR;
	*nfit_mem = (struct nfit_mem) {
		.adev = adev,
		.family = NVDIMM_FAMILY_INTEL,
		.dsm_mask = mask,
	};

	nvdimm = devm_kzalloc(dev, sizeof(*nvdimm), GFP_KERNEL);
	if (!nvdimm)
		return -ENOMEM;
	*nvdimm = (struct nvdimm) {
		.provider_data = nfit_mem,
		.cmd_mask = mask,
		.dev = {
			.init_name = "test-dimm",
		},
	};


	/* basic checkout of a typical 'get config size' command */
	cmd_size = sizeof(cmds.cfg_size);
	cmds.cfg_size = (struct nd_cmd_get_config_size) {
		.status = 0,
		.config_size = SZ_128K,
		.max_xfer = SZ_4K,
	};
	rc = setup_result(cmds.buf, cmd_size);
	if (rc)
		return rc;
	rc = acpi_nfit_ctl(&acpi_desc->nd_desc, nvdimm, ND_CMD_GET_CONFIG_SIZE,
			cmds.buf, cmd_size, &cmd_rc);

	if (rc < 0 || cmd_rc || cmds.cfg_size.status != 0
			|| cmds.cfg_size.config_size != SZ_128K
			|| cmds.cfg_size.max_xfer != SZ_4K) {
		dev_dbg(dev, "%s: failed at: %d rc: %d cmd_rc: %d\n",
				__func__, __LINE__, rc, cmd_rc);
		return -EIO;
	}


	/* test ars_status with zero output */
	cmd_size = offsetof(struct nd_cmd_ars_status, address);
	cmds.ars_stat = (struct nd_cmd_ars_status) {
		.out_length = 0,
	};
	rc = setup_result(cmds.buf, cmd_size);
	if (rc)
		return rc;
	rc = acpi_nfit_ctl(&acpi_desc->nd_desc, NULL, ND_CMD_ARS_STATUS,
			cmds.buf, cmd_size, &cmd_rc);

	if (rc < 0 || cmd_rc) {
		dev_dbg(dev, "%s: failed at: %d rc: %d cmd_rc: %d\n",
				__func__, __LINE__, rc, cmd_rc);
		return -EIO;
	}


	/* test ars_cap with benign extended status */
	cmd_size = sizeof(cmds.ars_cap);
	cmds.ars_cap = (struct nd_cmd_ars_cap) {
		.status = ND_ARS_PERSISTENT << 16,
	};
	offset = offsetof(struct nd_cmd_ars_cap, status);
	rc = setup_result(cmds.buf + offset, cmd_size - offset);
	if (rc)
		return rc;
	rc = acpi_nfit_ctl(&acpi_desc->nd_desc, NULL, ND_CMD_ARS_CAP,
			cmds.buf, cmd_size, &cmd_rc);

	if (rc < 0 || cmd_rc) {
		dev_dbg(dev, "%s: failed at: %d rc: %d cmd_rc: %d\n",
				__func__, __LINE__, rc, cmd_rc);
		return -EIO;
	}


	/* test ars_status with 'status' trimmed from 'out_length' */
	cmd_size = sizeof(cmds.ars_stat) + sizeof(struct nd_ars_record);
	cmds.ars_stat = (struct nd_cmd_ars_status) {
		.out_length = cmd_size - 4,
	};
	record = &cmds.ars_stat.records[0];
	*record = (struct nd_ars_record) {
		.length = test_val,
	};
	rc = setup_result(cmds.buf, cmd_size);
	if (rc)
		return rc;
	rc = acpi_nfit_ctl(&acpi_desc->nd_desc, NULL, ND_CMD_ARS_STATUS,
			cmds.buf, cmd_size, &cmd_rc);

	if (rc < 0 || cmd_rc || record->length != test_val) {
		dev_dbg(dev, "%s: failed at: %d rc: %d cmd_rc: %d\n",
				__func__, __LINE__, rc, cmd_rc);
		return -EIO;
	}


	/* test ars_status with 'Output (Size)' including 'status' */
	cmd_size = sizeof(cmds.ars_stat) + sizeof(struct nd_ars_record);
	cmds.ars_stat = (struct nd_cmd_ars_status) {
		.out_length = cmd_size,
	};
	record = &cmds.ars_stat.records[0];
	*record = (struct nd_ars_record) {
		.length = test_val,
	};
	rc = setup_result(cmds.buf, cmd_size);
	if (rc)
		return rc;
	rc = acpi_nfit_ctl(&acpi_desc->nd_desc, NULL, ND_CMD_ARS_STATUS,
			cmds.buf, cmd_size, &cmd_rc);

	if (rc < 0 || cmd_rc || record->length != test_val) {
		dev_dbg(dev, "%s: failed at: %d rc: %d cmd_rc: %d\n",
				__func__, __LINE__, rc, cmd_rc);
		return -EIO;
	}


	/* test extended status for get_config_size results in failure */
	cmd_size = sizeof(cmds.cfg_size);
	cmds.cfg_size = (struct nd_cmd_get_config_size) {
		.status = 1 << 16,
	};
	rc = setup_result(cmds.buf, cmd_size);
	if (rc)
		return rc;
	rc = acpi_nfit_ctl(&acpi_desc->nd_desc, nvdimm, ND_CMD_GET_CONFIG_SIZE,
			cmds.buf, cmd_size, &cmd_rc);

	if (rc < 0 || cmd_rc >= 0) {
		dev_dbg(dev, "%s: failed at: %d rc: %d cmd_rc: %d\n",
				__func__, __LINE__, rc, cmd_rc);
		return -EIO;
	}

	/* test clear error */
	cmd_size = sizeof(cmds.clear_err);
	cmds.clear_err = (struct nd_cmd_clear_error) {
		.length = 512,
		.cleared = 512,
	};
	rc = setup_result(cmds.buf, cmd_size);
	if (rc)
		return rc;
	rc = acpi_nfit_ctl(&acpi_desc->nd_desc, NULL, ND_CMD_CLEAR_ERROR,
			cmds.buf, cmd_size, &cmd_rc);
	if (rc < 0 || cmd_rc) {
		dev_dbg(dev, "%s: failed at: %d rc: %d cmd_rc: %d\n",
				__func__, __LINE__, rc, cmd_rc);
		return -EIO;
	}

	return 0;
}

static int nfit_test_probe(struct platform_device *pdev)
{
	struct nvdimm_bus_descriptor *nd_desc;
	struct acpi_nfit_desc *acpi_desc;
	struct device *dev = &pdev->dev;
	struct nfit_test *nfit_test;
	struct nfit_mem *nfit_mem;
	union acpi_object *obj;
	int rc;

	if (strcmp(dev_name(&pdev->dev), "nfit_test.0") == 0) {
		rc = nfit_ctl_test(&pdev->dev);
		if (rc)
			return rc;
	}

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
		nfit_test->smart = devm_kcalloc(dev, num,
				sizeof(struct nd_intel_smart), GFP_KERNEL);
		nfit_test->smart_threshold = devm_kcalloc(dev, num,
				sizeof(struct nd_intel_smart_threshold),
				GFP_KERNEL);
		nfit_test->fw = devm_kcalloc(dev, num,
				sizeof(struct nfit_test_fw), GFP_KERNEL);
		if (nfit_test->dimm && nfit_test->dimm_dma && nfit_test->label
				&& nfit_test->label_dma && nfit_test->dcr
				&& nfit_test->dcr_dma && nfit_test->flush
				&& nfit_test->flush_dma
				&& nfit_test->fw)
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
	nd_desc->module = THIS_MODULE;
	nd_desc->ndctl = nfit_test_ctl;

	rc = acpi_nfit_init(acpi_desc, nfit_test->nfit_buf,
			nfit_test->nfit_filled);
	if (rc)
		return rc;

	rc = devm_add_action_or_reset(&pdev->dev, acpi_nfit_shutdown, acpi_desc);
	if (rc)
		return rc;

	if (nfit_test->setup != nfit_test0_setup)
		return 0;

	nfit_test->setup_hotplug = 1;
	nfit_test->setup(nfit_test);

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return -ENOMEM;
	obj->type = ACPI_TYPE_BUFFER;
	obj->buffer.length = nfit_test->nfit_size;
	obj->buffer.pointer = nfit_test->nfit_buf;
	*(nfit_test->_fit) = obj;
	__acpi_nfit_notify(&pdev->dev, nfit_test, 0x80);

	/* associate dimm devices with nfit_mem data for notification testing */
	mutex_lock(&acpi_desc->init_mutex);
	list_for_each_entry(nfit_mem, &acpi_desc->dimms, list) {
		u32 nfit_handle = __to_nfit_memdev(nfit_mem)->device_handle;
		int i;

		for (i = 0; i < ARRAY_SIZE(handle); i++)
			if (nfit_handle == handle[i])
				dev_set_drvdata(nfit_test->dimm_dev[i],
						nfit_mem);
	}
	mutex_unlock(&acpi_desc->init_mutex);

	return 0;
}

static int nfit_test_remove(struct platform_device *pdev)
{
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

static char mcsafe_buf[PAGE_SIZE] __attribute__((__aligned__(PAGE_SIZE)));

enum INJECT {
	INJECT_NONE,
	INJECT_SRC,
	INJECT_DST,
};

static void mcsafe_test_init(char *dst, char *src, size_t size)
{
	size_t i;

	memset(dst, 0xff, size);
	for (i = 0; i < size; i++)
		src[i] = (char) i;
}

static bool mcsafe_test_validate(unsigned char *dst, unsigned char *src,
		size_t size, unsigned long rem)
{
	size_t i;

	for (i = 0; i < size - rem; i++)
		if (dst[i] != (unsigned char) i) {
			pr_info_once("%s:%d: offset: %zd got: %#x expect: %#x\n",
					__func__, __LINE__, i, dst[i],
					(unsigned char) i);
			return false;
		}
	for (i = size - rem; i < size; i++)
		if (dst[i] != 0xffU) {
			pr_info_once("%s:%d: offset: %zd got: %#x expect: 0xff\n",
					__func__, __LINE__, i, dst[i]);
			return false;
		}
	return true;
}

void mcsafe_test(void)
{
	char *inject_desc[] = { "none", "source", "destination" };
	enum INJECT inj;

	if (IS_ENABLED(CONFIG_MCSAFE_TEST)) {
		pr_info("%s: run...\n", __func__);
	} else {
		pr_info("%s: disabled, skip.\n", __func__);
		return;
	}

	for (inj = INJECT_NONE; inj <= INJECT_DST; inj++) {
		int i;

		pr_info("%s: inject: %s\n", __func__, inject_desc[inj]);
		for (i = 0; i < 512; i++) {
			unsigned long expect, rem;
			void *src, *dst;
			bool valid;

			switch (inj) {
			case INJECT_NONE:
				mcsafe_inject_src(NULL);
				mcsafe_inject_dst(NULL);
				dst = &mcsafe_buf[2048];
				src = &mcsafe_buf[1024 - i];
				expect = 0;
				break;
			case INJECT_SRC:
				mcsafe_inject_src(&mcsafe_buf[1024]);
				mcsafe_inject_dst(NULL);
				dst = &mcsafe_buf[2048];
				src = &mcsafe_buf[1024 - i];
				expect = 512 - i;
				break;
			case INJECT_DST:
				mcsafe_inject_src(NULL);
				mcsafe_inject_dst(&mcsafe_buf[2048]);
				dst = &mcsafe_buf[2048 - i];
				src = &mcsafe_buf[1024];
				expect = 512 - i;
				break;
			}

			mcsafe_test_init(dst, src, 512);
			rem = __memcpy_mcsafe(dst, src, 512);
			valid = mcsafe_test_validate(dst, src, 512, expect);
			if (rem == expect && valid)
				continue;
			pr_info("%s: copy(%#lx, %#lx, %d) off: %d rem: %ld %s expect: %ld\n",
					__func__,
					((unsigned long) dst) & ~PAGE_MASK,
					((unsigned long ) src) & ~PAGE_MASK,
					512, i, rem, valid ? "valid" : "bad",
					expect);
		}
	}

	mcsafe_inject_src(NULL);
	mcsafe_inject_dst(NULL);
}

static __init int nfit_test_init(void)
{
	int rc, i;

	pmem_test();
	libnvdimm_test();
	acpi_nfit_test();
	device_dax_test();
	mcsafe_test();
	dax_pmem_test();
	dax_pmem_core_test();
#ifdef CONFIG_DEV_DAX_PMEM_COMPAT
	dax_pmem_compat_test();
#endif

	nfit_test_setup(nfit_test_lookup, nfit_test_evaluate_dsm);

	nfit_wq = create_singlethread_workqueue("nfit");
	if (!nfit_wq)
		return -ENOMEM;

	nfit_test_dimm = class_create(THIS_MODULE, "nfit_test_dimm");
	if (IS_ERR(nfit_test_dimm)) {
		rc = PTR_ERR(nfit_test_dimm);
		goto err_register;
	}

	nfit_pool = gen_pool_create(ilog2(SZ_4M), NUMA_NO_NODE);
	if (!nfit_pool) {
		rc = -ENOMEM;
		goto err_register;
	}

	if (gen_pool_add(nfit_pool, SZ_4G, SZ_4G, NUMA_NO_NODE)) {
		rc = -ENOMEM;
		goto err_register;
	}

	for (i = 0; i < NUM_NFITS; i++) {
		struct nfit_test *nfit_test;
		struct platform_device *pdev;

		nfit_test = kzalloc(sizeof(*nfit_test), GFP_KERNEL);
		if (!nfit_test) {
			rc = -ENOMEM;
			goto err_register;
		}
		INIT_LIST_HEAD(&nfit_test->resources);
		badrange_init(&nfit_test->badrange);
		switch (i) {
		case 0:
			nfit_test->num_pm = NUM_PM;
			nfit_test->dcr_idx = 0;
			nfit_test->num_dcr = NUM_DCR;
			nfit_test->alloc = nfit_test0_alloc;
			nfit_test->setup = nfit_test0_setup;
			break;
		case 1:
			nfit_test->num_pm = 2;
			nfit_test->dcr_idx = NUM_DCR;
			nfit_test->num_dcr = 2;
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
		get_device(&pdev->dev);

		rc = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
		if (rc)
			goto err_register;

		instances[i] = nfit_test;
		INIT_WORK(&nfit_test->work, uc_error_notify);
	}

	rc = platform_driver_register(&nfit_test_driver);
	if (rc)
		goto err_register;
	return 0;

 err_register:
	if (nfit_pool)
		gen_pool_destroy(nfit_pool);

	destroy_workqueue(nfit_wq);
	for (i = 0; i < NUM_NFITS; i++)
		if (instances[i])
			platform_device_unregister(&instances[i]->pdev);
	nfit_test_teardown();
	for (i = 0; i < NUM_NFITS; i++)
		if (instances[i])
			put_device(&instances[i]->pdev.dev);

	return rc;
}

static __exit void nfit_test_exit(void)
{
	int i;

	flush_workqueue(nfit_wq);
	destroy_workqueue(nfit_wq);
	for (i = 0; i < NUM_NFITS; i++)
		platform_device_unregister(&instances[i]->pdev);
	platform_driver_unregister(&nfit_test_driver);
	nfit_test_teardown();

	gen_pool_destroy(nfit_pool);

	for (i = 0; i < NUM_NFITS; i++)
		put_device(&instances[i]->pdev.dev);
	class_destroy(nfit_test_dimm);
}

module_init(nfit_test_init);
module_exit(nfit_test_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Intel Corporation");
