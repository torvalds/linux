/* SPDX-License-Identifier: LGPL-2.1 */
/*
 *
 *   Copyright (c) International Business Machines  Corp., 2009, 2013
 *                 Etersoft, 2012
 *                 2018 Samsung Electronics Co., Ltd.
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *              Pavel Shilovsky (pshilovsky@samba.org) 2012
 *              Namjae Jeon (linkinjeon@kernel.org)
 *
 */
#ifndef _COMMON_SMB_FSCC_H
#define _COMMON_SMB_FSCC_H

/* See MS-FSCC 2.4.8 */
typedef struct {
	__le32 NextEntryOffset;
	__u32 FileIndex;
	__le64 CreationTime;
	__le64 LastAccessTime;
	__le64 LastWriteTime;
	__le64 ChangeTime;
	__le64 EndOfFile;
	__le64 AllocationSize;
	__le32 ExtFileAttributes;
	__le32 FileNameLength;
	__le32 EaSize; /* length of the xattrs */
	__u8   ShortNameLength;
	__u8   Reserved;
	__u8   ShortName[24];
	char FileName[];
} __packed FILE_BOTH_DIRECTORY_INFO; /* level 0x104 FFrsp data */

/* See MS-FSCC 2.4.10 */
typedef struct {
	__le32 NextEntryOffset;
	__u32 FileIndex;
	__le64 CreationTime;
	__le64 LastAccessTime;
	__le64 LastWriteTime;
	__le64 ChangeTime;
	__le64 EndOfFile;
	__le64 AllocationSize;
	__le32 ExtFileAttributes;
	__le32 FileNameLength;
	char FileName[];
} __packed FILE_DIRECTORY_INFO;   /* level 0x101 FF resp data */

/* See MS-FSCC 2.4.14 */
typedef struct {
	__le32 NextEntryOffset;
	__u32 FileIndex;
	__le64 CreationTime;
	__le64 LastAccessTime;
	__le64 LastWriteTime;
	__le64 ChangeTime;
	__le64 EndOfFile;
	__le64 AllocationSize;
	__le32 ExtFileAttributes;
	__le32 FileNameLength;
	__le32 EaSize; /* length of the xattrs */
	char FileName[];
} __packed FILE_FULL_DIRECTORY_INFO; /* level 0x102 rsp data */

/* See MS-FSCC 2.4.24 */
typedef struct {
	__le32 NextEntryOffset;
	__u32 FileIndex;
	__le64 CreationTime;
	__le64 LastAccessTime;
	__le64 LastWriteTime;
	__le64 ChangeTime;
	__le64 EndOfFile;
	__le64 AllocationSize;
	__le32 ExtFileAttributes;
	__le32 FileNameLength;
	__le32 EaSize; /* EA size */
	__le32 Reserved;
	__le64 UniqueId; /* inode num - le since Samba puts ino in low 32 bit*/
	char FileName[];
} __packed FILE_ID_FULL_DIR_INFO; /* level 0x105 FF rsp data */

/* See MS-FSCC 2.4.34 */
struct smb2_file_network_open_info {
	struct_group_attr(network_open_info, __packed,
		__le64 CreationTime;
		__le64 LastAccessTime;
		__le64 LastWriteTime;
		__le64 ChangeTime;
		__le64 AllocationSize;
		__le64 EndOfFile;
		__le32 Attributes;
	);
	__le32 Reserved;
} __packed; /* level 34 Query also similar returned in close rsp and open rsp */

/* See MS-FSCC 2.5.1 */
#define MAX_FS_NAME_LEN		52
typedef struct {
	__le32 Attributes;
	__le32 MaxPathNameComponentLength;
	__le32 FileSystemNameLen;
	__le16 FileSystemName[]; /* do not have to save this - get subset? */
} __packed FILE_SYSTEM_ATTRIBUTE_INFO;

