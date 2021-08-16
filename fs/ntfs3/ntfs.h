/* SPDX-License-Identifier: GPL-2.0 */
/*
 *
 * Copyright (C) 2019-2021 Paragon Software GmbH, All rights reserved.
 *
 * on-disk ntfs structs
 */

// clang-format off
#ifndef _LINUX_NTFS3_NTFS_H
#define _LINUX_NTFS3_NTFS_H

/* TODO:
 * - Check 4K mft record and 512 bytes cluster
 */

/*
 * Activate this define to use binary search in indexes
 */
#define NTFS3_INDEX_BINARY_SEARCH

/*
 * Check each run for marked clusters
 */
#define NTFS3_CHECK_FREE_CLST

#define NTFS_NAME_LEN 255

/*
 * ntfs.sys used 500 maximum links
 * on-disk struct allows up to 0xffff
 */
#define NTFS_LINK_MAX 0x400
//#define NTFS_LINK_MAX 0xffff

/*
 * Activate to use 64 bit clusters instead of 32 bits in ntfs.sys
 * Logical and virtual cluster number
 * If needed, may be redefined to use 64 bit value
 */
//#define CONFIG_NTFS3_64BIT_CLUSTER

#define NTFS_LZNT_MAX_CLUSTER	4096
#define NTFS_LZNT_CUNIT		4
#define NTFS_LZNT_CLUSTERS	(1u<<NTFS_LZNT_CUNIT)

struct GUID {
	__le32 Data1;
	__le16 Data2;
	__le16 Data3;
	u8 Data4[8];
};

/*
 * this struct repeats layout of ATTR_FILE_NAME
 * at offset 0x40
 * it used to store global constants NAME_MFT/NAME_MIRROR...
 * most constant names are shorter than 10
 */
struct cpu_str {
	u8 len;
	u8 unused;
	u16 name[10];
};

struct le_str {
	u8 len;
	u8 unused;
	__le16 name[];
};

static_assert(SECTOR_SHIFT == 9);

#ifdef CONFIG_NTFS3_64BIT_CLUSTER
typedef u64 CLST;
static_assert(sizeof(size_t) == 8);
#else
typedef u32 CLST;
#endif

#define SPARSE_LCN64   ((u64)-1)
#define SPARSE_LCN     ((CLST)-1)
#define RESIDENT_LCN   ((CLST)-2)
#define COMPRESSED_LCN ((CLST)-3)

#define COMPRESSION_UNIT     4
#define COMPRESS_MAX_CLUSTER 0x1000
#define MFT_INCREASE_CHUNK   1024

enum RECORD_NUM {
	MFT_REC_MFT		= 0,
	MFT_REC_MIRR		= 1,
	MFT_REC_LOG		= 2,
	MFT_REC_VOL		= 3,
	MFT_REC_ATTR		= 4,
	MFT_REC_ROOT		= 5,
	MFT_REC_BITMAP		= 6,
	MFT_REC_BOOT		= 7,
	MFT_REC_BADCLUST	= 8,
	//MFT_REC_QUOTA		= 9,
	MFT_REC_SECURE		= 9, // NTFS 3.0
	MFT_REC_UPCASE		= 10,
	MFT_REC_EXTEND		= 11, // NTFS 3.0
	MFT_REC_RESERVED	= 11,
	MFT_REC_FREE		= 16,
	MFT_REC_USER		= 24,
};

enum ATTR_TYPE {
	ATTR_ZERO		= cpu_to_le32(0x00),
	ATTR_STD		= cpu_to_le32(0x10),
	ATTR_LIST		= cpu_to_le32(0x20),
	ATTR_NAME		= cpu_to_le32(0x30),
	// ATTR_VOLUME_VERSION on Nt4
	ATTR_ID			= cpu_to_le32(0x40),
	ATTR_SECURE		= cpu_to_le32(0x50),
	ATTR_LABEL		= cpu_to_le32(0x60),
	ATTR_VOL_INFO		= cpu_to_le32(0x70),
	ATTR_DATA		= cpu_to_le32(0x80),
	ATTR_ROOT		= cpu_to_le32(0x90),
	ATTR_ALLOC		= cpu_to_le32(0xA0),
	ATTR_BITMAP		= cpu_to_le32(0xB0),
	// ATTR_SYMLINK on Nt4
	ATTR_REPARSE		= cpu_to_le32(0xC0),
	ATTR_EA_INFO		= cpu_to_le32(0xD0),
	ATTR_EA			= cpu_to_le32(0xE0),
	ATTR_PROPERTYSET	= cpu_to_le32(0xF0),
	ATTR_LOGGED_UTILITY_STREAM = cpu_to_le32(0x100),
	ATTR_END		= cpu_to_le32(0xFFFFFFFF)
};

static_assert(sizeof(enum ATTR_TYPE) == 4);

enum FILE_ATTRIBUTE {
	FILE_ATTRIBUTE_READONLY		= cpu_to_le32(0x00000001),
	FILE_ATTRIBUTE_HIDDEN		= cpu_to_le32(0x00000002),
	FILE_ATTRIBUTE_SYSTEM		= cpu_to_le32(0x00000004),
	FILE_ATTRIBUTE_ARCHIVE		= cpu_to_le32(0x00000020),
	FILE_ATTRIBUTE_DEVICE		= cpu_to_le32(0x00000040),
	FILE_ATTRIBUTE_TEMPORARY	= cpu_to_le32(0x00000100),
	FILE_ATTRIBUTE_SPARSE_FILE	= cpu_to_le32(0x00000200),
	FILE_ATTRIBUTE_REPARSE_POINT	= cpu_to_le32(0x00000400),
	FILE_ATTRIBUTE_COMPRESSED	= cpu_to_le32(0x00000800),
	FILE_ATTRIBUTE_OFFLINE		= cpu_to_le32(0x00001000),
	FILE_ATTRIBUTE_NOT_CONTENT_INDEXED = cpu_to_le32(0x00002000),
	FILE_ATTRIBUTE_ENCRYPTED	= cpu_to_le32(0x00004000),
	FILE_ATTRIBUTE_VALID_FLAGS	= cpu_to_le32(0x00007fb7),
	FILE_ATTRIBUTE_DIRECTORY	= cpu_to_le32(0x10000000),
};

static_assert(sizeof(enum FILE_ATTRIBUTE) == 4);

