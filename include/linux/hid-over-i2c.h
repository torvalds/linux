/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2024 Intel Corporation */

#ifndef _HID_OVER_I2C_H_
#define _HID_OVER_I2C_H_

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

#endif /* _HID_OVER_I2C_H_ */