/* List of FileSystemAttributes - see MS-FSCC 2.5.1 */
#define FILE_SUPPORTS_SPARSE_VDL	0x10000000 /* faster nonsparse extend */
#define FILE_SUPPORTS_BLOCK_REFCOUNTING	0x08000000 /* allow ioctl dup extents */
#define FILE_SUPPORT_INTEGRITY_STREAMS	0x04000000
#define FILE_SUPPORTS_USN_JOURNAL	0x02000000
#define FILE_SUPPORTS_OPEN_BY_FILE_ID	0x01000000
#define FILE_SUPPORTS_EXTENDED_ATTRIBUTES 0x00800000
#define FILE_SUPPORTS_HARD_LINKS	0x00400000
#define FILE_SUPPORTS_TRANSACTIONS	0x00200000
#define FILE_SEQUENTIAL_WRITE_ONCE	0x00100000
#define FILE_READ_ONLY_VOLUME		0x00080000
#define FILE_NAMED_STREAMS		0x00040000
#define FILE_SUPPORTS_ENCRYPTION	0x00020000
#define FILE_SUPPORTS_OBJECT_IDS	0x00010000
#define FILE_VOLUME_IS_COMPRESSED	0x00008000
#define FILE_SUPPORTS_POSIX_UNLINK_RENAME 0x00000400
#define FILE_RETURNS_CLEANUP_RESULT_INFO  0x00000200
#define FILE_SUPPORTS_REMOTE_STORAGE	0x00000100
#define FILE_SUPPORTS_REPARSE_POINTS	0x00000080
#define FILE_SUPPORTS_SPARSE_FILES	0x00000040
#define FILE_VOLUME_QUOTAS		0x00000020
#define FILE_FILE_COMPRESSION		0x00000010
#define FILE_PERSISTENT_ACLS		0x00000008
#define FILE_UNICODE_ON_DISK		0x00000004
#define FILE_CASE_PRESERVED_NAMES	0x00000002
#define FILE_CASE_SENSITIVE_SEARCH	0x00000001

/* See MS-FSCC 2.5.8 */
typedef struct {
	__le64 TotalAllocationUnits;
	__le64 AvailableAllocationUnits;
	__le32 SectorsPerAllocationUnit;
	__le32 BytesPerSector;
} __packed FILE_SYSTEM_SIZE_INFO;	/* size info, level 0x103 */

/* See MS-FSCC 2.5.10 */
typedef struct {
	__le32 DeviceType;
	__le32 DeviceCharacteristics;
} __packed FILE_SYSTEM_DEVICE_INFO; /* device info level 0x104 */

/*
 * File Attributes
 * See MS-FSCC 2.6
 */
#define FILE_ATTRIBUTE_READONLY			0x00000001
#define FILE_ATTRIBUTE_HIDDEN			0x00000002
#define FILE_ATTRIBUTE_SYSTEM			0x00000004
#define FILE_ATTRIBUTE_DIRECTORY		0x00000010
#define FILE_ATTRIBUTE_ARCHIVE			0x00000020
#define FILE_ATTRIBUTE_NORMAL			0x00000080
#define FILE_ATTRIBUTE_TEMPORARY		0x00000100
#define FILE_ATTRIBUTE_SPARSE_FILE		0x00000200
#define FILE_ATTRIBUTE_REPARSE_POINT		0x00000400
#define FILE_ATTRIBUTE_COMPRESSED		0x00000800
#define FILE_ATTRIBUTE_OFFLINE			0x00001000
#define FILE_ATTRIBUTE_NOT_CONTENT_INDEXED	0x00002000
#define FILE_ATTRIBUTE_ENCRYPTED		0x00004000
#define FILE_ATTRIBUTE_INTEGRITY_STREAM		0x00008000
#define FILE_ATTRIBUTE_NO_SCRUB_DATA		0x00020000
#define FILE_ATTRIBUTE_MASK (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | \
		FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_DIRECTORY | \
		FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NORMAL | \
		FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_SPARSE_FILE | \
		FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_COMPRESSED | \
		FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED | \
		FILE_ATTRIBUTE_ENCRYPTED | FILE_ATTRIBUTE_INTEGRITY_STREAM | \
		FILE_ATTRIBUTE_NO_SCRUB_DATA)

