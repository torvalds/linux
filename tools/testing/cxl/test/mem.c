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

#define PASS_TRY_LIMIT 3

struct cxl_mockmem_data {
	void *lsa;
	u32 security_state;
	u8 user_pass[NVDIMM_PASSPHRASE_LEN];
	u8 master_pass[NVDIMM_PASSPHRASE_LEN];
	int user_limit;
	int master_limit;

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

static int mock_rcd_id(struct cxl_dev_state *cxlds, struct cxl_mbox_cmd *cmd)
{
	struct cxl_mbox_identify id = {
		.fw_revision = { "mock fw v1 " },
		.total_capacity =
			cpu_to_le64(DEV_SIZE / CXL_CAPACITY_MULTIPLIER),
		.volatile_capacity =
			cpu_to_le64(DEV_SIZE / CXL_CAPACITY_MULTIPLIER),
	};

	if (cmd->size_out < sizeof(id))
		return -EINVAL;

	memcpy(cmd->payload_out, &id, sizeof(id));

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

static int mock_get_security_state(struct cxl_dev_state *cxlds,
				   struct cxl_mbox_cmd *cmd)
{
	struct cxl_mockmem_data *mdata = dev_get_drvdata(cxlds->dev);

	if (cmd->size_in)
		return -EINVAL;

	if (cmd->size_out != sizeof(u32))
		return -EINVAL;

	memcpy(cmd->payload_out, &mdata->security_state, sizeof(u32));

	return 0;
}

static void master_plimit_check(struct cxl_mockmem_data *mdata)
{
	if (mdata->master_limit == PASS_TRY_LIMIT)
		return;
	mdata->master_limit++;
	if (mdata->master_limit == PASS_TRY_LIMIT)
		mdata->security_state |= CXL_PMEM_SEC_STATE_MASTER_PLIMIT;
}

static void user_plimit_check(struct cxl_mockmem_data *mdata)
{
	if (mdata->user_limit == PASS_TRY_LIMIT)
		return;
	mdata->user_limit++;
	if (mdata->user_limit == PASS_TRY_LIMIT)
		mdata->security_state |= CXL_PMEM_SEC_STATE_USER_PLIMIT;
}

static int mock_set_passphrase(struct cxl_dev_state *cxlds, struct cxl_mbox_cmd *cmd)
{
	struct cxl_mockmem_data *mdata = dev_get_drvdata(cxlds->dev);
	struct cxl_set_pass *set_pass;

	if (cmd->size_in != sizeof(*set_pass))
		return -EINVAL;

	if (cmd->size_out != 0)
		return -EINVAL;

	if (mdata->security_state & CXL_PMEM_SEC_STATE_FROZEN) {
		cmd->return_code = CXL_MBOX_CMD_RC_SECURITY;
		return -ENXIO;
	}

	set_pass = cmd->payload_in;
	switch (set_pass->type) {
	case CXL_PMEM_SEC_PASS_MASTER:
		if (mdata->security_state & CXL_PMEM_SEC_STATE_MASTER_PLIMIT) {
			cmd->return_code = CXL_MBOX_CMD_RC_SECURITY;
			return -ENXIO;
		}
		/*
		 * CXL spec rev3.0 8.2.9.8.6.2, The master pasphrase shall only be set in
		 * the security disabled state when the user passphrase is not set.
		 */
		if (mdata->security_state & CXL_PMEM_SEC_STATE_USER_PASS_SET) {
			cmd->return_code = CXL_MBOX_CMD_RC_SECURITY;
			return -ENXIO;
		}
		if (memcmp(mdata->master_pass, set_pass->old_pass, NVDIMM_PASSPHRASE_LEN)) {
			master_plimit_check(mdata);
			cmd->return_code = CXL_MBOX_CMD_RC_PASSPHRASE;
			return -ENXIO;
		}
		memcpy(mdata->master_pass, set_pass->new_pass, NVDIMM_PASSPHRASE_LEN);
		mdata->security_state |= CXL_PMEM_SEC_STATE_MASTER_PASS_SET;
		return 0;

	case CXL_PMEM_SEC_PASS_USER:
		if (mdata->security_state & CXL_PMEM_SEC_STATE_USER_PLIMIT) {
			cmd->return_code = CXL_MBOX_CMD_RC_SECURITY;
			return -ENXIO;
		}
		if (memcmp(mdata->user_pass, set_pass->old_pass, NVDIMM_PASSPHRASE_LEN)) {
			user_plimit_check(mdata);
			cmd->return_code = CXL_MBOX_CMD_RC_PASSPHRASE;
			return -ENXIO;
		}
		memcpy(mdata->user_pass, set_pass->new_pass, NVDIMM_PASSPHRASE_LEN);
		mdata->security_state |= CXL_PMEM_SEC_STATE_USER_PASS_SET;
		return 0;

	default:
		cmd->return_code = CXL_MBOX_CMD_RC_INPUT;
	}
	return -EINVAL;
}

static int mock_disable_passphrase(struct cxl_dev_state *cxlds, struct cxl_mbox_cmd *cmd)
{
	struct cxl_mockmem_data *mdata = dev_get_drvdata(cxlds->dev);
	struct cxl_disable_pass *dis_pass;

	if (cmd->size_in != sizeof(*dis_pass))
		return -EINVAL;

	if (cmd->size_out != 0)
		return -EINVAL;

	if (mdata->security_state & CXL_PMEM_SEC_STATE_FROZEN) {
		cmd->return_code = CXL_MBOX_CMD_RC_SECURITY;
		return -ENXIO;
	}

	dis_pass = cmd->payload_in;
	switch (dis_pass->type) {
	case CXL_PMEM_SEC_PASS_MASTER:
		if (mdata->security_state & CXL_PMEM_SEC_STATE_MASTER_PLIMIT) {
			cmd->return_code = CXL_MBOX_CMD_RC_SECURITY;
			return -ENXIO;
		}

		if (!(mdata->security_state & CXL_PMEM_SEC_STATE_MASTER_PASS_SET)) {
			cmd->return_code = CXL_MBOX_CMD_RC_SECURITY;
			return -ENXIO;
		}

		if (memcmp(dis_pass->pass, mdata->master_pass, NVDIMM_PASSPHRASE_LEN)) {
			master_plimit_check(mdata);
			cmd->return_code = CXL_MBOX_CMD_RC_PASSPHRASE;
			return -ENXIO;
		}

		mdata->master_limit = 0;
		memset(mdata->master_pass, 0, NVDIMM_PASSPHRASE_LEN);
		mdata->security_state &= ~CXL_PMEM_SEC_STATE_MASTER_PASS_SET;
		return 0;

	case CXL_PMEM_SEC_PASS_USER:
		if (mdata->security_state & CXL_PMEM_SEC_STATE_USER_PLIMIT) {
			cmd->return_code = CXL_MBOX_CMD_RC_SECURITY;
			return -ENXIO;
		}

		if (!(mdata->security_state & CXL_PMEM_SEC_STATE_USER_PASS_SET)) {
			cmd->return_code = CXL_MBOX_CMD_RC_SECURITY;
			return -ENXIO;
		}

		if (memcmp(dis_pass->pass, mdata->user_pass, NVDIMM_PASSPHRASE_LEN)) {
			user_plimit_check(mdata);
			cmd->return_code = CXL_MBOX_CMD_RC_PASSPHRASE;
			return -ENXIO;
		}

		mdata->user_limit = 0;
		memset(mdata->user_pass, 0, NVDIMM_PASSPHRASE_LEN);
		mdata->security_state &= ~(CXL_PMEM_SEC_STATE_USER_PASS_SET |
					   CXL_PMEM_SEC_STATE_LOCKED);
		return 0;

	default:
		cmd->return_code = CXL_MBOX_CMD_RC_INPUT;
		return -EINVAL;
	}

	return 0;
}

static int mock_freeze_security(struct cxl_dev_state *cxlds, struct cxl_mbox_cmd *cmd)
{
	struct cxl_mockmem_data *mdata = dev_get_drvdata(cxlds->dev);

	if (cmd->size_in != 0)
		return -EINVAL;

	if (cmd->size_out != 0)
		return -EINVAL;

	if (mdata->security_state & CXL_PMEM_SEC_STATE_FROZEN)
		return 0;

	mdata->security_state |= CXL_PMEM_SEC_STATE_FROZEN;
	return 0;
}

static int mock_unlock_security(struct cxl_dev_state *cxlds, struct cxl_mbox_cmd *cmd)
{
	struct cxl_mockmem_data *mdata = dev_get_drvdata(cxlds->dev);

	if (cmd->size_in != NVDIMM_PASSPHRASE_LEN)
		return -EINVAL;

	if (cmd->size_out != 0)
		return -EINVAL;

	if (mdata->security_state & CXL_PMEM_SEC_STATE_FROZEN) {
		cmd->return_code = CXL_MBOX_CMD_RC_SECURITY;
		return -ENXIO;
	}

	if (!(mdata->security_state & CXL_PMEM_SEC_STATE_USER_PASS_SET)) {
		cmd->return_code = CXL_MBOX_CMD_RC_SECURITY;
		return -ENXIO;
	}

	if (mdata->security_state & CXL_PMEM_SEC_STATE_USER_PLIMIT) {
		cmd->return_code = CXL_MBOX_CMD_RC_SECURITY;
		return -ENXIO;
	}

	if (!(mdata->security_state & CXL_PMEM_SEC_STATE_LOCKED)) {
		cmd->return_code = CXL_MBOX_CMD_RC_SECURITY;
		return -ENXIO;
	}

	if (memcmp(cmd->payload_in, mdata->user_pass, NVDIMM_PASSPHRASE_LEN)) {
		if (++mdata->user_limit == PASS_TRY_LIMIT)
			mdata->security_state |= CXL_PMEM_SEC_STATE_USER_PLIMIT;
		cmd->return_code = CXL_MBOX_CMD_RC_PASSPHRASE;
		return -ENXIO;
	}

	mdata->user_limit = 0;
	mdata->security_state &= ~CXL_PMEM_SEC_STATE_LOCKED;
	return 0;
}

static int mock_passphrase_secure_erase(struct cxl_dev_state *cxlds,
					struct cxl_mbox_cmd *cmd)
{
	struct cxl_mockmem_data *mdata = dev_get_drvdata(cxlds->dev);
	struct cxl_pass_erase *erase;

	if (cmd->size_in != sizeof(*erase))
		return -EINVAL;

	if (cmd->size_out != 0)
		return -EINVAL;

	erase = cmd->payload_in;
	if (mdata->security_state & CXL_PMEM_SEC_STATE_FROZEN) {
		cmd->return_code = CXL_MBOX_CMD_RC_SECURITY;
		return -ENXIO;
	}

	if (mdata->security_state & CXL_PMEM_SEC_STATE_USER_PLIMIT &&
	    erase->type == CXL_PMEM_SEC_PASS_USER) {
		cmd->return_code = CXL_MBOX_CMD_RC_SECURITY;
		return -ENXIO;
	}

	if (mdata->security_state & CXL_PMEM_SEC_STATE_MASTER_PLIMIT &&
	    erase->type == CXL_PMEM_SEC_PASS_MASTER) {
		cmd->return_code = CXL_MBOX_CMD_RC_SECURITY;
		return -ENXIO;
	}

	switch (erase->type) {
	case CXL_PMEM_SEC_PASS_MASTER:
		/*
		 * The spec does not clearly define the behavior of the scenario
		 * where a master passphrase is passed in while the master
		 * passphrase is not set and user passphrase is not set. The
		 * code will take the assumption that it will behave the same
		 * as a CXL secure erase command without passphrase (0x4401).
		 */
		if (mdata->security_state & CXL_PMEM_SEC_STATE_MASTER_PASS_SET) {
			if (memcmp(mdata->master_pass, erase->pass,
				   NVDIMM_PASSPHRASE_LEN)) {
				master_plimit_check(mdata);
				cmd->return_code = CXL_MBOX_CMD_RC_PASSPHRASE;
				return -ENXIO;
			}
			mdata->master_limit = 0;
			mdata->user_limit = 0;
			mdata->security_state &= ~CXL_PMEM_SEC_STATE_USER_PASS_SET;
			memset(mdata->user_pass, 0, NVDIMM_PASSPHRASE_LEN);
			mdata->security_state &= ~CXL_PMEM_SEC_STATE_LOCKED;
		} else {
			/*
			 * CXL rev3 8.2.9.8.6.3 Disable Passphrase
			 * When master passphrase is disabled, the device shall
			 * return Invalid Input for the Passphrase Secure Erase
			 * command with master passphrase.
			 */
			return -EINVAL;
		}
		/* Scramble encryption keys so that data is effectively erased */
		break;
	case CXL_PMEM_SEC_PASS_USER:
		/*
		 * The spec does not clearly define the behavior of the scenario
		 * where a user passphrase is passed in while the user
		 * passphrase is not set. The code will take the assumption that
		 * it will behave the same as a CXL secure erase command without
		 * passphrase (0x4401).
		 */
		if (mdata->security_state & CXL_PMEM_SEC_STATE_USER_PASS_SET) {
			if (memcmp(mdata->user_pass, erase->pass,
				   NVDIMM_PASSPHRASE_LEN)) {
				user_plimit_check(mdata);
				cmd->return_code = CXL_MBOX_CMD_RC_PASSPHRASE;
				return -ENXIO;
			}
			mdata->user_limit = 0;
			mdata->security_state &= ~CXL_PMEM_SEC_STATE_USER_PASS_SET;
			memset(mdata->user_pass, 0, NVDIMM_PASSPHRASE_LEN);
		}

		/*
		 * CXL rev3 Table 8-118
		 * If user passphrase is not set or supported by device, current
		 * passphrase value is ignored. Will make the assumption that
		 * the operation will proceed as secure erase w/o passphrase
		 * since spec is not explicit.
		 */

		/* Scramble encryption keys so that data is effectively erased */
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int mock_get_lsa(struct cxl_dev_state *cxlds, struct cxl_mbox_cmd *cmd)
{
	struct cxl_mbox_get_lsa *get_lsa = cmd->payload_in;
	struct cxl_mockmem_data *mdata = dev_get_drvdata(cxlds->dev);
	void *lsa = mdata->lsa;
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
	struct cxl_mockmem_data *mdata = dev_get_drvdata(cxlds->dev);
	void *lsa = mdata->lsa;
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
		if (cxlds->rcd)
			rc = mock_rcd_id(cxlds, cmd);
		else
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
	case CXL_MBOX_OP_GET_SECURITY_STATE:
		rc = mock_get_security_state(cxlds, cmd);
		break;
	case CXL_MBOX_OP_SET_PASSPHRASE:
		rc = mock_set_passphrase(cxlds, cmd);
		break;
	case CXL_MBOX_OP_DISABLE_PASSPHRASE:
		rc = mock_disable_passphrase(cxlds, cmd);
		break;
	case CXL_MBOX_OP_FREEZE_SECURITY:
		rc = mock_freeze_security(cxlds, cmd);
		break;
	case CXL_MBOX_OP_UNLOCK:
		rc = mock_unlock_security(cxlds, cmd);
		break;
	case CXL_MBOX_OP_PASSPHRASE_SECURE_ERASE:
		rc = mock_passphrase_secure_erase(cxlds, cmd);
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

static bool is_rcd(struct platform_device *pdev)
{
	const struct platform_device_id *id = platform_get_device_id(pdev);

	return !!id->driver_data;
}

static int cxl_mock_mem_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cxl_memdev *cxlmd;
	struct cxl_dev_state *cxlds;
	struct cxl_mockmem_data *mdata;
	int rc;

	mdata = devm_kzalloc(dev, sizeof(*mdata), GFP_KERNEL);
	if (!mdata)
		return -ENOMEM;
	dev_set_drvdata(dev, mdata);

	mdata->lsa = vmalloc(LSA_SIZE);
	if (!mdata->lsa)
		return -ENOMEM;
	rc = devm_add_action_or_reset(dev, label_area_release, mdata->lsa);
	if (rc)
		return rc;

	cxlds = cxl_dev_state_create(dev);
	if (IS_ERR(cxlds))
		return PTR_ERR(cxlds);

	cxlds->serial = pdev->id;
	cxlds->mbox_send = cxl_mock_mbox_send;
	cxlds->payload_size = SZ_4K;
	if (is_rcd(pdev)) {
		cxlds->rcd = true;
		cxlds->component_reg_phys = CXL_RESOURCE_NONE;
	}

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

	return 0;
}

static ssize_t security_lock_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct cxl_mockmem_data *mdata = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n",
			  !!(mdata->security_state & CXL_PMEM_SEC_STATE_LOCKED));
}

static ssize_t security_lock_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct cxl_mockmem_data *mdata = dev_get_drvdata(dev);
	u32 mask = CXL_PMEM_SEC_STATE_FROZEN | CXL_PMEM_SEC_STATE_USER_PLIMIT |
		   CXL_PMEM_SEC_STATE_MASTER_PLIMIT;
	int val;

	if (kstrtoint(buf, 0, &val) < 0)
		return -EINVAL;

	if (val == 1) {
		if (!(mdata->security_state & CXL_PMEM_SEC_STATE_USER_PASS_SET))
			return -ENXIO;
		mdata->security_state |= CXL_PMEM_SEC_STATE_LOCKED;
		mdata->security_state &= ~mask;
	} else {
		return -EINVAL;
	}
	return count;
}

static DEVICE_ATTR_RW(security_lock);

static struct attribute *cxl_mock_mem_attrs[] = {
	&dev_attr_security_lock.attr,
	NULL
};
ATTRIBUTE_GROUPS(cxl_mock_mem);

static const struct platform_device_id cxl_mock_mem_ids[] = {
	{ .name = "cxl_mem", 0 },
	{ .name = "cxl_rcd", 1 },
	{ },
};
MODULE_DEVICE_TABLE(platform, cxl_mock_mem_ids);

static struct platform_driver cxl_mock_mem_driver = {
	.probe = cxl_mock_mem_probe,
	.id_table = cxl_mock_mem_ids,
	.driver = {
		.name = KBUILD_MODNAME,
		.dev_groups = cxl_mock_mem_groups,
	},
};

module_platform_driver(cxl_mock_mem_driver);
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(CXL);
