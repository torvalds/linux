/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * All NTFS associated on-disk structures.
 *
 * Copyright (c) 2001-2005 Anton Altaparmakov
 * Copyright (c) 2002 Richard Russon
 */

#ifndef _LINUX_NTFS_LAYOUT_H
#define _LINUX_NTFS_LAYOUT_H

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/list.h>
#include <asm/byteorder.h>

/* The NTFS oem_id "NTFS    " */
#define magicNTFS	cpu_to_le64(0x202020205346544eULL)

/*
 * Location of bootsector on partition:
 *	The standard NTFS_BOOT_SECTOR is on sector 0 of the partition.
 *	On NT4 and above there is one backup copy of the boot sector to
 *	be found on the last sector of the partition (not normally accessible
 *	from within Windows as the bootsector contained number of sectors
 *	value is one less than the actual value!).
 *	On versions of NT 3.51 and earlier, the backup copy was located at
 *	number of sectors/2 (integer divide), i.e. in the middle of the volume.
 */

/*
 * BIOS parameter block (bpb) structure.
 *
 * @bytes_per_sector:       Size of a sector in bytes (usually 512).
 *                          Matches the logical sector size of the underlying device.
 * @sectors_per_cluster:    Size of a cluster in sectors (NTFS cluster size / sector size).
 * @reserved_sectors:       Number of reserved sectors at the beginning of the volume.
 *                          Always set to 0 in NTFS.
 * @fats:                   Number of FAT tables.
 *                          Always 0 in NTFS (no FAT tables exist).
 * @root_entries:           Number of entries in the root directory.
 *                          Always 0 in NTFS.
 * @sectors:                Total number of sectors on the volume.
 *                          Always 0 in NTFS (use @large_sectors instead).
 * @media_type:             Media descriptor byte.
 *                          0xF8 for hard disk (fixed media) in NTFS.
 * @sectors_per_fat:        Number of sectors per FAT table.
 *                          Always 0 in NTFS.
 * @sectors_per_track:      Number of sectors per track.
 *                          Irrelevant for NTFS.
 * @heads:                  Number of heads (CHS geometry).
 *                          Irrelevant for NTFS.
 * @hidden_sectors:         Number of hidden sectors before the start of the partition.
 *                          Always 0 in NTFS boot sector.
 * @large_sectors:          Total number of sectors on the volume.
 */
struct bios_parameter_block {
	__le16 bytes_per_sector;
	u8 sectors_per_cluster;
	__le16 reserved_sectors;
	u8 fats;
	__le16 root_entries;
	__le16 sectors;
	u8 media_type;
	__le16 sectors_per_fat;
	__le16 sectors_per_track;
	__le16 heads;
	__le32 hidden_sectors;
	__le32 large_sectors;
} __packed;

/*
 * NTFS boot sector structure.
 *
 * @jump:               3-byte jump instruction to boot code (irrelevant for NTFS).
 *                      Typically 0xEB 0x52 0x90 or similar.
 * @oem_id:             OEM identifier string (8 bytes).
 *                      Always "NTFS    " (with trailing spaces) in NTFS volumes.
 * @bpb:                Legacy BIOS Parameter Block (see struct bios_parameter_block).
 *                      Mostly zeroed or set to fixed values for NTFS compatibility.
 * @unused:             4 bytes, reserved/unused.
 *                      NTFS disk editors show it as:
 *                        - physical_drive (0x80 for fixed disk)
 *                        - current_head (0)
 *                        - extended_boot_signature (0x80 or 0x28)
 *                        - unused (0)
 *                      Always zero in practice for NTFS.
 * @number_of_sectors:  Number of sectors in volume. Gives maximum volume
 *                      size of 2^63 sectors. Assuming standard sector
 *                      size of 512 bytes, the maximum byte size is
 *                      approx. 4.7x10^21 bytes. (-;
 * @mft_lcn:            Logical cluster number (LCN) of the $MFT data attribute.
 *                      Location of the Master File Table.
 * @mftmirr_lcn:        LCN of the $MFTMirr (first 3-4 MFT records copy).
 *                      Mirror for boot-time recovery.
 * @clusters_per_mft_record:
 *                      Size of each MFT record in clusters.
 * @reserved0:          3 bytes, reserved/zero.
 * @clusters_per_index_record:
 *                      Size of each index block/record in clusters.
 * @reserved1:          3 bytes, reserved/zero.
 * @volume_serial_number:
 *                      64-bit volume serial number.
 *                      Used for identification (irrelevant for NTFS operation).
 * @checksum:           32-bit checksum of the boot sector (excluding this field).
 *                      Used to detect boot sector corruption.
 * @bootstrap:          426 bytes of bootstrap code.
 *                      Irrelevant for NTFS (contains x86 boot loader stub).
 * @end_of_sector_marker:
 *                      2-byte end-of-sector signature.
 *                      Always 0xAA55 (little-endian magic number).
 */
struct ntfs_boot_sector {
	u8 jump[3];
	__le64 oem_id;
	struct bios_parameter_block bpb;
	u8 unused[4];
	__le64 number_of_sectors;
	__le64 mft_lcn;
	__le64 mftmirr_lcn;
	s8 clusters_per_mft_record;
	u8 reserved0[3];
	s8 clusters_per_index_record;
	u8 reserved1[3];
	__le64 volume_serial_number;
	__le32 checksum;
	u8 bootstrap[426];
	__le16 end_of_sector_marker;
} __packed;

static_assert(sizeof(struct ntfs_boot_sector) == 512);

/*
 * Magic identifiers present at the beginning of all ntfs record containing
 * records (like mft records for example).
 *
 * magic_FILE:      MFT entry header ("FILE" in ASCII).
 *                  Used in $MFT/$DATA for all master file table records.
 * magic_INDX:      Index buffer header ("INDX" in ASCII).
 *                  Used in $INDEX_ALLOCATION attributes (directories, $I30 indexes).
 * magic_HOLE:      Hole marker ("HOLE" in ASCII).
 *                  Introduced in NTFS 3.0+, used for sparse/hole regions in some contexts.
 * magic_RSTR:      Restart page header ("RSTR" in ASCII).
 *                  Used in LogFile for restart pages (transaction log recovery).
 * magic_RCRD:      Log record page header ("RCRD" in ASCII).
 *                  Used in LogFile for individual log record pages.
 * magic_CHKD:      Chkdsk modified marker ("CHKD" in ASCII).
 *                  Set by chkdsk when it modifies a record; indicates repair was done.
 * magic_BAAD:      Bad record marker ("BAAD" in ASCII).
 *                  Indicates a multi-sector transfer failure was detected.
 *                  The record is corrupted/unusable; often set during I/O errors.
 * magic_empty:     Empty/uninitialized page marker (0xffffffff).
 *                  Used in LogFile when a page is filled with 0xff bytes
 *                  and has not yet been initialized. Must be formatted before use.
 */
enum {
	magic_FILE = cpu_to_le32(0x454c4946),
	magic_INDX = cpu_to_le32(0x58444e49),
	magic_HOLE = cpu_to_le32(0x454c4f48),
	magic_RSTR = cpu_to_le32(0x52545352),
	magic_RCRD = cpu_to_le32(0x44524352),
	magic_CHKD = cpu_to_le32(0x444b4843),
	magic_BAAD = cpu_to_le32(0x44414142),
	magic_empty = cpu_to_le32(0xffffffff)
};

/*
 * Generic magic comparison macros. Finally found a use for the ## preprocessor
 * operator! (-8
 */

static inline bool __ntfs_is_magic(__le32 x, __le32 r)
{
	return (x == r);
}
#define ntfs_is_magic(x, m)	__ntfs_is_magic(x, magic_##m)

static inline bool __ntfs_is_magicp(__le32 *p, __le32 r)
{
	return (*p == r);
}
#define ntfs_is_magicp(p, m)	__ntfs_is_magicp(p, magic_##m)

/*
 * Specialised magic comparison macros for the NTFS_RECORD_TYPEs defined above.
 */
#define ntfs_is_file_record(x)		(ntfs_is_magic(x, FILE))
#define ntfs_is_file_recordp(p)		(ntfs_is_magicp(p, FILE))
#define ntfs_is_mft_record(x)		(ntfs_is_file_record(x))
#define ntfs_is_mft_recordp(p)		(ntfs_is_file_recordp(p))
#define ntfs_is_indx_record(x)		(ntfs_is_magic(x, INDX))
#define ntfs_is_indx_recordp(p)		(ntfs_is_magicp(p, INDX))
#define ntfs_is_hole_record(x)		(ntfs_is_magic(x, HOLE))
#define ntfs_is_hole_recordp(p)		(ntfs_is_magicp(p, HOLE))

#define ntfs_is_rstr_record(x)		(ntfs_is_magic(x, RSTR))
#define ntfs_is_rstr_recordp(p)		(ntfs_is_magicp(p, RSTR))
#define ntfs_is_rcrd_record(x)		(ntfs_is_magic(x, RCRD))
#define ntfs_is_rcrd_recordp(p)		(ntfs_is_magicp(p, RCRD))

#define ntfs_is_chkd_record(x)		(ntfs_is_magic(x, CHKD))
#define ntfs_is_chkd_recordp(p)		(ntfs_is_magicp(p, CHKD))

#define ntfs_is_baad_record(x)		(ntfs_is_magic(x, BAAD))
#define ntfs_is_baad_recordp(p)		(ntfs_is_magicp(p, BAAD))

#define ntfs_is_empty_record(x)		(ntfs_is_magic(x, empty))
#define ntfs_is_empty_recordp(p)	(ntfs_is_magicp(p, empty))

/*
 * struct ntfs_record - Common header for all multi-sector protected NTFS records
 *
 * @magic:      4-byte magic identifier for the record type and/or status.
 *              Common values are defined in the magic_* enum (FILE, INDX, RSTR,
 *              RCRD, CHKD, BAAD, HOLE, empty).
 *              - "FILE" = MFT record
 *              - "INDX" = Index allocation block
 *              - "BAAD" = Record corrupted (multi-sector fixup failed)
 *              - 0xffffffff = Uninitialized/empty page
 * @usa_ofs:    Offset (in bytes) from the start of this record to the Update
 *              Sequence Array (USA).
 *              The USA is located at record + usa_ofs.
 * @usa_count:  Number of 16-bit entries in the USA array (including the Update
 *              Sequence Number itself).
 *              - Number of fixup locations = usa_count - 1
 *              - Each fixup location is a 16-bit value in the record that needs
 *                protection against torn writes.
 *
 * The Update Sequence Array (usa) is an array of the __le16 values which belong
 * to the end of each sector protected by the update sequence record in which
 * this array is contained. Note that the first entry is the Update Sequence
 * Number (usn), a cyclic counter of how many times the protected record has
 * been written to disk. The values 0 and -1 (ie. 0xffff) are not used. All
 * last le16's of each sector have to be equal to the usn (during reading) or
 * are set to it (during writing). If they are not, an incomplete multi sector
 * transfer has occurred when the data was written.
 * The maximum size for the update sequence array is fixed to:
 *	maximum size = usa_ofs + (usa_count * 2) = 510 bytes
 * The 510 bytes comes from the fact that the last __le16 in the array has to
 * (obviously) finish before the last __le16 of the first 512-byte sector.
 * This formula can be used as a consistency check in that usa_ofs +
 * (usa_count * 2) has to be less than or equal to 510.
 */
struct ntfs_record {
	__le32 magic;
	__le16 usa_ofs;
	__le16 usa_count;
} __packed;

/*
 * System files mft record numbers. All these files are always marked as used
 * in the bitmap attribute of the mft; presumably in order to avoid accidental
 * allocation for random other mft records. Also, the sequence number for each
 * of the system files is always equal to their mft record number and it is
 * never modified.
 *
 * FILE_MFT:        Master File Table (MFT) itself.
 *                  Data attribute contains all MFT entries;
 *                  Bitmap attribute tracks which records are in use (bit==1).
 * FILE_MFTMirr:    MFT mirror: copy of the first four (or more) MFT records
 *                  in its data attribute.
 *                  If cluster size > 4 KiB, copies first N records where
 *                  N = cluster_size / mft_record_size.
 * FILE_LogFile:    Journaling log (LogFile) in data attribute.
 *                  Used for transaction logging and recovery.
 * FILE_Volume:     Volume information and name.
 *                  Contains $VolumeName (label) and $VolumeInformation
 *                  (flags, NTFS version). Windows calls this the volume DASD.
 * FILE_AttrDef:    Attribute definitions array in data attribute.
 *                  Defines all possible attribute types and their properties.
 * FILE_root:       Root directory ($Root).
 *                  The top-level directory of the filesystem.
 * FILE_Bitmap:     Cluster allocation bitmap ($Bitmap) in data attribute.
 *                  Tracks free/used clusters (LCNs) on the volume.
 * FILE_Boot:       Boot sector ($Boot) in data attribute.
 *                  Always located at cluster 0; contains BPB and NTFS parameters.
 * FILE_BadClus:    Bad cluster list ($BadClus) in non-resident data attribute.
 *                  Marks all known bad clusters.
 * FILE_Secure:     Security descriptors ($Secure).
 *                  Contains shared $SDS (security descriptors) and two indexes
 *                  ($SDH, $SII). Introduced in Windows 2000.
 *                  Before that, it was called $Quota but was unused.
 * FILE_UpCase:     Uppercase table ($UpCase) in data attribute.
 *                  Maps all 65536 Unicode characters to their uppercase forms.
 * FILE_Extend:     System directory ($Extend).
 *                  Contains additional system files ($ObjId, $Quota, $Reparse,
 *                  $UsnJrnl, etc.). Introduced in NTFS 3.0 (Windows 2000).
 * FILE_reserved12: Reserved for future use (MFT records 12–15).
 * FILE_reserved13: Reserved.
 * FILE_reserved14: Reserved.
 * FILE_reserved15: Reserved.
 * FILE_first_user: First possible user-created file MFT record number.
 *                  Used as a boundary to distinguish system files from user files.
 */
