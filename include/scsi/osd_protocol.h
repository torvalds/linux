/*
 * osd_protocol.h - OSD T10 standard C definitions.
 *
 * Copyright (C) 2008 Panasas Inc.  All rights reserved.
 *
 * Authors:
 *   Boaz Harrosh <bharrosh@panasas.com>
 *   Benny Halevy <bhalevy@panasas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 *
 * This file contains types and constants that are defined by the protocol
 * Note: All names and symbols are taken from the OSD standard's text.
 */
#ifndef __OSD_PROTOCOL_H__
#define __OSD_PROTOCOL_H__

#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/unaligned.h>
#include <scsi/scsi.h>

enum {
	OSDv1_ADDITIONAL_CDB_LENGTH = 192,
	OSDv1_TOTAL_CDB_LEN = OSDv1_ADDITIONAL_CDB_LENGTH + 8,
	OSDv1_CAP_LEN = 80,

	/* Latest supported version */
	OSDv2_ADDITIONAL_CDB_LENGTH = 228,
	OSD_ADDITIONAL_CDB_LENGTH =
		OSDv2_ADDITIONAL_CDB_LENGTH,
	OSD_TOTAL_CDB_LEN = OSD_ADDITIONAL_CDB_LENGTH + 8,
	OSD_CAP_LEN = 104,

	OSD_SYSTEMID_LEN = 20,
	OSDv1_CRYPTO_KEYID_SIZE = 20,
	OSDv2_CRYPTO_KEYID_SIZE = 32,
	OSD_CRYPTO_KEYID_SIZE = OSDv2_CRYPTO_KEYID_SIZE,
	OSD_CRYPTO_SEED_SIZE = 4,
	OSD_CRYPTO_NONCE_SIZE = 12,
	OSD_MAX_SENSE_LEN = 252, /* from SPC-3 */

	OSD_PARTITION_FIRST_ID = 0x10000,
	OSD_OBJECT_FIRST_ID = 0x10000,
};

/* (osd-r10 5.2.4)
 * osd2r03: 5.2.3 Caching control bits
 */
enum osd_options_byte {
	OSD_CDB_FUA = 0x08,	/* Force Unit Access */
	OSD_CDB_DPO = 0x10,	/* Disable Page Out */
};

/*
 * osd2r03: 5.2.5 Isolation.
 * First 3 bits, V2-only.
 * Also for attr 110h "default isolation method" at Root Information page
 */
enum osd_options_byte_isolation {
	OSD_ISOLATION_DEFAULT = 0,
	OSD_ISOLATION_NONE = 1,
	OSD_ISOLATION_STRICT = 2,
	OSD_ISOLATION_RANGE = 4,
	OSD_ISOLATION_FUNCTIONAL = 5,
	OSD_ISOLATION_VENDOR = 7,
};

/* (osd-r10: 6.7)
 * osd2r03: 6.8 FLUSH, FLUSH COLLECTION, FLUSH OSD, FLUSH PARTITION
 */
enum osd_options_flush_scope_values {
	OSD_CDB_FLUSH_ALL = 0,
	OSD_CDB_FLUSH_ATTR_ONLY = 1,

	OSD_CDB_FLUSH_ALL_RECURSIVE = 2,
	/* V2-only */
	OSD_CDB_FLUSH_ALL_RANGE = 2,
};

/* osd2r03: 5.2.10 Timestamps control */
enum {
	OSD_CDB_NORMAL_TIMESTAMPS = 0,
	OSD_CDB_BYPASS_TIMESTAMPS = 0x7f,
};

/* (osd-r10: 5.2.2.1)
 * osd2r03: 5.2.4.1 Get and set attributes CDB format selection
 *	2 bits at second nibble of command_specific_options byte
 */
enum osd_attributes_mode {
	/* V2-only */
	OSD_CDB_SET_ONE_ATTR = 0x10,

	OSD_CDB_GET_ATTR_PAGE_SET_ONE = 0x20,
	OSD_CDB_GET_SET_ATTR_LISTS = 0x30,

	OSD_CDB_GET_SET_ATTR_MASK = 0x30,
};

/* (osd-r10: 4.12.5)
 * osd2r03: 4.14.5 Data-In and Data-Out buffer offsets
 *	byte offset = mantissa * (2^(exponent+8))
 *	struct {
 *		unsigned mantissa: 28;
 *		int exponent: 04;
 *	}
 */
typedef __be32 osd_cdb_offset;

