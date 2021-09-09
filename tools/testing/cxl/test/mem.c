// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2021 Intel Corporation. All rights reserved.

#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/sizes.h>
#include <linux/bits.h>
#include <cxlmem.h>

#define LSA_SIZE SZ_128K
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
		.opcode = cpu_to_le16(CXL_MBOX_OP_SET_LSA),
		.effect = cpu_to_le16(EFFECT(1) | EFFECT(2)),
	},
};

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

static int mock_get_log(struct cxl_mem *cxlm, struct cxl_mbox_cmd *cmd)
{
	struct cxl_mbox_get_log *gl = cmd->payload_in;
	u32 offset = le32_to_cpu(gl->offset);
	u32 length = le32_to_cpu(gl->length);
	uuid_t uuid = DEFINE_CXL_CEL_UUID;
	void *data = &mock_cel;

	if (cmd->size_in < sizeof(*gl))
		return -EINVAL;
	if (length > cxlm->payload_size)
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

static int mock_id(struct cxl_mem *cxlm, struct cxl_mbox_cmd *cmd)
{
	struct platform_device *pdev = to_platform_device(cxlm->dev);
	struct cxl_mbox_identify id = {
		.fw_revision = { "mock fw v1 " },
		.lsa_size = cpu_to_le32(LSA_SIZE),
		/* FIXME: Add partition support */
		.partition_align = cpu_to_le64(0),
	};
	u64 capacity = 0;
	int i;

	if (cmd->size_out < sizeof(id))
		return -EINVAL;

	for (i = 0; i < 2; i++) {
		struct resource *res;

		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res)
			break;

		capacity += resource_size(res) / CXL_CAPACITY_MULTIPLIER;

		if (le64_to_cpu(id.partition_align))
			continue;

		if (res->desc == IORES_DESC_PERSISTENT_MEMORY)
			id.persistent_capacity = cpu_to_le64(
				resource_size(res) / CXL_CAPACITY_MULTIPLIER);
		else
			id.volatile_capacity = cpu_to_le64(
				resource_size(res) / CXL_CAPACITY_MULTIPLIER);
	}

	id.total_capacity = cpu_to_le64(capacity);

	memcpy(cmd->payload_out, &id, sizeof(id));

	return 0;
}

static int mock_get_lsa(struct cxl_mem *cxlm, struct cxl_mbox_cmd *cmd)
{
	struct cxl_mbox_get_lsa *get_lsa = cmd->payload_in;
	void *lsa = dev_get_drvdata(cxlm->dev);
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

static int mock_set_lsa(struct cxl_mem *cxlm, struct cxl_mbox_cmd *cmd)
{
	struct cxl_mbox_set_lsa *set_lsa = cmd->payload_in;
	void *lsa = dev_get_drvdata(cxlm->dev);
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

static int cxl_mock_mbox_send(struct cxl_mem *cxlm, struct cxl_mbox_cmd *cmd)
{
	struct device *dev = cxlm->dev;
	int rc = -EIO;

	switch (cmd->opcode) {
	case CXL_MBOX_OP_GET_SUPPORTED_LOGS:
		rc = mock_gsl(cmd);
		break;
	case CXL_MBOX_OP_GET_LOG:
		rc = mock_get_log(cxlm, cmd);
		break;
	case CXL_MBOX_OP_IDENTIFY:
		rc = mock_id(cxlm, cmd);
		break;
	case CXL_MBOX_OP_GET_LSA:
		rc = mock_get_lsa(cxlm, cmd);
		break;
	case CXL_MBOX_OP_SET_LSA:
		rc = mock_set_lsa(cxlm, cmd);
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
	struct cxl_mem *cxlm;
	void *lsa;
	int rc;

	lsa = vmalloc(LSA_SIZE);
	if (!lsa)
		return -ENOMEM;
	rc = devm_add_action_or_reset(dev, label_area_release, lsa);
	if (rc)
		return rc;
	dev_set_drvdata(dev, lsa);

	cxlm = cxl_mem_create(dev);
	if (IS_ERR(cxlm))
		return PTR_ERR(cxlm);

	cxlm->mbox_send = cxl_mock_mbox_send;
	cxlm->payload_size = SZ_4K;

	rc = cxl_mem_enumerate_cmds(cxlm);
	if (rc)
		return rc;

	rc = cxl_mem_identify(cxlm);
	if (rc)
		return rc;

	rc = cxl_mem_create_range_info(cxlm);
	if (rc)
		return rc;

	cxlmd = devm_cxl_add_memdev(cxlm);
	if (IS_ERR(cxlmd))
		return PTR_ERR(cxlmd);

	if (range_len(&cxlm->pmem_range) && IS_ENABLED(CONFIG_CXL_PMEM))
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
