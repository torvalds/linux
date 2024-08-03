/* SPDX-License-Identifier: GPL-2.0-only */

#define ETHTOOL_CMIS_CDB_LPL_MAX_PL_LENGTH		120
#define ETHTOOL_CMIS_CDB_CMD_PAGE			0x9F
#define ETHTOOL_CMIS_CDB_PAGE_I2C_ADDR			0x50

/**
 * struct ethtool_cmis_cdb - CDB commands parameters
 * @cmis_rev: CMIS revision major.
 * @read_write_len_ext: Allowable additional number of byte octets to the LPL
 *			in a READ or a WRITE CDB commands.
 * @max_completion_time:  Maximum CDB command completion time in msec.
 */
struct ethtool_cmis_cdb {
	u8	cmis_rev;
	u8      read_write_len_ext;
	u16     max_completion_time;
};

enum ethtool_cmis_cdb_cmd_id {
	ETHTOOL_CMIS_CDB_CMD_QUERY_STATUS		= 0x0000,
	ETHTOOL_CMIS_CDB_CMD_MODULE_FEATURES		= 0x0040,
	ETHTOOL_CMIS_CDB_CMD_FW_MANAGMENT_FEATURES	= 0x0041,
	ETHTOOL_CMIS_CDB_CMD_START_FW_DOWNLOAD		= 0x0101,
	ETHTOOL_CMIS_CDB_CMD_WRITE_FW_BLOCK_LPL		= 0x0103,
	ETHTOOL_CMIS_CDB_CMD_COMPLETE_FW_DOWNLOAD	= 0x0107,
	ETHTOOL_CMIS_CDB_CMD_RUN_FW_IMAGE		= 0x0109,
	ETHTOOL_CMIS_CDB_CMD_COMMIT_FW_IMAGE		= 0x010A,
};

/**
 * struct ethtool_cmis_cdb_request - CDB commands request fields as decribed in
 *				the CMIS standard
 * @id: Command ID.
 * @epl_len: EPL memory length.
 * @lpl_len: LPL memory length.
 * @chk_code: Check code for the previous field and the payload.
 * @resv1: Added to match the CMIS standard request continuity.
 * @resv2: Added to match the CMIS standard request continuity.
 * @payload: Payload for the CDB commands.
 */
struct ethtool_cmis_cdb_request {
	__be16 id;
	struct_group(body,
		__be16 epl_len;
		u8 lpl_len;
		u8 chk_code;
		u8 resv1;
		u8 resv2;
		u8 payload[ETHTOOL_CMIS_CDB_LPL_MAX_PL_LENGTH];
	);
};

#define CDB_F_COMPLETION_VALID		BIT(0)
#define CDB_F_STATUS_VALID		BIT(1)
#define CDB_F_MODULE_STATE_VALID	BIT(2)

/**
 * struct ethtool_cmis_cdb_cmd_args - CDB commands execution arguments
 * @req: CDB command fields as described in the CMIS standard.
 * @max_duration: Maximum duration time for command completion in msec.
 * @read_write_len_ext: Allowable additional number of byte octets to the LPL
 *			in a READ or a WRITE commands.
 * @msleep_pre_rpl: Waiting time before checking reply in msec.
 * @rpl_exp_len: Expected reply length in bytes.
 * @flags: Validation flags for CDB commands.
 * @err_msg: Error message to be sent to user space.
 */
struct ethtool_cmis_cdb_cmd_args {
	struct ethtool_cmis_cdb_request req;
	u16				max_duration;
	u8				read_write_len_ext;
	u8				msleep_pre_rpl;
	u8                              rpl_exp_len;
	u8				flags;
	char				*err_msg;
};

/**
 * struct ethtool_cmis_cdb_rpl_hdr - CDB commands reply header arguments
 * @rpl_len: Reply length.
 * @rpl_chk_code: Reply check code.
 */
struct ethtool_cmis_cdb_rpl_hdr {
	u8 rpl_len;
	u8 rpl_chk_code;
};

/**
 * struct ethtool_cmis_cdb_rpl - CDB commands reply arguments
 * @hdr: CDB commands reply header arguments.
 * @payload: Payload for the CDB commands reply.
 */
struct ethtool_cmis_cdb_rpl {
	struct ethtool_cmis_cdb_rpl_hdr hdr;
	u8 payload[ETHTOOL_CMIS_CDB_LPL_MAX_PL_LENGTH];
};

u32 ethtool_cmis_get_max_payload_size(u8 num_of_byte_octs);

void ethtool_cmis_cdb_compose_args(struct ethtool_cmis_cdb_cmd_args *args,
				   enum ethtool_cmis_cdb_cmd_id cmd, u8 *pl,
				   u8 lpl_len, u16 max_duration,
				   u8 read_write_len_ext, u16 msleep_pre_rpl,
				   u8 rpl_exp_len, u8 flags);

void ethtool_cmis_cdb_check_completion_flag(u8 cmis_rev, u8 *flags);

void ethtool_cmis_page_init(struct ethtool_module_eeprom *page_data,
			    u8 page, u32 offset, u32 length);

struct ethtool_cmis_cdb *
ethtool_cmis_cdb_init(struct net_device *dev,
		      const struct ethtool_module_fw_flash_params *params,
		      struct ethnl_module_fw_flash_ntf_params *ntf_params);
void ethtool_cmis_cdb_fini(struct ethtool_cmis_cdb *cdb);

int ethtool_cmis_wait_for_cond(struct net_device *dev, u8 flags, u8 flag,
			       u16 max_duration, u32 offset,
			       bool (*cond_success)(u8), bool (*cond_fail)(u8), u8 *state);

int ethtool_cmis_cdb_execute_cmd(struct net_device *dev,
				 struct ethtool_cmis_cdb_cmd_args *args);