extern const struct cpu_str NAME_MFT;
extern const struct cpu_str NAME_MIRROR;
extern const struct cpu_str NAME_LOGFILE;
extern const struct cpu_str NAME_VOLUME;
extern const struct cpu_str NAME_ATTRDEF;
extern const struct cpu_str NAME_ROOT;
extern const struct cpu_str NAME_BITMAP;
extern const struct cpu_str NAME_BOOT;
extern const struct cpu_str NAME_BADCLUS;
extern const struct cpu_str NAME_QUOTA;
extern const struct cpu_str NAME_SECURE;
extern const struct cpu_str NAME_UPCASE;
extern const struct cpu_str NAME_EXTEND;
extern const struct cpu_str NAME_OBJID;
extern const struct cpu_str NAME_REPARSE;
extern const struct cpu_str NAME_USNJRNL;

extern const __le16 I30_NAME[4];
extern const __le16 SII_NAME[4];
extern const __le16 SDH_NAME[4];
extern const __le16 SO_NAME[2];
extern const __le16 SQ_NAME[2];
extern const __le16 SR_NAME[2];

extern const __le16 BAD_NAME[4];
extern const __le16 SDS_NAME[4];
extern const __le16 WOF_NAME[17];	/* WofCompressedData */

/* MFT record number structure */
struct MFT_REF {
	__le32 low;	// The low part of the number
	__le16 high;	// The high part of the number
	__le16 seq;	// The sequence number of MFT record
};

static_assert(sizeof(__le64) == sizeof(struct MFT_REF));

static inline CLST ino_get(const struct MFT_REF *ref)
{
#ifdef CONFIG_NTFS3_64BIT_CLUSTER
	return le32_to_cpu(ref->low) | ((u64)le16_to_cpu(ref->high) << 32);
#else
	return le32_to_cpu(ref->low);
#endif
}

struct NTFS_BOOT {
	u8 jump_code[3];	// 0x00: Jump to boot code
	u8 system_id[8];	// 0x03: System ID, equals "NTFS    "

	// NOTE: this member is not aligned(!)
	// bytes_per_sector[0] must be 0
	// bytes_per_sector[1] must be multiplied by 256
	u8 bytes_per_sector[2];	// 0x0B: Bytes per sector

	u8 sectors_per_clusters;// 0x0D: Sectors per cluster
	u8 unused1[7];
	u8 media_type;		// 0x15: Media type (0xF8 - harddisk)
	u8 unused2[2];
	__le16 sct_per_track;	// 0x18: number of sectors per track
	__le16 heads;		// 0x1A: number of heads per cylinder
	__le32 hidden_sectors;	// 0x1C: number of 'hidden' sectors
	u8 unused3[4];
	u8 bios_drive_num;	// 0x24: BIOS drive number =0x80
	u8 unused4;
	u8 signature_ex;	// 0x26: Extended BOOT signature =0x80
	u8 unused5;
	__le64 sectors_per_volume;// 0x28: size of volume in sectors
	__le64 mft_clst;	// 0x30: first cluster of $MFT
	__le64 mft2_clst;	// 0x38: first cluster of $MFTMirr
	s8 record_size;		// 0x40: size of MFT record in clusters(sectors)
	u8 unused6[3];
	s8 index_size;		// 0x44: size of INDX record in clusters(sectors)
	u8 unused7[3];
	__le64 serial_num;	// 0x48: Volume serial number
	__le32 check_sum;	// 0x50: Simple additive checksum of all
				// of the u32's which precede the 'check_sum'

	u8 boot_code[0x200 - 0x50 - 2 - 4]; // 0x54:
	u8 boot_magic[2];	// 0x1FE: Boot signature =0x55 + 0xAA
};

static_assert(sizeof(struct NTFS_BOOT) == 0x200);

enum NTFS_SIGNATURE {
	NTFS_FILE_SIGNATURE = cpu_to_le32(0x454C4946), // 'FILE'
	NTFS_INDX_SIGNATURE = cpu_to_le32(0x58444E49), // 'INDX'
	NTFS_CHKD_SIGNATURE = cpu_to_le32(0x444B4843), // 'CHKD'
	NTFS_RSTR_SIGNATURE = cpu_to_le32(0x52545352), // 'RSTR'
	NTFS_RCRD_SIGNATURE = cpu_to_le32(0x44524352), // 'RCRD'
	NTFS_BAAD_SIGNATURE = cpu_to_le32(0x44414142), // 'BAAD'
	NTFS_HOLE_SIGNATURE = cpu_to_le32(0x454C4F48), // 'HOLE'
	NTFS_FFFF_SIGNATURE = cpu_to_le32(0xffffffff),
};

static_assert(sizeof(enum NTFS_SIGNATURE) == 4);

/* MFT Record header structure */
struct NTFS_RECORD_HEADER {
	/* Record magic number, equals 'FILE'/'INDX'/'RSTR'/'RCRD' */
	enum NTFS_SIGNATURE sign; // 0x00:
	__le16 fix_off;		// 0x04:
	__le16 fix_num;		// 0x06:
	__le64 lsn;		// 0x08: Log file sequence number
};

static_assert(sizeof(struct NTFS_RECORD_HEADER) == 0x10);

static inline int is_baad(const struct NTFS_RECORD_HEADER *hdr)
{
	return hdr->sign == NTFS_BAAD_SIGNATURE;
}

/* Possible bits in struct MFT_REC.flags */
enum RECORD_FLAG {
	RECORD_FLAG_IN_USE	= cpu_to_le16(0x0001),
	RECORD_FLAG_DIR		= cpu_to_le16(0x0002),
	RECORD_FLAG_SYSTEM	= cpu_to_le16(0x0004),
	RECORD_FLAG_UNKNOWN	= cpu_to_le16(0x0008),
};

/* MFT Record structure */
struct MFT_REC {
	struct NTFS_RECORD_HEADER rhdr; // 'FILE'

	__le16 seq;		// 0x10: Sequence number for this record
	__le16 hard_links;	// 0x12: The number of hard links to record
	__le16 attr_off;	// 0x14: Offset to attributes
	__le16 flags;		// 0x16: See RECORD_FLAG
	__le32 used;		// 0x18: The size of used part
	__le32 total;		// 0x1C: Total record size

	struct MFT_REF parent_ref; // 0x20: Parent MFT record
	__le16 next_attr_id;	// 0x28: The next attribute Id

	__le16 res;		// 0x2A: High part of mft record?
	__le32 mft_record;	// 0x2C: Current mft record number
	__le16 fixups[];	// 0x30:
};

