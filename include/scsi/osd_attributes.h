/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __OSD_ATTRIBUTES_H__
#define __OSD_ATTRIBUTES_H__

#include <scsi/osd_protocol.h>

/*
 * Contains types and constants that define attribute pages and attribute
 * numbers and their data types.
 */

#define ATTR_SET(pg, id, l, ptr) \
	{ .attr_page = pg, .attr_id = id, .len = l, .val_ptr = ptr }

#define ATTR_DEF(pg, id, l) ATTR_SET(pg, id, l, NULL)

/* osd-r10 4.7.3 Attributes pages */
enum {
	OSD_APAGE_OBJECT_FIRST		= 0x0,
	OSD_APAGE_OBJECT_DIRECTORY	= 0,
	OSD_APAGE_OBJECT_INFORMATION	= 1,
	OSD_APAGE_OBJECT_QUOTAS		= 2,
	OSD_APAGE_OBJECT_TIMESTAMP	= 3,
	OSD_APAGE_OBJECT_COLLECTIONS	= 4,
	OSD_APAGE_OBJECT_SECURITY	= 5,
	OSD_APAGE_OBJECT_LAST		= 0x2fffffff,

	OSD_APAGE_PARTITION_FIRST	= 0x30000000,
	OSD_APAGE_PARTITION_DIRECTORY	= OSD_APAGE_PARTITION_FIRST + 0,
	OSD_APAGE_PARTITION_INFORMATION = OSD_APAGE_PARTITION_FIRST + 1,
	OSD_APAGE_PARTITION_QUOTAS	= OSD_APAGE_PARTITION_FIRST + 2,
	OSD_APAGE_PARTITION_TIMESTAMP	= OSD_APAGE_PARTITION_FIRST + 3,
	OSD_APAGE_PARTITION_ATTR_ACCESS = OSD_APAGE_PARTITION_FIRST + 4,
	OSD_APAGE_PARTITION_SECURITY	= OSD_APAGE_PARTITION_FIRST + 5,
	OSD_APAGE_PARTITION_LAST	= 0x5FFFFFFF,

	OSD_APAGE_COLLECTION_FIRST	= 0x60000000,
	OSD_APAGE_COLLECTION_DIRECTORY	= OSD_APAGE_COLLECTION_FIRST + 0,
	OSD_APAGE_COLLECTION_INFORMATION = OSD_APAGE_COLLECTION_FIRST + 1,
	OSD_APAGE_COLLECTION_TIMESTAMP	= OSD_APAGE_COLLECTION_FIRST + 3,
	OSD_APAGE_COLLECTION_SECURITY	= OSD_APAGE_COLLECTION_FIRST + 5,
	OSD_APAGE_COLLECTION_LAST	= 0x8FFFFFFF,

	OSD_APAGE_ROOT_FIRST		= 0x90000000,
	OSD_APAGE_ROOT_DIRECTORY	= OSD_APAGE_ROOT_FIRST + 0,
	OSD_APAGE_ROOT_INFORMATION	= OSD_APAGE_ROOT_FIRST + 1,
	OSD_APAGE_ROOT_QUOTAS		= OSD_APAGE_ROOT_FIRST + 2,
	OSD_APAGE_ROOT_TIMESTAMP	= OSD_APAGE_ROOT_FIRST + 3,
	OSD_APAGE_ROOT_SECURITY		= OSD_APAGE_ROOT_FIRST + 5,
	OSD_APAGE_ROOT_LAST		= 0xBFFFFFFF,

	OSD_APAGE_RESERVED_TYPE_FIRST	= 0xC0000000,
	OSD_APAGE_RESERVED_TYPE_LAST	= 0xEFFFFFFF,

	OSD_APAGE_COMMON_FIRST		= 0xF0000000,
	OSD_APAGE_COMMON_LAST		= 0xFFFFFFFD,

	OSD_APAGE_CURRENT_COMMAND	= 0xFFFFFFFE,

	OSD_APAGE_REQUEST_ALL		= 0xFFFFFFFF,
};

