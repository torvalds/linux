/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2019, Intel Corporation. */

#ifndef _PLDMFW_PRIVATE_H_
#define _PLDMFW_PRIVATE_H_

/* The following data structures define the layout of a firmware binary
 * following the "PLDM For Firmware Update Specification", DMTF standard
 * #DSP0267.
 *
 * pldmfw.c uses these structures to implement a simple engine that will parse
 * a fw binary file in this format and perform a firmware update for a given
 * device.
 *
 * Due to the variable sized data layout, alignment of fields within these
 * structures is not guaranteed when reading. For this reason, all multi-byte
 * field accesses should be done using the unaligned access macros.
 * Additionally, the standard specifies that multi-byte fields are in
 * LittleEndian format.
 *
 * The structure definitions are not made public, in order to keep direct
 * accesses within code that is prepared to deal with the limitation of
 * unaligned access.
 */

/* UUID for PLDM firmware packages: f018878c-cb7d-4943-9800-a02f059aca02 */
static const uuid_t pldm_firmware_header_id =
	UUID_INIT(0xf018878c, 0xcb7d, 0x4943,
		  0x98, 0x00, 0xa0, 0x2f, 0x05, 0x9a, 0xca, 0x02);

/* Revision number of the PLDM header format this code supports */
#define PACKAGE_HEADER_FORMAT_REVISION 0x01

/* timestamp104 structure defined in PLDM Base specification */
#define PLDM_TIMESTAMP_SIZE 13
struct __pldm_timestamp {
	u8 b[PLDM_TIMESTAMP_SIZE];
} __packed __aligned(1);

/* Package Header Information */
struct __pldm_header {
	uuid_t id;			    /* PackageHeaderIdentifier */
	u8 revision;			    /* PackageHeaderFormatRevision */
	__le16 size;			    /* PackageHeaderSize */
	struct __pldm_timestamp release_date; /* PackageReleaseDateTime */
	__le16 component_bitmap_len;	    /* ComponentBitmapBitLength */
	u8 version_type;		    /* PackageVersionStringType */
	u8 version_len;			    /* PackageVersionStringLength */

	/*
	 * DSP0267 also includes the following variable length fields at the
	 * end of this structure:
	 *
	 * PackageVersionString, length is version_len.
	 *
	 * The total size of this section is
	 *   sizeof(pldm_header) + version_len;
	 */
	u8 version_string[];		/* PackageVersionString */
} __packed __aligned(1);

/* Firmware Device ID Record */
struct __pldmfw_record_info {
	__le16 record_len;		/* RecordLength */
	u8 descriptor_count;		/* DescriptorCount */
	__le32 device_update_flags;	/* DeviceUpdateOptionFlags */
	u8 version_type;		/* ComponentImageSetVersionType */
	u8 version_len;			/* ComponentImageSetVersionLength */
	__le16 package_data_len;	/* FirmwareDevicePackageDataLength */

	/*
	 * DSP0267 also includes the following variable length fields at the
	 * end of this structure:
	 *
	 * ApplicableComponents, length is component_bitmap_len from header
	 * ComponentImageSetVersionString, length is version_len
	 * RecordDescriptors, a series of TLVs with 16bit type and length
	 * FirmwareDevicePackageData, length is package_data_len
	 *
	 * The total size of each record is
	 *   sizeof(pldmfw_record_info) +
	 *   component_bitmap_len (converted to bytes!) +
	 *   version_len +
	 *   <length of RecordDescriptors> +
	 *   package_data_len
	 */
	u8 variable_record_data[];
} __packed __aligned(1);

/* Firmware Descriptor Definition */
struct __pldmfw_desc_tlv {
	__le16 type;			/* DescriptorType */
	__le16 size;			/* DescriptorSize */
	u8 data[];			/* DescriptorData */
} __aligned(1);

/* Firmware Device Identification Area */
struct __pldmfw_record_area {
	u8 record_count;		/* DeviceIDRecordCount */
	/* This is not a struct type because the size of each record varies */
	u8 records[];
} __aligned(1);