enum {
	OSD_OFFSET_UNUSED = 0xFFFFFFFF,
	OSD_OFFSET_MAX_BITS = 28,

	OSDv1_OFFSET_MIN_SHIFT = 8,
	OSD_OFFSET_MIN_SHIFT = 3,
	OSD_OFFSET_MAX_SHIFT = 16,
};

/* Return the smallest allowed encoded offset that contains @offset.
 *
 * The actual encoded offset returned is @offset + *padding.
 * (up to max_shift, non-inclusive)
 */
osd_cdb_offset __osd_encode_offset(u64 offset, unsigned *padding,
	int min_shift, int max_shift);

/* Minimum alignment is 256 bytes
 * Note: Seems from std v1 that exponent can be from 0+8 to 0xE+8 (inclusive)
 * which is 8 to 23 but IBM code restricts it to 16, so be it.
 */
static inline osd_cdb_offset osd_encode_offset_v1(u64 offset, unsigned *padding)
{
	return __osd_encode_offset(offset, padding,
				OSDv1_OFFSET_MIN_SHIFT, OSD_OFFSET_MAX_SHIFT);
}

/* Minimum 8 bytes alignment
 * Same as v1 but since exponent can be signed than a less than
 * 256 alignment can be reached with small offsets (<2GB)
 */
static inline osd_cdb_offset osd_encode_offset_v2(u64 offset, unsigned *padding)
{
	return __osd_encode_offset(offset, padding,
				   OSD_OFFSET_MIN_SHIFT, OSD_OFFSET_MAX_SHIFT);
}

/* osd2r03: 5.2.1 Overview */
struct osd_cdb_head {
	struct scsi_varlen_cdb_hdr varlen_cdb;
/*10*/	u8		options;
	u8		command_specific_options;
	u8		timestamp_control;
/*13*/	u8		reserved1[3];
/*16*/	__be64		partition;
/*24*/	__be64		object;
/*32*/	union { /* V1 vs V2 alignment differences */
		struct __osdv1_cdb_addr_len {
/*32*/			__be32 		list_identifier;/* Rarely used */
/*36*/			__be64		length;
/*44*/			__be64		start_address;
		} __packed v1;

		struct __osdv2_cdb_addr_len {
			/* called allocation_length in some commands */
/*32*/			__be64	length;
/*40*/			__be64	start_address;
			union {
/*48*/				__be32 list_identifier;/* Rarely used */
				/* OSD2r05 5.2.5 CDB continuation length */
/*48*/				__be32 cdb_continuation_length;
			};
		} __packed v2;
	};
/*52*/	union { /* selected attributes mode Page/List/Single */
		struct osd_attributes_page_mode {
/*52*/			__be32		get_attr_page;
/*56*/			__be32		get_attr_alloc_length;
/*60*/			osd_cdb_offset	get_attr_offset;

/*64*/			__be32		set_attr_page;
/*68*/			__be32		set_attr_id;
/*72*/			__be32		set_attr_length;
/*76*/			osd_cdb_offset	set_attr_offset;
/*80*/		} __packed attrs_page;

		struct osd_attributes_list_mode {
/*52*/			__be32		get_attr_desc_bytes;
/*56*/			osd_cdb_offset	get_attr_desc_offset;

/*60*/			__be32		get_attr_alloc_length;
/*64*/			osd_cdb_offset	get_attr_offset;

/*68*/			__be32		set_attr_bytes;
/*72*/			osd_cdb_offset	set_attr_offset;
			__be32 not_used;
/*80*/		} __packed attrs_list;

		/* osd2r03:5.2.4.2 Set one attribute value using CDB fields */
		struct osd_attributes_cdb_mode {
/*52*/			__be32		set_attr_page;
/*56*/			__be32		set_attr_id;
/*60*/			__be16		set_attr_len;
/*62*/			u8		set_attr_val[18];
/*80*/		} __packed attrs_cdb;
/*52*/		u8 get_set_attributes_parameters[28];
	};
} __packed;
/*80*/

/*160 v1*/
struct osdv1_security_parameters {
/*160*/u8	integrity_check_value[OSDv1_CRYPTO_KEYID_SIZE];
/*180*/u8	request_nonce[OSD_CRYPTO_NONCE_SIZE];
/*192*/osd_cdb_offset	data_in_integrity_check_offset;
/*196*/osd_cdb_offset	data_out_integrity_check_offset;
} __packed;
/*200 v1*/