/* subcategories of attr pages within each range above */
enum {
	OSD_APAGE_STD_FIRST		= 0x0,
	OSD_APAGE_STD_DIRECTORY		= 0,
	OSD_APAGE_STD_INFORMATION	= 1,
	OSD_APAGE_STD_QUOTAS		= 2,
	OSD_APAGE_STD_TIMESTAMP		= 3,
	OSD_APAGE_STD_COLLECTIONS	= 4,
	OSD_APAGE_STD_POLICY_SECURITY	= 5,
	OSD_APAGE_STD_LAST		= 0x0000007F,

	OSD_APAGE_RESERVED_FIRST	= 0x00000080,
	OSD_APAGE_RESERVED_LAST		= 0x00007FFF,

	OSD_APAGE_OTHER_STD_FIRST	= 0x00008000,
	OSD_APAGE_OTHER_STD_LAST	= 0x0000EFFF,

	OSD_APAGE_PUBLIC_FIRST		= 0x0000F000,
	OSD_APAGE_PUBLIC_LAST		= 0x0000FFFF,

	OSD_APAGE_APP_DEFINED_FIRST	= 0x00010000,
	OSD_APAGE_APP_DEFINED_LAST	= 0x1FFFFFFF,

	OSD_APAGE_VENDOR_SPECIFIC_FIRST	= 0x20000000,
	OSD_APAGE_VENDOR_SPECIFIC_LAST	= 0x2FFFFFFF,
};

enum {
	OSD_ATTR_PAGE_IDENTIFICATION = 0, /* in all pages 40 bytes */
};

struct page_identification {
	u8 vendor_identification[8];
	u8 page_identification[32];
}  __packed;

struct osd_attr_page_header {
	__be32 page_number;
	__be32 page_length;
} __packed;

/* 7.1.2.8 Root Information attributes page (OSD_APAGE_ROOT_INFORMATION) */
enum {
	OSD_ATTR_RI_OSD_SYSTEM_ID            = 0x3,   /* 20       */
	OSD_ATTR_RI_VENDOR_IDENTIFICATION    = 0x4,   /* 8        */
	OSD_ATTR_RI_PRODUCT_IDENTIFICATION   = 0x5,   /* 16       */
	OSD_ATTR_RI_PRODUCT_MODEL            = 0x6,   /* 32       */
	OSD_ATTR_RI_PRODUCT_REVISION_LEVEL   = 0x7,   /* 4        */
	OSD_ATTR_RI_PRODUCT_SERIAL_NUMBER    = 0x8,   /* variable */
	OSD_ATTR_RI_OSD_NAME                 = 0x9,   /* variable */
	OSD_ATTR_RI_MAX_CDB_CONTINUATION_LEN = 0xA,   /* 4        */
	OSD_ATTR_RI_TOTAL_CAPACITY           = 0x80,  /* 8        */
	OSD_ATTR_RI_USED_CAPACITY            = 0x81,  /* 8        */
	OSD_ATTR_RI_NUMBER_OF_PARTITIONS     = 0xC0,  /* 8        */
	OSD_ATTR_RI_CLOCK                    = 0x100, /* 6        */
	OARI_DEFAULT_ISOLATION_METHOD        = 0X110, /* 1        */
	OARI_SUPPORTED_ISOLATION_METHODS     = 0X111, /* 32       */

	OARI_DATA_ATOMICITY_GUARANTEE                   = 0X120,   /* 8       */
	OARI_DATA_ATOMICITY_ALIGNMENT                   = 0X121,   /* 8       */
	OARI_ATTRIBUTES_ATOMICITY_GUARANTEE             = 0X122,   /* 8       */
	OARI_DATA_ATTRIBUTES_ATOMICITY_MULTIPLIER       = 0X123,   /* 1       */

