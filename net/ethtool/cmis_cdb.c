// SPDX-License-Identifier: GPL-2.0-only

#include <linux/ethtool.h>
#include <linux/jiffies.h>

#include "common.h"
#include "module_fw.h"
#include "cmis.h"

/* For accessing the LPL field on page 9Fh, the allowable length extension is
 * min(i, 15) byte octets where i specifies the allowable additional number of
 * byte octets in a READ or a WRITE.
 */
u32 ethtool_cmis_get_max_payload_size(u8 num_of_byte_octs)
{
	return 8 * (1 + min_t(u8, num_of_byte_octs, 15));
}

void ethtool_cmis_cdb_compose_args(struct ethtool_cmis_cdb_cmd_args *args,
				   enum ethtool_cmis_cdb_cmd_id cmd, u8 *pl,
				   u8 lpl_len, u16 max_duration,
				   u8 read_write_len_ext, u16 msleep_pre_rpl,
				   u8 rpl_exp_len, u8 flags)
{
	args->req.id = cpu_to_be16(cmd);
	args->req.lpl_len = lpl_len;
	if (pl)
		memcpy(args->req.payload, pl, args->req.lpl_len);

	args->max_duration = max_duration;
	args->read_write_len_ext =
		ethtool_cmis_get_max_payload_size(read_write_len_ext);
	args->msleep_pre_rpl = msleep_pre_rpl;
	args->rpl_exp_len = rpl_exp_len;
	args->flags = flags;
	args->err_msg = NULL;
}

void ethtool_cmis_page_init(struct ethtool_module_eeprom *page_data,
			    u8 page, u32 offset, u32 length)
{
	page_data->page = page;
	page_data->offset = offset;
	page_data->length = length;
	page_data->i2c_address = ETHTOOL_CMIS_CDB_PAGE_I2C_ADDR;
}

#define CMIS_REVISION_PAGE	0x00
#define CMIS_REVISION_OFFSET	0x01

struct cmis_rev_rpl {
	u8 rev;
};

static u8 cmis_rev_rpl_major(struct cmis_rev_rpl *rpl)
{
	return rpl->rev >> 4;
}

static int cmis_rev_major_get(struct net_device *dev, u8 *rev_major)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	struct ethtool_module_eeprom page_data = {0};
	struct netlink_ext_ack extack = {};
	struct cmis_rev_rpl rpl = {};
	int err;

	ethtool_cmis_page_init(&page_data, CMIS_REVISION_PAGE,
			       CMIS_REVISION_OFFSET, sizeof(rpl));
	page_data.data = (u8 *)&rpl;

	err = ops->get_module_eeprom_by_page(dev, &page_data, &extack);
	if (err < 0) {
		if (extack._msg)
			netdev_err(dev, "%s\n", extack._msg);
		return err;
	}

	*rev_major = cmis_rev_rpl_major(&rpl);

	return 0;
}

#define CMIS_CDB_ADVERTISEMENT_PAGE	0x01
#define CMIS_CDB_ADVERTISEMENT_OFFSET	0xA3

/* Based on section 8.4.11 "CDB Messaging Support Advertisement" in CMIS
 * standard revision 5.2.
 */
struct cmis_cdb_advert_rpl {
	u8	inst_supported;
	u8	read_write_len_ext;
	u8	resv1;
	u8	resv2;
};

static u8 cmis_cdb_advert_rpl_inst_supported(struct cmis_cdb_advert_rpl *rpl)
{
	return rpl->inst_supported >> 6;
}

static int cmis_cdb_advertisement_get(struct ethtool_cmis_cdb *cdb,
				      struct net_device *dev,
				      struct ethnl_module_fw_flash_ntf_params *ntf_params)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	struct ethtool_module_eeprom page_data = {};
	struct cmis_cdb_advert_rpl rpl = {};
	struct netlink_ext_ack extack = {};
	int err;

	ethtool_cmis_page_init(&page_data, CMIS_CDB_ADVERTISEMENT_PAGE,
			       CMIS_CDB_ADVERTISEMENT_OFFSET, sizeof(rpl));
	page_data.data = (u8 *)&rpl;

	err = ops->get_module_eeprom_by_page(dev, &page_data, &extack);
	if (err < 0) {
		if (extack._msg)
			netdev_err(dev, "%s\n", extack._msg);
		return err;
	}

	if (!cmis_cdb_advert_rpl_inst_supported(&rpl)) {
		ethnl_module_fw_flash_ntf_err(dev, ntf_params,
					      "CDB functionality is not supported",
					      NULL);
		return -EOPNOTSUPP;
	}

	cdb->read_write_len_ext = rpl.read_write_len_ext;

	return 0;
}

