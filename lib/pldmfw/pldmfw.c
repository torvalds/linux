// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2019, Intel Corporation. */

#include <linux/unaligned.h>
#include <linux/crc32.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pldmfw.h>
#include <linux/slab.h>
#include <linux/uuid.h>

#include "pldmfw_private.h"

/* Internal structure used to store details about the PLDM image file as it is
 * being validated and processed.
 */
struct pldmfw_priv {
	struct pldmfw *context;
	const struct firmware *fw;

	/* current offset of firmware image */
	size_t offset;

	struct list_head records;
	struct list_head components;

	/* PLDM Firmware Package Header */
	const struct __pldm_header *header;
	u16 total_header_size;

	/* length of the component bitmap */
	u16 component_bitmap_len;
	u16 bitmap_size;

	/* Start of the component image information */
	u16 component_count;
	const u8 *component_start;

	/* Start pf the firmware device id records */
	const u8 *record_start;
	u8 record_count;

	/* The CRC at the end of the package header */
	u32 header_crc;

	struct pldmfw_record *matching_record;
};

/**
 * pldm_check_fw_space - Verify that the firmware image has space left
 * @data: pointer to private data
 * @offset: offset to start from
 * @length: length to check for
 *
 * Verify that the firmware data can hold a chunk of bytes with the specified
 * offset and length.
 *
 * Returns: zero on success, or -EFAULT if the image does not have enough
 * space left to fit the expected length.
 */
static int
pldm_check_fw_space(struct pldmfw_priv *data, size_t offset, size_t length)
{
	size_t expected_size = offset + length;
	struct device *dev = data->context->dev;

	if (data->fw->size < expected_size) {
		dev_dbg(dev, "Firmware file size smaller than expected. Got %zu bytes, needed %zu bytes\n",
			data->fw->size, expected_size);
		return -EFAULT;
	}

	return 0;
}

/**
 * pldm_move_fw_offset - Move the current firmware offset forward
 * @data: pointer to private data
 * @bytes_to_move: number of bytes to move the offset forward by
 *
 * Check that there is enough space past the current offset, and then move the
 * offset forward by this amount.
 *
 * Returns: zero on success, or -EFAULT if the image is too small to fit the
 * expected length.
 */
static int
pldm_move_fw_offset(struct pldmfw_priv *data, size_t bytes_to_move)
{
	int err;

	err = pldm_check_fw_space(data, data->offset, bytes_to_move);
	if (err)
		return err;

	data->offset += bytes_to_move;

	return 0;
}

/**
 * pldm_parse_header - Validate and extract details about the PLDM header
 * @data: pointer to private data
 *
 * Performs initial basic verification of the PLDM image, up to the first
 * firmware record.
 *
 * This includes the following checks and extractions
 *
 *   * Verify that the UUID at the start of the header matches the expected
 *     value as defined in the DSP0267 PLDM specification
 *   * Check that the revision is 0x01
 *   * Extract the total header_size and verify that the image is large enough
 *     to contain at least the length of this header
 *   * Extract the size of the component bitmap length
 *   * Extract a pointer to the start of the record area
 *
 * Returns: zero on success, or a negative error code on failure.
 */
static int pldm_parse_header(struct pldmfw_priv *data)
{
	const struct __pldmfw_record_area *record_area;
	struct device *dev = data->context->dev;
	const struct __pldm_header *header;
	size_t header_size;
	int err;

	err = pldm_move_fw_offset(data, sizeof(*header));
	if (err)
		return err;

	header = (const struct __pldm_header *)data->fw->data;
	data->header = header;

	if (!uuid_equal(&header->id, &pldm_firmware_header_id)) {
		dev_dbg(dev, "Invalid package header identifier. Expected UUID %pUB, but got %pUB\n",
			&pldm_firmware_header_id, &header->id);
		return -EINVAL;
	}

	if (header->revision != PACKAGE_HEADER_FORMAT_REVISION) {
		dev_dbg(dev, "Invalid package header revision. Expected revision %u but got %u\n",
			PACKAGE_HEADER_FORMAT_REVISION, header->revision);
		return -EOPNOTSUPP;
	}

	data->total_header_size = get_unaligned_le16(&header->size);
	header_size = data->total_header_size - sizeof(*header);

	err = pldm_check_fw_space(data, data->offset, header_size);
	if (err)
		return err;

	data->component_bitmap_len =
		get_unaligned_le16(&header->component_bitmap_len);

	if (data->component_bitmap_len % 8 != 0) {
		dev_dbg(dev, "Invalid component bitmap length. The length is %u, which is not a multiple of 8\n",
			data->component_bitmap_len);
		return -EINVAL;
	}

	data->bitmap_size = data->component_bitmap_len / 8;

	err = pldm_move_fw_offset(data, header->version_len);
	if (err)
		return err;

	/* extract a pointer to the record area, which just follows the main
	 * PLDM header data.
	 */
	record_area = (const struct __pldmfw_record_area *)(data->fw->data +
							 data->offset);

	err = pldm_move_fw_offset(data, sizeof(*record_area));
	if (err)
		return err;

	data->record_count = record_area->record_count;
	data->record_start = record_area->records;

	return 0;
}