#define MFTRECORD_FIXUP_OFFSET_1 offsetof(struct MFT_REC, res)
#define MFTRECORD_FIXUP_OFFSET_3 offsetof(struct MFT_REC, fixups)

static_assert(MFTRECORD_FIXUP_OFFSET_1 == 0x2A);
static_assert(MFTRECORD_FIXUP_OFFSET_3 == 0x30);

static inline bool is_rec_base(const struct MFT_REC *rec)
{
	const struct MFT_REF *r = &rec->parent_ref;

	return !r->low && !r->high && !r->seq;
}

static inline bool is_mft_rec5(const struct MFT_REC *rec)
{
	return le16_to_cpu(rec->rhdr.fix_off) >=
	       offsetof(struct MFT_REC, fixups);
}

static inline bool is_rec_inuse(const struct MFT_REC *rec)
{
	return rec->flags & RECORD_FLAG_IN_USE;
}

static inline bool clear_rec_inuse(struct MFT_REC *rec)
{
	return rec->flags &= ~RECORD_FLAG_IN_USE;
}

/* Possible values of ATTR_RESIDENT.flags */
#define RESIDENT_FLAG_INDEXED 0x01

struct ATTR_RESIDENT {
	__le32 data_size;	// 0x10: The size of data
	__le16 data_off;	// 0x14: Offset to data
	u8 flags;		// 0x16: resident flags ( 1 - indexed )
	u8 res;			// 0x17:
}; // sizeof() = 0x18

struct ATTR_NONRESIDENT {
	__le64 svcn;		// 0x10: Starting VCN of this segment
	__le64 evcn;		// 0x18: End VCN of this segment
	__le16 run_off;		// 0x20: Offset to packed runs
	//  Unit of Compression size for this stream, expressed
	//  as a log of the cluster size.
	//
	//	0 means file is not compressed
	//	1, 2, 3, and 4 are potentially legal values if the
	//	    stream is compressed, however the implementation
	//	    may only choose to use 4, or possibly 3.  Note
	//	    that 4 means cluster size time 16.	If convenient
	//	    the implementation may wish to accept a
	//	    reasonable range of legal values here (1-5?),
	//	    even if the implementation only generates
	//	    a smaller set of values itself.
	u8 c_unit;		// 0x22
	u8 res1[5];		// 0x23:
	__le64 alloc_size;	// 0x28: The allocated size of attribute in bytes
				// (multiple of cluster size)
	__le64 data_size;	// 0x30: The size of attribute  in bytes <= alloc_size
	__le64 valid_size;	// 0x38: The size of valid part in bytes <= data_size
	__le64 total_size;	// 0x40: The sum of the allocated clusters for a file
				// (present only for the first segment (0 == vcn)
				// of compressed attribute)

}; // sizeof()=0x40 or 0x48 (if compressed)

/* Possible values of ATTRIB.flags: */
#define ATTR_FLAG_COMPRESSED	  cpu_to_le16(0x0001)
#define ATTR_FLAG_COMPRESSED_MASK cpu_to_le16(0x00FF)
#define ATTR_FLAG_ENCRYPTED	  cpu_to_le16(0x4000)
#define ATTR_FLAG_SPARSED	  cpu_to_le16(0x8000)

struct ATTRIB {
	enum ATTR_TYPE type;	// 0x00: The type of this attribute
	__le32 size;		// 0x04: The size of this attribute
	u8 non_res;		// 0x08: Is this attribute non-resident ?
	u8 name_len;		// 0x09: This attribute name length
	__le16 name_off;	// 0x0A: Offset to the attribute name
	__le16 flags;		// 0x0C: See ATTR_FLAG_XXX
	__le16 id;		// 0x0E: unique id (per record)

	union {
		struct ATTR_RESIDENT res;     // 0x10
		struct ATTR_NONRESIDENT nres; // 0x10
	};
};

/* Define attribute sizes */
#define SIZEOF_RESIDENT			0x18
#define SIZEOF_NONRESIDENT_EX		0x48
#define SIZEOF_NONRESIDENT		0x40

#define SIZEOF_RESIDENT_LE		cpu_to_le16(0x18)
#define SIZEOF_NONRESIDENT_EX_LE	cpu_to_le16(0x48)
#define SIZEOF_NONRESIDENT_LE		cpu_to_le16(0x40)

static inline u64 attr_ondisk_size(const struct ATTRIB *attr)
{
	return attr->non_res ? ((attr->flags &
				 (ATTR_FLAG_COMPRESSED | ATTR_FLAG_SPARSED)) ?
					le64_to_cpu(attr->nres.total_size) :
					le64_to_cpu(attr->nres.alloc_size)) :
			       QuadAlign(le32_to_cpu(attr->res.data_size));
}

static inline u64 attr_size(const struct ATTRIB *attr)
{
	return attr->non_res ? le64_to_cpu(attr->nres.data_size) :
			       le32_to_cpu(attr->res.data_size);
}

static inline bool is_attr_encrypted(const struct ATTRIB *attr)
{
	return attr->flags & ATTR_FLAG_ENCRYPTED;
}

static inline bool is_attr_sparsed(const struct ATTRIB *attr)
{
	return attr->flags & ATTR_FLAG_SPARSED;
}

static inline bool is_attr_compressed(const struct ATTRIB *attr)
{
	return attr->flags & ATTR_FLAG_COMPRESSED;
}

static inline bool is_attr_ext(const struct ATTRIB *attr)
{
	return attr->flags & (ATTR_FLAG_SPARSED | ATTR_FLAG_COMPRESSED);
}

static inline bool is_attr_indexed(const struct ATTRIB *attr)
{
	return !attr->non_res && (attr->res.flags & RESIDENT_FLAG_INDEXED);
}

static inline __le16 const *attr_name(const struct ATTRIB *attr)
{
	return Add2Ptr(attr, le16_to_cpu(attr->name_off));
}

static inline u64 attr_svcn(const struct ATTRIB *attr)
{
	return attr->non_res ? le64_to_cpu(attr->nres.svcn) : 0;
}

/* the size of resident attribute by its resident size */
#define BYTES_PER_RESIDENT(b) (0x18 + (b))

static_assert(sizeof(struct ATTRIB) == 0x48);
static_assert(sizeof(((struct ATTRIB *)NULL)->res) == 0x08);
static_assert(sizeof(((struct ATTRIB *)NULL)->nres) == 0x38);

