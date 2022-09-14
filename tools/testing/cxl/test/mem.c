// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2021 Intel Corporation. All rights reserved.

#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/sizes.h>
#include <linux/bits.h>
#include <cxlmem.h>

#define LSA_SIZE SZ_128K
#define DEV_SIZE SZ_2G
#define EFFECT(x) (1U << x)

static struct cxl_cel_entry mock_cel[] = {
	{
		.opcode = cpu_to_le16(CXL_MBOX_OP_GET_SUPPORTED_LOGS),
		.effect = cpu_to_le16(0),
	},
	{
		.opcode = cpu_to_le16(CXL_MBOX_OP_IDENTIFY),
		.effect = cpu_to_le16(0),
	},
	{
		.opcode = cpu_to_le16(CXL_MBOX_OP_GET_LSA),
		.effect = cpu_to_le16(0),
	},
	{
		.opcode = cpu_to_le16(CXL_MBOX_OP_GET_PARTITION_INFO),
		.effect = cpu_to_le16(0),
	},
	{
		.opcode = cpu_to_le16(CXL_MBOX_OP_SET_LSA),
		.effect = cpu_to_le16(EFFECT(1) | EFFECT(2)),
	},
	{
		.opcode = cpu_to_le16(CXL_MBOX_OP_GET_HEALTH_INFO),
		.effect = cpu_to_le16(0),
	},
};

/* See CXL 2.0 Table 181 Get Health Info Output Payload */
struct cxl_mbox_health_info {
	u8 health_status;
	u8 media_status;
	u8 ext_status;
	u8 life_used;
	__le16 temperature;
	__le32 dirty_shutdowns;
	__le32 volatile_errors;
	__le32 pmem_errors;
} __packed;

static struct {
	struct cxl_mbox_get_supported_logs gsl;
	struct cxl_gsl_entry entry;
} mock_gsl_payload = {
	.gsl = {
		.entries = cpu_to_le16(1),
	},
	.entry = {
		.uuid = DEFINE_CXL_CEL_UUID,
		.size = cpu_to_le32(sizeof(mock_cel)),
	},
};

static int mock_gsl(struct cxl_mbox_cmd *cmd)
{
	if (cmd->size_out < sizeof(mock_gsl_payload))
		return -EINVAL;

	memcpy(cmd->payload_out, &mock_gsl_payload, sizeof(mock_gsl_payload));
	cmd->size_out = sizeof(mock_gsl_payload);

	return 0;
}

static int mock_get_log(struct cxl_dev_state *cxlds, struct cxl_mbox_cmd *cmd)
{
	struct cxl_mbox_get_log *gl = cmd->payload_in;
	u32 offset = le32_to_cpu(gl->offset);
	u32 length = le32_to_cpu(gl->length);
	uuid_t uuid = DEFINE_CXL_CEL_UUID;
	void *data = &mock_cel;

	if (cmd->size_in < sizeof(*gl))
		return -EINVAL;
	if (length > cxlds->payload_size)
		return -EINVAL;
	if (offset + length > sizeof(mock_cel))
		return -EINVAL;
	if (!uuid_equal(&gl->uuid, &uuid))
		return -EINVAL;
	if (length > cmd->size_out)
		return -EINVAL;

	memcpy(cmd->payload_out, data + offset, length);

	return 0;
}

static int mock_id(struct cxl_dev_state *cxlds, struct cxl_mbox_cmd *cmd)
{
	struct cxl_mbox_identify id = {
		.fw_revision = { "mock fw v1 " },
		.lsa_size = cpu_to_le32(LSA_SIZE),
		.partition_align =
			cpu_to_le64(SZ_256M / CXL_CAPACITY_MULTIPLIER),
		.total_capacity =
			cpu_to_le64(DEV_SIZE / CXL_CAPACITY_MULTIPLIER),
	};

	if (cmd->size_out < sizeof(id))
		return -EINVAL;

	memcpy(cmd->payload_out, &id, sizeof(id));

	return 0;
}

static int mock_partition_info(struct cxl_dev_state *cxlds,
			       struct cxl_mbox_cmd *cmd)
{
	struct cxl_mbox_get_partition_info pi = {
		.active_volatile_cap =
			cpu_to_le64(DEV_SIZE / 2 / CXL_CAPACITY_MULTIPLIER),
		.active_persistent_cap =
			cpu_to_le64(DEV_SIZE / 2 / CXL_CAPACITY_MULTIPLIER),
	};

	if (cmd->size_out < sizeof(pi))
		return -EINVAL;

	memcpy(cmd->payload_out, &pi, sizeof(pi));

	return 0;
}

static int mock_get_lsa(struct cxl_dev_state *cxlds, struct cxl_mbox_cmd *cmd)
{
	struct cxl_mbox_get_lsa *get_lsa = cmd->payload_in;
	void *lsa = dev_get_drvdata(cxlds->dev);
	u32 offset, length;

	if (sizeof(*get_lsa) > cmd->size_in)
		return -EINVAL;
	offset = le32_to_cpu(get_lsa->offset);
	length = le32_to_cpu(get_lsa->length);
	if (offset + length > LSA_SIZE)
		return -EINVAL;
	if (length > cmd->size_out)
		return -EINVAL;

	memcpy(cmd->payload_out, lsa + offset, length);
	return 0;
}