/**
 * pldm_check_desc_tlv_len - Check that the length matches expectation
 * @data: pointer to image details
 * @type: the descriptor type
 * @size: the length from the descriptor header
 *
 * If the descriptor type is one of the documented descriptor types according
 * to the standard, verify that the provided length matches.
 *
 * If the type is not recognized or is VENDOR_DEFINED, return zero.
 *
 * Returns: zero on success, or -EINVAL if the specified size of a standard
 * TLV does not match the expected value defined for that TLV.
 */
static int
pldm_check_desc_tlv_len(struct pldmfw_priv *data, u16 type, u16 size)
{
	struct device *dev = data->context->dev;
	u16 expected_size;

	switch (type) {
	case PLDM_DESC_ID_PCI_VENDOR_ID:
	case PLDM_DESC_ID_PCI_DEVICE_ID:
	case PLDM_DESC_ID_PCI_SUBVENDOR_ID:
	case PLDM_DESC_ID_PCI_SUBDEV_ID:
		expected_size = 2;
		break;
	case PLDM_DESC_ID_PCI_REVISION_ID:
		expected_size = 1;
		break;
	case PLDM_DESC_ID_PNP_VENDOR_ID:
		expected_size = 3;
		break;
	case PLDM_DESC_ID_IANA_ENTERPRISE_ID:
	case PLDM_DESC_ID_ACPI_VENDOR_ID:
	case PLDM_DESC_ID_PNP_PRODUCT_ID:
	case PLDM_DESC_ID_ACPI_PRODUCT_ID:
		expected_size = 4;
		break;
	case PLDM_DESC_ID_UUID:
		expected_size = 16;
		break;
	case PLDM_DESC_ID_VENDOR_DEFINED:
		return 0;
	default:
		/* Do not report an error on an unexpected TLV */
		dev_dbg(dev, "Found unrecognized TLV type 0x%04x\n", type);
		return 0;
	}

	if (size != expected_size) {
		dev_dbg(dev, "Found TLV type 0x%04x with unexpected length. Got %u bytes, but expected %u bytes\n",
			type, size, expected_size);
		return -EINVAL;
	}

	return 0;
}

/**
 * pldm_parse_desc_tlvs - Check and skip past a number of TLVs
 * @data: pointer to private data
 * @record: pointer to the record this TLV belongs too
 * @desc_count: descriptor count
 *
 * From the current offset, read and extract the descriptor TLVs, updating the
 * current offset each time.
 *
 * Returns: zero on success, or a negative error code on failure.
 */
static int
pldm_parse_desc_tlvs(struct pldmfw_priv *data, struct pldmfw_record *record, u8 desc_count)
{
	const struct __pldmfw_desc_tlv *__desc;
	const u8 *desc_start;
	u8 i;

	desc_start = data->fw->data + data->offset;

	pldm_for_each_desc_tlv(i, __desc, desc_start, desc_count) {
		struct pldmfw_desc_tlv *desc;
		int err;
		u16 type, size;

		err = pldm_move_fw_offset(data, sizeof(*__desc));
		if (err)
			return err;

		type = get_unaligned_le16(&__desc->type);

		/* According to DSP0267, this only includes the data field */
		size = get_unaligned_le16(&__desc->size);

		err = pldm_check_desc_tlv_len(data, type, size);
		if (err)
			return err;

		/* check that we have space and move the offset forward */
		err = pldm_move_fw_offset(data, size);
		if (err)
			return err;

		desc = kzalloc(sizeof(*desc), GFP_KERNEL);
		if (!desc)
			return -ENOMEM;

		desc->type = type;
		desc->size = size;
		desc->data = __desc->data;

		list_add_tail(&desc->entry, &record->descs);
	}

	return 0;
}