static inline void *resident_data_ex(const struct ATTRIB *attr, u32 datasize)
{
	u32 asize, rsize;
	u16 off;

	if (attr->non_res)
		return NULL;

	asize = le32_to_cpu(attr->size);
	off = le16_to_cpu(attr->res.data_off);

	if (asize < datasize + off)
		return NULL;

	rsize = le32_to_cpu(attr->res.data_size);
	if (rsize < datasize)
		return NULL;

	return Add2Ptr(attr, off);
}

static inline void *resident_data(const struct ATTRIB *attr)
{
	return Add2Ptr(attr, le16_to_cpu(attr->res.data_off));
}

static inline void *attr_run(const struct ATTRIB *attr)
{
	return Add2Ptr(attr, le16_to_cpu(attr->nres.run_off));
}

/* Standard information attribute (0x10) */
struct ATTR_STD_INFO {
	__le64 cr_time;		// 0x00: File creation file
	__le64 m_time;		// 0x08: File modification time
	__le64 c_time;		// 0x10: Last time any attribute was modified
	__le64 a_time;		// 0x18: File last access time
	enum FILE_ATTRIBUTE fa; // 0x20: Standard DOS attributes & more
	__le32 max_ver_num;	// 0x24: Maximum Number of Versions
	__le32 ver_num;		// 0x28: Version Number
	__le32 class_id;	// 0x2C: Class Id from bidirectional Class Id index
};

static_assert(sizeof(struct ATTR_STD_INFO) == 0x30);

#define SECURITY_ID_INVALID 0x00000000
#define SECURITY_ID_FIRST 0x00000100

struct ATTR_STD_INFO5 {
	__le64 cr_time;		// 0x00: File creation file
	__le64 m_time;		// 0x08: File modification time
	__le64 c_time;		// 0x10: Last time any attribute was modified
	__le64 a_time;		// 0x18: File last access time
	enum FILE_ATTRIBUTE fa; // 0x20: Standard DOS attributes & more
	__le32 max_ver_num;	// 0x24: Maximum Number of Versions
	__le32 ver_num;		// 0x28: Version Number
	__le32 class_id;	// 0x2C: Class Id from bidirectional Class Id index

	__le32 owner_id;	// 0x30: Owner Id of the user owning the file.
	__le32 security_id;	// 0x34: The Security Id is a key in the $SII Index and $SDS
	__le64 quota_charge;	// 0x38:
	__le64 usn;		// 0x40: Last Update Sequence Number of the file. This is a direct
				// index into the file $UsnJrnl. If zero, the USN Journal is
				// disabled.
};

static_assert(sizeof(struct ATTR_STD_INFO5) == 0x48);

/* attribute list entry structure (0x20) */
struct ATTR_LIST_ENTRY {
	enum ATTR_TYPE type;	// 0x00: The type of attribute
	__le16 size;		// 0x04: The size of this record
	u8 name_len;		// 0x06: The length of attribute name
	u8 name_off;		// 0x07: The offset to attribute name
	__le64 vcn;		// 0x08: Starting VCN of this attribute
	struct MFT_REF ref;	// 0x10: MFT record number with attribute
	__le16 id;		// 0x18: struct ATTRIB ID
	__le16 name[3];		// 0x1A: Just to align. To get real name can use bNameOffset

}; // sizeof(0x20)

static_assert(sizeof(struct ATTR_LIST_ENTRY) == 0x20);

static inline u32 le_size(u8 name_len)
{
	return QuadAlign(offsetof(struct ATTR_LIST_ENTRY, name) +
			 name_len * sizeof(short));
}

/* returns 0 if 'attr' has the same type and name */
static inline int le_cmp(const struct ATTR_LIST_ENTRY *le,
			 const struct ATTRIB *attr)
{
	return le->type != attr->type || le->name_len != attr->name_len ||
	       (!le->name_len &&
		memcmp(Add2Ptr(le, le->name_off),
		       Add2Ptr(attr, le16_to_cpu(attr->name_off)),
		       le->name_len * sizeof(short)));
}

static inline __le16 const *le_name(const struct ATTR_LIST_ENTRY *le)
{
	return Add2Ptr(le, le->name_off);
}

/* File name types (the field type in struct ATTR_FILE_NAME ) */
#define FILE_NAME_POSIX   0
#define FILE_NAME_UNICODE 1
#define FILE_NAME_DOS	  2
#define FILE_NAME_UNICODE_AND_DOS (FILE_NAME_DOS | FILE_NAME_UNICODE)

/* Filename attribute structure (0x30) */
struct NTFS_DUP_INFO {
	__le64 cr_time;		// 0x00: File creation file
	__le64 m_time;		// 0x08: File modification time
	__le64 c_time;		// 0x10: Last time any attribute was modified
	__le64 a_time;		// 0x18: File last access time
	__le64 alloc_size;	// 0x20: Data attribute allocated size, multiple of cluster size
	__le64 data_size;	// 0x28: Data attribute size <= Dataalloc_size
	enum FILE_ATTRIBUTE fa;	// 0x30: Standard DOS attributes & more
	__le16 ea_size;		// 0x34: Packed EAs
	__le16 reparse;		// 0x36: Used by Reparse

}; // 0x38

struct ATTR_FILE_NAME {
	struct MFT_REF home;	// 0x00: MFT record for directory
	struct NTFS_DUP_INFO dup;// 0x08
	u8 name_len;		// 0x40: File name length in words
	u8 type;		// 0x41: File name type
	__le16 name[];		// 0x42: File name
};

static_assert(sizeof(((struct ATTR_FILE_NAME *)NULL)->dup) == 0x38);
static_assert(offsetof(struct ATTR_FILE_NAME, name) == 0x42);
#define SIZEOF_ATTRIBUTE_FILENAME     0x44
#define SIZEOF_ATTRIBUTE_FILENAME_MAX (0x42 + 255 * 2)

static inline struct ATTRIB *attr_from_name(struct ATTR_FILE_NAME *fname)
{
	return (struct ATTRIB *)((char *)fname - SIZEOF_RESIDENT);
}

static inline u16 fname_full_size(const struct ATTR_FILE_NAME *fname)
{
	// don't return struct_size(fname, name, fname->name_len);
	return offsetof(struct ATTR_FILE_NAME, name) +
	       fname->name_len * sizeof(short);
}

static inline u8 paired_name(u8 type)
{
	if (type == FILE_NAME_UNICODE)
		return FILE_NAME_DOS;
	if (type == FILE_NAME_DOS)
		return FILE_NAME_UNICODE;
	return FILE_NAME_POSIX;
}