	OARI_MAXIMUM_SNAPSHOTS_COUNT                    = 0X1C1,    /* 0 or 4 */
	OARI_MAXIMUM_CLONES_COUNT                       = 0X1C2,    /* 0 or 4 */
	OARI_MAXIMUM_BRANCH_DEPTH                       = 0X1CC,    /* 0 or 4 */
	OARI_SUPPORTED_OBJECT_DUPLICATION_METHOD_FIRST  = 0X200,    /* 0 or 4 */
	OARI_SUPPORTED_OBJECT_DUPLICATION_METHOD_LAST   = 0X2ff,    /* 0 or 4 */
	OARI_SUPPORTED_TIME_OF_DUPLICATION_METHOD_FIRST = 0X300,    /* 0 or 4 */
	OARI_SUPPORTED_TIME_OF_DUPLICATION_METHOD_LAST  = 0X30F,    /* 0 or 4 */
	OARI_SUPPORT_FOR_DUPLICATED_OBJECT_FREEZING     = 0X310,    /* 0 or 4 */
	OARI_SUPPORT_FOR_SNAPSHOT_REFRESHING            = 0X311,    /* 0 or 1 */
	OARI_SUPPORTED_CDB_CONTINUATION_DESC_TYPE_FIRST = 0X7000001,/* 0 or 4 */
	OARI_SUPPORTED_CDB_CONTINUATION_DESC_TYPE_LAST  = 0X700FFFF,/* 0 or 4 */
};
/* Root_Information_attributes_page does not have a get_page structure */

/* 7.1.2.9 Partition Information attributes page
 * (OSD_APAGE_PARTITION_INFORMATION)
 */
enum {
	OSD_ATTR_PI_PARTITION_ID            = 0x1,     /* 8        */
	OSD_ATTR_PI_USERNAME                = 0x9,     /* variable */
	OSD_ATTR_PI_USED_CAPACITY           = 0x81,    /* 8        */
	OSD_ATTR_PI_USED_CAPACITY_INCREMENT = 0x84,    /* 0 or 8   */
	OSD_ATTR_PI_NUMBER_OF_OBJECTS       = 0xC1,    /* 8        */

	OSD_ATTR_PI_ACTUAL_DATA_SPACE                      = 0xD1, /* 0 or 8 */
	OSD_ATTR_PI_RESERVED_DATA_SPACE                    = 0xD2, /* 0 or 8 */
	OSD_ATTR_PI_DEFAULT_SNAPSHOT_DUPLICATION_METHOD    = 0x200,/* 0 or 4 */
	OSD_ATTR_PI_DEFAULT_CLONE_DUPLICATION_METHOD       = 0x201,/* 0 or 4 */
	OSD_ATTR_PI_DEFAULT_SP_TIME_OF_DUPLICATION         = 0x300,/* 0 or 4 */
	OSD_ATTR_PI_DEFAULT_CLONE_TIME_OF_DUPLICATION      = 0x301,/* 0 or 4 */
};
/* Partition Information attributes page does not have a get_page structure */

/* 7.1.2.10 Collection Information attributes page
 * (OSD_APAGE_COLLECTION_INFORMATION)
 */
enum {
	OSD_ATTR_CI_PARTITION_ID           = 0x1,       /* 8        */
	OSD_ATTR_CI_COLLECTION_OBJECT_ID   = 0x2,       /* 8        */
	OSD_ATTR_CI_USERNAME               = 0x9,       /* variable */
	OSD_ATTR_CI_COLLECTION_TYPE        = 0xA,       /* 1        */
	OSD_ATTR_CI_USED_CAPACITY          = 0x81,      /* 8        */
};
/* Collection Information attributes page does not have a get_page structure */

/* 7.1.2.11 User Object Information attributes page
 * (OSD_APAGE_OBJECT_INFORMATION)
 */
enum {
	OSD_ATTR_OI_PARTITION_ID         = 0x1,       /* 8        */
	OSD_ATTR_OI_OBJECT_ID            = 0x2,       /* 8        */
	OSD_ATTR_OI_USERNAME             = 0x9,       /* variable */
	OSD_ATTR_OI_USED_CAPACITY        = 0x81,      /* 8        */
	OSD_ATTR_OI_LOGICAL_LENGTH       = 0x82,      /* 8        */
	SD_ATTR_OI_ACTUAL_DATA_SPACE     = 0XD1,      /* 0 OR 8   */
	SD_ATTR_OI_RESERVED_DATA_SPACE   = 0XD2,      /* 0 OR 8   */
};
/* Object Information attributes page does not have a get_page structure */