/**
 * pldm_parse_one_record - Verify size of one PLDM record
 * @data: pointer to image details
 * @__record: pointer to the record to check
 *
 * This function checks that the record size does not exceed either the size
 * of the firmware file or the total length specified in the header section.
 *
 * It also verifies that the recorded length of the start of the record
 * matches the size calculated by adding the static structure length, the
 * component bitmap length, the version string length, the length of all
 * descriptor TLVs, and the length of the package data.
 *
 * Returns: zero on success, or a negative error code on failure.
 */
static int
pldm_parse_one_record(struct pldmfw_priv *data,
		      const struct __pldmfw_record_info *__record)
{
	struct pldmfw_record *record;
	size_t measured_length;
	int err;
	const u8 *bitmap_ptr;
	u16 record_len;
	int i;

	/* Make a copy and insert it into the record list */
	record = kzalloc(sizeof(*record), GFP_KERNEL);
	if (!record)
		return -ENOMEM;

	INIT_LIST_HEAD(&record->descs);
	list_add_tail(&record->entry, &data->records);

	/* Then check that we have space and move the offset */
	err = pldm_move_fw_offset(data, sizeof(*__record));
	if (err)
		return err;

	record_len = get_unaligned_le16(&__record->record_len);
	record->package_data_len = get_unaligned_le16(&__record->package_data_len);
	record->version_len = __record->version_len;
	record->version_type = __record->version_type;

	bitmap_ptr = data->fw->data + data->offset;

	/* check that we have space for the component bitmap length */
	err = pldm_move_fw_offset(data, data->bitmap_size);
	if (err)
		return err;

	record->component_bitmap_len = data->component_bitmap_len;
	record->component_bitmap = bitmap_zalloc(record->component_bitmap_len,
						 GFP_KERNEL);
	if (!record->component_bitmap)
		return -ENOMEM;

	for (i = 0; i < data->bitmap_size; i++)
		bitmap_set_value8(record->component_bitmap, bitmap_ptr[i], i * 8);

	record->version_string = data->fw->data + data->offset;

	err = pldm_move_fw_offset(data, record->version_len);
	if (err)
		return err;

	/* Scan through the descriptor TLVs and find the end */
	err = pldm_parse_desc_tlvs(data, record, __record->descriptor_count);
	if (err)
		return err;

	record->package_data = data->fw->data + data->offset;

	err = pldm_move_fw_offset(data, record->package_data_len);
	if (err)
		return err;

	measured_length = data->offset - ((const u8 *)__record - data->fw->data);
	if (measured_length != record_len) {
		dev_dbg(data->context->dev, "Unexpected record length. Measured record length is %zu bytes, expected length is %u bytes\n",
			measured_length, record_len);
		return -EFAULT;
	}

	return 0;
}

/**
 * pldm_parse_records - Locate the start of the component area
 * @data: pointer to private data
 *
 * Extract the record count, and loop through each record, searching for the
 * component area.
 *
 * Returns: zero on success, or a negative error code on failure.
 */
static int pldm_parse_records(struct pldmfw_priv *data)
{
	const struct __pldmfw_component_area *component_area;
	const struct __pldmfw_record_info *record;
	int err;
	u8 i;

	pldm_for_each_record(i, record, data->record_start, data->record_count) {
		err = pldm_parse_one_record(data, record);
		if (err)
			return err;
	}

	/* Extract a pointer to the component area, which just follows the
	 * PLDM device record data.
	 */
	component_area = (const struct __pldmfw_component_area *)(data->fw->data + data->offset);

	err = pldm_move_fw_offset(data, sizeof(*component_area));
	if (err)
		return err;

	data->component_count =
		get_unaligned_le16(&component_area->component_image_count);
	data->component_start = component_area->components;

	return 0;
}

/**
 * pldm_parse_components - Locate the CRC header checksum
 * @data: pointer to private data
 *
 * Extract the component count, and find the pointer to the component area.
 * Scan through each component searching for the end, which should point to
 * the package header checksum.
 *
 * Extract the package header CRC and save it for verification.
 *
 * Returns: zero on success, or a negative error code on failure.
 */