/* Index entry defines ( the field flags in NtfsDirEntry ) */
#define NTFS_IE_HAS_SUBNODES	cpu_to_le16(1)
#define NTFS_IE_LAST		cpu_to_le16(2)

/* Directory entry structure */
struct NTFS_DE {
	union {
		struct MFT_REF ref; // 0x00: MFT record number with this file
		struct {
			__le16 data_off;  // 0x00:
			__le16 data_size; // 0x02:
			__le32 res;	  // 0x04: must be 0
		} view;
	};
	__le16 size;		// 0x08: The size of this entry
	__le16 key_size;	// 0x0A: The size of File name length in bytes + 0x42
	__le16 flags;		// 0x0C: Entry flags: NTFS_IE_XXX
	__le16 res;		// 0x0E:

	// Here any indexed attribute can be placed
	// One of them is:
	// struct ATTR_FILE_NAME AttrFileName;
	//

	// The last 8 bytes of this structure contains
	// the VBN of subnode
	// !!! Note !!!
	// This field is presented only if (flags & NTFS_IE_HAS_SUBNODES)
	// __le64 vbn;
};

static_assert(sizeof(struct NTFS_DE) == 0x10);

static inline void de_set_vbn_le(struct NTFS_DE *e, __le64 vcn)
{
	__le64 *v = Add2Ptr(e, le16_to_cpu(e->size) - sizeof(__le64));

	*v = vcn;
}

static inline void de_set_vbn(struct NTFS_DE *e, CLST vcn)
{
	__le64 *v = Add2Ptr(e, le16_to_cpu(e->size) - sizeof(__le64));

	*v = cpu_to_le64(vcn);
}

static inline __le64 de_get_vbn_le(const struct NTFS_DE *e)
{
	return *(__le64 *)Add2Ptr(e, le16_to_cpu(e->size) - sizeof(__le64));
}

static inline CLST de_get_vbn(const struct NTFS_DE *e)
{
	__le64 *v = Add2Ptr(e, le16_to_cpu(e->size) - sizeof(__le64));

	return le64_to_cpu(*v);
}

static inline struct NTFS_DE *de_get_next(const struct NTFS_DE *e)
{
	return Add2Ptr(e, le16_to_cpu(e->size));
}

static inline struct ATTR_FILE_NAME *de_get_fname(const struct NTFS_DE *e)
{
	return le16_to_cpu(e->key_size) >= SIZEOF_ATTRIBUTE_FILENAME ?
		       Add2Ptr(e, sizeof(struct NTFS_DE)) :
		       NULL;
}

static inline bool de_is_last(const struct NTFS_DE *e)
{
	return e->flags & NTFS_IE_LAST;
}

static inline bool de_has_vcn(const struct NTFS_DE *e)
{
	return e->flags & NTFS_IE_HAS_SUBNODES;
}

static inline bool de_has_vcn_ex(const struct NTFS_DE *e)
{
	return (e->flags & NTFS_IE_HAS_SUBNODES) &&
	       (u64)(-1) != *((u64 *)Add2Ptr(e, le16_to_cpu(e->size) -
							sizeof(__le64)));
}

#define MAX_BYTES_PER_NAME_ENTRY					       \
	QuadAlign(sizeof(struct NTFS_DE) +				       \
		  offsetof(struct ATTR_FILE_NAME, name) +		       \
		  NTFS_NAME_LEN * sizeof(short))

struct INDEX_HDR {
	__le32 de_off;	// 0x00: The offset from the start of this structure
			// to the first NTFS_DE
	__le32 used;	// 0x04: The size of this structure plus all
			// entries (quad-word aligned)
	__le32 total;	// 0x08: The allocated size of for this structure plus all entries
	u8 flags;	// 0x0C: 0x00 = Small directory, 0x01 = Large directory
	u8 res[3];

	//
	// de_off + used <= total
	//
};

static_assert(sizeof(struct INDEX_HDR) == 0x10);

static inline struct NTFS_DE *hdr_first_de(const struct INDEX_HDR *hdr)
{
	u32 de_off = le32_to_cpu(hdr->de_off);
	u32 used = le32_to_cpu(hdr->used);
	struct NTFS_DE *e = Add2Ptr(hdr, de_off);
	u16 esize;

	if (de_off >= used || de_off >= le32_to_cpu(hdr->total))
		return NULL;

	esize = le16_to_cpu(e->size);
	if (esize < sizeof(struct NTFS_DE) || de_off + esize > used)
		return NULL;

	return e;
}

static inline struct NTFS_DE *hdr_next_de(const struct INDEX_HDR *hdr,
					  const struct NTFS_DE *e)
{
	size_t off = PtrOffset(hdr, e);
	u32 used = le32_to_cpu(hdr->used);
	u16 esize;

	if (off >= used)
		return NULL;

	esize = le16_to_cpu(e->size);

	if (esize < sizeof(struct NTFS_DE) ||
	    off + esize + sizeof(struct NTFS_DE) > used)
		return NULL;

	return Add2Ptr(e, esize);
}

static inline bool hdr_has_subnode(const struct INDEX_HDR *hdr)
{
	return hdr->flags & 1;
}

struct INDEX_BUFFER {
	struct NTFS_RECORD_HEADER rhdr; // 'INDX'
	__le64 vbn; // 0x10: vcn if index >= cluster or vsn id index < cluster
	struct INDEX_HDR ihdr; // 0x18:
};

static_assert(sizeof(struct INDEX_BUFFER) == 0x28);

static inline bool ib_is_empty(const struct INDEX_BUFFER *ib)
{
	const struct NTFS_DE *first = hdr_first_de(&ib->ihdr);

	return !first || de_is_last(first);
}

static inline bool ib_is_leaf(const struct INDEX_BUFFER *ib)
{
	return !(ib->ihdr.flags & 1);
}

/* Index root structure ( 0x90 ) */
enum COLLATION_RULE {
	NTFS_COLLATION_TYPE_BINARY	= cpu_to_le32(0),
	// $I30
	NTFS_COLLATION_TYPE_FILENAME	= cpu_to_le32(0x01),
	// $SII of $Secure and $Q of Quota
	NTFS_COLLATION_TYPE_UINT	= cpu_to_le32(0x10),
	// $O of Quota
	NTFS_COLLATION_TYPE_SID		= cpu_to_le32(0x11),
	// $SDH of $Secure
	NTFS_COLLATION_TYPE_SECURITY_HASH = cpu_to_le32(0x12),
	// $O of ObjId and "$R" for Reparse
	NTFS_COLLATION_TYPE_UINTS	= cpu_to_le32(0x13)
};