/* 7.1.2.12 Root Quotas attributes page (OSD_APAGE_ROOT_QUOTAS) */
enum {
	OSD_ATTR_RQ_DEFAULT_MAXIMUM_USER_OBJECT_LENGTH     = 0x1,      /* 8  */
	OSD_ATTR_RQ_PARTITION_CAPACITY_QUOTA               = 0x10001,  /* 8  */
	OSD_ATTR_RQ_PARTITION_OBJECT_COUNT                 = 0x10002,  /* 8  */
	OSD_ATTR_RQ_PARTITION_COLLECTIONS_PER_USER_OBJECT  = 0x10081,  /* 4  */
	OSD_ATTR_RQ_PARTITION_COUNT                        = 0x20002,  /* 8  */
};

struct Root_Quotas_attributes_page {
	struct osd_attr_page_header hdr; /* id=R+2, size=0x24 */
	__be64 default_maximum_user_object_length;
	__be64 partition_capacity_quota;
	__be64 partition_object_count;
	__be64 partition_collections_per_user_object;
	__be64 partition_count;
}  __packed;

/* 7.1.2.13 Partition Quotas attributes page (OSD_APAGE_PARTITION_QUOTAS)*/
enum {
	OSD_ATTR_PQ_DEFAULT_MAXIMUM_USER_OBJECT_LENGTH  = 0x1,        /* 8 */
	OSD_ATTR_PQ_CAPACITY_QUOTA                      = 0x10001,    /* 8 */
	OSD_ATTR_PQ_OBJECT_COUNT                        = 0x10002,    /* 8 */
	OSD_ATTR_PQ_COLLECTIONS_PER_USER_OBJECT         = 0x10081,    /* 4 */
};

struct Partition_Quotas_attributes_page {
	struct osd_attr_page_header hdr; /* id=P+2, size=0x1C */
	__be64 default_maximum_user_object_length;
	__be64 capacity_quota;
	__be64 object_count;
	__be64 collections_per_user_object;
}  __packed;

/* 7.1.2.14 User Object Quotas attributes page (OSD_APAGE_OBJECT_QUOTAS) */
enum {
	OSD_ATTR_OQ_MAXIMUM_LENGTH  = 0x1,        /* 8 */
};

struct Object_Quotas_attributes_page {
	struct osd_attr_page_header hdr; /* id=U+2, size=0x8 */
	__be64 maximum_length;
}  __packed;

/* 7.1.2.15 Root Timestamps attributes page (OSD_APAGE_ROOT_TIMESTAMP) */
enum {
	OSD_ATTR_RT_ATTRIBUTES_ACCESSED_TIME  = 0x2,        /* 6 */
	OSD_ATTR_RT_ATTRIBUTES_MODIFIED_TIME  = 0x3,        /* 6 */
	OSD_ATTR_RT_TIMESTAMP_BYPASS          = 0xFFFFFFFE, /* 1 */
};

struct root_timestamps_attributes_page {
	struct osd_attr_page_header hdr; /* id=R+3, size=0xD */
	struct osd_timestamp attributes_accessed_time;
	struct osd_timestamp attributes_modified_time;
	u8 timestamp_bypass;
}  __packed;

/* 7.1.2.16 Partition Timestamps attributes page
 * (OSD_APAGE_PARTITION_TIMESTAMP)
 */
enum {
	OSD_ATTR_PT_CREATED_TIME              = 0x1,        /* 6 */
	OSD_ATTR_PT_ATTRIBUTES_ACCESSED_TIME  = 0x2,        /* 6 */
	OSD_ATTR_PT_ATTRIBUTES_MODIFIED_TIME  = 0x3,        /* 6 */
	OSD_ATTR_PT_DATA_ACCESSED_TIME        = 0x4,        /* 6 */
	OSD_ATTR_PT_DATA_MODIFIED_TIME        = 0x5,        /* 6 */
	OSD_ATTR_PT_TIMESTAMP_BYPASS          = 0xFFFFFFFE, /* 1 */
};