static int pldm_parse_components(struct pldmfw_priv *data)
{
	const struct __pldmfw_component_info *__component;
	struct device *dev = data->context->dev;
	const u8 *header_crc_ptr;
	int err;
	u8 i;

	pldm_for_each_component(i, __component, data->component_start, data->component_count) {
		struct pldmfw_component *component;
		u32 offset, size;

		err = pldm_move_fw_offset(data, sizeof(*__component));
		if (err)
			return err;

		err = pldm_move_fw_offset(data, __component->version_len);
		if (err)
			return err;

		offset = get_unaligned_le32(&__component->location_offset);
		size = get_unaligned_le32(&__component->size);

		err = pldm_check_fw_space(data, offset, size);
		if (err)
			return err;

		component = kzalloc(sizeof(*component), GFP_KERNEL);
		if (!component)
			return -ENOMEM;

		component->index = i;
		component->classification = get_unaligned_le16(&__component->classification);
		component->identifier = get_unaligned_le16(&__component->identifier);
		component->comparison_stamp = get_unaligned_le32(&__component->comparison_stamp);
		component->options = get_unaligned_le16(&__component->options);
		component->activation_method = get_unaligned_le16(&__component->activation_method);
		component->version_type = __component->version_type;
		component->version_len = __component->version_len;
		component->version_string = __component->version_string;
		component->component_data = data->fw->data + offset;
		component->component_size = size;

		list_add_tail(&component->entry, &data->components);
	}

	header_crc_ptr = data->fw->data + data->offset;

	err = pldm_move_fw_offset(data, sizeof(data->header_crc));
	if (err)
		return err;

	/* Make sure that we reached the expected offset */
	if (data->offset != data->total_header_size) {
		dev_dbg(dev, "Invalid firmware header size. Expected %u but got %zu\n",
			data->total_header_size, data->offset);
		return -EFAULT;
	}

	data->header_crc = get_unaligned_le32(header_crc_ptr);

	return 0;
}

/**
 * pldm_verify_header_crc - Verify that the CRC in the header matches
 * @data: pointer to private data
 *
 * Calculates the 32-bit CRC using the standard IEEE 802.3 CRC polynomial and
 * compares it to the value stored in the header.
 *
 * Returns: zero on success if the CRC matches, or -EBADMSG on an invalid CRC.
 */
static int pldm_verify_header_crc(struct pldmfw_priv *data)
{
	struct device *dev = data->context->dev;
	u32 calculated_crc;
	size_t length;

	/* Calculate the 32-bit CRC of the header header contents up to but
	 * not including the checksum. Note that the Linux crc32_le function
	 * does not perform an expected final XOR.
	 */
	length = data->offset - sizeof(data->header_crc);
	calculated_crc = crc32_le(~0, data->fw->data, length) ^ ~0;

	if (calculated_crc != data->header_crc) {
		dev_dbg(dev, "Invalid CRC in firmware header. Got 0x%08x but expected 0x%08x\n",
			calculated_crc, data->header_crc);
		return -EBADMSG;
	}

	return 0;
}

/**
 * pldmfw_free_priv - Free memory allocated while parsing the PLDM image
 * @data: pointer to the PLDM data structure
 *
 * Loops through and clears all allocated memory associated with each
 * allocated descriptor, record, and component.
 */
static void pldmfw_free_priv(struct pldmfw_priv *data)
{
	struct pldmfw_component *component, *c_safe;
	struct pldmfw_record *record, *r_safe;
	struct pldmfw_desc_tlv *desc, *d_safe;

	list_for_each_entry_safe(component, c_safe, &data->components, entry) {
		list_del(&component->entry);
		kfree(component);
	}

	list_for_each_entry_safe(record, r_safe, &data->records, entry) {
		list_for_each_entry_safe(desc, d_safe, &record->descs, entry) {
			list_del(&desc->entry);
			kfree(desc);
		}

		if (record->component_bitmap) {
			bitmap_free(record->component_bitmap);
			record->component_bitmap = NULL;
		}

		list_del(&record->entry);
		kfree(record);
	}
}