static_assert(sizeof(enum COLLATION_RULE) == 4);

//
struct INDEX_ROOT {
	enum ATTR_TYPE type;	// 0x00: The type of attribute to index on
	enum COLLATION_RULE rule; // 0x04: The rule
	__le32 index_block_size;// 0x08: The size of index record
	u8 index_block_clst;	// 0x0C: The number of clusters or sectors per index
	u8 res[3];
	struct INDEX_HDR ihdr;	// 0x10:
};

static_assert(sizeof(struct INDEX_ROOT) == 0x20);
static_assert(offsetof(struct INDEX_ROOT, ihdr) == 0x10);

#define VOLUME_FLAG_DIRTY	    cpu_to_le16(0x0001)
#define VOLUME_FLAG_RESIZE_LOG_FILE cpu_to_le16(0x0002)

struct VOLUME_INFO {
	__le64 res1;	// 0x00
	u8 major_ver;	// 0x08: NTFS major version number (before .)
	u8 minor_ver;	// 0x09: NTFS minor version number (after .)
	__le16 flags;	// 0x0A: Volume flags, see VOLUME_FLAG_XXX

}; // sizeof=0xC

#define SIZEOF_ATTRIBUTE_VOLUME_INFO 0xc

#define NTFS_LABEL_MAX_LENGTH		(0x100 / sizeof(short))
#define NTFS_ATTR_INDEXABLE		cpu_to_le32(0x00000002)
#define NTFS_ATTR_DUPALLOWED		cpu_to_le32(0x00000004)
#define NTFS_ATTR_MUST_BE_INDEXED	cpu_to_le32(0x00000010)
#define NTFS_ATTR_MUST_BE_NAMED		cpu_to_le32(0x00000020)
#define NTFS_ATTR_MUST_BE_RESIDENT	cpu_to_le32(0x00000040)
#define NTFS_ATTR_LOG_ALWAYS		cpu_to_le32(0x00000080)

/* $AttrDef file entry */
struct ATTR_DEF_ENTRY {
	__le16 name[0x40];	// 0x00: Attr name
	enum ATTR_TYPE type;	// 0x80: struct ATTRIB type
	__le32 res;		// 0x84:
	enum COLLATION_RULE rule; // 0x88:
	__le32 flags;		// 0x8C: NTFS_ATTR_XXX (see above)
	__le64 min_sz;		// 0x90: Minimum attribute data size
	__le64 max_sz;		// 0x98: Maximum attribute data size
};

static_assert(sizeof(struct ATTR_DEF_ENTRY) == 0xa0);

/* Object ID (0x40) */
struct OBJECT_ID {
	struct GUID ObjId;	// 0x00: Unique Id assigned to file
	struct GUID BirthVolumeId;// 0x10: Birth Volume Id is the Object Id of the Volume on
				// which the Object Id was allocated. It never changes
	struct GUID BirthObjectId; // 0x20: Birth Object Id is the first Object Id that was
				// ever assigned to this MFT Record. I.e. If the Object Id
				// is changed for some reason, this field will reflect the
				// original value of the Object Id.
	struct GUID DomainId;	// 0x30: Domain Id is currently unused but it is intended to be
				// used in a network environment where the local machine is
				// part of a Windows 2000 Domain. This may be used in a Windows
				// 2000 Advanced Server managed domain.
};

static_assert(sizeof(struct OBJECT_ID) == 0x40);

/* O Directory entry structure ( rule = 0x13 ) */
struct NTFS_DE_O {
	struct NTFS_DE de;
	struct GUID ObjId;	// 0x10: Unique Id assigned to file
	struct MFT_REF ref;	// 0x20: MFT record number with this file
	struct GUID BirthVolumeId; // 0x28: Birth Volume Id is the Object Id of the Volume on
				// which the Object Id was allocated. It never changes
	struct GUID BirthObjectId; // 0x38: Birth Object Id is the first Object Id that was
				// ever assigned to this MFT Record. I.e. If the Object Id
				// is changed for some reason, this field will reflect the
				// original value of the Object Id.
				// This field is valid if data_size == 0x48
	struct GUID BirthDomainId; // 0x48: Domain Id is currently unused but it is intended
				// to be used in a network environment where the local
				// machine is part of a Windows 2000 Domain. This may be
				// used in a Windows 2000 Advanced Server managed domain.
};

static_assert(sizeof(struct NTFS_DE_O) == 0x58);

#define NTFS_OBJECT_ENTRY_DATA_SIZE1					       \
	0x38 // struct NTFS_DE_O.BirthDomainId is not used
#define NTFS_OBJECT_ENTRY_DATA_SIZE2					       \
	0x48 // struct NTFS_DE_O.BirthDomainId is used

/* Q Directory entry structure ( rule = 0x11 ) */
struct NTFS_DE_Q {
	struct NTFS_DE de;
	__le32 owner_id;	// 0x10: Unique Id assigned to file
	__le32 Version;		// 0x14: 0x02
	__le32 flags2;		// 0x18: Quota flags, see above
	__le64 BytesUsed;	// 0x1C:
	__le64 ChangeTime;	// 0x24:
	__le64 WarningLimit;	// 0x28:
	__le64 HardLimit;	// 0x34:
	__le64 ExceededTime;	// 0x3C:

	// SID is placed here
}; // sizeof() = 0x44

#define SIZEOF_NTFS_DE_Q 0x44

#define SecurityDescriptorsBlockSize 0x40000 // 256K
#define SecurityDescriptorMaxSize    0x20000 // 128K
#define Log2OfSecurityDescriptorsBlockSize 18

struct SECURITY_KEY {
	__le32 hash; //  Hash value for descriptor
	__le32 sec_id; //  Security Id (guaranteed unique)
};

/* Security descriptors (the content of $Secure::SDS data stream) */
struct SECURITY_HDR {
	struct SECURITY_KEY key;	// 0x00: Security Key
	__le64 off;			// 0x08: Offset of this entry in the file
	__le32 size;			// 0x10: Size of this entry, 8 byte aligned
	//
	// Security descriptor itself is placed here
	// Total size is 16 byte aligned
	//
} __packed;

#define SIZEOF_SECURITY_HDR 0x14