/*184 v2*/
struct osdv2_security_parameters {
/*184*/u8	integrity_check_value[OSDv2_CRYPTO_KEYID_SIZE];
/*216*/u8	request_nonce[OSD_CRYPTO_NONCE_SIZE];
/*228*/osd_cdb_offset	data_in_integrity_check_offset;
/*232*/osd_cdb_offset	data_out_integrity_check_offset;
} __packed;
/*236 v2*/

struct osd_security_parameters {
	union {
		struct osdv1_security_parameters v1;
		struct osdv2_security_parameters v2;
	};
};

struct osdv1_cdb {
	struct osd_cdb_head h;
	u8 caps[OSDv1_CAP_LEN];
	struct osdv1_security_parameters sec_params;
} __packed;

struct osdv2_cdb {
	struct osd_cdb_head h;
	u8 caps[OSD_CAP_LEN];
	struct osdv2_security_parameters sec_params;
} __packed;

struct osd_cdb {
	union {
		struct osdv1_cdb v1;
		struct osdv2_cdb v2;
		u8 buff[OSD_TOTAL_CDB_LEN];
	};
} __packed;

static inline struct osd_cdb_head *osd_cdb_head(struct osd_cdb *ocdb)
{
	return (struct osd_cdb_head *)ocdb->buff;
}

/* define both version actions
 * Ex name = FORMAT_OSD we have OSD_ACT_FORMAT_OSD && OSDv1_ACT_FORMAT_OSD
 */
#define OSD_ACT___(Name, Num) \
	OSD_ACT_##Name = __constant_cpu_to_be16(0x8880 + Num), \
	OSDv1_ACT_##Name = __constant_cpu_to_be16(0x8800 + Num),

/* V2 only actions */
#define OSD_ACT_V2(Name, Num) \
	OSD_ACT_##Name = __constant_cpu_to_be16(0x8880 + Num),

#define OSD_ACT_V1_V2(Name, Num1, Num2) \
	OSD_ACT_##Name = __constant_cpu_to_be16(Num2), \
	OSDv1_ACT_##Name = __constant_cpu_to_be16(Num1),

enum osd_service_actions {
	OSD_ACT_V2(OBJECT_STRUCTURE_CHECK,	0x00)
	OSD_ACT___(FORMAT_OSD,			0x01)
	OSD_ACT___(CREATE,			0x02)
	OSD_ACT___(LIST,			0x03)
	OSD_ACT_V2(PUNCH,			0x04)
	OSD_ACT___(READ,			0x05)
	OSD_ACT___(WRITE,			0x06)
	OSD_ACT___(APPEND,			0x07)
	OSD_ACT___(FLUSH,			0x08)
	OSD_ACT_V2(CLEAR,			0x09)
	OSD_ACT___(REMOVE,			0x0A)
	OSD_ACT___(CREATE_PARTITION,		0x0B)
	OSD_ACT___(REMOVE_PARTITION,		0x0C)
	OSD_ACT___(GET_ATTRIBUTES,		0x0E)
	OSD_ACT___(SET_ATTRIBUTES,		0x0F)
	OSD_ACT___(CREATE_AND_WRITE,		0x12)
	OSD_ACT___(CREATE_COLLECTION,		0x15)
	OSD_ACT___(REMOVE_COLLECTION,		0x16)
	OSD_ACT___(LIST_COLLECTION,		0x17)
	OSD_ACT___(SET_KEY,			0x18)
	OSD_ACT___(SET_MASTER_KEY,		0x19)
	OSD_ACT___(FLUSH_COLLECTION,		0x1A)
	OSD_ACT___(FLUSH_PARTITION,		0x1B)
	OSD_ACT___(FLUSH_OSD,			0x1C)

	OSD_ACT_V2(QUERY,			0x20)
	OSD_ACT_V2(REMOVE_MEMBER_OBJECTS,	0x21)
	OSD_ACT_V2(GET_MEMBER_ATTRIBUTES,	0x22)
	OSD_ACT_V2(SET_MEMBER_ATTRIBUTES,	0x23)

	OSD_ACT_V2(CREATE_CLONE,		0x28)
	OSD_ACT_V2(CREATE_SNAPSHOT,		0x29)
	OSD_ACT_V2(DETACH_CLONE,		0x2A)
	OSD_ACT_V2(REFRESH_SNAPSHOT_CLONE,	0x2B)
	OSD_ACT_V2(RESTORE_PARTITION_FROM_SNAPSHOT, 0x2C)

	OSD_ACT_V2(READ_MAP,			0x31)
	OSD_ACT_V2(READ_MAPS_COMPARE,		0x32)