#define CMIS_PASSWORD_ENTRY_PAGE	0x00
#define CMIS_PASSWORD_ENTRY_OFFSET	0x7A

struct cmis_password_entry_pl {
	__be32 password;
};

/* See section 9.3.1 "CMD 0000h: Query Status" in CMIS standard revision 5.2.
 * struct cmis_cdb_query_status_pl and struct cmis_cdb_query_status_rpl are
 * structured layouts of the flat arrays,
 * struct ethtool_cmis_cdb_request::payload and
 * struct ethtool_cmis_cdb_rpl::payload respectively.
 */
struct cmis_cdb_query_status_pl {
	u16 response_delay;
};

struct cmis_cdb_query_status_rpl {
	u8 length;
	u8 status;
};

static int
cmis_cdb_validate_password(struct ethtool_cmis_cdb *cdb,
			   struct net_device *dev,
			   const struct ethtool_module_fw_flash_params *params,
			   struct ethnl_module_fw_flash_ntf_params *ntf_params)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	struct cmis_cdb_query_status_pl qs_pl = {0};
	struct ethtool_module_eeprom page_data = {};
	struct ethtool_cmis_cdb_cmd_args args = {};
	struct cmis_password_entry_pl pe_pl = {};
	struct cmis_cdb_query_status_rpl *rpl;
	struct netlink_ext_ack extack = {};
	int err;

	ethtool_cmis_page_init(&page_data, CMIS_PASSWORD_ENTRY_PAGE,
			       CMIS_PASSWORD_ENTRY_OFFSET, sizeof(pe_pl));
	page_data.data = (u8 *)&pe_pl;

	pe_pl = *((struct cmis_password_entry_pl *)page_data.data);
	pe_pl.password = params->password;
	err = ops->set_module_eeprom_by_page(dev, &page_data, &extack);
	if (err < 0) {
		if (extack._msg)
			netdev_err(dev, "%s\n", extack._msg);
		return err;
	}

	ethtool_cmis_cdb_compose_args(&args, ETHTOOL_CMIS_CDB_CMD_QUERY_STATUS,
				      (u8 *)&qs_pl, sizeof(qs_pl), 0,
				      cdb->read_write_len_ext, 1000,
				      sizeof(*rpl),
				      CDB_F_COMPLETION_VALID | CDB_F_STATUS_VALID);

	err = ethtool_cmis_cdb_execute_cmd(dev, &args);
	if (err < 0) {
		ethnl_module_fw_flash_ntf_err(dev, ntf_params,
					      "Query Status command failed",
					      args.err_msg);
		return err;
	}

	rpl = (struct cmis_cdb_query_status_rpl *)args.req.payload;
	if (!rpl->length || !rpl->status) {
		ethnl_module_fw_flash_ntf_err(dev, ntf_params,
					      "Password was not accepted",
					      NULL);
		return -EINVAL;
	}

	return 0;
}

/* Some CDB commands asserts the CDB completion flag only from CMIS
 * revision 5. Therefore, check the relevant validity flag only when
 * the revision supports it.
 */
void ethtool_cmis_cdb_check_completion_flag(u8 cmis_rev, u8 *flags)
{
	*flags |= cmis_rev >= 5 ? CDB_F_COMPLETION_VALID : 0;
}

#define CMIS_CDB_MODULE_FEATURES_RESV_DATA	34

/* See section 9.4.1 "CMD 0040h: Module Features" in CMIS standard revision 5.2.
 * struct cmis_cdb_module_features_rpl is structured layout of the flat
 * array, ethtool_cmis_cdb_rpl::payload.
 */