/**
 * pldm_parse_image - parse and extract details from PLDM image
 * @data: pointer to private data
 *
 * Verify that the firmware file contains valid data for a PLDM firmware
 * file. Extract useful pointers and data from the firmware file and store
 * them in the data structure.
 *
 * The PLDM firmware file format is defined in DMTF DSP0267 1.0.0. Care
 * should be taken to use get_unaligned_le* when accessing data from the
 * pointers in data.
 *
 * Returns: zero on success, or a negative error code on failure.
 */
static int pldm_parse_image(struct pldmfw_priv *data)
{
	int err;

	if (WARN_ON(!(data->context->dev && data->fw->data && data->fw->size)))
		return -EINVAL;

	err = pldm_parse_header(data);
	if (err)
		return err;

	err = pldm_parse_records(data);
	if (err)
		return err;

	err = pldm_parse_components(data);
	if (err)
		return err;

	return pldm_verify_header_crc(data);
}

/* these are u32 so that we can store PCI_ANY_ID */
struct pldm_pci_record_id {
	int vendor;
	int device;
	int subsystem_vendor;
	int subsystem_device;
};

/**
 * pldmfw_op_pci_match_record - Check if a PCI device matches the record
 * @context: PLDM fw update structure
 * @record: list of records extracted from the PLDM image
 *
 * Determine of the PCI device associated with this device matches the record
 * data provided.
 *
 * Searches the descriptor TLVs and extracts the relevant descriptor data into
 * a pldm_pci_record_id. This is then compared against the PCI device ID
 * information.
 *
 * Returns: true if the device matches the record, false otherwise.
 */
bool pldmfw_op_pci_match_record(struct pldmfw *context, struct pldmfw_record *record)
{
	struct pci_dev *pdev = to_pci_dev(context->dev);
	struct pldm_pci_record_id id = {
		.vendor = PCI_ANY_ID,
		.device = PCI_ANY_ID,
		.subsystem_vendor = PCI_ANY_ID,
		.subsystem_device = PCI_ANY_ID,
	};
	struct pldmfw_desc_tlv *desc;

	list_for_each_entry(desc, &record->descs, entry) {
		u16 value;
		int *ptr;

		switch (desc->type) {
		case PLDM_DESC_ID_PCI_VENDOR_ID:
			ptr = &id.vendor;
			break;
		case PLDM_DESC_ID_PCI_DEVICE_ID:
			ptr = &id.device;
			break;
		case PLDM_DESC_ID_PCI_SUBVENDOR_ID:
			ptr = &id.subsystem_vendor;
			break;
		case PLDM_DESC_ID_PCI_SUBDEV_ID:
			ptr = &id.subsystem_device;
			break;
		default:
			/* Skip unrelated TLVs */
			continue;
		}

		value = get_unaligned_le16(desc->data);
		/* A value of zero for one of the descriptors is sometimes
		 * used when the record should ignore this field when matching
		 * device. For example if the record applies to any subsystem
		 * device or vendor.
		 */
		if (value)
			*ptr = (int)value;
		else
			*ptr = PCI_ANY_ID;
	}

	if ((id.vendor == PCI_ANY_ID || id.vendor == pdev->vendor) &&
	    (id.device == PCI_ANY_ID || id.device == pdev->device) &&
	    (id.subsystem_vendor == PCI_ANY_ID || id.subsystem_vendor == pdev->subsystem_vendor) &&
	    (id.subsystem_device == PCI_ANY_ID || id.subsystem_device == pdev->subsystem_device))
		return true;
	else
		return false;
}
EXPORT_SYMBOL(pldmfw_op_pci_match_record);

/**
 * pldm_find_matching_record - Find the first matching PLDM record
 * @data: pointer to private data
 *
 * Search through PLDM records and find the first matching entry. It is
 * expected that only one entry matches.
 *
 * Store a pointer to the matching record, if found.
 *
 * Returns: zero on success, or -ENOENT if no matching record is found.
 */
static int pldm_find_matching_record(struct pldmfw_priv *data)
{
	struct pldmfw_record *record;

	list_for_each_entry(record, &data->records, entry) {
		if (data->context->ops->match_record(data->context, record)) {
			data->matching_record = record;
			return 0;
		}
	}

	return -ENOENT;
}

/**
 * pldm_send_package_data - Send firmware the package data for the record
 * @data: pointer to private data
 *
 * Send the package data associated with the matching record to the firmware,
 * using the send_pkg_data operation.
 *
 * Returns: zero on success, or a negative error code on failure.
 */