#define FILE_ATTRIBUTE_READONLY_LE		cpu_to_le32(FILE_ATTRIBUTE_READONLY)
#define FILE_ATTRIBUTE_HIDDEN_LE		cpu_to_le32(FILE_ATTRIBUTE_HIDDEN)
#define FILE_ATTRIBUTE_SYSTEM_LE		cpu_to_le32(FILE_ATTRIBUTE_SYSTEM)
#define FILE_ATTRIBUTE_DIRECTORY_LE		cpu_to_le32(FILE_ATTRIBUTE_DIRECTORY)
#define FILE_ATTRIBUTE_ARCHIVE_LE		cpu_to_le32(FILE_ATTRIBUTE_ARCHIVE)
#define FILE_ATTRIBUTE_NORMAL_LE		cpu_to_le32(FILE_ATTRIBUTE_NORMAL)
#define FILE_ATTRIBUTE_TEMPORARY_LE		cpu_to_le32(FILE_ATTRIBUTE_TEMPORARY)
#define FILE_ATTRIBUTE_SPARSE_FILE_LE		cpu_to_le32(FILE_ATTRIBUTE_SPARSE_FILE)
#define FILE_ATTRIBUTE_REPARSE_POINT_LE		cpu_to_le32(FILE_ATTRIBUTE_REPARSE_POINT)
#define FILE_ATTRIBUTE_COMPRESSED_LE		cpu_to_le32(FILE_ATTRIBUTE_COMPRESSED)
#define FILE_ATTRIBUTE_OFFLINE_LE		cpu_to_le32(FILE_ATTRIBUTE_OFFLINE)
#define FILE_ATTRIBUTE_NOT_CONTENT_INDEXED_LE	cpu_to_le32(FILE_ATTRIBUTE_NOT_CONTENT_INDEXED)
#define FILE_ATTRIBUTE_ENCRYPTED_LE		cpu_to_le32(FILE_ATTRIBUTE_ENCRYPTED)
#define FILE_ATTRIBUTE_INTEGRITY_STREAM_LE	cpu_to_le32(FILE_ATTRIBUTE_INTEGRITY_STREAM)
#define FILE_ATTRIBUTE_NO_SCRUB_DATA_LE		cpu_to_le32(FILE_ATTRIBUTE_NO_SCRUB_DATA)
#define FILE_ATTRIBUTE_MASK_LE			cpu_to_le32(FILE_ATTRIBUTE_MASK)

/*
 * Response contains array of the following structures
 * See MS-FSCC 2.7.1
 */
struct file_notify_information {
	__le32 NextEntryOffset;
	__le32 Action;
	__le32 FileNameLength;
	__u8  FileName[];
} __packed;

/*
 * See POSIX Extensions to MS-FSCC 2.3.2.1
 * Link: https://gitlab.com/samba-team/smb3-posix-spec/-/blob/master/fscc_posix_extensions.md
 */
typedef struct {
	/* For undefined recommended transfer size return -1 in that field */
	__le32 OptimalTransferSize;  /* bsize on some os, iosize on other os */
	__le32 BlockSize;
	/* The next three fields are in terms of the block size.
	 * (above). If block size is unknown, 4096 would be a
	 * reasonable block size for a server to report.
	 * Note that returning the blocks/blocksavail removes need
	 * to make a second call (to QFSInfo level 0x103 to get this info.
	 * UserBlockAvail is typically less than or equal to BlocksAvail,
	 * if no distinction is made return the same value in each
	 */
	__le64 TotalBlocks;
	__le64 BlocksAvail;       /* bfree */
	__le64 UserBlocksAvail;   /* bavail */
	/* For undefined Node fields or FSID return -1 */
	__le64 TotalFileNodes;
	__le64 FreeFileNodes;
	__le64 FileSysIdentifier;   /* fsid */
	/* NB Namelen comes from FILE_SYSTEM_ATTRIBUTE_INFO call */
	/* NB flags can come from FILE_SYSTEM_DEVICE_INFO call   */
} __packed FILE_SYSTEM_POSIX_INFO;

#endif /* _COMMON_SMB_FSCC_H */
