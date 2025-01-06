/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2024 Intel Corporation */

#ifndef _HID_OVER_SPI_H_
#define _HID_OVER_SPI_H_

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

#endif /* _HID_OVER_SPI_H_ */
