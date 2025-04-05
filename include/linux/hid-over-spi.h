/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2024 Intel Corporation */

#ifndef _HID_OVER_SPI_H_
#define _HID_OVER_SPI_H_

#include <linux/bits.h>
#include <linux/types.h>

/* Input report type definition in HIDSPI protocol */
enum input_report_type {
	INVALID_INPUT_REPORT_TYPE_0	= 0,
	DATA				= 1,
	INVALID_TYPE_2			= 2,
	RESET_RESPONSE			= 3,
	COMMAND_RESPONSE		= 4,
	GET_FEATURE_RESPONSE		= 5,
	INVALID_TYPE_6			= 6,
	DEVICE_DESCRIPTOR_RESPONSE	= 7,
	REPORT_DESCRIPTOR_RESPONSE	= 8,
	SET_FEATURE_RESPONSE		= 9,
	OUTPUT_REPORT_RESPONSE		= 10,
	GET_INPUT_REPORT_RESPONSE	= 11,
	INVALID_INPUT_REPORT_TYPE	= 0xF,
};

/* Output report type definition in HIDSPI protocol */
enum output_report_type {
	INVALID_OUTPUT_REPORT_TYPE_0	= 0,
	DEVICE_DESCRIPTOR		= 1,
	REPORT_DESCRIPTOR		= 2,
	SET_FEATURE			= 3,
	GET_FEATURE			= 4,
	OUTPUT_REPORT			= 5,
	GET_INPUT_REPORT		= 6,
	COMMAND_CONTENT			= 7,
};

/* Set power command ID for output report */
#define HIDSPI_SET_POWER_CMD_ID  1

/* Power state definition in HIDSPI protocol */
enum hidspi_power_state {
	HIDSPI_ON	= 1,
	HIDSPI_SLEEP	= 2,
	HIDSPI_OFF	= 3,
};

/**
 * Input report header definition in HIDSPI protocol
 * Report header size is 32bits, it includes:
 * protocol_ver:     [0:3] Current supported HIDSPI protocol version, must be 0x3
 * reserved0:        [4:7] Reserved bits
 * input_report_len: [8:21] Input report length in number bytes divided by 4
 * last_frag_flag:   [22]Indicate if this packet is last fragment.
 *                       1 - indicates last fragment
 *                       0 - indicates additional fragments
 * reserved1:        [23] Reserved bits
 * @sync_const:      [24:31] Used to validate input report header, must be 0x5A
 */
#define HIDSPI_INPUT_HEADER_SIZE		sizeof(u32)
#define HIDSPI_INPUT_HEADER_VER			GENMASK(3, 0)
#define HIDSPI_INPUT_HEADER_REPORT_LEN		GENMASK(21, 8)
#define HIDSPI_INPUT_HEADER_LAST_FLAG		BIT(22)
#define HIDSPI_INPUT_HEADER_SYNC		GENMASK(31, 24)

/**
 * struct input_report_body_header - Input report body header definition in HIDSPI protocol
 * @input_report_type: indicate input report type, reference to enum input_report_type
 * @content_len: this input report body packet length
 * @content_id: indicate this input report's report id
 */
struct input_report_body_header {
	u8 input_report_type;
	__le16 content_len;
	u8 content_id;
} __packed;

#define HIDSPI_INPUT_BODY_HEADER_SIZE	sizeof(struct input_report_body_header)

/**
 * struct input_report_body - Input report body definition in HIDSPI protocol
 * @body_hdr: input report body header
 * @content: input report body content
 */
struct input_report_body {
	struct input_report_body_header body_hdr;
	u8 content[];
} __packed;

#define HIDSPI_INPUT_BODY_SIZE(content_len)	((content_len) + HIDSPI_INPUT_BODY_HEADER_SIZE)

/**
 * struct output_report_header - Output report header definition in HIDSPI protocol
 * @report_type: output report type, reference to enum output_report_type
 * @content_len: length of content
 * @content_id: 0x00 - descriptors
 *              report id - Set/Feature feature or Input/Output Reports
 *              command opcode - for commands
 */
struct output_report_header {
	u8 report_type;
	__le16 content_len;
	u8 content_id;
} __packed;

#define HIDSPI_OUTPUT_REPORT_HEADER_SIZE	sizeof(struct output_report_header)

/**
 * struct output_report - Output report definition in HIDSPI protocol
 * @output_hdr: output report header
 * @content: output report content
 */
struct output_report {
	struct output_report_header output_hdr;
	u8 content[];
} __packed;

#define HIDSPI_OUTPUT_REPORT_SIZE(content_len)	((content_len) + HIDSPI_OUTPUT_REPORT_HEADER_SIZE)

/**
 * struct hidspi_dev_descriptor - HIDSPI device descriptor definition
 * @dev_desc_len: The length of the complete device descriptor, fixed to 0x18 (24).
 * @bcd_ver: The version number of the HIDSPI protocol supported.
 *           In binary coded decimal (BCD) format. Must be fixed to 0x0300.
 * @rep_desc_len: The length of the report descriptor
 * @max_input_len: The length of the largest possible HID input (or feature) report
 * @max_output_len: The length of the largest output (or feature) report
 * @max_frag_len: The length of the largest fragment, where a fragment represents
 *                the body of an input report.
 * @vendor_id: Device manufacturers vendor ID
 * @product_id: Device unique model/product ID
 * @version_id: Device’s unique version
 * @flags: Specify flags for the device’s operation
 * @reserved: Reserved and should be 0
 */
struct hidspi_dev_descriptor {
	__le16 dev_desc_len;
	__le16 bcd_ver;
	__le16 rep_desc_len;
	__le16 max_input_len;
	__le16 max_output_len;
	__le16 max_frag_len;
	__le16 vendor_id;
	__le16 product_id;
	__le16 version_id;
	__le16 flags;
	__le32 reserved;
};

#define HIDSPI_DEVICE_DESCRIPTOR_SIZE		sizeof(struct hidspi_dev_descriptor)
#define HIDSPI_INPUT_DEVICE_DESCRIPTOR_SIZE	\
	(HIDSPI_INPUT_BODY_HEADER_SIZE + HIDSPI_DEVICE_DESCRIPTOR_SIZE)

#endif /* _HID_OVER_SPI_H_ */
