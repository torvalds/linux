/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2024 Intel Corporation */

#include <linux/bits.h>

#ifndef _HID_OVER_I2C_H_
#define _HID_OVER_I2C_H_

#define HIDI2C_REG_LEN		sizeof(__le16)

/* Input report type definition in HIDI2C protocol */
enum hidi2c_report_type {
	HIDI2C_RESERVED = 0,
	HIDI2C_INPUT,
	HIDI2C_OUTPUT,
	HIDI2C_FEATURE,
};

/* Power state type definition in HIDI2C protocol */
enum hidi2c_power_state {
	HIDI2C_ON,
	HIDI2C_SLEEP,
};

/* Opcode type definition in HIDI2C protocol */
enum hidi2c_opcode {
	HIDI2C_RESET = 1,
	HIDI2C_GET_REPORT,
	HIDI2C_SET_REPORT,
	HIDI2C_GET_IDLE,
	HIDI2C_SET_IDLE,
	HIDI2C_GET_PROTOCOL,
	HIDI2C_SET_PROTOCOL,
	HIDI2C_SET_POWER,
};

/**
 * struct hidi2c_report_packet - Report packet definition in HIDI2C protocol
 * @len: data field length
 * @data: HIDI2C report packet data
 */
struct hidi2c_report_packet {
	__le16 len;
	u8 data[];
} __packed;

#define HIDI2C_LENGTH_LEN	sizeof(__le16)

#define HIDI2C_PACKET_LEN(data_len)	((data_len) + HIDI2C_LENGTH_LEN)
#define HIDI2C_DATA_LEN(pkt_len)	((pkt_len) - HIDI2C_LENGTH_LEN)

#define HIDI2C_CMD_MAX_RI	0x0F

/**
 * HIDI2C command data packet - Command packet definition in HIDI2C protocol
 * @report_id:		[0:3] report id (<15) for features or output reports
 * @report_type:	[4:5] indicate report type, reference to hidi2c_report_type
 * @reserved0:		[6:7] reserved bits
 * @opcode:		[8:11] command operation code, reference to hidi2c_opcode
 * @reserved1:		[12:15] reserved bits
 * @report_id_optional: [23:16] appended 3rd byte.
 *                      If the report_id in the low byte is set to the
 *                      sentinel value (HIDI2C_CMD_MAX_RI), then this
 *                      optional third byte represents the report id (>=15)
 *                      Otherwise, not this 3rd byte.
 */

#define HIDI2C_CMD_LEN			sizeof(__le16)
#define HIDI2C_CMD_LEN_OPT		(sizeof(__le16) + 1)
#define HIDI2C_CMD_REPORT_ID		GENMASK(3, 0)
#define HIDI2C_CMD_REPORT_TYPE		GENMASK(5, 4)
#define HIDI2C_CMD_OPCODE		GENMASK(11, 8)
#define HIDI2C_CMD_OPCODE		GENMASK(11, 8)
#define HIDI2C_CMD_3RD_BYTE		GENMASK(23, 16)

#define HIDI2C_HID_DESC_BCDVERSION	0x100

/**
 * struct hidi2c_dev_descriptor - HIDI2C device descriptor definition
 * @dev_desc_len: The length of the complete device descriptor, fixed to 0x1E (30).
 * @bcd_ver: The version number of the HIDI2C protocol supported.
 *           In binary coded decimal (BCD) format.
 * @report_desc_len: The length of the report descriptor
 * @report_desc_reg: The register address to retrieve report descriptor
 * @input_reg: the register address to retrieve input report
 * @max_input_len: The length of the largest possible HID input (or feature) report
 * @output_reg: the register address to send output report
 * @max_output_len: The length of the largest output (or feature) report
 * @cmd_reg: the register address to send command
 * @data_reg: the register address to send command data
 * @vendor_id: Device manufacturers vendor ID
 * @product_id: Device unique model/product ID
 * @version_id: Device’s unique version
 * @reserved0: Reserved and should be 0
 * @reserved1: Reserved and should be 0
 */
struct hidi2c_dev_descriptor {
	__le16 dev_desc_len;
	__le16 bcd_ver;
	__le16 report_desc_len;
	__le16 report_desc_reg;
	__le16 input_reg;
	__le16 max_input_len;
	__le16 output_reg;
	__le16 max_output_len;
	__le16 cmd_reg;
	__le16 data_reg;
	__le16 vendor_id;
	__le16 product_id;
	__le16 version_id;
	__le16 reserved0;
	__le16 reserved1;
} __packed;

#define HIDI2C_DEV_DESC_LEN		sizeof(struct hidi2c_dev_descriptor)

#endif /* _HID_OVER_I2C_H_ */