enum {
	FILE_MFT       = 0,
	FILE_MFTMirr   = 1,
	FILE_LogFile   = 2,
	FILE_Volume    = 3,
	FILE_AttrDef   = 4,
	FILE_root      = 5,
	FILE_Bitmap    = 6,
	FILE_Boot      = 7,
	FILE_BadClus   = 8,
	FILE_Secure    = 9,
	FILE_UpCase    = 10,
	FILE_Extend    = 11,
	FILE_reserved12 = 12,
	FILE_reserved13 = 13,
	FILE_reserved14 = 14,
	FILE_reserved15 = 15,
	FILE_first_user = 16,
};

/*
 * enum - Flags for MFT record header
 *
 * These are the so far known MFT_RECORD_* flags (16-bit) which contain
 * information about the mft record in which they are present.
 *
 * MFT_RECORD_IN_USE:        This MFT record is allocated and in use.
 *                           (bit set = record is valid/used; clear = free)
 * MFT_RECORD_IS_DIRECTORY:  This MFT record represents a directory.
 *                           (Used to quickly distinguish files from directories)
 * MFT_RECORD_IS_4:          Indicates the record is a special "record 4" type.
 *                           (Rarely used; related to NTFS internal special cases,
 *                           often for $AttrDef or early system files)
 * MFT_RECORD_IS_VIEW_INDEX: This MFT record is used as a view index.
 *                           (Specific to NTFS indexed views or object ID indexes)
 * MFT_REC_SPACE_FILLER:     Dummy value to force the enum to be 16-bit wide.
 *                           (Not a real flag; just a sentinel to ensure the type
 *                           is __le16 and no higher bits are accidentally used)
 */
enum {
	MFT_RECORD_IN_USE		= cpu_to_le16(0x0001),
	MFT_RECORD_IS_DIRECTORY		= cpu_to_le16(0x0002),
	MFT_RECORD_IS_4			= cpu_to_le16(0x0004),
	MFT_RECORD_IS_VIEW_INDEX	= cpu_to_le16(0x0008),
	MFT_REC_SPACE_FILLER		= cpu_to_le16(0xffff), /*Just to make flags 16-bit.*/
} __packed;

/*
 * mft references (aka file references or file record segment references) are
 * used whenever a structure needs to refer to a record in the mft.
 *
 * A reference consists of a 48-bit index into the mft and a 16-bit sequence
 * number used to detect stale references.
 *
 * For error reporting purposes we treat the 48-bit index as a signed quantity.
 *
 * The sequence number is a circular counter (skipping 0) describing how many
 * times the referenced mft record has been (re)used. This has to match the
 * sequence number of the mft record being referenced, otherwise the reference
 * is considered stale and removed.
 *
 * If the sequence number is zero it is assumed that no sequence number
 * consistency checking should be performed.
 */

/*
 * Define two unpacking macros to get to the reference (MREF) and
 * sequence number (MSEQNO) respectively.
 * The _LE versions are to be applied on little endian MFT_REFs.
 * Note: The _LE versions will return a CPU endian formatted value!
 */
#define MFT_REF_MASK_CPU 0x0000ffffffffffffULL
#define MFT_REF_MASK_LE cpu_to_le64(MFT_REF_MASK_CPU)

#define MK_MREF(m, s)	((u64)(((u64)(s) << 48) |		\
					((u64)(m) & MFT_REF_MASK_CPU)))
#define MK_LE_MREF(m, s) cpu_to_le64(MK_MREF(m, s))

#define MREF(x)		((unsigned long)((x) & MFT_REF_MASK_CPU))
#define MSEQNO(x)	((u16)(((x) >> 48) & 0xffff))
#define MREF_LE(x)	((unsigned long)(le64_to_cpu(x) & MFT_REF_MASK_CPU))
#define MREF_INO(x)	((unsigned long)MREF_LE(x))
#define MSEQNO_LE(x)	((u16)((le64_to_cpu(x) >> 48) & 0xffff))

#define IS_ERR_MREF(x)	(((x) & 0x0000800000000000ULL) ? true : false)
#define ERR_MREF(x)	((u64)((s64)(x)))
#define MREF_ERR(x)	((int)((s64)(x)))

/*
 * struct mft_record - NTFS Master File Table (MFT) record header
 *
 * The mft record header present at the beginning of every record in the mft.
 * This is followed by a sequence of variable length attribute records which
 * is terminated by an attribute of type AT_END which is a truncated attribute
 * in that it only consists of the attribute type code AT_END and none of the
 * other members of the attribute structure are present.
 *
 * magic:               Record magic ("FILE" for valid MFT entries).
 *                      See ntfs_record magic enum for other values.
 * usa_ofs:             Offset to Update Sequence Array (see ntfs_record).
 * usa_count:           Number of entries in USA (see ntfs_record).
 * lsn:                 Log sequence number (LSN) from LogFile.
 *                      Incremented on every modification to this record.
 * sequence_number:     Reuse count of this MFT record slot.
 *                      Incremented (skipping zero) when the file is deleted.
 *                      Zero means never reused or special case.
 *                      Part of MFT reference (together with record number).
 * link_count:          Number of hard links (directory entries) to this file.
 *                      Only meaningful in base MFT records.
 *                      When deleting a directory entry:
 *                        - If link_count == 1, delete the whole file
 *                        - Else remove only the $FILE_NAME attribute and decrement
 * attrs_offset:        Byte offset from start of MFT record to first attribute.
 *                      Must be 8-byte aligned.
 * flags:               Bit array of MFT_RECORD_* flags (see MFT_RECORD_IN_USE enum).
 *                      MFT_RECORD_IN_USE cleared when record is freed/deleted.
 * bytes_in_use:        Number of bytes actually used in this MFT record.
 *                      Must be 8-byte aligned.
 *                      Includes header + all attributes + padding.
 * bytes_allocated:     Total allocated size of this MFT record.
 *                      Usually equal to MFT record size (1024 bytes or cluster size).
 * base_mft_record:     MFT reference to the base record.
 *                      0 for base records.
 *                      Non-zero for extension records → points to base record
 *                      containing the $ATTRIBUTE_LIST that describes this extension.
 * next_attr_instance:  Next attribute instance number to assign.
 *                      Incremented after each use.
 *                      Reset to 0 when MFT record is reused.
 *                      First instance is always 0.
 * reserved:            Reserved for alignment (NTFS 3.1+).
 * mft_record_number:   This MFT record's number (index in $MFT).
 *                      Only present in NTFS 3.1+ (Windows XP and above).
 */
struct mft_record {
	__le32 magic;
	__le16 usa_ofs;
	__le16 usa_count;

	__le64 lsn;
	__le16 sequence_number;
	__le16 link_count;
	__le16 attrs_offset;
	__le16 flags;
	__le32 bytes_in_use;
	__le32 bytes_allocated;
	__le64 base_mft_record;
	__le16 next_attr_instance;
	__le16 reserved;
	__le32 mft_record_number;
} __packed;

static_assert(sizeof(struct mft_record) == 48);

/**x
 * struct mft_record_old - Old NTFS MFT record header (pre-NTFS 3.1 / Windows XP)
 *
 * This is the older version of the MFT record header used in NTFS versions
 * prior to 3.1 (Windows XP and later). It lacks the additional fields
 * @reserved and @mft_record_number that were added in NTFS 3.1+.
 *
 * @magic:              Record magic ("FILE" for valid MFT entries).
 *                      See ntfs_record magic enum for other values.
 * @usa_ofs:            Offset to Update Sequence Array (see ntfs_record).
 * @usa_count:          Number of entries in USA (see ntfs_record).
 * @lsn:                Log sequence number (LSN) from LogFile.
 *                      Incremented on every modification to this record.
 * @sequence_number:    Reuse count of this MFT record slot.
 *                      Incremented (skipping zero) when the file is deleted.
 *                      Zero means never reused or special case.
 *                      Part of MFT reference (together with record number).
 * @link_count:         Number of hard links (directory entries) to this file.
 *                      Only meaningful in base MFT records.
 *                      When deleting a directory entry:
 *                        - If link_count == 1, delete the whole file
 *                        - Else remove only the $FILE_NAME attribute and decrement
 * @attrs_offset:       Byte offset from start of MFT record to first attribute.
 *                      Must be 8-byte aligned.
 * @flags:              Bit array of MFT_RECORD_* flags (see MFT_RECORD_IN_USE enum).
 *                      MFT_RECORD_IN_USE cleared when record is freed/deleted.
 * @bytes_in_use:       Number of bytes actually used in this MFT record.
 *                      Must be 8-byte aligned.
 *                      Includes header + all attributes + padding.
 * @bytes_allocated:    Total allocated size of this MFT record.
 *                      Usually equal to MFT record size (1024 bytes or cluster size).
 * @base_mft_record:    MFT reference to the base record.
 *                      0 for base records.
 *                      Non-zero for extension records → points to base record
 *                      containing the $ATTRIBUTE_LIST that describes this extension.
 * @next_attr_instance: Next attribute instance number to assign.
 *                      Incremented after each use.
 *                      Reset to 0 when MFT record is reused.
 *                      First instance is always 0.
 */
struct mft_record_old {
	__le32 magic;
	__le16 usa_ofs;
	__le16 usa_count;

	__le64 lsn;
	__le16 sequence_number;
	__le16 link_count;
	__le16 attrs_offset;
	__le16 flags;
	__le32 bytes_in_use;
	__le32 bytes_allocated;
	__le64 base_mft_record;
	__le16 next_attr_instance;
} __packed;

static_assert(sizeof(struct mft_record_old) == 42);

/*
 * System defined attributes (32-bit).  Each attribute type has a corresponding
 * attribute name (Unicode string of maximum 64 character length) as described
 * by the attribute definitions present in the data attribute of the $AttrDef
 * system file.  On NTFS 3.0 volumes the names are just as the types are named
 * in the below defines exchanging AT_ for the dollar sign ($).  If that is not
 * a revealing choice of symbol I do not know what is... (-;
 */
enum {
	AT_UNUSED			= cpu_to_le32(0),
	AT_STANDARD_INFORMATION		= cpu_to_le32(0x10),
	AT_ATTRIBUTE_LIST		= cpu_to_le32(0x20),
	AT_FILE_NAME			= cpu_to_le32(0x30),
	AT_OBJECT_ID			= cpu_to_le32(0x40),
	AT_SECURITY_DESCRIPTOR		= cpu_to_le32(0x50),
	AT_VOLUME_NAME			= cpu_to_le32(0x60),
	AT_VOLUME_INFORMATION		= cpu_to_le32(0x70),
	AT_DATA				= cpu_to_le32(0x80),
	AT_INDEX_ROOT			= cpu_to_le32(0x90),
	AT_INDEX_ALLOCATION		= cpu_to_le32(0xa0),
	AT_BITMAP			= cpu_to_le32(0xb0),
	AT_REPARSE_POINT		= cpu_to_le32(0xc0),
	AT_EA_INFORMATION		= cpu_to_le32(0xd0),
	AT_EA				= cpu_to_le32(0xe0),
	AT_PROPERTY_SET			= cpu_to_le32(0xf0),
	AT_LOGGED_UTILITY_STREAM	= cpu_to_le32(0x100),
	AT_FIRST_USER_DEFINED_ATTRIBUTE	= cpu_to_le32(0x1000),
	AT_END				= cpu_to_le32(0xffffffff)
};

/*
 * The collation rules for sorting views/indexes/etc (32-bit).
 *
 * COLLATION_BINARY - Collate by binary compare where the first byte is most
 *	significant.
 * COLLATION_UNICODE_STRING - Collate Unicode strings by comparing their binary
 *	Unicode values, except that when a character can be uppercased, the
 *	upper case value collates before the lower case one.
 * COLLATION_FILE_NAME - Collate file names as Unicode strings. The collation
 *	is done very much like COLLATION_UNICODE_STRING. In fact I have no idea
 *	what the difference is. Perhaps the difference is that file names
 *	would treat some special characters in an odd way (see
 *	unistr.c::ntfs_collate_names() and unistr.c::legal_ansi_char_array[]
 *	for what I mean but COLLATION_UNICODE_STRING would not give any special
 *	treatment to any characters at all, but this is speculation.
 * COLLATION_NTOFS_ULONG - Sorting is done according to ascending __le32 key
 *	values. E.g. used for $SII index in FILE_Secure, which sorts by
 *	security_id (le32).
 * COLLATION_NTOFS_SID - Sorting is done according to ascending SID values.
 *	E.g. used for $O index in FILE_Extend/$Quota.
 * COLLATION_NTOFS_SECURITY_HASH - Sorting is done first by ascending hash
 *	values and second by ascending security_id values. E.g. used for $SDH
 *	index in FILE_Secure.
 * COLLATION_NTOFS_ULONGS - Sorting is done according to a sequence of ascending
 *	__le32 key values. E.g. used for $O index in FILE_Extend/$ObjId, which
 *	sorts by object_id (16-byte), by splitting up the object_id in four
 *	__le32 values and using them as individual keys. E.g. take the following
 *	two security_ids, stored as follows on disk:
 *		1st: a1 61 65 b7 65 7b d4 11 9e 3d 00 e0 81 10 42 59
 *		2nd: 38 14 37 d2 d2 f3 d4 11 a5 21 c8 6b 79 b1 97 45
 *	To compare them, they are split into four __le32 values each, like so:
 *		1st: 0xb76561a1 0x11d47b65 0xe0003d9e 0x59421081
 *		2nd: 0xd2371438 0x11d4f3d2 0x6bc821a5 0x4597b179
 *	Now, it is apparent why the 2nd object_id collates after the 1st: the
 *	first __le32 value of the 1st object_id is less than the first __le32 of
 *	the 2nd object_id. If the first __le32 values of both object_ids were
 *	equal then the second __le32 values would be compared, etc.
 */