struct partition_timestamps_attributes_page {
	struct osd_attr_page_header hdr; /* id=P+3, size=0x1F */
	struct osd_timestamp created_time;
	struct osd_timestamp attributes_accessed_time;
	struct osd_timestamp attributes_modified_time;
	struct osd_timestamp data_accessed_time;
	struct osd_timestamp data_modified_time;
	u8 timestamp_bypass;
}  __packed;

/* 7.1.2.17/18 Collection/Object Timestamps attributes page
 * (OSD_APAGE_COLLECTION_TIMESTAMP/OSD_APAGE_OBJECT_TIMESTAMP)
 */
enum {
	OSD_ATTR_OT_CREATED_TIME              = 0x1,        /* 6 */
	OSD_ATTR_OT_ATTRIBUTES_ACCESSED_TIME  = 0x2,        /* 6 */
	OSD_ATTR_OT_ATTRIBUTES_MODIFIED_TIME  = 0x3,        /* 6 */
	OSD_ATTR_OT_DATA_ACCESSED_TIME        = 0x4,        /* 6 */
	OSD_ATTR_OT_DATA_MODIFIED_TIME        = 0x5,        /* 6 */
};

/* same for collection */
struct object_timestamps_attributes_page {
	struct osd_attr_page_header hdr; /* id=C+3/3, size=0x1E */
	struct osd_timestamp created_time;
	struct osd_timestamp attributes_accessed_time;
	struct osd_timestamp attributes_modified_time;
	struct osd_timestamp data_accessed_time;
	struct osd_timestamp data_modified_time;
}  __packed;

/* OSD2r05: 7.1.3.19 Attributes Access attributes page
 * (OSD_APAGE_PARTITION_ATTR_ACCESS)
 *
 * each attribute is of the form below. Total array length is deduced
 * from the attribute's length
 * (See allowed_attributes_access of the struct osd_cap_object_descriptor)
 */
struct attributes_access_attr {
	struct osd_attributes_list_attrid attr_list[0];
} __packed;

/* OSD2r05: 7.1.2.21 Collections attributes page */
/* TBD */

/* 7.1.2.20 Root Policy/Security attributes page (OSD_APAGE_ROOT_SECURITY) */
enum {
	OSD_ATTR_RS_DEFAULT_SECURITY_METHOD           = 0x1,       /* 1      */
	OSD_ATTR_RS_OLDEST_VALID_NONCE_LIMIT          = 0x2,       /* 6      */
	OSD_ATTR_RS_NEWEST_VALID_NONCE_LIMIT          = 0x3,       /* 6      */
	OSD_ATTR_RS_PARTITION_DEFAULT_SECURITY_METHOD = 0x6,       /* 1      */
	OSD_ATTR_RS_SUPPORTED_SECURITY_METHODS        = 0x7,       /* 2      */
	OSD_ATTR_RS_ADJUSTABLE_CLOCK                  = 0x9,       /* 6      */
	OSD_ATTR_RS_MASTER_KEY_IDENTIFIER             = 0x7FFD,    /* 0 or 7 */
	OSD_ATTR_RS_ROOT_KEY_IDENTIFIER               = 0x7FFE,    /* 0 or 7 */
	OSD_ATTR_RS_SUPPORTED_INTEGRITY_ALGORITHM_0   = 0x80000000,/* 1,(x16)*/
	OSD_ATTR_RS_SUPPORTED_DH_GROUP_0              = 0x80000010,/* 1,(x16)*/
};

struct root_security_attributes_page {
	struct osd_attr_page_header hdr; /* id=R+5, size=0x3F */
	u8 default_security_method;
	u8 partition_default_security_method;
	__be16 supported_security_methods;
	u8 mki_valid_rki_valid;
	struct osd_timestamp oldest_valid_nonce_limit;
	struct osd_timestamp newest_valid_nonce_limit;
	struct osd_timestamp adjustable_clock;
	u8 master_key_identifier[32-25];
	u8 root_key_identifier[39-32];
	u8 supported_integrity_algorithm[16];
	u8 supported_dh_group[16];
}  __packed;

