/*	$OpenBSD: vioscsi.h,v 1.4 2025/08/02 15:16:18 dv Exp $  */

/*
 * Copyright (c) 2017 Carlos Cardenas <ccardenas@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Constants */
#define VIOSCSI_SEG_MAX			17
#define VIOSCSI_CDB_LEN			32
#define VIOSCSI_SENSE_LEN		96

#define VIOSCSI_NUM_REQ_QUEUES		1
#define VIOSCSI_CMD_PER_LUN		1
#define VIOSCSI_MAX_TARGET		1
#define VIOSCSI_MAX_LUN			1

#define VIOSCSI_BLOCK_SIZE_CDROM	2048

#define READ_TOC_START_TRACK		0x01
#define READ_TOC_LAST_TRACK		0x01
#define READ_TOC_LEAD_OUT_TRACK		0xaa
#define READ_TOC_ADR_CTL		0x14

#define SENSE_DEFAULT_ASCQ		0x00
#define SENSE_LBA_OUT_OF_RANGE		0x21
#define SENSE_ILLEGAL_CDB_FIELD		0x24
#define SENSE_MEDIUM_NOT_PRESENT	0x3a

#define INQUIRY_VENDOR			"OpenBSD "
#define INQUIRY_VENDOR_LEN		8
#define INQUIRY_PRODUCT			"VMM CD-ROM      "
#define INQUIRY_PRODUCT_LEN		16
#define INQUIRY_REVISION		"001 "
#define INQUIRY_REVISION_LEN		4

#define MODE_MEDIUM_TYPE_CODE		0x70
#define MODE_ERR_RECOVERY_PAGE_CODE	0x01
#define MODE_ERR_RECOVERY_PAGE_LEN	0x0a
#define MODE_READ_RETRY_COUNT		0x05
#define MODE_CDVD_CAP_PAGE_CODE		0x2a
#define MODE_CDVD_CAP_READ_CODE		0x08
#define MODE_CDVD_CAP_NUM_LEVELS	0x02

#define GESN_HEADER_LEN			0x06

#define G_CONFIG_REPLY_SIZE		56
#define G_CONFIG_REPLY_SIZE_HEX		0x0034

#define RPL_MIN_SIZE			16

/* Opcodes not defined in scsi */
#define GET_EVENT_STATUS_NOTIFICATION	0x4a
#define GET_CONFIGURATION		0x46
#define READ_DISC_INFORMATION		0x51
#define MECHANISM_STATUS		0xbd

/* Sizes for reply structures */
#define TOC_DATA_SIZE			20
#define GESN_SIZE			8
#define RESP_SENSE_LEN			14

/* Structures for Opcodes defined locally */
struct scsi_mechanism_status {
	u_int8_t opcode;
	u_int8_t unused[7];
	u_int8_t length[2];
	u_int8_t unused1;
	u_int8_t control;
};

struct scsi_mechanism_status_header {
	u_int8_t byte1;
	u_int8_t byte2;
	u_int8_t addr[3];
	u_int8_t num_slots;
	u_int8_t slot_len[2];
};

struct scsi_read_disc_information {
	u_int8_t opcode;
	u_int8_t byte2;
	u_int8_t unused[5];
	u_int8_t length[2];
	u_int8_t control;
};

struct scsi_gesn {
	u_int8_t opcode;
	u_int8_t byte2;
	u_int8_t unused[2];
	u_int8_t notify_class;
	u_int8_t unused1[2];
	u_int8_t length[2];
	u_int8_t control;
};

struct scsi_gesn_event_header {
	u_int8_t length[2];
	u_int8_t notification;
#define GESN_NOTIFY_NONE		0x0
#define GESN_NOTIFY_OP_CHANGE		0x1
#define GESN_NOTIFY_POWER_MGMT		0x2
#define GESN_NOTIFY_EXT_REQUEST		0x3
#define GESN_NOTIFY_MEDIA		0x4
#define GESN_NOTIFY_MULTIPLE_HOSTS	0x5
#define GESN_NOTIFY_DEVICE_BUSY		0x6
#define GESN_NOTIFY_RESERVED		0x7
	u_int8_t supported_event;
#define GESN_EVENT_NONE			0x1
#define GESN_EVENT_OP_CHANGE		0x2
#define GESN_EVENT_POWER_MGMT		0x4
#define GESN_EVENT_EXT_REQUEST		0x8
#define GESN_EVENT_MEDIA		0x10
#define GESN_EVENT_MULTIPLE_HOSTS	0x20
#define GESN_EVENT_DEVICE_BUSY		0x40
#define GESN_EVENT_RESERVED		0x80
};