enum {
	COLLATION_BINARY		= cpu_to_le32(0x00),
	COLLATION_FILE_NAME		= cpu_to_le32(0x01),
	COLLATION_UNICODE_STRING	= cpu_to_le32(0x02),
	COLLATION_NTOFS_ULONG		= cpu_to_le32(0x10),
	COLLATION_NTOFS_SID		= cpu_to_le32(0x11),
	COLLATION_NTOFS_SECURITY_HASH	= cpu_to_le32(0x12),
	COLLATION_NTOFS_ULONGS		= cpu_to_le32(0x13),
};

/*
 * enum - Attribute definition flags
 *
 * The flags (32-bit) describing attribute properties in the attribute
 * definition structure.
 * The INDEXABLE flag is fairly certainly correct as only the file
 * name attribute has this flag set and this is the only attribute indexed in
 * NT4.
 *
 * ATTR_DEF_INDEXABLE:      Attribute can be indexed.
 *                          (Used for creating indexes like $I30, $SDH, etc.)
 * ATTR_DEF_MULTIPLE:       Attribute type can be present multiple times
 *                          in the MFT record of an inode.
 *                          (e.g., multiple $FILE_NAME, $DATA streams)
 * ATTR_DEF_NOT_ZERO:       Attribute value must contain at least one non-zero byte.
 *                          (Prevents empty or all-zero values)
 * ATTR_DEF_INDEXED_UNIQUE: Attribute must be indexed and the value must be unique
 *                          for this attribute type across all MFT records of an inode.
 *                          (e.g., security descriptor IDs in $Secure)
 * ATTR_DEF_NAMED_UNIQUE:   Attribute must be named and the name must be unique
 *                          for this attribute type across all MFT records of an inode.
 *                          (e.g., named $DATA streams or alternate data streams)
 * ATTR_DEF_RESIDENT:       Attribute must be resident (stored in MFT record).
 *                          (Cannot be non-resident/sparse/compressed)
 * ATTR_DEF_ALWAYS_LOG:     Always log modifications to this attribute in LogFile,
 *                          regardless of whether it is resident or non-resident.
 *                          Without this flag, modifications are logged only if resident.
 *                          (Used for critical metadata attributes)
 */
enum {
	ATTR_DEF_INDEXABLE	= cpu_to_le32(0x02),
	ATTR_DEF_MULTIPLE	= cpu_to_le32(0x04),
	ATTR_DEF_NOT_ZERO	= cpu_to_le32(0x08),
	ATTR_DEF_INDEXED_UNIQUE	= cpu_to_le32(0x10),
	ATTR_DEF_NAMED_UNIQUE	= cpu_to_le32(0x20),
	ATTR_DEF_RESIDENT	= cpu_to_le32(0x40),
	ATTR_DEF_ALWAYS_LOG	= cpu_to_le32(0x80),
};

/*
 * struct attr_def - Attribute definition entry ($AttrDef array)
 *
 * The data attribute of FILE_AttrDef contains a sequence of attribute
 * definitions for the NTFS volume. With this, it is supposed to be safe for an
 * older NTFS driver to mount a volume containing a newer NTFS version without
 * damaging it (that's the theory. In practice it's: not damaging it too much).
 * Entries are sorted by attribute type. The flags describe whether the
 * attribute can be resident/non-resident and possibly other things, but the
 * actual bits are unknown.
 *
 * @name:           Unicode (UTF-16LE) name of the attribute (e.g. "$DATA", "$FILE_NAME").
 *                  Zero-terminated string, maximum 0x40 characters (128 bytes).
 *                  Used for human-readable display and debugging.
 * @type:           Attribute type code (ATTR_TYPE_* constants).
 *                  Defines which attribute this entry describes.
 * @display_rule:   Default display rule (usually 0; rarely used in modern NTFS).
 *                  Controls how the attribute is displayed in tools (legacy).
 * @collation_rule: Default collation rule for indexing this attribute.
 *                  Determines sort order when indexed (e.g. CASE_SENSITIVE, UNICODE).
 *                  Used in $I30, $SDH, $SII, etc.
 * @flags:          Bit array of attribute constraints (ATTR_DEF_* flags).
 *                  See ATTR_DEF_INDEXABLE, ATTR_DEF_MULTIPLE, etc.
 *                  Defines whether the attribute can be indexed, multiple, resident-only, etc.
 * @min_size:       Optional minimum size of the attribute value (in bytes).
 *                  0 means no minimum enforced.
 * @max_size:       Maximum allowed size of the attribute value (in bytes).
 */
struct attr_def {
	__le16 name[0x40];
	__le32 type;
	__le32 display_rule;
	__le32 collation_rule;
	__le32 flags;
	__le64 min_size;
	__le64 max_size;
} __packed;

static_assert(sizeof(struct attr_def) == 160);

/*
 * enum - Attribute flags (16-bit) for non-resident attributes
 *
 * ATTR_IS_COMPRESSED:      Attribute is compressed.
 *                          If set, data is compressed using the method in
 *                          ATTR_COMPRESSION_MASK.
 * ATTR_COMPRESSION_MASK:   Mask for compression method.
 *                          Valid values are defined in NTFS compression types
 *                          (e.g., 0x02 = LZNT1, etc.).
 *                          Also serves as the first illegal value for method.
 * ATTR_IS_ENCRYPTED:       Attribute is encrypted.
 *                          Data is encrypted using EFS (Encrypting File System).
 * ATTR_IS_SPARSE:          Attribute is sparse.
 *                          Contains holes (unallocated regions) that read as zeros.
 */
enum {
	ATTR_IS_COMPRESSED    = cpu_to_le16(0x0001),
	ATTR_COMPRESSION_MASK = cpu_to_le16(0x00ff),
	ATTR_IS_ENCRYPTED     = cpu_to_le16(0x4000),
	ATTR_IS_SPARSE	      = cpu_to_le16(0x8000),
} __packed;

/*
 * Attribute compression.
 *
 * Only the data attribute is ever compressed in the current ntfs driver in
 * Windows. Further, compression is only applied when the data attribute is
 * non-resident. Finally, to use compression, the maximum allowed cluster size
 * on a volume is 4kib.
 *
 * The compression method is based on independently compressing blocks of X
 * clusters, where X is determined from the compression_unit value found in the
 * non-resident attribute record header (more precisely: X = 2^compression_unit
 * clusters). On Windows NT/2k, X always is 16 clusters (compression_unit = 4).
 *
 * There are three different cases of how a compression block of X clusters
 * can be stored:
 *
 *   1) The data in the block is all zero (a sparse block):
 *	  This is stored as a sparse block in the runlist, i.e. the runlist
 *	  entry has length = X and lcn = -1. The mapping pairs array actually
 *	  uses a delta_lcn value length of 0, i.e. delta_lcn is not present at
 *	  all, which is then interpreted by the driver as lcn = -1.
 *	  NOTE: Even uncompressed files can be sparse on NTFS 3.0 volumes, then
 *	  the same principles apply as above, except that the length is not
 *	  restricted to being any particular value.
 *
 *   2) The data in the block is not compressed:
 *	  This happens when compression doesn't reduce the size of the block
 *	  in clusters. I.e. if compression has a small effect so that the
 *	  compressed data still occupies X clusters, then the uncompressed data
 *	  is stored in the block.
 *	  This case is recognised by the fact that the runlist entry has
 *	  length = X and lcn >= 0. The mapping pairs array stores this as
 *	  normal with a run length of X and some specific delta_lcn, i.e.
 *	  delta_lcn has to be present.
 *
 *   3) The data in the block is compressed:
 *	  The common case. This case is recognised by the fact that the run
 *	  list entry has length L < X and lcn >= 0. The mapping pairs array
 *	  stores this as normal with a run length of X and some specific
 *	  delta_lcn, i.e. delta_lcn has to be present. This runlist entry is
 *	  immediately followed by a sparse entry with length = X - L and
 *	  lcn = -1. The latter entry is to make up the vcn counting to the
 *	  full compression block size X.
 *
 * In fact, life is more complicated because adjacent entries of the same type
 * can be coalesced. This means that one has to keep track of the number of
 * clusters handled and work on a basis of X clusters at a time being one
 * block. An example: if length L > X this means that this particular runlist
 * entry contains a block of length X and part of one or more blocks of length
 * L - X. Another example: if length L < X, this does not necessarily mean that
 * the block is compressed as it might be that the lcn changes inside the block
 * and hence the following runlist entry describes the continuation of the
 * potentially compressed block. The block would be compressed if the
 * following runlist entry describes at least X - L sparse clusters, thus
 * making up the compression block length as described in point 3 above. (Of
 * course, there can be several runlist entries with small lengths so that the
 * sparse entry does not follow the first data containing entry with
 * length < X.)
 *
 * NOTE: At the end of the compressed attribute value, there most likely is not
 * just the right amount of data to make up a compression block, thus this data
 * is not even attempted to be compressed. It is just stored as is, unless
 * the number of clusters it occupies is reduced when compressed in which case
 * it is stored as a compressed compression block, complete with sparse
 * clusters at the end.
 */

/*
 * enum - Flags for resident attributes (8-bit)
 *
 * RESIDENT_ATTR_IS_INDEXED: Attribute is referenced in an index.
 *                           (e.g., part of an index key or entry)
 *                           Has implications for deletion and modification:
 *                            - Cannot be freely removed if indexed
 *                            - Index must be updated when value changes
 *                            - Used for attributes like $FILE_NAME in directories
 */
enum {
	RESIDENT_ATTR_IS_INDEXED = 0x01,
} __packed;

/*
 * struct attr_record - NTFS attribute record header
 *
 * Common header for both resident and non-resident attributes.
 * Always aligned to an 8-byte boundary on disk.
 * Located at attrs_offset in the MFT record (see struct mft_record).
 *
 * @type:           32-bit attribute type (ATTR_TYPE_* constants).
 *                  Identifies the attribute
 *                  (e.g. 0x10 = $STANDARD_INFORMATION).
 * @length:         Total byte size of this attribute record (resident).
 *                  8-byte aligned; used to locate the next attribute.
 * @non_resident:   0 = resident attribute
 *                  1 = non-resident attribute
 * @name_length:    Number of Unicode characters in the attribute name.
 *                  0 if unnamed (most system attributes are unnamed).
 * @name_offset:    Byte offset from start of attribute record to the name.
 *                  8-byte aligned; when creating, place at end of header.
 * @flags:          Attribute flags (see ATTR_IS_COMPRESSED,
 *                  ATTR_IS_ENCRYPTED, etc.).
 *                  For resident: see RESIDENT_ATTR_* flags.
 * @instance:       Unique instance number within this MFT record.
 *                  Incremented via next_attr_instance; unique per record.
 *
 * Resident attributes (when @non_resident == 0):
 * @data.resident.value_length:     Byte size of the attribute value.
 * @data.resident.value_offset:     Byte offset from start of attribute
 *                                  record to the value data.
 *                                  8-byte aligned if name present.
 * @data.resident.flags:            Resident-specific flags
 * @data.resident.reserved:         Reserved/alignment to 8 bytes.
 *
 * Non-resident attributes (when @non_resident == 1):
 * @data.non_resident.lowest_vcn:   Lowest valid VCN in this extent.
 *                                  Usually 0 unless attribute list is used.
 * @data.non_resident.highest_vcn:  Highest valid VCN in this extent.
 *                                  -1 for zero-length, 0 for single extent.
 * @data.non_resident.mapping_pairs_offset:
 *                                  Byte offset to mapping pairs array
 *                                  (VCN → LCN mappings).
 *                                  8-byte aligned when creating.
 * @data.non_resident.compression_unit:
 *                                  Log2 of clusters per compression unit.
 *                                  0 = not compressed.
 *                                  WinNT4 used 4; sparse files use 0
 *                                  on XP SP2+.
 * @data.non_resident.reserved:     5 bytes for 8-byte alignment.
 * @data.non_resident.allocated_size:
 *                                  Allocated disk space in bytes.
 *                                  For compressed: logical allocated size.
 * @data.non_resident.data_size:    Logical attribute value size in bytes.
 *                                  Can be larger than allocated_size if
 *                                  compressed/sparse.
 * @data.non_resident.initialized_size:
 *                                  Initialized portion size in bytes.
 *                                  Usually equals data_size.
 * @data.non_resident.compressed_size:
 *                                  Compressed on-disk size in bytes.
 *                                  Only present when compressed or sparse.
 *                                  Actual disk usage.
 */
struct attr_record {
	__le32 type;
	__le32 length;
	u8 non_resident;
	u8 name_length;
	__le16 name_offset;
	__le16 flags;
	__le16 instance;
	union {
		struct {
			__le32 value_length;
			__le16 value_offset;
			u8 flags;
			s8 reserved;
		} __packed resident;
		struct {
			__le64 lowest_vcn;
			__le64 highest_vcn;
			__le16 mapping_pairs_offset;
			u8 compression_unit;
			u8 reserved[5];
			__le64 allocated_size;
			__le64 data_size;
			__le64 initialized_size;
			__le64 compressed_size;
		} __packed non_resident;
	} __packed data;
} __packed;