/* 7.1.2.21 Partition Policy/Security attributes page
 * (OSD_APAGE_PARTITION_SECURITY)
 */
enum {
	OSD_ATTR_PS_DEFAULT_SECURITY_METHOD        = 0x1,        /* 1      */
	OSD_ATTR_PS_OLDEST_VALID_NONCE             = 0x2,        /* 6      */
	OSD_ATTR_PS_NEWEST_VALID_NONCE             = 0x3,        /* 6      */
	OSD_ATTR_PS_REQUEST_NONCE_LIST_DEPTH       = 0x4,        /* 2      */
	OSD_ATTR_PS_FROZEN_WORKING_KEY_BIT_MASK    = 0x5,        /* 2      */
	OSD_ATTR_PS_PARTITION_KEY_IDENTIFIER       = 0x7FFF,     /* 0 or 7 */
	OSD_ATTR_PS_WORKING_KEY_IDENTIFIER_FIRST   = 0x8000,     /* 0 or 7 */
	OSD_ATTR_PS_WORKING_KEY_IDENTIFIER_LAST    = 0x800F,     /* 0 or 7 */
	OSD_ATTR_PS_POLICY_ACCESS_TAG              = 0x40000001, /* 4      */
	OSD_ATTR_PS_USER_OBJECT_POLICY_ACCESS_TAG  = 0x40000002, /* 4      */
};

struct partition_security_attributes_page {
	struct osd_attr_page_header hdr; /* id=p+5, size=0x8f */
	u8 reserved[3];
	u8 default_security_method;
	struct osd_timestamp oldest_valid_nonce;
	struct osd_timestamp newest_valid_nonce;
	__be16 request_nonce_list_depth;
	__be16 frozen_working_key_bit_mask;
	__be32 policy_access_tag;
	__be32 user_object_policy_access_tag;
	u8 pki_valid;
	__be16 wki_00_0f_vld;
	struct osd_key_identifier partition_key_identifier;
	struct osd_key_identifier working_key_identifiers[16];
}  __packed;

/* 7.1.2.22/23 Collection/Object Policy-Security attributes page
 * (OSD_APAGE_COLLECTION_SECURITY/OSD_APAGE_OBJECT_SECURITY)
 */
enum {
	OSD_ATTR_OS_POLICY_ACCESS_TAG              = 0x40000001, /* 4      */
};

struct object_security_attributes_page {
	struct osd_attr_page_header hdr; /* id=C+5/5, size=4 */
	__be32 policy_access_tag;
}  __packed;

/* OSD2r05: 7.1.3.31 Current Command attributes page
 * (OSD_APAGE_CURRENT_COMMAND)
 */
enum {
	OSD_ATTR_CC_RESPONSE_INTEGRITY_CHECK_VALUE     = 0x1, /* 32  */
	OSD_ATTR_CC_OBJECT_TYPE                        = 0x2, /* 1   */
	OSD_ATTR_CC_PARTITION_ID                       = 0x3, /* 8   */
	OSD_ATTR_CC_OBJECT_ID                          = 0x4, /* 8   */
	OSD_ATTR_CC_STARTING_BYTE_ADDRESS_OF_APPEND    = 0x5, /* 8   */
	OSD_ATTR_CC_CHANGE_IN_USED_CAPACITY            = 0x6, /* 8   */
};

/*TBD: osdv1_current_command_attributes_page */

struct osdv2_current_command_attributes_page {
	struct osd_attr_page_header hdr;  /* id=0xFFFFFFFE, size=0x44 */
	u8 response_integrity_check_value[OSD_CRYPTO_KEYID_SIZE];
	u8 object_type;
	u8 reserved[3];
	__be64 partition_id;
	__be64 object_id;
	__be64 starting_byte_address_of_append;
	__be64 change_in_used_capacity;
};

#endif /*ndef __OSD_ATTRIBUTES_H__*/