/* SII Directory entry structure */
struct NTFS_DE_SII {
	struct NTFS_DE de;
	__le32 sec_id;			// 0x10: Key: sizeof(security_id) = wKeySize
	struct SECURITY_HDR sec_hdr;	// 0x14:
} __packed;

#define SIZEOF_SII_DIRENTRY 0x28

/* SDH Directory entry structure */
struct NTFS_DE_SDH {
	struct NTFS_DE de;
	struct SECURITY_KEY key;	// 0x10: Key
	struct SECURITY_HDR sec_hdr;	// 0x18: Data
	__le16 magic[2];		// 0x2C: 0x00490049 "I I"
};

#define SIZEOF_SDH_DIRENTRY 0x30

struct REPARSE_KEY {
	__le32 ReparseTag;		// 0x00: Reparse Tag
	struct MFT_REF ref;		// 0x04: MFT record number with this file
}; // sizeof() = 0x0C

static_assert(offsetof(struct REPARSE_KEY, ref) == 0x04);
#define SIZEOF_REPARSE_KEY 0x0C

/* Reparse Directory entry structure */
struct NTFS_DE_R {
	struct NTFS_DE de;
	struct REPARSE_KEY key;		// 0x10: Reparse Key
	u32 zero;			// 0x1c
}; // sizeof() = 0x20

static_assert(sizeof(struct NTFS_DE_R) == 0x20);

/* CompressReparseBuffer.WofVersion */
#define WOF_CURRENT_VERSION		cpu_to_le32(1)
/* CompressReparseBuffer.WofProvider */
#define WOF_PROVIDER_WIM		cpu_to_le32(1)
/* CompressReparseBuffer.WofProvider */
#define WOF_PROVIDER_SYSTEM		cpu_to_le32(2)
/* CompressReparseBuffer.ProviderVer */
#define WOF_PROVIDER_CURRENT_VERSION	cpu_to_le32(1)

#define WOF_COMPRESSION_XPRESS4K	cpu_to_le32(0) // 4k
#define WOF_COMPRESSION_LZX32K		cpu_to_le32(1) // 32k
#define WOF_COMPRESSION_XPRESS8K	cpu_to_le32(2) // 8k
#define WOF_COMPRESSION_XPRESS16K	cpu_to_le32(3) // 16k

/*
 * ATTR_REPARSE (0xC0)
 *
 * The reparse struct GUID structure is used by all 3rd party layered drivers to
 * store data in a reparse point. For non-Microsoft tags, The struct GUID field
 * cannot be GUID_NULL.
 * The constraints on reparse tags are defined below.
 * Microsoft tags can also be used with this format of the reparse point buffer.
 */
struct REPARSE_POINT {
	__le32 ReparseTag;	// 0x00:
	__le16 ReparseDataLength;// 0x04:
	__le16 Reserved;

	struct GUID Guid;	// 0x08:

	//
	// Here GenericReparseBuffer is placed
	//
};

static_assert(sizeof(struct REPARSE_POINT) == 0x18);

//
// Maximum allowed size of the reparse data.
//
#define MAXIMUM_REPARSE_DATA_BUFFER_SIZE	(16 * 1024)

//
// The value of the following constant needs to satisfy the following
// conditions:
//  (1) Be at least as large as the largest of the reserved tags.
//  (2) Be strictly smaller than all the tags in use.
//
#define IO_REPARSE_TAG_RESERVED_RANGE		1

//
// The reparse tags are a ULONG. The 32 bits are laid out as follows:
//
//   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
//   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
//  +-+-+-+-+-----------------------+-------------------------------+
//  |M|R|N|R|	  Reserved bits     |	    Reparse Tag Value	    |
//  +-+-+-+-+-----------------------+-------------------------------+
//
// M is the Microsoft bit. When set to 1, it denotes a tag owned by Microsoft.
//   All ISVs must use a tag with a 0 in this position.
//   Note: If a Microsoft tag is used by non-Microsoft software, the
//   behavior is not defined.
//
// R is reserved.  Must be zero for non-Microsoft tags.
//
// N is name surrogate. When set to 1, the file represents another named
//   entity in the system.
//
// The M and N bits are OR-able.
// The following macros check for the M and N bit values:
//

//
// Macro to determine whether a reparse point tag corresponds to a tag
// owned by Microsoft.
//
#define IsReparseTagMicrosoft(_tag)	(((_tag)&IO_REPARSE_TAG_MICROSOFT))

//
// Macro to determine whether a reparse point tag is a name surrogate
//
#define IsReparseTagNameSurrogate(_tag)	(((_tag)&IO_REPARSE_TAG_NAME_SURROGATE))

//
// The following constant represents the bits that are valid to use in
// reparse tags.
//
#define IO_REPARSE_TAG_VALID_VALUES	0xF000FFFF

//
// Macro to determine whether a reparse tag is a valid tag.
//
#define IsReparseTagValid(_tag)						       \
	(!((_tag) & ~IO_REPARSE_TAG_VALID_VALUES) &&			       \
	 ((_tag) > IO_REPARSE_TAG_RESERVED_RANGE))

//
// Microsoft tags for reparse points.
//

enum IO_REPARSE_TAG {
	IO_REPARSE_TAG_SYMBOLIC_LINK	= cpu_to_le32(0),
	IO_REPARSE_TAG_NAME_SURROGATE	= cpu_to_le32(0x20000000),
	IO_REPARSE_TAG_MICROSOFT	= cpu_to_le32(0x80000000),
	IO_REPARSE_TAG_MOUNT_POINT	= cpu_to_le32(0xA0000003),
	IO_REPARSE_TAG_SYMLINK		= cpu_to_le32(0xA000000C),
	IO_REPARSE_TAG_HSM		= cpu_to_le32(0xC0000004),
	IO_REPARSE_TAG_SIS		= cpu_to_le32(0x80000007),
	IO_REPARSE_TAG_DEDUP		= cpu_to_le32(0x80000013),
	IO_REPARSE_TAG_COMPRESS		= cpu_to_le32(0x80000017),

	//
	// The reparse tag 0x80000008 is reserved for Microsoft internal use
	// (may be published in the future)
	//

	//
	// Microsoft reparse tag reserved for DFS
	//
	IO_REPARSE_TAG_DFS		= cpu_to_le32(0x8000000A),

	//
	// Microsoft reparse tag reserved for the file system filter manager
	//
	IO_REPARSE_TAG_FILTER_MANAGER	= cpu_to_le32(0x8000000B),

	//
	// Non-Microsoft tags for reparse points
	//