/*
 * enum - NTFS file attribute flags (32-bit)
 *
 * File attribute flags (32-bit) appearing in the file_attributes fields of the
 * STANDARD_INFORMATION attribute of MFT_RECORDs and the FILENAME_ATTR
 * attributes of MFT_RECORDs and directory index entries.
 *
 * All of the below flags appear in the directory index entries but only some
 * appear in the STANDARD_INFORMATION attribute whilst only some others appear
 * in the FILENAME_ATTR attribute of MFT_RECORDs.  Unless otherwise stated the
 * flags appear in all of the above.
 *
 * FILE_ATTR_READONLY:         File is read-only.
 * FILE_ATTR_HIDDEN:           File is hidden (not shown by default).
 * FILE_ATTR_SYSTEM:           System file (protected by OS).
 * FILE_ATTR_DIRECTORY:        Directory flag (reserved in NT; use MFT flag instead).
 * FILE_ATTR_ARCHIVE:          File needs archiving (backup flag).
 * FILE_ATTR_DEVICE:           Device file (rarely used).
 * FILE_ATTR_NORMAL:           Normal file (no special attributes).
 * FILE_ATTR_TEMPORARY:        Temporary file (delete on close).
 * FILE_ATTR_SPARSE_FILE:      Sparse file (contains holes).
 * FILE_ATTR_REPARSE_POINT:    Reparse point (junction, symlink, mount point).
 * FILE_ATTR_COMPRESSED:       File is compressed.
 * FILE_ATTR_OFFLINE:          File data is offline (not locally available).
 * FILE_ATTR_NOT_CONTENT_INDEXED:
 *                              File is excluded from content indexing.
 * FILE_ATTR_ENCRYPTED:        File is encrypted (EFS).
 * FILE_ATTR_VALID_FLAGS:      Mask of all valid flags for reading.
 * FILE_ATTR_VALID_SET_FLAGS:  Mask of flags that can be set by user.
 * FILE_ATTRIBUTE_RECALL_ON_OPEN:
 *                              Recall data on open (cloud/HSM related).
 * FILE_ATTR_DUP_FILE_NAME_INDEX_PRESENT:
 *                              $FILE_NAME has duplicate index entry.
 * FILE_ATTR_DUP_VIEW_INDEX_PRESENT:
 *                              Duplicate view index present (object ID, quota, etc.).
 */
enum {
	FILE_ATTR_READONLY		= cpu_to_le32(0x00000001),
	FILE_ATTR_HIDDEN		= cpu_to_le32(0x00000002),
	FILE_ATTR_SYSTEM		= cpu_to_le32(0x00000004),
	/* Old DOS volid. Unused in NT.	= cpu_to_le32(0x00000008), */
	FILE_ATTR_DIRECTORY		= cpu_to_le32(0x00000010),
	FILE_ATTR_ARCHIVE		= cpu_to_le32(0x00000020),
	FILE_ATTR_DEVICE		= cpu_to_le32(0x00000040),
	FILE_ATTR_NORMAL		= cpu_to_le32(0x00000080),

	FILE_ATTR_TEMPORARY		= cpu_to_le32(0x00000100),
	FILE_ATTR_SPARSE_FILE		= cpu_to_le32(0x00000200),
	FILE_ATTR_REPARSE_POINT		= cpu_to_le32(0x00000400),
	FILE_ATTR_COMPRESSED		= cpu_to_le32(0x00000800),

	FILE_ATTR_OFFLINE		= cpu_to_le32(0x00001000),
	FILE_ATTR_NOT_CONTENT_INDEXED	= cpu_to_le32(0x00002000),
	FILE_ATTR_ENCRYPTED		= cpu_to_le32(0x00004000),

	FILE_ATTR_VALID_FLAGS		= cpu_to_le32(0x00007fb7),
	FILE_ATTR_VALID_SET_FLAGS	= cpu_to_le32(0x000031a7),
	FILE_ATTRIBUTE_RECALL_ON_OPEN	= cpu_to_le32(0x00040000),
	FILE_ATTR_DUP_FILE_NAME_INDEX_PRESENT	= cpu_to_le32(0x10000000),
	FILE_ATTR_DUP_VIEW_INDEX_PRESENT	= cpu_to_le32(0x20000000),
};

/*
 * NOTE on times in NTFS: All times are in MS standard time format, i.e. they
 * are the number of 100-nanosecond intervals since 1st January 1601, 00:00:00
 * universal coordinated time (UTC). (In Linux time starts 1st January 1970,
 * 00:00:00 UTC and is stored as the number of 1-second intervals since then.)
 */

/*
 * struct standard_information - $STANDARD_INFORMATION attribute content
 *
 * NOTE: Always resident.
 * NOTE: Present in all base file records on a volume.
 * NOTE: There is conflicting information about the meaning of each of the time
 *	 fields but the meaning as defined below has been verified to be
 *	 correct by practical experimentation on Windows NT4 SP6a and is hence
 *	 assumed to be the one and only correct interpretation.
 *
 * @creation_time:          File creation time (NTFS timestamp).
 *                          Updated on filename change(?).
 * @last_data_change_time:  Last modification time of data streams.
 * @last_mft_change_time:   Last modification time of this MFT record.
 * @last_access_time:       Last access time (approximate).
 *                          Not updated on read-only volumes; can be disabled.
 * @file_attributes:        File attribute flags (FILE_ATTR_* bits).
 *
 * Union (version-specific fields):
 * @ver.v1.reserved12:      12 bytes reserved/alignment (NTFS 1.2 only).
 *
 * @ver.v3 (NTFS 3.x / Windows 2000+):
 * @maximum_versions:       Max allowed file versions (0 = disabled).
 * @version_number:         Current version number (0 if disabled).
 * @class_id:               Class ID (from bidirectional index?).
 * @owner_id:               Owner ID (maps to $Quota via $Q index).
 * @security_id:            Security ID (maps to $Secure $SII/$SDS).
 * @quota_charged:          Quota charge in bytes (0 if quotas disabled).
 * @usn:                    Last USN from $UsnJrnl (0 if disabled).
 */
struct standard_information {
	__le64 creation_time;
	__le64 last_data_change_time;
	__le64 last_mft_change_time;
	__le64 last_access_time;
	__le32 file_attributes;
	union {
		struct {
			u8 reserved12[12];
		} __packed v1;
		struct {
			__le32 maximum_versions;
			__le32 version_number;
			__le32 class_id;
			__le32 owner_id;
			__le32 security_id;
			__le64 quota_charged;
			__le64 usn;
		} __packed v3;
	} __packed ver;
} __packed;

/*
 * struct attr_list_entry - Entry in $ATTRIBUTE_LIST attribute.
 *
 * @type:           Attribute type code (ATTR_TYPE_*).
 * @length:         Byte size of this entry (8-byte aligned).
 * @name_length:    Unicode char count of attribute name (0 if unnamed).
 * @name_offset:    Byte offset from start of entry to name (always set).
 * @lowest_vcn:     Lowest VCN of this attribute extent (usually 0).
 *                  Signed value; non-zero when attribute spans extents.
 * @mft_reference:  MFT record reference holding this attribute extent.
 * @instance:       Attribute instance number (if lowest_vcn == 0); else 0.
 * @name:           Variable Unicode name (use @name_offset when reading).
 *
 * - Can be either resident or non-resident.
 * - Value consists of a sequence of variable length, 8-byte aligned,
 * ATTR_LIST_ENTRY records.
 * - The list is not terminated by anything at all! The only way to know when
 * the end is reached is to keep track of the current offset and compare it to
 * the attribute value size.
 * - The attribute list attribute contains one entry for each attribute of
 * the file in which the list is located, except for the list attribute
 * itself. The list is sorted: first by attribute type, second by attribute
 * name (if present), third by instance number. The extents of one
 * non-resident attribute (if present) immediately follow after the initial
 * extent. They are ordered by lowest_vcn and have their instance set to zero.
 * It is not allowed to have two attributes with all sorting keys equal.
 * - Further restrictions:
 *	- If not resident, the vcn to lcn mapping array has to fit inside the
 *	  base mft record.
 *	- The attribute list attribute value has a maximum size of 256kb. This
 *	  is imposed by the Windows cache manager.
 * - Attribute lists are only used when the attributes of mft record do not
 * fit inside the mft record despite all attributes (that can be made
 * non-resident) having been made non-resident. This can happen e.g. when:
 *	- File has a large number of hard links (lots of file name
 *	  attributes present).
 *	- The mapping pairs array of some non-resident attribute becomes so
 *	  large due to fragmentation that it overflows the mft record.
 *	- The security descriptor is very complex (not applicable to
 *	  NTFS 3.0 volumes).
 *	- There are many named streams.
 */
struct attr_list_entry {
	__le32 type;
	__le16 length;
	u8 name_length;
	u8 name_offset;
	__le64 lowest_vcn;
	__le64 mft_reference;
	__le16 instance;
	__le16 name[];
} __packed;

/*
 * The maximum allowed length for a file name.
 */
#define MAXIMUM_FILE_NAME_LENGTH	255

/*
 * enum - Possible namespaces for filenames in ntfs (8-bit).
 *
 * FILE_NAME_POSIX        POSIX namespace (case sensitive, most permissive).
 *                        Allows all Unicode except '\0' and '/'.
 *                        WinNT/2k/2003 default utilities ignore case
 *                        differences. SFU (Services For Unix) enables true
 *                        case sensitivity.
 *                        SFU restricts some chars: '"', '/', '<', '>', '\'.
 * FILE_NAME_WIN32        Standard WinNT/2k long filename namespace
 *                        (case insensitive).
 *                        Disallows '\0', '"', '*', '/', ':', '<', '>', '?',
 *                        '\', '|'. Names cannot end with '.' or space.
 * FILE_NAME_DOS          DOS 8.3 namespace (uppercase only).
 *                        Allows 8-bit chars > space except '"', '*', '+',
 *                        ',', '/', ':', ';', '<', '=', '>', '?', '\'.
 * FILE_NAME_WIN32_AND_DOS
 *                        Win32 and DOS names are identical (single record).
 *                        Value 0x03 indicates both are stored in one entry.
 */
enum {
	FILE_NAME_POSIX		= 0x00,
	FILE_NAME_WIN32		= 0x01,
	FILE_NAME_DOS		= 0x02,
	FILE_NAME_WIN32_AND_DOS	= 0x03,
} __packed;

/*
 * struct file_name_attr - $FILE_NAME attribute content
 *
 * NOTE: Always resident.
 * NOTE: All fields, except the parent_directory, are only updated when the
 *	 filename is changed. Until then, they just become out of sync with
 *	 reality and the more up to date values are present in the standard
 *	 information attribute.
 * NOTE: There is conflicting information about the meaning of each of the time
 *	 fields but the meaning as defined below has been verified to be
 *	 correct by practical experimentation on Windows NT4 SP6a and is hence
 *	 assumed to be the one and only correct interpretation.
 *
 * @parent_directory:   MFT reference to parent directory.
 * @creation_time:      File creation time (NTFS timestamp).
 * @last_data_change_time:
 *                      Last data modification time.
 * @last_mft_change_time:
 *                      Last MFT record modification time.
 * @last_access_time:   Last access time (approximate; may not
 *                      update always).
 * @allocated_size:     On-disk allocated size for unnamed $DATA.
 *                      Equals compressed_size if compressed/sparse.
 *                      0 for directories or no $DATA.
 *                      Multiple of cluster size.
 * @data_size:          Logical size of unnamed $DATA.
 *                      0 for directories or no $DATA.
 * @file_attributes:    File attribute flags (FILE_ATTR_* bits).
 * @type.ea.packed_ea_size:
 *                      Size needed to pack EAs (if present).
 * @type.ea.reserved:   Alignment padding.
 * @type.rp.reparse_point_tag:
 *                      Reparse point type (if reparse point, no EAs).
 * @file_name_length:   Length of filename in Unicode characters.
 * @file_name_type:     Namespace (FILE_NAME_POSIX, WIN32, DOS, etc.).
 * @file_name:          Variable-length Unicode filename.
 */
struct file_name_attr {
	__le64 parent_directory;
	__le64 creation_time;
	__le64 last_data_change_time;
	__le64 last_mft_change_time;
	__le64 last_access_time;
	__le64 allocated_size;
	__le64 data_size;
	__le32 file_attributes;
	union {
		struct {
			__le16 packed_ea_size;
			__le16 reserved;
		} __packed ea;
		struct {
			__le32 reparse_point_tag;
		} __packed rp;
	} __packed type;
	u8 file_name_length;
	u8 file_name_type;
	__le16 file_name[];
} __packed;

/*
 * struct guid - Globally Unique Identifier (GUID) structure
 *
 * GUID structures store globally unique identifiers (GUID). A GUID is a
 * 128-bit value consisting of one group of eight hexadecimal digits, followed
 * by three groups of four hexadecimal digits each, followed by one group of
 * twelve hexadecimal digits. GUIDs are Microsoft's implementation of the
 * distributed computing environment (DCE) universally unique identifier (UUID).
 * Example of a GUID:
 *	1F010768-5A73-BC91-0010A52216A7
 *
 * @data1:      First 32 bits (first 8 hex digits).
 * @data2:      Next 16 bits (first group of 4 hex digits).
 * @data3:      Next 16 bits (second group of 4 hex digits).
 * @data4:      Final 64 bits (third group of 4 + last 12 hex digits).
 *              data4[0-1]: third group; data4[2-7]: remaining part.
 */
struct guid {
	__le32 data1;
	__le16 data2;
	__le16 data3;
	u8 data4[8];
} __packed;

/*
 * struct object_id_attr - $OBJECT_ID attribute content (NTFS 3.0+)
 *
 * NOTE: Always resident.
 *
 * @object_id:          Unique 128-bit GUID assigned to the file.
 *                      Core identifier; always present.
 *
 * Optional extended info (union; total value size 16–64 bytes):
 * @extended_info.birth_volume_id:
 *                      Birth volume GUID (where file was first created).
 * @extended_info.birth_object_id:
 *                      Birth object GUID (original ID before copy/move).
 * @extended_info.domain_id:
 *                      Domain GUID (usually zero; reserved).
 */
struct object_id_attr {
	struct guid object_id;
	union {
		struct {
			struct guid birth_volume_id;
			struct guid birth_object_id;
			struct guid domain_id;
		} __packed;
		u8 extended_info[48];
	} __packed;
} __packed;