struct scsi_gesn_power_event {
	u_int8_t event_code;
#define GESN_CODE_NOCHG			0x0
#define GESN_CODE_PWRCHG_SUCCESS	0x1
#define GESN_CODE_PWRCHG_FAIL		0x2
#define GESN_CODE_RESERVED		0x3
	u_int8_t status;
#define GESN_STATUS_RESERVED		0x0
#define GESN_STATUS_ACTIVE		0x1
#define GESN_STATUS_IDLE		0x2
#define GESN_STATUS_STANDBY		0x3
#define GESN_STATUS_SLEEP		0x4
	u_int8_t unused[2];
};

struct scsi_get_configuration {
	u_int8_t opcode;
	u_int8_t byte2;
	u_int8_t feature[2];
	u_int8_t unused[3];
	u_int8_t length[2];
	u_int8_t control;
};

struct scsi_config_feature_header {
	u_int8_t length[4];
	u_int8_t unused[2];
	u_int8_t current_profile[2];
	/* Complete Profile List in MMC-5, 5.3.1, Table 89 */
#define CONFIG_PROFILE_RESERVED		0x0000
#define CONFIG_PROFILE_CD_ROM		0x0008
#define CONFIG_PROFILE_NON_CONFORM	0xffff
};

struct scsi_config_generic_descriptor {
	u_int8_t feature_code[2];
	/* Complete Feature Code List in MMC-5, 5.2.3, Table 86 */
#define CONFIG_FEATURE_CODE_PROFILE		0x0000
#define CONFIG_FEATURE_CODE_CORE		0x0001
#define CONFIG_FEATURE_CODE_MORPHING		0x0002
#define CONFIG_FEATURE_CODE_REMOVE_MEDIA	0x0004
#define CONFIG_FEATURE_CODE_RANDOM_READ		0x0010
	u_int8_t byte3;
#define CONFIG_PROFILELIST_BYTE3		0x03
	u_int8_t length;
#define CONFIG_PROFILELIST_LENGTH		0x04
};

struct scsi_config_profile_descriptor {
	u_int8_t profile_number[2];
	u_int8_t byte3;
#define CONFIG_PROFILE_BYTE3			0x01
	u_int8_t unused;
};

struct scsi_config_core_descriptor {
	u_int8_t feature_code[2];
	u_int8_t byte3;
#define CONFIG_CORE_BYTE3		0x11
	u_int8_t length;
#define CONFIG_CORE_LENGTH		0x08
	u_int8_t phy_std[4];
	/* Complete PHYs List in MMC-5, 5.3.2, Table 91 */
#define CONFIG_CORE_PHY_SCSI		0x00000001
	u_int8_t unused[4];
};

struct scsi_config_morphing_descriptor {
	u_int8_t feature_code[2];
	u_int8_t byte3;
#define CONFIG_MORPHING_BYTE3		0x07
	u_int8_t length;
#define CONFIG_MORPHING_LENGTH		0x04
	/* OCE (bit 1), always set and ASYNC (bit 0) Bit */
	u_int8_t byte5;
#define CONFIG_MORPHING_BYTE5		0x2
	u_int8_t unused[3];
};

struct scsi_config_remove_media_descriptor {
	u_int8_t feature_code[2];
	u_int8_t byte3;
#define CONFIG_REMOVE_MEDIA_BYTE3	0x03
	u_int8_t length;
#define CONFIG_REMOVE_MEDIA_LENGTH	0x04
	/* Ejection Type */
	u_int8_t byte5;
#define CONFIG_REMOVE_MEDIA_BYTE5	0x09
	u_int8_t unused[3];
};

struct scsi_config_random_read_descriptor {
	u_int8_t feature_code[2];
	u_int8_t byte3;
#define CONFIG_RANDOM_READ_BYTE3	0x03
	u_int8_t length;
#define CONFIG_RANDOM_READ_LENGTH	0x08
	u_int8_t block_size[4];
	u_int8_t blocking_type[2];
#define CONFIG_RANDOM_READ_BLOCKING_TYPE	0x0010
	u_int8_t unused[2];
};

/*
 * Variant of scsi_report_luns_data in scsi_all.h
 * but with only one lun in the lun list
 */
struct vioscsi_report_luns_data {
	u_int8_t length[4];
	u_int8_t reserved[4];
#define RPL_SINGLE_LUN			8
	u_int8_t lun[RPL_SINGLE_LUN];
};