static int
pldm_send_package_data(struct pldmfw_priv *data)
{
	struct pldmfw_record *record = data->matching_record;
	const struct pldmfw_ops *ops = data->context->ops;

	return ops->send_package_data(data->context, record->package_data,
				      record->package_data_len);
}

/**
 * pldm_send_component_tables - Send component table information to firmware
 * @data: pointer to private data
 *
 * Loop over each component, sending the applicable components to the firmware
 * via the send_component_table operation.
 *
 * Returns: zero on success, or a negative error code on failure.
 */
static int
pldm_send_component_tables(struct pldmfw_priv *data)
{
	unsigned long *bitmap = data->matching_record->component_bitmap;
	struct pldmfw_component *component;
	int err;

	list_for_each_entry(component, &data->components, entry) {
		u8 index = component->index, transfer_flag = 0;

		/* Skip components which are not intended for this device */
		if (!test_bit(index, bitmap))
			continue;

		/* determine whether this is the start, middle, end, or both
		 * the start and end of the component tables
		 */
		if (index == find_first_bit(bitmap, data->component_bitmap_len))
			transfer_flag |= PLDM_TRANSFER_FLAG_START;
		if (index == find_last_bit(bitmap, data->component_bitmap_len))
			transfer_flag |= PLDM_TRANSFER_FLAG_END;
		if (!transfer_flag)
			transfer_flag = PLDM_TRANSFER_FLAG_MIDDLE;

		err = data->context->ops->send_component_table(data->context,
							       component,
							       transfer_flag);
		if (err)
			return err;
	}

	return 0;
}

/**
 * pldm_flash_components - Program each component to device flash
 * @data: pointer to private data
 *
 * Loop through each component that is active for the matching device record,
 * and send it to the device driver for flashing.
 *
 * Returns: zero on success, or a negative error code on failure.
 */
static int pldm_flash_components(struct pldmfw_priv *data)
{
	unsigned long *bitmap = data->matching_record->component_bitmap;
	struct pldmfw_component *component;
	int err;

	list_for_each_entry(component, &data->components, entry) {
		u8 index = component->index;

		/* Skip components which are not intended for this device */
		if (!test_bit(index, bitmap))
			continue;

		err = data->context->ops->flash_component(data->context, component);
		if (err)
			return err;
	}

	return 0;
}

/**
 * pldm_finalize_update - Finalize the device flash update
 * @data: pointer to private data
 *
 * Tell the device driver to perform any remaining logic to complete the
 * device update.
 *
 * Returns: zero on success, or a PLFM_FWU error indicating the reason for
 * failure.
 */
static int pldm_finalize_update(struct pldmfw_priv *data)
{
	if (data->context->ops->finalize_update)
		return data->context->ops->finalize_update(data->context);

	return 0;
}

/**
 * pldmfw_flash_image - Write a PLDM-formatted firmware image to the device
 * @context: ops and data for firmware update
 * @fw: firmware object pointing to the relevant firmware file to program
 *
 * Parse the data for a given firmware file, verifying that it is a valid PLDM
 * formatted image that matches this device.
 *
 * Extract the device record Package Data and Component Tables and send them
 * to the device firmware. Extract and write the flash data for each of the
 * components indicated in the firmware file.
 *
 * Returns: zero on success, or a negative error code on failure.
 */
int pldmfw_flash_image(struct pldmfw *context, const struct firmware *fw)
{
	struct pldmfw_priv *data;
	int err;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	INIT_LIST_HEAD(&data->records);
	INIT_LIST_HEAD(&data->components);

	data->fw = fw;
	data->context = context;

	err = pldm_parse_image(data);
	if (err)
		goto out_release_data;

	err = pldm_find_matching_record(data);
	if (err)
		goto out_release_data;

	err = pldm_send_package_data(data);
	if (err)
		goto out_release_data;

	err = pldm_send_component_tables(data);
	if (err)
		goto out_release_data;

	err = pldm_flash_components(data);
	if (err)
		goto out_release_data;

	err = pldm_finalize_update(data);

out_release_data:
	pldmfw_free_priv(data);
	kfree(data);

	return err;
}
EXPORT_SYMBOL(pldmfw_flash_image);

MODULE_AUTHOR("Jacob Keller <jacob.e.keller@intel.com>");
MODULE_DESCRIPTION("PLDM firmware flash update library");