/*
 * enum - RIDs (Relative Identifiers) in Windows/NTFS security
 *
 * These relative identifiers (RIDs) are used with the above identifier
 * authorities to make up universal well-known SIDs.
 *
 * SECURITY_NULL_RID              S-1-0 (Null authority)
 * SECURITY_WORLD_RID             S-1-1 (World/Everyone)
 * SECURITY_LOCAL_RID             S-1-2 (Local)
 * SECURITY_CREATOR_OWNER_RID     S-1-3-0 (Creator Owner)
 * SECURITY_CREATOR_GROUP_RID     S-1-3-1 (Creator Group)
 * SECURITY_CREATOR_OWNER_SERVER_RID S-1-3-2 (Server Creator Owner)
 * SECURITY_CREATOR_GROUP_SERVER_RID S-1-3-3 (Server Creator Group)
 * SECURITY_DIALUP_RID            S-1-5-1 (Dialup)
 * SECURITY_NETWORK_RID           S-1-5-2 (Network)
 * SECURITY_BATCH_RID             S-1-5-3 (Batch)
 * SECURITY_INTERACTIVE_RID       S-1-5-4 (Interactive)
 * SECURITY_SERVICE_RID           S-1-5-6 (Service)
 * SECURITY_ANONYMOUS_LOGON_RID   S-1-5-7 (Anonymous Logon)
 * SECURITY_PROXY_RID             S-1-5-8 (Proxy)
 * SECURITY_ENTERPRISE_CONTROLLERS_RID S-1-5-9 (Enterprise DCs)
 * SECURITY_SERVER_LOGON_RID      S-1-5-9 (Server Logon alias)
 * SECURITY_PRINCIPAL_SELF_RID    S-1-5-10 (Self/PrincipalSelf)
 * SECURITY_AUTHENTICATED_USER_RID S-1-5-11 (Authenticated Users)
 * SECURITY_RESTRICTED_CODE_RID   S-1-5-12 (Restricted Code)
 * SECURITY_TERMINAL_SERVER_RID   S-1-5-13 (Terminal Server)
 * SECURITY_LOGON_IDS_RID         S-1-5-5 (Logon session IDs base)
 * SECURITY_LOCAL_SYSTEM_RID      S-1-5-18 (Local System)
 * SECURITY_NT_NON_UNIQUE         S-1-5-21 (NT non-unique authority)
 * SECURITY_BUILTIN_DOMAIN_RID    S-1-5-32 (Built-in domain)
 *
 * Built-in domain relative RIDs (S-1-5-32-...):
 * Users:
 * DOMAIN_USER_RID_ADMIN          Administrator
 * DOMAIN_USER_RID_GUEST          Guest
 * DOMAIN_USER_RID_KRBTGT         krbtgt (Kerberos ticket-granting)
 *
 * Groups:
 * DOMAIN_GROUP_RID_ADMINS        Administrators
 * DOMAIN_GROUP_RID_USERS         Users
 * DOMAIN_GROUP_RID_GUESTS        Guests
 * DOMAIN_GROUP_RID_COMPUTERS     Computers
 * DOMAIN_GROUP_RID_CONTROLLERS   Domain Controllers
 * DOMAIN_GROUP_RID_CERT_ADMINS   Cert Publishers
 * DOMAIN_GROUP_RID_SCHEMA_ADMINS Schema Admins
 * DOMAIN_GROUP_RID_ENTERPRISE_ADMINS Enterprise Admins
 * DOMAIN_GROUP_RID_POLICY_ADMINS Policy Admins (if present)
 *
 * Aliases:
 * DOMAIN_ALIAS_RID_ADMINS        Administrators alias
 * DOMAIN_ALIAS_RID_USERS         Users alias
 * DOMAIN_ALIAS_RID_GUESTS        Guests alias
 * DOMAIN_ALIAS_RID_POWER_USERS   Power Users
 * DOMAIN_ALIAS_RID_ACCOUNT_OPS   Account Operators
 * DOMAIN_ALIAS_RID_SYSTEM_OPS    Server Operators
 * DOMAIN_ALIAS_RID_PRINT_OPS     Print Operators
 * DOMAIN_ALIAS_RID_BACKUP_OPS    Backup Operators
 * DOMAIN_ALIAS_RID_REPLICATOR    Replicator
 * DOMAIN_ALIAS_RID_RAS_SERVERS   RAS Servers
 * DOMAIN_ALIAS_RID_PREW2KCOMPACCESS Pre-Windows 2000 Compatible Access
 *
 * Note: The relative identifier (RID) refers to the portion of a SID, which
 * identifies a user or group in relation to the authority that issued the SID.
 * For example, the universal well-known SID Creator Owner ID (S-1-3-0) is
 * made up of the identifier authority SECURITY_CREATOR_SID_AUTHORITY (3) and
 * the relative identifier SECURITY_CREATOR_OWNER_RID (0).
 */
enum {					/* Identifier authority. */
	SECURITY_NULL_RID			= 0,	/* S-1-0 */
	SECURITY_WORLD_RID			= 0,	/* S-1-1 */
	SECURITY_LOCAL_RID			= 0,	/* S-1-2 */

	SECURITY_CREATOR_OWNER_RID		= 0,	/* S-1-3 */
	SECURITY_CREATOR_GROUP_RID		= 1,	/* S-1-3 */

	SECURITY_CREATOR_OWNER_SERVER_RID	= 2,	/* S-1-3 */
	SECURITY_CREATOR_GROUP_SERVER_RID	= 3,	/* S-1-3 */

	SECURITY_DIALUP_RID			= 1,
	SECURITY_NETWORK_RID			= 2,
	SECURITY_BATCH_RID			= 3,
	SECURITY_INTERACTIVE_RID		= 4,
	SECURITY_SERVICE_RID			= 6,
	SECURITY_ANONYMOUS_LOGON_RID		= 7,
	SECURITY_PROXY_RID			= 8,
	SECURITY_ENTERPRISE_CONTROLLERS_RID	= 9,
	SECURITY_SERVER_LOGON_RID		= 9,
	SECURITY_PRINCIPAL_SELF_RID		= 0xa,
	SECURITY_AUTHENTICATED_USER_RID		= 0xb,
	SECURITY_RESTRICTED_CODE_RID		= 0xc,
	SECURITY_TERMINAL_SERVER_RID		= 0xd,

	SECURITY_LOGON_IDS_RID			= 5,
	SECURITY_LOGON_IDS_RID_COUNT		= 3,

	SECURITY_LOCAL_SYSTEM_RID		= 0x12,

	SECURITY_NT_NON_UNIQUE			= 0x15,

	SECURITY_BUILTIN_DOMAIN_RID		= 0x20,

	/*
	 * Well-known domain relative sub-authority values (RIDs).
	 */

	/* Users. */
	DOMAIN_USER_RID_ADMIN			= 0x1f4,
	DOMAIN_USER_RID_GUEST			= 0x1f5,
	DOMAIN_USER_RID_KRBTGT			= 0x1f6,

	/* Groups. */
	DOMAIN_GROUP_RID_ADMINS			= 0x200,
	DOMAIN_GROUP_RID_USERS			= 0x201,
	DOMAIN_GROUP_RID_GUESTS			= 0x202,
	DOMAIN_GROUP_RID_COMPUTERS		= 0x203,
	DOMAIN_GROUP_RID_CONTROLLERS		= 0x204,
	DOMAIN_GROUP_RID_CERT_ADMINS		= 0x205,
	DOMAIN_GROUP_RID_SCHEMA_ADMINS		= 0x206,
	DOMAIN_GROUP_RID_ENTERPRISE_ADMINS	= 0x207,
	DOMAIN_GROUP_RID_POLICY_ADMINS		= 0x208,

	/* Aliases. */
	DOMAIN_ALIAS_RID_ADMINS			= 0x220,
	DOMAIN_ALIAS_RID_USERS			= 0x221,
	DOMAIN_ALIAS_RID_GUESTS			= 0x222,
	DOMAIN_ALIAS_RID_POWER_USERS		= 0x223,

	DOMAIN_ALIAS_RID_ACCOUNT_OPS		= 0x224,
	DOMAIN_ALIAS_RID_SYSTEM_OPS		= 0x225,
	DOMAIN_ALIAS_RID_PRINT_OPS		= 0x226,
	DOMAIN_ALIAS_RID_BACKUP_OPS		= 0x227,

	DOMAIN_ALIAS_RID_REPLICATOR		= 0x228,
	DOMAIN_ALIAS_RID_RAS_SERVERS		= 0x229,
	DOMAIN_ALIAS_RID_PREW2KCOMPACCESS	= 0x22a,
};

/*
 * The universal well-known SIDs:
 *
 *	NULL_SID			S-1-0-0
 *	WORLD_SID			S-1-1-0
 *	LOCAL_SID			S-1-2-0
 *	CREATOR_OWNER_SID		S-1-3-0
 *	CREATOR_GROUP_SID		S-1-3-1
 *	CREATOR_OWNER_SERVER_SID	S-1-3-2
 *	CREATOR_GROUP_SERVER_SID	S-1-3-3
 *
 *	(Non-unique IDs)		S-1-4
 *
 * NT well-known SIDs:
 *
 *	NT_AUTHORITY_SID	S-1-5
 *	DIALUP_SID		S-1-5-1
 *
 *	NETWORD_SID		S-1-5-2
 *	BATCH_SID		S-1-5-3
 *	INTERACTIVE_SID		S-1-5-4
 *	SERVICE_SID		S-1-5-6
 *	ANONYMOUS_LOGON_SID	S-1-5-7		(aka null logon session)
 *	PROXY_SID		S-1-5-8
 *	SERVER_LOGON_SID	S-1-5-9		(aka domain controller account)
 *	SELF_SID		S-1-5-10	(self RID)
 *	AUTHENTICATED_USER_SID	S-1-5-11
 *	RESTRICTED_CODE_SID	S-1-5-12	(running restricted code)
 *	TERMINAL_SERVER_SID	S-1-5-13	(running on terminal server)
 *
 *	(Logon IDs)		S-1-5-5-X-Y
 *
 *	(NT non-unique IDs)	S-1-5-0x15-...
 *
 *	(Built-in domain)	S-1-5-0x20
 */

/*
 * struct ntfs_sid - Security Identifier (SID) structure
 *
 * @revision:            SID revision level (usually 1).
 * @sub_authority_count: Number of sub-authorities (1 or more).
 * @identifier_authority:
 *                       48-bit identifier authority (S-1-x-...).
 *                       @parts.high_part: high 16 bits.
 *                       @parts.low_part: low 32 bits.
 *                       @value: raw 6-byte array.
 * @sub_authority:       Variable array of 32-bit RIDs.
 *                       At least one; defines the SID relative to authority.
 *
 * The SID structure is a variable-length structure used to uniquely identify
 * users or groups. SID stands for security identifier.
 *
 * The standard textual representation of the SID is of the form:
 *	S-R-I-S-S...
 * Where:
 *    - The first "S" is the literal character 'S' identifying the following
 *	digits as a SID.
 *    - R is the revision level of the SID expressed as a sequence of digits
 *	either in decimal or hexadecimal (if the later, prefixed by "0x").
 *    - I is the 48-bit identifier_authority, expressed as digits as R above.
 *    - S... is one or more sub_authority values, expressed as digits as above.
 *
 * Example SID; the domain-relative SID of the local Administrators group on
 * Windows NT/2k:
 *	S-1-5-32-544
 * This translates to a SID with:
 *	revision = 1,
 *	sub_authority_count = 2,
 *	identifier_authority = {0,0,0,0,0,5},	// SECURITY_NT_AUTHORITY
 *	sub_authority[0] = 32,			// SECURITY_BUILTIN_DOMAIN_RID
 *	sub_authority[1] = 544			// DOMAIN_ALIAS_RID_ADMINS
 */
struct ntfs_sid {
	u8 revision;
	u8 sub_authority_count;
	union {
		struct {
			u16 high_part;
			u32 low_part;
		} __packed parts;
		u8 value[6];
	} identifier_authority;
	__le32 sub_authority[];
} __packed;

/*
 * enum - Predefined ACE types (8-bit) for NTFS security descriptors
 *
 * ACCESS_MIN_MS_ACE_TYPE:         Minimum MS ACE type (0).
 * ACCESS_ALLOWED_ACE_TYPE:        Allow access (standard ACE).
 * ACCESS_DENIED_ACE_TYPE:         Deny access (standard ACE).
 * SYSTEM_AUDIT_ACE_TYPE:          Audit successful/failed access.
 * SYSTEM_ALARM_ACE_TYPE:          Alarm on access (not in Win2k+).
 * ACCESS_MAX_MS_V2_ACE_TYPE:      Max for V2 ACE types.
 * ACCESS_ALLOWED_COMPOUND_ACE_TYPE:
 *                                 Compound ACE (legacy).
 * ACCESS_MAX_MS_V3_ACE_TYPE:      Max for V3 ACE types.
 * ACCESS_MIN_MS_OBJECT_ACE_TYPE:  Min for object ACE types (Win2k+).
 * ACCESS_ALLOWED_OBJECT_ACE_TYPE: Allow with object-specific rights.
 * ACCESS_DENIED_OBJECT_ACE_TYPE:  Deny with object-specific rights.
 * SYSTEM_AUDIT_OBJECT_ACE_TYPE:   Audit with object-specific rights.
 * SYSTEM_ALARM_OBJECT_ACE_TYPE:   Alarm with object-specific rights.
 * ACCESS_MAX_MS_OBJECT_ACE_TYPE:  Max for object ACE types.
 * ACCESS_MAX_MS_V4_ACE_TYPE:      Max for V4 ACE types.
 * ACCESS_MAX_MS_ACE_TYPE:         Overall max ACE type (WinNT/2k).
 */
enum {
	ACCESS_MIN_MS_ACE_TYPE			= 0,
	ACCESS_ALLOWED_ACE_TYPE			= 0,
	ACCESS_DENIED_ACE_TYPE			= 1,
	SYSTEM_AUDIT_ACE_TYPE			= 2,
	SYSTEM_ALARM_ACE_TYPE			= 3,
	ACCESS_MAX_MS_V2_ACE_TYPE		= 3,

	ACCESS_ALLOWED_COMPOUND_ACE_TYPE	= 4,
	ACCESS_MAX_MS_V3_ACE_TYPE		= 4,
	ACCESS_MIN_MS_OBJECT_ACE_TYPE		= 5,
	ACCESS_ALLOWED_OBJECT_ACE_TYPE		= 5,
	ACCESS_DENIED_OBJECT_ACE_TYPE		= 6,
	SYSTEM_AUDIT_OBJECT_ACE_TYPE		= 7,
	SYSTEM_ALARM_OBJECT_ACE_TYPE		= 8,
	ACCESS_MAX_MS_OBJECT_ACE_TYPE		= 8,