struct cmis_cdb_module_features_rpl {
	u8	resv1[CMIS_CDB_MODULE_FEATURES_RESV_DATA];
	__be16	max_completion_time;
};

static u16
cmis_cdb_module_features_completion_time(struct cmis_cdb_module_features_rpl *rpl)
{
	return be16_to_cpu(rpl->max_completion_time);
}

static int cmis_cdb_module_features_get(struct ethtool_cmis_cdb *cdb,
					struct net_device *dev,
					struct ethnl_module_fw_flash_ntf_params *ntf_params)
{
	struct ethtool_cmis_cdb_cmd_args args = {};
	struct cmis_cdb_module_features_rpl *rpl;
	u8 flags = CDB_F_STATUS_VALID;
	int err;

	ethtool_cmis_cdb_check_completion_flag(cdb->cmis_rev, &flags);
	ethtool_cmis_cdb_compose_args(&args,
				      ETHTOOL_CMIS_CDB_CMD_MODULE_FEATURES,
				      NULL, 0, 0, cdb->read_write_len_ext,
				      1000, sizeof(*rpl), flags);

	err = ethtool_cmis_cdb_execute_cmd(dev, &args);
	if (err < 0) {
		ethnl_module_fw_flash_ntf_err(dev, ntf_params,
					      "Module Features command failed",
					      args.err_msg);
		return err;
	}

	rpl = (struct cmis_cdb_module_features_rpl *)args.req.payload;
	cdb->max_completion_time =
		cmis_cdb_module_features_completion_time(rpl);

	return 0;
}

struct ethtool_cmis_cdb *
ethtool_cmis_cdb_init(struct net_device *dev,
		      const struct ethtool_module_fw_flash_params *params,
		      struct ethnl_module_fw_flash_ntf_params *ntf_params)
{
	struct ethtool_cmis_cdb *cdb;
	int err;

	cdb = kzalloc(sizeof(*cdb), GFP_KERNEL);
	if (!cdb)
		return ERR_PTR(-ENOMEM);

	err = cmis_rev_major_get(dev, &cdb->cmis_rev);
	if (err < 0)
		goto err;

	if (cdb->cmis_rev < 4) {
		ethnl_module_fw_flash_ntf_err(dev, ntf_params,
					      "CMIS revision doesn't support module firmware flashing",
					      NULL);
		err = -EOPNOTSUPP;
		goto err;
	}

	err = cmis_cdb_advertisement_get(cdb, dev, ntf_params);
	if (err < 0)
		goto err;

	if (params->password_valid) {
		err = cmis_cdb_validate_password(cdb, dev, params, ntf_params);
		if (err < 0)
			goto err;
	}

	err = cmis_cdb_module_features_get(cdb, dev, ntf_params);
	if (err < 0)
		goto err;

	return cdb;

err:
	ethtool_cmis_cdb_fini(cdb);
	return ERR_PTR(err);
}

void ethtool_cmis_cdb_fini(struct ethtool_cmis_cdb *cdb)
{
	kfree(cdb);
}

static bool is_completed(u8 data)
{
	return !!(data & 0x40);
}

#define CMIS_CDB_STATUS_SUCCESS	0x01

static bool status_success(u8 data)
{
	return data == CMIS_CDB_STATUS_SUCCESS;
}

#define CMIS_CDB_STATUS_FAIL	0x40

static bool status_fail(u8 data)
{
	return data & CMIS_CDB_STATUS_FAIL;
}

struct cmis_wait_for_cond_rpl {
	u8 state;
};

static int
ethtool_cmis_module_poll(struct net_device *dev,
			 struct cmis_wait_for_cond_rpl *rpl, u32 offset,
			 bool (*cond_success)(u8), bool (*cond_fail)(u8))
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	struct ethtool_module_eeprom page_data = {0};
	struct netlink_ext_ack extack = {};
	int err;

	ethtool_cmis_page_init(&page_data, 0, offset, sizeof(rpl));
	page_data.data = (u8 *)rpl;

	err = ops->get_module_eeprom_by_page(dev, &page_data, &extack);
	if (err < 0) {
		if (extack._msg)
			netdev_err_once(dev, "%s\n", extack._msg);
		return -EBUSY;
	}

	if ((*cond_success)(rpl->state))
		return 0;

	if (*cond_fail && (*cond_fail)(rpl->state))
		return -EIO;

	return -EBUSY;
}