	OSD_ACT_V1_V2(PERFORM_SCSI_COMMAND,	0x8F7E, 0x8F7C)
	OSD_ACT_V1_V2(SCSI_TASK_MANAGEMENT,	0x8F7F, 0x8F7D)
	/* 0x8F80 to 0x8FFF are Vendor specific */
};

/* osd2r03: 7.1.3.2 List entry format for retrieving attributes */
struct osd_attributes_list_attrid {
	__be32 attr_page;
	__be32 attr_id;
} __packed;

/*
 * NOTE: v1: is not aligned.
 */
struct osdv1_attributes_list_element {
	__be32 attr_page;
	__be32 attr_id;
	__be16 attr_bytes; /* valid bytes at attr_val without padding */
	u8 attr_val[0];
} __packed;

/*
 * osd2r03: 7.1.3.3 List entry format for retrieved attributes and
 *                  for setting attributes
 * NOTE: v2 is 8-bytes aligned
 */
struct osdv2_attributes_list_element {
	__be32 attr_page;
	__be32 attr_id;
	u8 reserved[6];
	__be16 attr_bytes; /* valid bytes at attr_val without padding */
	u8 attr_val[0];
} __packed;

enum {
	OSDv1_ATTRIBUTES_ELEM_ALIGN = 1,
	OSD_ATTRIBUTES_ELEM_ALIGN = 8,
};

enum {
	OSD_ATTR_LIST_ALL_PAGES = 0xFFFFFFFF,
	OSD_ATTR_LIST_ALL_IN_PAGE = 0xFFFFFFFF,
};

static inline unsigned osdv1_attr_list_elem_size(unsigned len)
{
	return ALIGN(len + sizeof(struct osdv1_attributes_list_element),
		     OSDv1_ATTRIBUTES_ELEM_ALIGN);
}

static inline unsigned osdv2_attr_list_elem_size(unsigned len)
{
	return ALIGN(len + sizeof(struct osdv2_attributes_list_element),
		     OSD_ATTRIBUTES_ELEM_ALIGN);
}

/*
 * osd2r03: 7.1.3 OSD attributes lists (Table 184) — List type values
 */
enum osd_attr_list_types {
	OSD_ATTR_LIST_GET = 0x1, 	/* descriptors only */
	OSD_ATTR_LIST_SET_RETRIEVE = 0x9, /*descriptors/values variable-length*/
	OSD_V2_ATTR_LIST_MULTIPLE = 0xE,  /* ver2, Multiple Objects lists*/
	OSD_V1_ATTR_LIST_CREATE_MULTIPLE = 0xF,/*ver1, used by create_multple*/
};

/* osd2r03: 7.1.3.4 Multi-object retrieved attributes format */
struct osd_attributes_list_multi_header {
	__be64 object_id;
	u8 object_type; /* object_type enum below */
	u8 reserved[5];
	__be16 list_bytes;
	/* followed by struct osd_attributes_list_element's */
};

struct osdv1_attributes_list_header {
	u8 type;	/* low 4-bit only */
	u8 pad;
	__be16 list_bytes; /* Initiator shall set to Zero. Only set by target */
	/*
	 * type=9 followed by struct osd_attributes_list_element's
	 * type=E followed by struct osd_attributes_list_multi_header's
	 */
} __packed;

static inline unsigned osdv1_list_size(struct osdv1_attributes_list_header *h)
{
	return be16_to_cpu(h->list_bytes);
}

struct osdv2_attributes_list_header {
	u8 type;	/* lower 4-bits only */
	u8 pad[3];
/*4*/	__be32 list_bytes; /* Initiator shall set to zero. Only set by target */
	/*
	 * type=9 followed by struct osd_attributes_list_element's
	 * type=E followed by struct osd_attributes_list_multi_header's
	 */
} __packed;

static inline unsigned osdv2_list_size(struct osdv2_attributes_list_header *h)
{
	return be32_to_cpu(h->list_bytes);
}

/* (osd-r10 6.13)
 * osd2r03: 6.15 LIST (Table 79) LIST command parameter data.
 *	for root_lstchg below
 */
enum {
	OSD_OBJ_ID_LIST_PAR = 0x1, /* V1-only. Not used in V2 */
	OSD_OBJ_ID_LIST_LSTCHG = 0x2,
};

/*
 * osd2r03: 6.15.2 LIST command parameter data
 * (Also for LIST COLLECTION)
 */