static int mock_set_lsa(struct cxl_dev_state *cxlds, struct cxl_mbox_cmd *cmd)
{
	struct cxl_mbox_set_lsa *set_lsa = cmd->payload_in;
	void *lsa = dev_get_drvdata(cxlds->dev);
	u32 offset, length;

	if (sizeof(*set_lsa) > cmd->size_in)
		return -EINVAL;
	offset = le32_to_cpu(set_lsa->offset);
	length = cmd->size_in - sizeof(*set_lsa);
	if (offset + length > LSA_SIZE)
		return -EINVAL;

	memcpy(lsa + offset, &set_lsa->data[0], length);
	return 0;
}

static int mock_health_info(struct cxl_dev_state *cxlds,
			    struct cxl_mbox_cmd *cmd)
{
	struct cxl_mbox_health_info health_info = {
		/* set flags for maint needed, perf degraded, hw replacement */
		.health_status = 0x7,
		/* set media status to "All Data Lost" */
		.media_status = 0x3,
		/*
		 * set ext_status flags for:
		 *  ext_life_used: normal,
		 *  ext_temperature: critical,
		 *  ext_corrected_volatile: warning,
		 *  ext_corrected_persistent: normal,
		 */
		.ext_status = 0x18,
		.life_used = 15,
		.temperature = cpu_to_le16(25),
		.dirty_shutdowns = cpu_to_le32(10),
		.volatile_errors = cpu_to_le32(20),
		.pmem_errors = cpu_to_le32(30),
	};

	if (cmd->size_out < sizeof(health_info))
		return -EINVAL;

	memcpy(cmd->payload_out, &health_info, sizeof(health_info));
	return 0;
}

static int cxl_mock_mbox_send(struct cxl_dev_state *cxlds, struct cxl_mbox_cmd *cmd)
{
	struct device *dev = cxlds->dev;
	int rc = -EIO;

	switch (cmd->opcode) {
	case CXL_MBOX_OP_GET_SUPPORTED_LOGS:
		rc = mock_gsl(cmd);
		break;
	case CXL_MBOX_OP_GET_LOG:
		rc = mock_get_log(cxlds, cmd);
		break;
	case CXL_MBOX_OP_IDENTIFY:
		rc = mock_id(cxlds, cmd);
		break;
	case CXL_MBOX_OP_GET_LSA:
		rc = mock_get_lsa(cxlds, cmd);
		break;
	case CXL_MBOX_OP_GET_PARTITION_INFO:
		rc = mock_partition_info(cxlds, cmd);
		break;
	case CXL_MBOX_OP_SET_LSA:
		rc = mock_set_lsa(cxlds, cmd);
		break;
	case CXL_MBOX_OP_GET_HEALTH_INFO:
		rc = mock_health_info(cxlds, cmd);
		break;
	default:
		break;
	}

	dev_dbg(dev, "opcode: %#x sz_in: %zd sz_out: %zd rc: %d\n", cmd->opcode,
		cmd->size_in, cmd->size_out, rc);

	return rc;
}

static void label_area_release(void *lsa)
{
	vfree(lsa);
}

static int cxl_mock_mem_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cxl_memdev *cxlmd;
	struct cxl_dev_state *cxlds;
	void *lsa;
	int rc;

	lsa = vmalloc(LSA_SIZE);
	if (!lsa)
		return -ENOMEM;
	rc = devm_add_action_or_reset(dev, label_area_release, lsa);
	if (rc)
		return rc;
	dev_set_drvdata(dev, lsa);

	cxlds = cxl_dev_state_create(dev);
	if (IS_ERR(cxlds))
		return PTR_ERR(cxlds);

	cxlds->serial = pdev->id;
	cxlds->mbox_send = cxl_mock_mbox_send;
	cxlds->payload_size = SZ_4K;

	rc = cxl_enumerate_cmds(cxlds);
	if (rc)
		return rc;

	rc = cxl_dev_state_identify(cxlds);
	if (rc)
		return rc;

	rc = cxl_mem_create_range_info(cxlds);
	if (rc)
		return rc;

	cxlmd = devm_cxl_add_memdev(cxlds);
	if (IS_ERR(cxlmd))
		return PTR_ERR(cxlmd);

	if (resource_size(&cxlds->pmem_res) && IS_ENABLED(CONFIG_CXL_PMEM))
		rc = devm_cxl_add_nvdimm(dev, cxlmd);

	return 0;
}

static const struct platform_device_id cxl_mock_mem_ids[] = {
	{ .name = "cxl_mem", },
	{ },
};
MODULE_DEVICE_TABLE(platform, cxl_mock_mem_ids);

static struct platform_driver cxl_mock_mem_driver = {
	.probe = cxl_mock_mem_probe,
	.id_table = cxl_mock_mem_ids,
	.driver = {
		.name = KBUILD_MODNAME,
	},
};

module_platform_driver(cxl_mock_mem_driver);
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(CXL);