int ethtool_cmis_wait_for_cond(struct net_device *dev, u8 flags, u8 flag,
			       u16 max_duration, u32 offset,
			       bool (*cond_success)(u8), bool (*cond_fail)(u8),
			       u8 *state)
{
	struct cmis_wait_for_cond_rpl rpl = {};
	unsigned long end;
	int err;

	if (!(flags & flag))
		return 0;

	if (max_duration == 0)
		max_duration = U16_MAX;

	end = jiffies + msecs_to_jiffies(max_duration);
	do {
		err = ethtool_cmis_module_poll(dev, &rpl, offset, cond_success,
					       cond_fail);
		if (err != -EBUSY)
			goto out;

		msleep(20);
	} while (time_before(jiffies, end));

	err = ethtool_cmis_module_poll(dev, &rpl, offset, cond_success,
				       cond_fail);
	if (err == -EBUSY)
		err = -ETIMEDOUT;

out:
	*state = rpl.state;
	return err;
}

#define CMIS_CDB_COMPLETION_FLAG_OFFSET	0x08

static int cmis_cdb_wait_for_completion(struct net_device *dev,
					struct ethtool_cmis_cdb_cmd_args *args)
{
	u8 flag;
	int err;

	/* Some vendors demand waiting time before checking completion flag
	 * in some CDB commands.
	 */
	msleep(args->msleep_pre_rpl);

	err = ethtool_cmis_wait_for_cond(dev, args->flags,
					 CDB_F_COMPLETION_VALID,
					 args->max_duration,
					 CMIS_CDB_COMPLETION_FLAG_OFFSET,
					 is_completed, NULL, &flag);
	if (err < 0)
		args->err_msg = "Completion Flag did not set on time";

	return err;
}

#define CMIS_CDB_STATUS_OFFSET	0x25

static void cmis_cdb_status_fail_msg_get(u8 status, char **err_msg)
{
	switch (status) {
	case 0b10000001:
		*err_msg = "CDB Status is in progress: Busy capturing command";
		break;
	case 0b10000010:
		*err_msg =
			"CDB Status is in progress: Busy checking/validating command";
		break;
	case 0b10000011:
		*err_msg = "CDB Status is in progress: Busy executing";
		break;
	case 0b01000000:
		*err_msg = "CDB status failed: no specific failure";
		break;
	case 0b01000010:
		*err_msg =
			"CDB status failed: Parameter range error or parameter not supported";
		break;
	case 0b01000101:
		*err_msg = "CDB status failed: CdbChkCode error";
		break;
	case 0b01000110:
		*err_msg = "CDB status failed: Password error";
		break;
	default:
		*err_msg = "Unknown failure reason";
	}
};

static int cmis_cdb_wait_for_status(struct net_device *dev,
				    struct ethtool_cmis_cdb_cmd_args *args)
{
	u8 status;
	int err;

	/* Some vendors demand waiting time before checking status in some
	 * CDB commands.
	 */
	msleep(args->msleep_pre_rpl);

	err = ethtool_cmis_wait_for_cond(dev, args->flags, CDB_F_STATUS_VALID,
					 args->max_duration,
					 CMIS_CDB_STATUS_OFFSET,
					 status_success, status_fail, &status);
	if (err < 0 && !args->err_msg)
		cmis_cdb_status_fail_msg_get(status, &args->err_msg);

	return err;
}

#define CMIS_CDB_REPLY_OFFSET	0x86

static int cmis_cdb_process_reply(struct net_device *dev,
				  struct ethtool_module_eeprom *page_data,
				  struct ethtool_cmis_cdb_cmd_args *args)
{
	u8 rpl_hdr_len = sizeof(struct ethtool_cmis_cdb_rpl_hdr);
	u8 rpl_exp_len = args->rpl_exp_len + rpl_hdr_len;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	struct netlink_ext_ack extack = {};
	struct ethtool_cmis_cdb_rpl *rpl;
	int err;