struct osd_obj_id_list {
	__be64 list_bytes; /* bytes in list excluding list_bytes (-8) */
	__be64 continuation_id;
	__be32 list_identifier;
	u8 pad[3];
	u8 root_lstchg;
	__be64 object_ids[0];
} __packed;

static inline bool osd_is_obj_list_done(struct osd_obj_id_list *list,
	bool *is_changed)
{
	*is_changed = (0 != (list->root_lstchg & OSD_OBJ_ID_LIST_LSTCHG));
	return 0 != list->continuation_id;
}

/*
 * osd2r03: 4.12.4.5 The ALLDATA security method
 */
struct osd_data_out_integrity_info {
	__be64 data_bytes;
	__be64 set_attributes_bytes;
	__be64 get_attributes_bytes;
	__u8 integrity_check_value[OSD_CRYPTO_KEYID_SIZE];
} __packed;

/* Same osd_data_out_integrity_info is used for OSD2/OSD1. The only difference
 * Is the sizeof the structure since in OSD1 the last array is smaller. Use
 * below for version independent handling of this structure
 */
static inline int osd_data_out_integrity_info_sizeof(bool is_ver1)
{
	return sizeof(struct osd_data_out_integrity_info) -
		(is_ver1 * (OSDv2_CRYPTO_KEYID_SIZE - OSDv1_CRYPTO_KEYID_SIZE));
}

struct osd_data_in_integrity_info {
	__be64 data_bytes;
	__be64 retrieved_attributes_bytes;
	__u8 integrity_check_value[OSD_CRYPTO_KEYID_SIZE];
} __packed;

/* Same osd_data_in_integrity_info is used for OSD2/OSD1. The only difference
 * Is the sizeof the structure since in OSD1 the last array is smaller. Use
 * below for version independent handling of this structure
 */
static inline int osd_data_in_integrity_info_sizeof(bool is_ver1)
{
	return sizeof(struct osd_data_in_integrity_info) -
		(is_ver1 * (OSDv2_CRYPTO_KEYID_SIZE - OSDv1_CRYPTO_KEYID_SIZE));
}

struct osd_timestamp {
	u8 time[6]; /* number of milliseconds since 1/1/1970 UT (big endian) */
} __packed;
/* FIXME: define helper functions to convert to/from osd time format */

/*
 * Capability & Security definitions
 * osd2r03: 4.11.2.2 Capability format
 * osd2r03: 5.2.8 Security parameters
 */

struct osd_key_identifier {
	u8 id[7]; /* if you know why 7 please email bharrosh@panasas.com */
} __packed;

/* for osd_capability.format */
enum {
	OSD_SEC_CAP_FORMAT_NO_CAPS = 0,
	OSD_SEC_CAP_FORMAT_VER1 = 1,
	OSD_SEC_CAP_FORMAT_VER2 = 2,
};

/* security_method */
enum {
	OSD_SEC_NOSEC = 0,
	OSD_SEC_CAPKEY = 1,
	OSD_SEC_CMDRSP = 2,
	OSD_SEC_ALLDATA = 3,
};

enum object_type {
	OSD_SEC_OBJ_ROOT = 0x1,
	OSD_SEC_OBJ_PARTITION = 0x2,
	OSD_SEC_OBJ_COLLECTION = 0x40,
	OSD_SEC_OBJ_USER = 0x80,
};

enum osd_capability_bit_masks {
	OSD_SEC_CAP_APPEND	= BIT(0),
	OSD_SEC_CAP_OBJ_MGMT	= BIT(1),
	OSD_SEC_CAP_REMOVE	= BIT(2),
	OSD_SEC_CAP_CREATE	= BIT(3),
	OSD_SEC_CAP_SET_ATTR	= BIT(4),
	OSD_SEC_CAP_GET_ATTR	= BIT(5),
	OSD_SEC_CAP_WRITE	= BIT(6),
	OSD_SEC_CAP_READ	= BIT(7),

	OSD_SEC_CAP_NONE1	= BIT(8),
	OSD_SEC_CAP_NONE2	= BIT(9),
	OSD_SEC_GBL_REM 	= BIT(10), /*v2 only*/
	OSD_SEC_CAP_QUERY	= BIT(11), /*v2 only*/
	OSD_SEC_CAP_M_OBJECT	= BIT(12), /*v2 only*/
	OSD_SEC_CAP_POL_SEC	= BIT(13),
	OSD_SEC_CAP_GLOBAL	= BIT(14),
	OSD_SEC_CAP_DEV_MGMT	= BIT(15),
};