	ACCESS_MAX_MS_V4_ACE_TYPE		= 8,
	ACCESS_MAX_MS_ACE_TYPE			= 8,
} __packed;

/*
 * enum - ACE inheritance and audit flags (8-bit)
 *
 * OBJECT_INHERIT_ACE:         Object inherit (files inherit this ACE).
 * CONTAINER_INHERIT_ACE:      Container inherit (subdirectories inherit).
 * NO_PROPAGATE_INHERIT_ACE:   No propagation (stop inheritance after this level).
 * INHERIT_ONLY_ACE:           Inherit only (not applied to current object).
 * INHERITED_ACE:              ACE was inherited (Win2k+ only).
 * VALID_INHERIT_FLAGS:        Mask of all valid inheritance flags (0x1f).
 * SUCCESSFUL_ACCESS_ACE_FLAG: Audit successful access (system audit ACE).
 * FAILED_ACCESS_ACE_FLAG:     Audit failed access (system audit ACE).
 *
 * SUCCESSFUL_ACCESS_ACE_FLAG is only used with system audit and alarm ACE
 * types to indicate that a message is generated (in Windows!) for successful
 * accesses.
 *
 * FAILED_ACCESS_ACE_FLAG is only used with system audit and alarm ACE types
 * to indicate that a message is generated (in Windows!) for failed accesses.
 */
enum {
	OBJECT_INHERIT_ACE		= 0x01,
	CONTAINER_INHERIT_ACE		= 0x02,
	NO_PROPAGATE_INHERIT_ACE	= 0x04,
	INHERIT_ONLY_ACE		= 0x08,
	INHERITED_ACE			= 0x10,
	VALID_INHERIT_FLAGS		= 0x1f,
	SUCCESSFUL_ACCESS_ACE_FLAG	= 0x40,
	FAILED_ACCESS_ACE_FLAG		= 0x80,
} __packed;

/*
 * enum - NTFS access rights masks (32-bit)
 *
 * FILE_READ_DATA / FILE_LIST_DIRECTORY:  Read file data / list dir contents.
 * FILE_WRITE_DATA / FILE_ADD_FILE:       Write file data / create file in dir.
 * FILE_APPEND_DATA / FILE_ADD_SUBDIRECTORY: Append data / create subdir.
 * FILE_READ_EA:                          Read extended attributes.
 * FILE_WRITE_EA:                         Write extended attributes.
 * FILE_EXECUTE / FILE_TRAVERSE:          Execute file / traverse dir.
 * FILE_DELETE_CHILD:                     Delete children in dir.
 * FILE_READ_ATTRIBUTES:                  Read attributes.
 * FILE_WRITE_ATTRIBUTES:                 Write attributes.
 *
 * Standard rights (object-independent):
 * DELETE:                                Delete object.
 * READ_CONTROL:                          Read security descriptor/owner.
 * WRITE_DAC:                             Modify DACL.
 * WRITE_OWNER:                           Change owner.
 * SYNCHRONIZE:                           Wait on object signal state.
 *
 * Combinations:
 * STANDARD_RIGHTS_READ / WRITE / EXECUTE: Aliases for READ_CONTROL.
 * STANDARD_RIGHTS_REQUIRED:              DELETE + READ_CONTROL +
 *                                        WRITE_DAC + WRITE_OWNER.
 * STANDARD_RIGHTS_ALL:                   Above + SYNCHRONIZE.
 *
 * System/access types:
 * ACCESS_SYSTEM_SECURITY:                Access system ACL.
 * MAXIMUM_ALLOWED:                       Maximum allowed access.
 *
 * Generic rights (high bits, map to specific/standard):
 * GENERIC_ALL:                           Full access.
 * GENERIC_EXECUTE:                       Execute/traverse.
 * GENERIC_WRITE:                         Write (append, attrs, data, EA, etc.).
 * GENERIC_READ:                          Read (attrs, data, EA, etc.).
 *
 * The specific rights (bits 0 to 15).  These depend on the type of the object
 * being secured by the ACE.
 */
enum {
	FILE_READ_DATA			= cpu_to_le32(0x00000001),
	FILE_LIST_DIRECTORY		= cpu_to_le32(0x00000001),
	FILE_WRITE_DATA			= cpu_to_le32(0x00000002),
	FILE_ADD_FILE			= cpu_to_le32(0x00000002),
	FILE_APPEND_DATA		= cpu_to_le32(0x00000004),
	FILE_ADD_SUBDIRECTORY		= cpu_to_le32(0x00000004),
	FILE_READ_EA			= cpu_to_le32(0x00000008),
	FILE_WRITE_EA			= cpu_to_le32(0x00000010),
	FILE_EXECUTE			= cpu_to_le32(0x00000020),
	FILE_TRAVERSE			= cpu_to_le32(0x00000020),
	FILE_DELETE_CHILD		= cpu_to_le32(0x00000040),
	FILE_READ_ATTRIBUTES		= cpu_to_le32(0x00000080),
	FILE_WRITE_ATTRIBUTES		= cpu_to_le32(0x00000100),
	DELETE				= cpu_to_le32(0x00010000),
	READ_CONTROL			= cpu_to_le32(0x00020000),
	WRITE_DAC			= cpu_to_le32(0x00040000),
	WRITE_OWNER			= cpu_to_le32(0x00080000),
	SYNCHRONIZE			= cpu_to_le32(0x00100000),
	STANDARD_RIGHTS_READ		= cpu_to_le32(0x00020000),
	STANDARD_RIGHTS_WRITE		= cpu_to_le32(0x00020000),
	STANDARD_RIGHTS_EXECUTE		= cpu_to_le32(0x00020000),
	STANDARD_RIGHTS_REQUIRED	= cpu_to_le32(0x000f0000),
	STANDARD_RIGHTS_ALL		= cpu_to_le32(0x001f0000),
	ACCESS_SYSTEM_SECURITY		= cpu_to_le32(0x01000000),
	MAXIMUM_ALLOWED			= cpu_to_le32(0x02000000),
	GENERIC_ALL			= cpu_to_le32(0x10000000),
	GENERIC_EXECUTE			= cpu_to_le32(0x20000000),
	GENERIC_WRITE			= cpu_to_le32(0x40000000),
	GENERIC_READ			= cpu_to_le32(0x80000000),
};

/*
 * struct ntfs_ace - Access Control Entry (ACE) structure
 *
 * @type: ACE type (ACCESS_ALLOWED_ACE_TYPE, ACCESS_DENIED_ACE_TYPE, etc.).
 * @flags: Inheritance and audit flags (OBJECT_INHERIT_ACE, etc.).
 * @size: Total byte size of this ACE (header + SID + variable data).
 * @mask: Access rights mask (FILE_READ_DATA, DELETE, GENERIC_ALL, etc.).
 * @sid: Security Identifier (SID) this ACE applies to.
 */
struct ntfs_ace {
	u8 type;
	u8 flags;
	__le16 size;
	__le32 mask;
	struct ntfs_sid sid;
} __packed;

/*
 * The object ACE flags (32-bit).
 */
enum {
	ACE_OBJECT_TYPE_PRESENT			= cpu_to_le32(1),
	ACE_INHERITED_OBJECT_TYPE_PRESENT	= cpu_to_le32(2),
};

/*
 * struct ntfs_acl - NTFS Access Control List (ACL) header
 *
 * An ACL is an access-control list (ACL).
 * An ACL starts with an ACL header structure, which specifies the size of
 * the ACL and the number of ACEs it contains. The ACL header is followed by
 * zero or more access control entries (ACEs). The ACL as well as each ACE
 * are aligned on 4-byte boundaries.
 *
 * @revision:           ACL revision level (usually 2 or 4).
 * @alignment1:         Padding/alignment byte (zero).
 * @size:               Total allocated size in bytes (header + all ACEs +
 *                      free space).
 * @ace_count:          Number of ACE entries following the header.
 * @alignment2:         Padding/alignment (zero).
 */
struct ntfs_acl {
	u8 revision;
	u8 alignment1;
	__le16 size;
	__le16 ace_count;
	__le16 alignment2;
} __packed;

static_assert(sizeof(struct ntfs_acl) == 8);

/*
 * The security descriptor control flags (16-bit).
 *
 * SE_OWNER_DEFAULTED - This boolean flag, when set, indicates that the SID
 *	pointed to by the Owner field was provided by a defaulting mechanism
 *	rather than explicitly provided by the original provider of the
 *	security descriptor.  This may affect the treatment of the SID with
 *	respect to inheritance of an owner.
 *
 * SE_GROUP_DEFAULTED - This boolean flag, when set, indicates that the SID in
 *	the Group field was provided by a defaulting mechanism rather than
 *	explicitly provided by the original provider of the security
 *	descriptor.  This may affect the treatment of the SID with respect to
 *	inheritance of a primary group.
 *
 * SE_DACL_PRESENT - This boolean flag, when set, indicates that the security
 *	descriptor contains a discretionary ACL.  If this flag is set and the
 *	Dacl field of the SECURITY_DESCRIPTOR is null, then a null ACL is
 *	explicitly being specified.
 *
 * SE_DACL_DEFAULTED - This boolean flag, when set, indicates that the ACL
 *	pointed to by the Dacl field was provided by a defaulting mechanism
 *	rather than explicitly provided by the original provider of the
 *	security descriptor.  This may affect the treatment of the ACL with
 *	respect to inheritance of an ACL.  This flag is ignored if the
 *	DaclPresent flag is not set.
 *
 * SE_SACL_PRESENT - This boolean flag, when set,  indicates that the security
 *	descriptor contains a system ACL pointed to by the Sacl field.  If this
 *	flag is set and the Sacl field of the SECURITY_DESCRIPTOR is null, then
 *	an empty (but present) ACL is being specified.
 *
 * SE_SACL_DEFAULTED - This boolean flag, when set, indicates that the ACL
 *	pointed to by the Sacl field was provided by a defaulting mechanism
 *	rather than explicitly provided by the original provider of the
 *	security descriptor.  This may affect the treatment of the ACL with
 *	respect to inheritance of an ACL.  This flag is ignored if the
 *	SaclPresent flag is not set.
 *
 * SE_SELF_RELATIVE - This boolean flag, when set, indicates that the security
 *	descriptor is in self-relative form.  In this form, all fields of the
 *	security descriptor are contiguous in memory and all pointer fields are
 *	expressed as offsets from the beginning of the security descriptor.
 */
enum {
	SE_OWNER_DEFAULTED		= cpu_to_le16(0x0001),
	SE_GROUP_DEFAULTED		= cpu_to_le16(0x0002),
	SE_DACL_PRESENT			= cpu_to_le16(0x0004),
	SE_DACL_DEFAULTED		= cpu_to_le16(0x0008),

	SE_SACL_PRESENT			= cpu_to_le16(0x0010),
	SE_SACL_DEFAULTED		= cpu_to_le16(0x0020),

	SE_DACL_AUTO_INHERIT_REQ	= cpu_to_le16(0x0100),
	SE_SACL_AUTO_INHERIT_REQ	= cpu_to_le16(0x0200),
	SE_DACL_AUTO_INHERITED		= cpu_to_le16(0x0400),
	SE_SACL_AUTO_INHERITED		= cpu_to_le16(0x0800),

	SE_DACL_PROTECTED		= cpu_to_le16(0x1000),
	SE_SACL_PROTECTED		= cpu_to_le16(0x2000),
	SE_RM_CONTROL_VALID		= cpu_to_le16(0x4000),
	SE_SELF_RELATIVE		= cpu_to_le16(0x8000)
} __packed;

/*
 * struct security_descriptor_relative - Relative security descriptor
 *
 * Self-relative security descriptor. Contains the owner and group SIDs as well
 * as the sacl and dacl ACLs inside the security descriptor itself.
 *
 * @revision:          Security descriptor revision (usually 1).
 * @alignment:         Padding/alignment byte (zero).
 * @control:           Control flags (SE_OWNER_DEFAULTED, SE_DACL_PRESENT,
 *                     SE_SACL_PRESENT, SE_SACL_AUTO_INHERITED, etc.).
 * @owner:             Byte offset to owner SID (from start of descriptor).
 *                     0 if no owner SID present.
 * @group:             Byte offset to primary group SID.
 *                     0 if no group SID present.
 * @sacl:              Byte offset to System ACL (SACL).
 *                     Valid only if SE_SACL_PRESENT in @control.
 *                     0 means NULL SACL.
 * @dacl:              Byte offset to Discretionary ACL (DACL).
 *                     Valid only if SE_DACL_PRESENT in @control.
 *                     0 means NULL DACL (full access granted).
 */
struct security_descriptor_relative {
	u8 revision;
	u8 alignment;
	__le16 control;
	__le32 owner;
	__le32 group;
	__le32 sacl;
	__le32 dacl;
} __packed;

static_assert(sizeof(struct security_descriptor_relative) == 20);

