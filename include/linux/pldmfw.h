/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2019, Intel Corporation. */

#ifndef _PLDMFW_H_
#define _PLDMFW_H_

#include <linux/list.h>
#include <linux/firmware.h>

#define PLDM_DEVICE_UPDATE_CONTINUE_AFTER_FAIL BIT(0)

#define PLDM_STRING_TYPE_UNKNOWN	0
#define PLDM_STRING_TYPE_ASCII		1
#define PLDM_STRING_TYPE_UTF8		2
#define PLDM_STRING_TYPE_UTF16		3
#define PLDM_STRING_TYPE_UTF16LE	4
#define PLDM_STRING_TYPE_UTF16BE	5

struct pldmfw_record {
	struct list_head entry;

	/* List of descriptor TLVs */
	struct list_head descs;

	/* Component Set version string*/
	const u8 *version_string;
	u8 version_type;
	u8 version_len;

	/* Package Data length */
	u16 package_data_len;

	/* Bitfield of Device Update Flags */
	u32 device_update_flags;

	/* Package Data block */
	const u8 *package_data;

	/* Bitmap of components applicable to this record */
	unsigned long *component_bitmap;
	u16 component_bitmap_len;
};

/* Standard descriptor TLV identifiers */
#define PLDM_DESC_ID_PCI_VENDOR_ID	0x0000
#define PLDM_DESC_ID_IANA_ENTERPRISE_ID	0x0001
#define PLDM_DESC_ID_UUID		0x0002
#define PLDM_DESC_ID_PNP_VENDOR_ID	0x0003
#define PLDM_DESC_ID_ACPI_VENDOR_ID	0x0004
#define PLDM_DESC_ID_PCI_DEVICE_ID	0x0100
#define PLDM_DESC_ID_PCI_SUBVENDOR_ID	0x0101
#define PLDM_DESC_ID_PCI_SUBDEV_ID	0x0102
#define PLDM_DESC_ID_PCI_REVISION_ID	0x0103
#define PLDM_DESC_ID_PNP_PRODUCT_ID	0x0104
#define PLDM_DESC_ID_ACPI_PRODUCT_ID	0x0105
#define PLDM_DESC_ID_VENDOR_DEFINED	0xFFFF

struct pldmfw_desc_tlv {
	struct list_head entry;

	const u8 *data;
	u16 type;
	u16 size;
};

#define PLDM_CLASSIFICATION_UNKNOWN		0x0000
#define PLDM_CLASSIFICATION_OTHER		0x0001
#define PLDM_CLASSIFICATION_DRIVER		0x0002
#define PLDM_CLASSIFICATION_CONFIG_SW		0x0003
#define PLDM_CLASSIFICATION_APP_SW		0x0004
#define PLDM_CLASSIFICATION_INSTRUMENTATION	0x0005
#define PLDM_CLASSIFICATION_BIOS		0x0006
#define PLDM_CLASSIFICATION_DIAGNOSTIC_SW	0x0007
#define PLDM_CLASSIFICATION_OS			0x0008
#define PLDM_CLASSIFICATION_MIDDLEWARE		0x0009
#define PLDM_CLASSIFICATION_FIRMWARE		0x000A
#define PLDM_CLASSIFICATION_CODE		0x000B
#define PLDM_CLASSIFICATION_SERVICE_PACK	0x000C
#define PLDM_CLASSIFICATION_SOFTWARE_BUNDLE	0x000D

#define PLDM_ACTIVATION_METHOD_AUTO		BIT(0)
#define PLDM_ACTIVATION_METHOD_SELF_CONTAINED	BIT(1)
#define PLDM_ACTIVATION_METHOD_MEDIUM_SPECIFIC	BIT(2)
#define PLDM_ACTIVATION_METHOD_REBOOT		BIT(3)
#define PLDM_ACTIVATION_METHOD_DC_CYCLE		BIT(4)
#define PLDM_ACTIVATION_METHOD_AC_CYCLE		BIT(5)

#define PLDMFW_COMPONENT_OPTION_FORCE_UPDATE		BIT(0)
#define PLDMFW_COMPONENT_OPTION_USE_COMPARISON_STAMP	BIT(1)

struct pldmfw_component {
	struct list_head entry;

	/* component identifier */
	u16 classification;
	u16 identifier;

	u16 options;
	u16 activation_method;

	u32 comparison_stamp;

	u32 component_size;
	const u8 *component_data;

	/* Component version string */
	const u8 *version_string;
	u8 version_type;
	u8 version_len;

	/* component index */
	u8 index;

};

/* Transfer flag used for sending components to the firmware */
#define PLDM_TRANSFER_FLAG_START		BIT(0)
#define PLDM_TRANSFER_FLAG_MIDDLE		BIT(1)
#define PLDM_TRANSFER_FLAG_END			BIT(2)

struct pldmfw_ops;

/* Main entry point to the PLDM firmware update engine. Device drivers
 * should embed this in a private structure and use container_of to obtain
 * a pointer to their own data, used to implement the device specific
 * operations.
 */
struct pldmfw {
	const struct pldmfw_ops *ops;
	struct device *dev;
};

bool pldmfw_op_pci_match_record(struct pldmfw *context, struct pldmfw_record *record);

/* Operations invoked by the generic PLDM firmware update engine. Used to
 * implement device specific logic.
 *
 * @match_record: check if the device matches the given record. For
 * convenience, a standard implementation is provided for PCI devices.
 *
 * @send_package_data: send the package data associated with the matching
 * record to firmware.
 *
 * @send_component_table: send the component data associated with a given
 * component to firmware. Called once for each applicable component.
 *
 * @flash_component: Flash the data for a given component to the device.
 * Called once for each applicable component, after all component tables have
 * been sent.
 *
 * @finalize_update: (optional) Finish the update. Called after all components
 * have been flashed.
 */
struct pldmfw_ops {
	bool (*match_record)(struct pldmfw *context, struct pldmfw_record *record);
	int (*send_package_data)(struct pldmfw *context, const u8 *data, u16 length);
	int (*send_component_table)(struct pldmfw *context, struct pldmfw_component *component,
				    u8 transfer_flag);
	int (*flash_component)(struct pldmfw *context, struct pldmfw_component *component);
	int (*finalize_update)(struct pldmfw *context);
};

int pldmfw_flash_image(struct pldmfw *context, const struct firmware *fw);

#endif