/* for object_descriptor_type (hi nibble used) */
enum {
	OSD_SEC_OBJ_DESC_NONE = 0,     /* Not allowed */
	OSD_SEC_OBJ_DESC_OBJ = 1 << 4, /* v1: also collection */
	OSD_SEC_OBJ_DESC_PAR = 2 << 4, /* also root */
	OSD_SEC_OBJ_DESC_COL = 3 << 4, /* v2 only */
};

/* (osd-r10:4.9.2.2)
 * osd2r03:4.11.2.2 Capability format
 */
struct osd_capability_head {
	u8 format; /* low nibble */
	u8 integrity_algorithm__key_version; /* MAKE_BYTE(integ_alg, key_ver) */
	u8 security_method;
	u8 reserved1;
/*04*/	struct osd_timestamp expiration_time;
/*10*/	u8 audit[20];
/*30*/	u8 discriminator[12];
/*42*/	struct osd_timestamp object_created_time;
/*48*/	u8 object_type;
/*49*/	u8 permissions_bit_mask[5];
/*54*/	u8 reserved2;
/*55*/	u8 object_descriptor_type; /* high nibble */
} __packed;

/*56 v1*/
struct osdv1_cap_object_descriptor {
	union {
		struct {
/*56*/			__be32 policy_access_tag;
/*60*/			__be64 allowed_partition_id;
/*68*/			__be64 allowed_object_id;
/*76*/			__be32 reserved;
		} __packed obj_desc;

/*56*/		u8 object_descriptor[24];
	};
} __packed;
/*80 v1*/

/*56 v2*/
struct osd_cap_object_descriptor {
	union {
		struct {
/*56*/			__be32 allowed_attributes_access;
/*60*/			__be32 policy_access_tag;
/*64*/			__be16 boot_epoch;
/*66*/			u8 reserved[6];
/*72*/			__be64 allowed_partition_id;
/*80*/			__be64 allowed_object_id;
/*88*/			__be64 allowed_range_length;
/*96*/			__be64 allowed_range_start;
		} __packed obj_desc;

/*56*/		u8 object_descriptor[48];
	};
} __packed;
/*104 v2*/

struct osdv1_capability {
	struct osd_capability_head h;
	struct osdv1_cap_object_descriptor od;
} __packed;

struct osd_capability {
	struct osd_capability_head h;
	struct osd_cap_object_descriptor od;
} __packed;

/**
 * osd_sec_set_caps - set cap-bits into the capabilities header
 *
 * @cap:	The osd_capability_head to set cap bits to.
 * @bit_mask: 	Use an ORed list of enum osd_capability_bit_masks values
 *
 * permissions_bit_mask is unaligned use below to set into caps
 * in a version independent way
 */
static inline void osd_sec_set_caps(struct osd_capability_head *cap,
	u16 bit_mask)
{
	/*
	 *Note: The bits above are defined LE order this is because this way
	 *      they can grow in the future to more then 16, and still retain
	 *      there constant values.
	 */
	put_unaligned_le16(bit_mask, &cap->permissions_bit_mask);
}

/* osd2r05a sec 5.3: CDB continuation segment formats */
enum osd_continuation_segment_format {
	CDB_CONTINUATION_FORMAT_V2 = 0x01,
};

struct osd_continuation_segment_header {
	u8	format;
	u8	reserved1;
	__be16	service_action;
	__be32	reserved2;
	u8	integrity_check[OSDv2_CRYPTO_KEYID_SIZE];
} __packed;

/* osd2r05a sec 5.4.1: CDB continuation descriptors */
enum osd_continuation_descriptor_type {
	NO_MORE_DESCRIPTORS = 0x0000,
	SCATTER_GATHER_LIST = 0x0001,
	QUERY_LIST = 0x0002,
	USER_OBJECT = 0x0003,
	COPY_USER_OBJECT_SOURCE = 0x0101,
	EXTENSION_CAPABILITIES = 0xFFEE
};

struct osd_continuation_descriptor_header {
	__be16	type;
	u8	reserved;
	u8	pad_length;
	__be32	length;
} __packed;


/* osd2r05a sec 5.4.2: Scatter/gather list */
struct osd_sg_list_entry {
	__be64 offset;
	__be64 len;
};

struct osd_sg_continuation_descriptor {
	struct osd_continuation_descriptor_header hdr;
	struct osd_sg_list_entry entries[];
};

#endif /* ndef __OSD_PROTOCOL_H__ */