	//
	// Tag allocated to CONGRUENT, May 2000. Used by IFSTEST
	//
	IO_REPARSE_TAG_IFSTEST_CONGRUENT = cpu_to_le32(0x00000009),

	//
	// Tag allocated to ARKIVIO
	//
	IO_REPARSE_TAG_ARKIVIO		= cpu_to_le32(0x0000000C),

	//
	//  Tag allocated to SOLUTIONSOFT
	//
	IO_REPARSE_TAG_SOLUTIONSOFT	= cpu_to_le32(0x2000000D),

	//
	//  Tag allocated to COMMVAULT
	//
	IO_REPARSE_TAG_COMMVAULT	= cpu_to_le32(0x0000000E),

	// OneDrive??
	IO_REPARSE_TAG_CLOUD		= cpu_to_le32(0x9000001A),
	IO_REPARSE_TAG_CLOUD_1		= cpu_to_le32(0x9000101A),
	IO_REPARSE_TAG_CLOUD_2		= cpu_to_le32(0x9000201A),
	IO_REPARSE_TAG_CLOUD_3		= cpu_to_le32(0x9000301A),
	IO_REPARSE_TAG_CLOUD_4		= cpu_to_le32(0x9000401A),
	IO_REPARSE_TAG_CLOUD_5		= cpu_to_le32(0x9000501A),
	IO_REPARSE_TAG_CLOUD_6		= cpu_to_le32(0x9000601A),
	IO_REPARSE_TAG_CLOUD_7		= cpu_to_le32(0x9000701A),
	IO_REPARSE_TAG_CLOUD_8		= cpu_to_le32(0x9000801A),
	IO_REPARSE_TAG_CLOUD_9		= cpu_to_le32(0x9000901A),
	IO_REPARSE_TAG_CLOUD_A		= cpu_to_le32(0x9000A01A),
	IO_REPARSE_TAG_CLOUD_B		= cpu_to_le32(0x9000B01A),
	IO_REPARSE_TAG_CLOUD_C		= cpu_to_le32(0x9000C01A),
	IO_REPARSE_TAG_CLOUD_D		= cpu_to_le32(0x9000D01A),
	IO_REPARSE_TAG_CLOUD_E		= cpu_to_le32(0x9000E01A),
	IO_REPARSE_TAG_CLOUD_F		= cpu_to_le32(0x9000F01A),

};

#define SYMLINK_FLAG_RELATIVE		1

/* Microsoft reparse buffer. (see DDK for details) */
struct REPARSE_DATA_BUFFER {
	__le32 ReparseTag;		// 0x00:
	__le16 ReparseDataLength;	// 0x04:
	__le16 Reserved;

	union {
		// If ReparseTag == 0xA0000003 (IO_REPARSE_TAG_MOUNT_POINT)
		struct {
			__le16 SubstituteNameOffset; // 0x08
			__le16 SubstituteNameLength; // 0x0A
			__le16 PrintNameOffset;      // 0x0C
			__le16 PrintNameLength;      // 0x0E
			__le16 PathBuffer[];	     // 0x10
		} MountPointReparseBuffer;

		// If ReparseTag == 0xA000000C (IO_REPARSE_TAG_SYMLINK)
		// https://msdn.microsoft.com/en-us/library/cc232006.aspx
		struct {
			__le16 SubstituteNameOffset; // 0x08
			__le16 SubstituteNameLength; // 0x0A
			__le16 PrintNameOffset;      // 0x0C
			__le16 PrintNameLength;      // 0x0E
			// 0-absolute path 1- relative path, SYMLINK_FLAG_RELATIVE
			__le32 Flags;		     // 0x10
			__le16 PathBuffer[];	     // 0x14
		} SymbolicLinkReparseBuffer;

		// If ReparseTag == 0x80000017U
		struct {
			__le32 WofVersion;  // 0x08 == 1
			/* 1 - WIM backing provider ("WIMBoot"),
			 * 2 - System compressed file provider
			 */
			__le32 WofProvider; // 0x0C
			__le32 ProviderVer; // 0x10: == 1 WOF_FILE_PROVIDER_CURRENT_VERSION == 1
			__le32 CompressionFormat; // 0x14: 0, 1, 2, 3. See WOF_COMPRESSION_XXX
		} CompressReparseBuffer;

		struct {
			u8 DataBuffer[1];   // 0x08
		} GenericReparseBuffer;
	};
};

/* ATTR_EA_INFO (0xD0) */

#define FILE_NEED_EA 0x80 // See ntifs.h
/* FILE_NEED_EA, indicates that the file to which the EA belongs cannot be
 * interpreted without understanding the associated extended attributes.
 */
struct EA_INFO {
	__le16 size_pack;	// 0x00: Size of buffer to hold in packed form
	__le16 count;		// 0x02: Count of EA's with FILE_NEED_EA bit set
	__le32 size;		// 0x04: Size of buffer to hold in unpacked form
};

static_assert(sizeof(struct EA_INFO) == 8);

/* ATTR_EA (0xE0) */
struct EA_FULL {
	__le32 size;		// 0x00: (not in packed)
	u8 flags;		// 0x04
	u8 name_len;		// 0x05
	__le16 elength;		// 0x06
	u8 name[];		// 0x08
};

static_assert(offsetof(struct EA_FULL, name) == 8);

#define ACL_REVISION	2
#define ACL_REVISION_DS 4

#define SE_SELF_RELATIVE cpu_to_le16(0x8000)

struct SECURITY_DESCRIPTOR_RELATIVE {
	u8 Revision;
	u8 Sbz1;
	__le16 Control;
	__le32 Owner;
	__le32 Group;
	__le32 Sacl;
	__le32 Dacl;
};
static_assert(sizeof(struct SECURITY_DESCRIPTOR_RELATIVE) == 0x14);

struct ACE_HEADER {
	u8 AceType;
	u8 AceFlags;
	__le16 AceSize;
};
static_assert(sizeof(struct ACE_HEADER) == 4);

struct ACL {
	u8 AclRevision;
	u8 Sbz1;
	__le16 AclSize;
	__le16 AceCount;
	__le16 Sbz2;
};
static_assert(sizeof(struct ACL) == 8);

struct SID {
	u8 Revision;
	u8 SubAuthorityCount;
	u8 IdentifierAuthority[6];
	__le32 SubAuthority[];
};
static_assert(offsetof(struct SID, SubAuthority) == 8);

#endif /* _LINUX_NTFS3_NTFS_H */
// clang-format on