	if (!args->rpl_exp_len)
		return 0;

	ethtool_cmis_page_init(page_data, ETHTOOL_CMIS_CDB_CMD_PAGE,
			       CMIS_CDB_REPLY_OFFSET, rpl_exp_len);
	page_data->data = kmalloc(page_data->length, GFP_KERNEL);
	if (!page_data->data)
		return -ENOMEM;

	err = ops->get_module_eeprom_by_page(dev, page_data, &extack);
	if (err < 0) {
		if (extack._msg)
			netdev_err(dev, "%s\n", extack._msg);
		goto out;
	}

	rpl = (struct ethtool_cmis_cdb_rpl *)page_data->data;
	if ((args->rpl_exp_len > rpl->hdr.rpl_len + rpl_hdr_len) ||
	    !rpl->hdr.rpl_chk_code) {
		err = -EIO;
		goto out;
	}

	args->req.lpl_len = rpl->hdr.rpl_len;
	memcpy(args->req.payload, rpl->payload, args->req.lpl_len);

out:
	kfree(page_data->data);
	return err;
}

static int
__ethtool_cmis_cdb_execute_cmd(struct net_device *dev,
			       struct ethtool_module_eeprom *page_data,
			       u8 page, u32 offset, u32 length, void *data)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	struct netlink_ext_ack extack = {};
	int err;

	ethtool_cmis_page_init(page_data, page, offset, length);
	page_data->data = kmemdup(data, page_data->length, GFP_KERNEL);
	if (!page_data->data)
		return -ENOMEM;

	err = ops->set_module_eeprom_by_page(dev, page_data, &extack);
	if (err < 0) {
		if (extack._msg)
			netdev_err(dev, "%s\n", extack._msg);
	}

	kfree(page_data->data);
	return err;
}

static u8 cmis_cdb_calc_checksum(const void *data, size_t size)
{
	const u8 *bytes = (const u8 *)data;
	u8 checksum = 0;

	for (size_t i = 0; i < size; i++)
		checksum += bytes[i];

	return ~checksum;
}

#define CMIS_CDB_CMD_ID_OFFSET	0x80

int ethtool_cmis_cdb_execute_cmd(struct net_device *dev,
				 struct ethtool_cmis_cdb_cmd_args *args)
{
	struct ethtool_module_eeprom page_data = {};
	u32 offset;
	int err;

	args->req.chk_code =
		cmis_cdb_calc_checksum(&args->req, sizeof(args->req));

	if (args->req.lpl_len > args->read_write_len_ext) {
		args->err_msg = "LPL length is longer than CDB read write length extension allows";
		return -EINVAL;
	}

	/* According to the CMIS standard, there are two options to trigger the
	 * CDB commands. The default option is triggering the command by writing
	 * the CMDID bytes. Therefore, the command will be split to 2 calls:
	 * First, with everything except the CMDID field and then the CMDID
	 * field.
	 */
	offset = CMIS_CDB_CMD_ID_OFFSET +
		offsetof(struct ethtool_cmis_cdb_request, body);
	err = __ethtool_cmis_cdb_execute_cmd(dev, &page_data,
					     ETHTOOL_CMIS_CDB_CMD_PAGE, offset,
					     sizeof(args->req.body),
					     &args->req.body);
	if (err < 0)
		return err;

	offset = CMIS_CDB_CMD_ID_OFFSET +
		offsetof(struct ethtool_cmis_cdb_request, id);
	err = __ethtool_cmis_cdb_execute_cmd(dev, &page_data,
					     ETHTOOL_CMIS_CDB_CMD_PAGE, offset,
					     sizeof(args->req.id),
					     &args->req.id);
	if (err < 0)
		return err;

	err = cmis_cdb_wait_for_completion(dev, args);
	if (err < 0)
		return err;

	err = cmis_cdb_wait_for_status(dev, args);
	if (err < 0)
		return err;

	return cmis_cdb_process_reply(dev, &page_data, args);
}