/*
 * On NTFS 3.0+, all security descriptors are stored in FILE_Secure. Only one
 * referenced instance of each unique security descriptor is stored.
 *
 * FILE_Secure contains no unnamed data attribute, i.e. it has zero length. It
 * does, however, contain two indexes ($SDH and $SII) as well as a named data
 * stream ($SDS).
 *
 * Every unique security descriptor is assigned a unique security identifier
 * (security_id, not to be confused with a SID). The security_id is unique for
 * the NTFS volume and is used as an index into the $SII index, which maps
 * security_ids to the security descriptor's storage location within the $SDS
 * data attribute. The $SII index is sorted by ascending security_id.
 *
 * A simple hash is computed from each security descriptor. This hash is used
 * as an index into the $SDH index, which maps security descriptor hashes to
 * the security descriptor's storage location within the $SDS data attribute.
 * The $SDH index is sorted by security descriptor hash and is stored in a B+
 * tree. When searching $SDH (with the intent of determining whether or not a
 * new security descriptor is already present in the $SDS data stream), if a
 * matching hash is found, but the security descriptors do not match, the
 * search in the $SDH index is continued, searching for a next matching hash.
 *
 * When a precise match is found, the security_id coresponding to the security
 * descriptor in the $SDS attribute is read from the found $SDH index entry and
 * is stored in the $STANDARD_INFORMATION attribute of the file/directory to
 * which the security descriptor is being applied. The $STANDARD_INFORMATION
 * attribute is present in all base mft records (i.e. in all files and
 * directories).
 *
 * If a match is not found, the security descriptor is assigned a new unique
 * security_id and is added to the $SDS data attribute. Then, entries
 * referencing the this security descriptor in the $SDS data attribute are
 * added to the $SDH and $SII indexes.
 *
 * Note: Entries are never deleted from FILE_Secure, even if nothing
 * references an entry any more.
 */

/*
 * struct sii_index_key - Key for $SII index in $Secure file
 *
 * The index entry key used in the $SII index. The collation type is
 * COLLATION_NTOFS_ULONG.
 *
 * @security_id:    32-bit security identifier.
 *                  Unique ID assigned to a security descriptor.
 */
struct sii_index_key {
	__le32 security_id;
} __packed;

/*
 * struct sdh_index_key - Key for $SDH index in $Secure file
 *
 * The index entry key used in the $SDH index. The keys are sorted first by
 * hash and then by security_id. The collation rule is
 * COLLATION_NTOFS_SECURITY_HASH.
 *
 * @hash:           32-bit hash of the security descriptor.
 *                  Used for quick collision checks and indexing.
 * @security_id:    32-bit security identifier.
 *                  Unique ID assigned to the descriptor.
 */
struct sdh_index_key {
	__le32 hash;
	__le32 security_id;
} __packed;

/*
 * enum - NTFS volume flags (16-bit)
 *
 * These flags are stored in $VolumeInformation attribute.
 * They indicate volume state and required actions.
 *
 * VOLUME_IS_DIRTY:                Volume is dirty (needs chkdsk).
 * VOLUME_RESIZE_LOG_FILE:         Resize LogFile on next mount.
 * VOLUME_UPGRADE_ON_MOUNT:        Upgrade volume on mount (old NTFS).
 * VOLUME_MOUNTED_ON_NT4:          Mounted on NT4 (compatibility flag).
 * VOLUME_DELETE_USN_UNDERWAY:     USN journal deletion in progress.
 * VOLUME_REPAIR_OBJECT_ID:        Repair $ObjId on next mount.
 * VOLUME_CHKDSK_UNDERWAY:         Chkdsk is running.
 * VOLUME_MODIFIED_BY_CHKDSK:      Modified by chkdsk.
 * VOLUME_FLAGS_MASK:              Mask of all valid flags (0xc03f).
 * VOLUME_MUST_MOUNT_RO_MASK:      Flags forcing read-only mount (0xc027).
 *                                 If any set, mount read-only.
 */
enum {
	VOLUME_IS_DIRTY			= cpu_to_le16(0x0001),
	VOLUME_RESIZE_LOG_FILE		= cpu_to_le16(0x0002),
	VOLUME_UPGRADE_ON_MOUNT		= cpu_to_le16(0x0004),
	VOLUME_MOUNTED_ON_NT4		= cpu_to_le16(0x0008),

	VOLUME_DELETE_USN_UNDERWAY	= cpu_to_le16(0x0010),
	VOLUME_REPAIR_OBJECT_ID		= cpu_to_le16(0x0020),

	VOLUME_CHKDSK_UNDERWAY		= cpu_to_le16(0x4000),
	VOLUME_MODIFIED_BY_CHKDSK	= cpu_to_le16(0x8000),

	VOLUME_FLAGS_MASK		= cpu_to_le16(0xc03f),

	VOLUME_MUST_MOUNT_RO_MASK	= cpu_to_le16(0xc027),
} __packed;

/*
 * struct volume_information - $VOLUME_INFORMATION (0x70)
 *
 * @reserved:       Reserved 64-bit field (currently unused).
 * @major_ver:      Major NTFS version number (e.g., 3 for NTFS 3.1).
 * @minor_ver:      Minor NTFS version number (e.g., 1 for NTFS 3.1).
 * @flags:          Volume flags (VOLUME_IS_DIRTY, VOLUME_CHKDSK_UNDERWAY, etc.).
 *                  See volume flags enum for details.
 *
 * NOTE: Always resident.
 * NOTE: Present only in FILE_Volume.
 * NOTE: Windows 2000 uses NTFS 3.0 while Windows NT4 service pack 6a uses
 *	 NTFS 1.2. I haven't personally seen other values yet.
 */
struct volume_information {
	__le64 reserved;
	u8 major_ver;
	u8 minor_ver;
	__le16 flags;
} __packed;

/*
 * enum - Index header flags
 *
 * These flags are stored in the index header (INDEX_HEADER.flags) for both
 * index root ($INDEX_ROOT) and index allocation blocks ($INDEX_ALLOCATION).
 *
 * For index root ($INDEX_ROOT attribute):
 * SMALL_INDEX: Index fits entirely in root attribute (no $INDEX_ALLOCATION).
 * LARGE_INDEX: Index too large for root; $INDEX_ALLOCATION present.
 *
 * For index blocks ($INDEX_ALLOCATION):
 * LEAF_NODE:   Leaf node (no child nodes; contains actual entries).
 * INDEX_NODE:  Internal node (indexes other nodes; contains keys/pointers).
 *
 * NODE_MASK:   Mask to extract node type bits (0x01).
 */
enum {
	SMALL_INDEX = 0,
	LARGE_INDEX = 1,
	LEAF_NODE  = 0,
	INDEX_NODE = 1,
	NODE_MASK  = 1,
} __packed;

/*
 * struct index_header - Common header for index root and index blocks
 *
 * entries_offset:     Byte offset to first INDEX_ENTRY (8-byte aligned).
 * index_length:       Bytes used by index entries (8-byte aligned).
 *                     From entries_offset to end of used data.
 * allocated_size:     Total allocated bytes for this index block.
 *                     Fixed size in index allocation; dynamic in root.
 * flags:              Index flags (SMALL_INDEX, LARGE_INDEX, LEAF_NODE, etc.).
 *                     See INDEX_HEADER_FLAGS enum.
 * reserved:           3 bytes reserved/padding (zero, 8-byte aligned).
 *
 * This is the header for indexes, describing the INDEX_ENTRY records, which
 * follow the index_header. Together the index header and the index entries
 * make up a complete index.
 *
 * IMPORTANT NOTE: The offset, length and size structure members are counted
 * relative to the start of the index header structure and not relative to the
 * start of the index root or index allocation structures themselves.
 *
 * For the index root attribute, the above two numbers are always
 * equal, as the attribute is resident and it is resized as needed. In
 * the case of the index allocation attribute the attribute is not
 * resident and hence the allocated_size is a fixed value and must
 * equal the index_block_size specified by the INDEX_ROOT attribute
 * corresponding to the INDEX_ALLOCATION attribute this INDEX_BLOCK
 * belongs to.
 */
struct index_header {
	__le32 entries_offset;
	__le32 index_length;
	__le32 allocated_size;
	u8 flags;
	u8 reserved[3];
} __packed;

/*
 * struct index_root - $INDEX_ROOT attribute (0x90).
 *
 * @type:               Indexed attribute type ($FILE_NAME for dirs,
 *                      0 for view indexes).
 * @collation_rule:     Collation rule for sorting entries
 *                      (COLLATION_FILE_NAME for $FILE_NAME).
 * @index_block_size:   Size of each index block in bytes
 *                      (in $INDEX_ALLOCATION).
 * @clusters_per_index_block:
 *                      Clusters per index block (or log2(bytes)
 *                      if < cluster).
 *                      Power of 2; used for encoding block size.
 * @reserved:           3 bytes reserved/alignment (zero).
 * @index:              Index header for root entries (entries follow
 *                      immediately).
 *
 * NOTE: Always resident.
 *
 * This is followed by a sequence of index entries (INDEX_ENTRY structures)
 * as described by the index header.
 *
 * When a directory is small enough to fit inside the index root then this
 * is the only attribute describing the directory. When the directory is too
 * large to fit in the index root, on the other hand, two additional attributes
 * are present: an index allocation attribute, containing sub-nodes of the B+
 * directory tree (see below), and a bitmap attribute, describing which virtual
 * cluster numbers (vcns) in the index allocation attribute are in use by an
 * index block.
 *
 * NOTE: The root directory (FILE_root) contains an entry for itself. Other
 * directories do not contain entries for themselves, though.
 */
struct index_root {
	__le32 type;
	__le32 collation_rule;
	__le32 index_block_size;
	u8 clusters_per_index_block;
	u8 reserved[3];
	struct index_header index;
} __packed;

/*
 * struct index_block - Index allocation (0xa0).
 *
 * @magic:              Magic value "INDX" (see magic_INDX).
 * @usa_ofs:            Offset to Update Sequence Array (see ntfs_record).
 * @usa_count:          Number of USA entries (see ntfs_record).
 * @lsn:                Log sequence number of last modification.
 * @index_block_vcn:    VCN of this index block.
 *                      Units: clusters if cluster_size <= index_block_size;
 *                      sectors otherwise.
 * @index:              Index header describing entries in this block.
 *
 * When creating the index block, we place the update sequence array at this
 * offset, i.e. before we start with the index entries. This also makes sense,
 * otherwise we could run into problems with the update sequence array
 * containing in itself the last two bytes of a sector which would mean that
 * multi sector transfer protection wouldn't work. As you can't protect data
 * by overwriting it since you then can't get it back...
 * When reading use the data from the ntfs record header.
 *
 * NOTE: Always non-resident (doesn't make sense to be resident anyway!).
 *
 * This is an array of index blocks. Each index block starts with an
 * index_block structure containing an index header, followed by a sequence of
 * index entries (INDEX_ENTRY structures), as described by the struct index_header.
 */
struct index_block {
	__le32 magic;
	__le16 usa_ofs;
	__le16 usa_count;
	__le64 lsn;
	__le64 index_block_vcn;
	struct index_header index;
} __packed;

static_assert(sizeof(struct index_block) == 40);

/*
 * struct reparse_index_key - Key for $R reparse index in $Extend/$Reparse
 *
 * @reparse_tag:    Reparse point type (including flags, REPARSE_TAG_*).
 * @file_id:        MFT record number of the file with $REPARSE_POINT
 *                  attribute.
 *
 * The system file FILE_Extend/$Reparse contains an index named $R listing
 * all reparse points on the volume. The index entry keys are as defined
 * below. Note, that there is no index data associated with the index entries.
 *
 * The index entries are sorted by the index key file_id. The collation rule is
 * COLLATION_NTOFS_ULONGS.
 */
struct reparse_index_key {
	__le32 reparse_tag;
	__le64 file_id;
} __packed;

/*
 * enum - Quota entry flags (32-bit) in $Quota/$Q
 *
 * These flags are stored in quota control entries ($Quota file).
 * They control quota tracking, limits, and state.
 *
 * User quota flags (mask 0x00000007):
 * @QUOTA_FLAG_DEFAULT_LIMITS:   Use default limits.
 * @QUOTA_FLAG_LIMIT_REACHED:    Quota limit reached.
 * @QUOTA_FLAG_ID_DELETED:       Quota ID deleted.
 * @QUOTA_FLAG_USER_MASK:        Mask for user quota flags (0x00000007).
 *
 * Default entry flags (owner_id = QUOTA_DEFAULTS_ID):
 * @QUOTA_FLAG_TRACKING_ENABLED:    Quota tracking enabled.
 * @QUOTA_FLAG_ENFORCEMENT_ENABLED: Quota enforcement enabled.
 * @QUOTA_FLAG_TRACKING_REQUESTED:  Tracking requested (pending).
 * @QUOTA_FLAG_LOG_THRESHOLD:       Log when threshold reached.
 * @QUOTA_FLAG_LOG_LIMIT:           Log when limit reached.
 * @QUOTA_FLAG_OUT_OF_DATE:         Quota data out of date.
 * @QUOTA_FLAG_CORRUPT:             Quota entry corrupt.
 * @QUOTA_FLAG_PENDING_DELETES:     Pending quota deletes.
 *
 */
enum {
	QUOTA_FLAG_DEFAULT_LIMITS	= cpu_to_le32(0x00000001),
	QUOTA_FLAG_LIMIT_REACHED	= cpu_to_le32(0x00000002),
	QUOTA_FLAG_ID_DELETED		= cpu_to_le32(0x00000004),

	QUOTA_FLAG_USER_MASK		= cpu_to_le32(0x00000007),
	QUOTA_FLAG_TRACKING_ENABLED	= cpu_to_le32(0x00000010),
	QUOTA_FLAG_ENFORCEMENT_ENABLED	= cpu_to_le32(0x00000020),
	QUOTA_FLAG_TRACKING_REQUESTED	= cpu_to_le32(0x00000040),
	QUOTA_FLAG_LOG_THRESHOLD	= cpu_to_le32(0x00000080),

	QUOTA_FLAG_LOG_LIMIT		= cpu_to_le32(0x00000100),
	QUOTA_FLAG_OUT_OF_DATE		= cpu_to_le32(0x00000200),
	QUOTA_FLAG_CORRUPT		= cpu_to_le32(0x00000400),
	QUOTA_FLAG_PENDING_DELETES	= cpu_to_le32(0x00000800),
};