/* Individual Component Image Information */
struct __pldmfw_component_info {
	__le16 classification;		/* ComponentClassfication */
	__le16 identifier;		/* ComponentIdentifier */
	__le32 comparison_stamp;	/* ComponentComparisonStamp */
	__le16 options;			/* componentOptions */
	__le16 activation_method;	/* RequestedComponentActivationMethod */
	__le32 location_offset;		/* ComponentLocationOffset */
	__le32 size;			/* ComponentSize */
	u8 version_type;		/* ComponentVersionStringType */
	u8 version_len;		/* ComponentVersionStringLength */

	/*
	 * DSP0267 also includes the following variable length fields at the
	 * end of this structure:
	 *
	 * ComponentVersionString, length is version_len
	 *
	 * The total size of this section is
	 *   sizeof(pldmfw_component_info) + version_len;
	 */
	u8 version_string[];		/* ComponentVersionString */
} __packed __aligned(1);

/* Component Image Information Area */
struct __pldmfw_component_area {
	__le16 component_image_count;
	/* This is not a struct type because the component size varies */
	u8 components[];
} __aligned(1);

/**
 * pldm_first_desc_tlv
 * @start: byte offset of the start of the descriptor TLVs
 *
 * Converts the starting offset of the descriptor TLVs into a pointer to the
 * first descriptor.
 */
#define pldm_first_desc_tlv(start)					\
	((const struct __pldmfw_desc_tlv *)(start))

/**
 * pldm_next_desc_tlv
 * @desc: pointer to a descriptor TLV
 *
 * Finds the pointer to the next descriptor following a given descriptor
 */
#define pldm_next_desc_tlv(desc)						\
	((const struct __pldmfw_desc_tlv *)((desc)->data +			\
					     get_unaligned_le16(&(desc)->size)))

/**
 * pldm_for_each_desc_tlv
 * @i: variable to store descriptor index
 * @desc: variable to store descriptor pointer
 * @start: byte offset of the start of the descriptors
 * @count: the number of descriptors
 *
 * for loop macro to iterate over all of the descriptors of a given PLDM
 * record.
 */
#define pldm_for_each_desc_tlv(i, desc, start, count)			\
	for ((i) = 0, (desc) = pldm_first_desc_tlv(start);		\
	     (i) < (count);						\
	     (i)++, (desc) = pldm_next_desc_tlv(desc))

/**
 * pldm_first_record
 * @start: byte offset of the start of the PLDM records
 *
 * Converts a starting offset of the PLDM records into a pointer to the first
 * record.
 */
#define pldm_first_record(start)					\
	((const struct __pldmfw_record_info *)(start))

/**
 * pldm_next_record
 * @record: pointer to a PLDM record
 *
 * Finds a pointer to the next record following a given record
 */
#define pldm_next_record(record)					\
	((const struct __pldmfw_record_info *)				\
	 ((const u8 *)(record) + get_unaligned_le16(&(record)->record_len)))

/**
 * pldm_for_each_record
 * @i: variable to store record index
 * @record: variable to store record pointer
 * @start: byte offset of the start of the records
 * @count: the number of records
 *
 * for loop macro to iterate over all of the records of a PLDM file.
 */
#define pldm_for_each_record(i, record, start, count)			\
	for ((i) = 0, (record) = pldm_first_record(start);		\
	     (i) < (count);						\
	     (i)++, (record) = pldm_next_record(record))

/**
 * pldm_first_component
 * @start: byte offset of the start of the PLDM components
 *
 * Convert a starting offset of the PLDM components into a pointer to the
 * first component
 */
#define pldm_first_component(start)					\
	((const struct __pldmfw_component_info *)(start))

/**
 * pldm_next_component
 * @component: pointer to a PLDM component
 *
 * Finds a pointer to the next component following a given component
 */
#define pldm_next_component(component)						\
	((const struct __pldmfw_component_info *)((component)->version_string +	\
						  (component)->version_len))

/**
 * pldm_for_each_component
 * @i: variable to store component index
 * @component: variable to store component pointer
 * @start: byte offset to the start of the first component
 * @count: the number of components
 *
 * for loop macro to iterate over all of the components of a PLDM file.
 */
#define pldm_for_each_component(i, component, start, count)		\
	for ((i) = 0, (component) = pldm_first_component(start);	\
	     (i) < (count);						\
	     (i)++, (component) = pldm_next_component(component))

#endif