/*
 * struct quota_control_entry - Quota entry in $Quota/$Q
 *
 * @version:        Currently 2.
 * @flags:          Quota flags (QUOTA_FLAG_* bits).
 * @bytes_used:     Current quota usage in bytes.
 * @change_time:    Last modification time (NTFS timestamp).
 * @threshold:      Soft quota limit (-1 = unlimited).
 * @limit:          Hard quota limit (-1 = unlimited).
 * @exceeded_time:  Time soft quota has been exceeded.
 * @sid:            SID of user/object (zero for defaults entry).
 *
 * The system file FILE_Extend/$Quota contains two indexes $O and $Q. Quotas
 * are on a per volume and per user basis.
 *
 * The $Q index contains one entry for each existing user_id on the volume. The
 * index key is the user_id of the user/group owning this quota control entry,
 * i.e. the key is the owner_id. The user_id of the owner of a file, i.e. the
 * owner_id, is found in the standard information attribute. The collation rule
 * for $Q is COLLATION_NTOFS_ULONG.
 *
 * The $O index contains one entry for each user/group who has been assigned
 * a quota on that volume. The index key holds the SID of the user_id the
 * entry belongs to, i.e. the owner_id. The collation rule for $O is
 * COLLATION_NTOFS_SID.
 *
 * The $O index entry data is the user_id of the user corresponding to the SID.
 * This user_id is used as an index into $Q to find the quota control entry
 * associated with the SID.
 *
 * The $Q index entry data is the quota control entry and is defined below.
 */
struct quota_control_entry {
	__le32 version;
	__le32 flags;
	__le64 bytes_used;
	__le64 change_time;
	__le64 threshold;
	__le64 limit;
	__le64 exceeded_time;
	struct ntfs_sid sid;
} __packed;

/*
 * Predefined owner_id values (32-bit).
 */
enum {
	QUOTA_INVALID_ID	= cpu_to_le32(0x00000000),
	QUOTA_DEFAULTS_ID	= cpu_to_le32(0x00000001),
	QUOTA_FIRST_USER_ID	= cpu_to_le32(0x00000100),
};

/*
 * Current constants for quota control entries.
 */
enum {
	/* Current version. */
	QUOTA_VERSION	= 2,
};

/*
 * enum - Index entry flags (16-bit)
 *
 * These flags are in INDEX_ENTRY.flags (after key data).
 * They describe entry type and status in index blocks/root.
 *
 * @INDEX_ENTRY_NODE:     Entry points to a sub-node (index block VCN).
 *                        (Not a leaf entry; internal node reference.)
 *                        i.e. a reference to an index block in form of
 *                        a virtual cluster number
 * @INDEX_ENTRY_END:      Last entry in index block/root.
 *                        Does not represent a real file; can point to sub-node.
 * @INDEX_ENTRY_SPACE_FILLER:
 *                        Dummy value to force enum to 16-bit width.
 */
enum {
	INDEX_ENTRY_NODE = cpu_to_le16(1),
	INDEX_ENTRY_END  = cpu_to_le16(2),
	INDEX_ENTRY_SPACE_FILLER = cpu_to_le16(0xffff),
} __packed;

/*
 * struct index_entry_header - Common header for all NTFS index entries
 *
 * This is the fixed header at the start of every INDEX_ENTRY in index
 * blocks or index root. It is followed by the variable key, data, and
 * sub-node VCN.
 *
 * Union @data:
 * - When INDEX_ENTRY_END is not set:
 *   @data.dir.indexed_file: MFT reference of the file described by
 *                           this entry. Used in directory indexes ($I30).
 * - When INDEX_ENTRY_END is set or for view indexes:
 *   @data.vi.data_offset: Byte offset from end of this header to
 *                         entry data.
 *   @data.vi.data_length: Length of data in bytes.
 *   @data.vi.reservedV:   Reserved (zero).
 *
 * @length:         Total byte size of this index entry
 *                  (multiple of 8 bytes).
 * @key_length:     Byte size of the key (not multiple of 8 bytes).
 *                  Key follows the header immediately.
 * @flags:          Bit field of INDEX_ENTRY_* flags (INDEX_ENTRY_NODE, etc.).
 * @reserved:       Reserved/padding (zero; align to 8 bytes).
 */
struct index_entry_header {
	union {
		struct {
			__le64 indexed_file;
		} __packed dir;
		struct {
			__le16 data_offset;
			__le16 data_length;
			__le32 reservedV;
		} __packed vi;
	} __packed data;
	__le16 length;
	__le16 key_length;
	__le16 flags;
	__le16 reserved;
} __packed;

static_assert(sizeof(struct index_entry_header) == 16);

/*
 * struct index_entry - NTFS index entry structure
 *
 * This is an index entry. A sequence of such entries follows each index_header
 * structure. Together they make up a complete index. The index follows either
 * an index root attribute or an index allocation attribute.
 *
 * Union @data (valid when INDEX_ENTRY_END not set):
 * @data.dir.indexed_file: MFT ref of file (for directory indexes).
 * @data.vi.data_offset:   Offset to data after key.
 * @data.vi.data_length:   Length of data in bytes.
 * @data.vi.reservedV:     Reserved (zero).
 *
 * Fields:
 * @length:         Total byte size of entry (multiple of 8 bytes).
 * @key_length:     Byte size of key (not multiple of 8).
 * @flags:          INDEX_ENTRY_* flags (NODE, END, etc.).
 * @reserved:       Reserved/padding (zero).
 *
 * Union @key (valid when INDEX_ENTRY_END not set)
 * The key of the indexed attribute. NOTE: Only present
 * if INDEX_ENTRY_END bit in flags is not set. NOTE: On
 * NTFS versions before 3.0 the only valid key is the
 * struct file_name_attr. On NTFS 3.0+ the following
 * additional index keys are defined:
 * @key.file_name:  $FILE_NAME attr (for $I30 directory indexes).
 * @key.sii:        $SII key (for $Secure $SII index).
 * @key.sdh:        $SDH key (for $Secure $SDH index).
 * @key.object_id:  GUID (for $ObjId $O index).
 * @key.reparse:    Reparse tag + file ID (for $Reparse $R).
 * @key.sid:        SID (for $Quota $O index).
 * @key.owner_id:   User ID (for $Quota $Q index).
 *
 * The (optional) index data is inserted here when creating.
 * __le64 vcn;     If INDEX_ENTRY_NODE bit in flags is set, the last
 *                 eight bytes of this index entry contain the virtual
 *                 cluster number of the index block that holds the
 *                 entries immediately preceding the current entry (the
 *                 vcn references the corresponding cluster in the data
 *                 of the non-resident index allocation attribute). If
 *                 the key_length is zero, then the vcn immediately
 *                 follows the INDEX_ENTRY_HEADER. Regardless of
 *                 key_length, the address of the 8-byte boundary
 *                 aligned vcn of INDEX_ENTRY{_HEADER} *ie is given by
 *                 (char*)ie + le16_to_cpu(ie*)->length) - sizeof(VCN),
 *                 where sizeof(VCN) can be hardcoded as 8 if wanted.
 *
 * NOTE: Before NTFS 3.0 only filename attributes were indexed.
 */
struct index_entry {
	union {
		struct {
			__le64 indexed_file;
		} __packed dir;
		struct {
			__le16 data_offset;
			__le16 data_length;
			__le32 reservedV;
		} __packed vi;
	} __packed data;
	__le16 length;
	__le16 key_length;
	__le16 flags;
	__le16 reserved;
	union {
		struct file_name_attr file_name;
		struct sii_index_key sii;
		struct sdh_index_key sdh;
		struct guid object_id;
		struct reparse_index_key reparse;
		struct ntfs_sid sid;
		__le32 owner_id;
	} __packed key;
} __packed;

/*
 * The reparse point tag defines the type of the reparse point. It also
 * includes several flags, which further describe the reparse point.
 *
 * The reparse point tag is an unsigned 32-bit value divided in three parts:
 *
 * 1. The least significant 16 bits (i.e. bits 0 to 15) specify the type of
 *    the reparse point.
 * 2. The 12 bits after this (i.e. bits 16 to 27) are reserved for future use.
 * 3. The most significant four bits are flags describing the reparse point.
 *    They are defined as follows:
 *	bit 28: Directory bit. If set, the directory is not a surrogate
 *		and can be used the usual way.
 *	bit 29: Name surrogate bit. If set, the filename is an alias for
 *		another object in the system.
 *	bit 30: High-latency bit. If set, accessing the first byte of data will
 *		be slow. (E.g. the data is stored on a tape drive.)
 *	bit 31: Microsoft bit. If set, the tag is owned by Microsoft. User
 *		defined tags have to use zero here.
 * 4. Moreover, on Windows 10 :
 *	Some flags may be used in bits 12 to 15 to further describe the
 *	reparse point.
 */
enum {
	IO_REPARSE_TAG_DIRECTORY	= cpu_to_le32(0x10000000),
	IO_REPARSE_TAG_IS_ALIAS		= cpu_to_le32(0x20000000),
	IO_REPARSE_TAG_IS_HIGH_LATENCY	= cpu_to_le32(0x40000000),
	IO_REPARSE_TAG_IS_MICROSOFT	= cpu_to_le32(0x80000000),

	IO_REPARSE_TAG_RESERVED_ZERO	= cpu_to_le32(0x00000000),
	IO_REPARSE_TAG_RESERVED_ONE	= cpu_to_le32(0x00000001),
	IO_REPARSE_TAG_RESERVED_RANGE	= cpu_to_le32(0x00000001),

	IO_REPARSE_TAG_CSV		= cpu_to_le32(0x80000009),
	IO_REPARSE_TAG_DEDUP		= cpu_to_le32(0x80000013),
	IO_REPARSE_TAG_DFS		= cpu_to_le32(0x8000000A),
	IO_REPARSE_TAG_DFSR		= cpu_to_le32(0x80000012),
	IO_REPARSE_TAG_HSM		= cpu_to_le32(0xC0000004),
	IO_REPARSE_TAG_HSM2		= cpu_to_le32(0x80000006),
	IO_REPARSE_TAG_MOUNT_POINT	= cpu_to_le32(0xA0000003),
	IO_REPARSE_TAG_NFS		= cpu_to_le32(0x80000014),
	IO_REPARSE_TAG_SIS		= cpu_to_le32(0x80000007),
	IO_REPARSE_TAG_SYMLINK		= cpu_to_le32(0xA000000C),
	IO_REPARSE_TAG_WIM		= cpu_to_le32(0x80000008),
	IO_REPARSE_TAG_DFM		= cpu_to_le32(0x80000016),
	IO_REPARSE_TAG_WOF		= cpu_to_le32(0x80000017),
	IO_REPARSE_TAG_WCI		= cpu_to_le32(0x80000018),
	IO_REPARSE_TAG_CLOUD		= cpu_to_le32(0x9000001A),
	IO_REPARSE_TAG_APPEXECLINK	= cpu_to_le32(0x8000001B),
	IO_REPARSE_TAG_GVFS		= cpu_to_le32(0x9000001C),
	IO_REPARSE_TAG_LX_SYMLINK	= cpu_to_le32(0xA000001D),
	IO_REPARSE_TAG_AF_UNIX		= cpu_to_le32(0x80000023),
	IO_REPARSE_TAG_LX_FIFO		= cpu_to_le32(0x80000024),
	IO_REPARSE_TAG_LX_CHR		= cpu_to_le32(0x80000025),
	IO_REPARSE_TAG_LX_BLK		= cpu_to_le32(0x80000026),

	IO_REPARSE_TAG_VALID_VALUES	= cpu_to_le32(0xf000ffff),
	IO_REPARSE_PLUGIN_SELECT	= cpu_to_le32(0xffff0fff),
};

/*
 * struct reparse_point - $REPARSE_POINT attribute content (0xc0)\
 *
 * @reparse_tag:        Reparse point type (with flags; REPARSE_TAG_*).
 * @reparse_data_length: Byte size of @reparse_data.
 * @reserved:           Reserved/padding (zero; 8-byte alignment).
 * @reparse_data:       Variable reparse data (meaning depends on @reparse_tag).
 *                      - Symbolic link/junction: struct reparse_symlink
 *                      - Mount point: similar symlink structure
 *                      - Other tags: vendor-specific or extended data
 *
 * NOTE: Can be resident or non-resident.
 */
struct reparse_point {
	__le32 reparse_tag;
	__le16 reparse_data_length;
	__le16 reserved;
	u8 reparse_data[];
} __packed;

/*
 * struct ea_information - $EA_INFORMATION attribute content (0xd0)
 *
 * @ea_length:      Byte size of packed EAs.
 * @need_ea_count:  Number of EAs with NEED_EA bit set.
 * @ea_query_length: Byte size needed to unpack/query EAs via ZwQueryEaFile().
 *                   (Unpacked format size.)
 *
 * NOTE: Always resident. (Is this true???)
 */
struct ea_information {
	__le16 ea_length;
	__le16 need_ea_count;
	__le32 ea_query_length;
} __packed;

/*
 * enum - Extended attribute flags (8-bit)
 *
 * These flags are stored in the EA header of each extended attribute
 * (in $EA attribute, type 0xe0).
 *
 * @NEED_EA:        If set, the file cannot be properly interpreted
 *                  without understanding its associated EAs.
 *                  (Critical EA; applications must process it.)
 */
enum {
	NEED_EA	= 0x80
} __packed;

/*
 * struct ea_attr - Extended attribute (EA) entry (0xe0)
 *
 * @next_entry_offset:  Byte offset to the next EA_ATTR entry.
 *                      (From start of current entry.)
 * @flags:              EA flags (NEED_EA = 0x80 if critical).
 * @ea_name_length:     Length of @ea_name in bytes (excluding '\0').
 * @ea_value_length:    Byte size of the EA value.
 * @ea_name:            ASCII name of the EA (zero-terminated).
 *                      Value immediately follows the name.
 * u8 ea_value[];       The value of the EA.  Immediately follows the name.
 *
 * This is one variable-length record in the $EA attribute value.
 * The attribute can be resident or non-resident.
 * Sequence of these entries forms the packed EA list.
 *
 * NOTE: Can be resident or non-resident.
 */
struct ea_attr {
	__le32 next_entry_offset;
	u8 flags;
	u8 ea_name_length;
	__le16 ea_value_length;
	u8 ea_name[];
} __packed;

#endif /* _LINUX_NTFS_LAYOUT_H */
