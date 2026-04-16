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

/* Reparse structures - see MS-FSCC 2.1.2 */

/* struct fsctl_reparse_info_req is empty, only response structs (see below) */
struct reparse_data_buffer {
	__le32	ReparseTag;
	__le16	ReparseDataLength;
	__u16	Reserved;
	__u8	DataBuffer[]; /* Variable Length */
} __packed;

struct reparse_guid_data_buffer {
	__le32	ReparseTag;
	__le16	ReparseDataLength;
	__u16	Reserved;
	__u8	ReparseGuid[16];
	__u8	DataBuffer[]; /* Variable Length */
} __packed;

struct reparse_mount_point_data_buffer {
	__le32	ReparseTag;
	__le16	ReparseDataLength;
	__u16	Reserved;
	__le16	SubstituteNameOffset;
	__le16	SubstituteNameLength;
	__le16	PrintNameOffset;
	__le16	PrintNameLength;
	__u8	PathBuffer[]; /* Variable Length */
} __packed;

#define SYMLINK_FLAG_RELATIVE 0x00000001

struct reparse_symlink_data_buffer {
	__le32	ReparseTag;
	__le16	ReparseDataLength;
	__u16	Reserved;
	__le16	SubstituteNameOffset;
	__le16	SubstituteNameLength;
	__le16	PrintNameOffset;
	__le16	PrintNameLength;
	__le32	Flags;
	__u8	PathBuffer[]; /* Variable Length */
} __packed;

/* For IO_REPARSE_TAG_NFS - see MS-FSCC 2.1.2.6 */
#define NFS_SPECFILE_LNK	0x00000000014B4E4C
#define NFS_SPECFILE_CHR	0x0000000000524843
#define NFS_SPECFILE_BLK	0x00000000004B4C42
#define NFS_SPECFILE_FIFO	0x000000004F464946
#define NFS_SPECFILE_SOCK	0x000000004B434F53
struct reparse_nfs_data_buffer {
	__le32	ReparseTag;
	__le16	ReparseDataLength;
	__u16	Reserved;
	__le64	InodeType; /* NFS_SPECFILE_* */
	__u8	DataBuffer[];
} __packed;

/* For IO_REPARSE_TAG_LX_SYMLINK - see MS-FSCC 2.1.2.7 */
struct reparse_wsl_symlink_data_buffer {
	__le32	ReparseTag;
	__le16	ReparseDataLength;
	__u16	Reserved;
	__le32	Version; /* Always 2 */
	__u8	Target[]; /* Variable Length UTF-8 string without nul-term */
} __packed;

/* See MS-FSCC 2.3.7 */
struct duplicate_extents_to_file {
	__u64 PersistentFileHandle; /* source file handle, opaque endianness */
	__u64 VolatileFileHandle;
	__le64 SourceFileOffset;
	__le64 TargetFileOffset;
	__le64 ByteCount;  /* Bytes to be copied */
} __packed;

/* See MS-FSCC 2.3.9 */
#define DUPLICATE_EXTENTS_DATA_EX_SOURCE_ATOMIC	0x00000001
struct duplicate_extents_to_file_ex {
	__le64 StructureSize; /* MUST be set to 0x30 */
	__u64 PersistentFileHandle; /* source file handle, opaque endianness */
	__u64 VolatileFileHandle;
	__le64 SourceFileOffset;
	__le64 TargetFileOffset;
	__le64 ByteCount;  /* Bytes to be copied */
	__le32 Flags;
	__le32 Reserved;
} __packed;

/* See MS-FSCC 2.3.20 */
struct fsctl_get_integrity_information_rsp {
	__le16	ChecksumAlgorithm;
	__le16	Reserved;
	__le32	Flags;
	__le32	ChecksumChunkSizeInBytes;
	__le32	ClusterSizeInBytes;
} __packed;

/* See MS-FSCC 2.3.52 */
struct file_allocated_range_buffer {
	__le64	file_offset;
	__le64	length;
} __packed;

/* See MS-FSCC 2.3.55 */
struct fsctl_query_file_regions_req {
	__le64	FileOffset;
	__le64	Length;
	__le32	DesiredUsage;
	__le32	Reserved;
} __packed;

/* DesiredUsage flags see MS-FSCC 2.3.56.1 */
#define FILE_USAGE_INVALID_RANGE	0x00000000
#define FILE_USAGE_VALID_CACHED_DATA	0x00000001
#define FILE_USAGE_NONCACHED_DATA	0x00000002
struct file_region_info {
	__le64	FileOffset;
	__le64	Length;
	__le32	DesiredUsage;
	__le32	Reserved;
} __packed;

/* See MS-FSCC 2.3.56 */
struct fsctl_query_file_region_rsp {
	__le32 Flags;
	__le32 TotalRegionEntryCount;
	__le32 RegionEntryCount;
	__u32  Reserved;
	struct  file_region_info Regions[];
} __packed;

/* See MS-FSCC 2.3.58 */
struct fsctl_query_on_disk_vol_info_rsp {
	__le64	DirectoryCount;
	__le64	FileCount;
	__le16	FsFormatMajVersion;
	__le16	FsFormatMinVersion;
	__u8	FsFormatName[24];
	__le64	FormatTime;
	__le64	LastUpdateTime;
	__u8	CopyrightInfo[68];
	__u8	AbstractInfo[68];
	__u8	FormatImplInfo[68];
	__u8	LastModifyImplInfo[68];
} __packed;

/* See MS-FSCC 2.3.73 */
struct fsctl_set_integrity_information_req {
	__le16	ChecksumAlgorithm;
	__le16	Reserved;
	__le32	Flags;
} __packed;

/* See MS-FSCC 2.3.75 */
struct fsctl_set_integrity_info_ex_req {
	__u8	EnableIntegrity;
	__u8	KeepState;
	__u16	Reserved;
	__le32	Flags;
	__u8	Version;
	__u8	Reserved2[7];
} __packed;

/*
 * this goes in the ioctl buffer when doing FSCTL_SET_ZERO_DATA
 * See MS-FSCC 2.3.85
 */
struct file_zero_data_information {
	__le64	FileOffset;
	__le64	BeyondFinalZero;
} __packed;

/*
 * This level 18, although with struct with same name is different from cifs
 * level 0x107. Level 0x107 has an extra u64 between AccessFlags and
 * CurrentByteOffset.
 * See MS-FSCC 2.4.2
 */
struct smb2_file_all_info { /* data block encoding of response to level 18 */
	__le64 CreationTime;	/* Beginning of FILE_BASIC_INFO equivalent */
	__le64 LastAccessTime;
	__le64 LastWriteTime;
	__le64 ChangeTime;
	__le32 Attributes;
	__u32  Pad1;		/* End of FILE_BASIC_INFO_INFO equivalent */
	__le64 AllocationSize;	/* Beginning of FILE_STANDARD_INFO equivalent */
	__le64 EndOfFile;	/* size ie offset to first free byte in file */
	__le32 NumberOfLinks;	/* hard links */
	__u8   DeletePending;
	__u8   Directory;
	__u16  Pad2;		/* End of FILE_STANDARD_INFO equivalent */
	__le64 IndexNumber;
	__le32 EASize;
	__le32 AccessFlags;
	__le64 CurrentByteOffset;
	__le32 Mode;
	__le32 AlignmentRequirement;
	__le32 FileNameLength;
	union {
		char __pad;	/* Legacy structure padding */
		DECLARE_FLEX_ARRAY(char, FileName);
	};
} __packed; /* level 18 Query */

/* See MS-FSCC 2.4.7 */
typedef struct file_basic_info { /* data block encoding of response to level 18 */
	__le64 CreationTime;
	__le64 LastAccessTime;
	__le64 LastWriteTime;
	__le64 ChangeTime;
	__le32 Attributes;
	__u32  Pad;
} __packed FILE_BASIC_INFO;	/* size info, level 0x101 */

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

/* See MS-FSCC 2.4.13 */
struct smb2_file_eof_info { /* encoding of request for level 10 */
	__le64 EndOfFile; /* new end of file value */
} __packed; /* level 20 Set */

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

/* See MS-FSCC 2.4.27 */
struct smb2_file_internal_info {
	__le64 IndexNumber;
} __packed; /* level 6 Query */

/* See MS-FSCC 2.4.28.2 */
struct smb2_file_link_info { /* encoding of request for level 11 */
	/* New members MUST be added within the struct_group() macro below. */
	__struct_group(smb2_file_link_info_hdr, __hdr, __packed,
		__u8   ReplaceIfExists; /* 1 = replace existing link with new */
					/* 0 = fail if link already exists */
		__u8   Reserved[7];
		__u64  RootDirectory;  /* MBZ for network operations (why says spec?) */
		__le32 FileNameLength;
	);
	char   FileName[];     /* Name to be assigned to new link */
} __packed; /* level 11 Set */
static_assert(offsetof(struct smb2_file_link_info, FileName) == sizeof(struct smb2_file_link_info_hdr),
	      "struct member likely outside of __struct_group()");

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

/* See MS-FSCC 2.4.42.2 */
struct smb2_file_rename_info { /* encoding of request for level 10 */
	/* New members MUST be added within the struct_group() macro below. */
	__struct_group(smb2_file_rename_info_hdr, __hdr, __packed,
		__u8   ReplaceIfExists; /* 1 = replace existing target with new */
					/* 0 = fail if target already exists */
		__u8   Reserved[7];
		__u64  RootDirectory;  /* MBZ for network operations (why says spec?) */
		__le32 FileNameLength;
	);
	char   FileName[];     /* New name to be assigned */
	/* padding - overall struct size must be >= 24 so filename + pad >= 6 */
} __packed; /* level 10 Set */
static_assert(offsetof(struct smb2_file_rename_info, FileName) == sizeof(struct smb2_file_rename_info_hdr),
	      "struct member likely outside of __struct_group()");

/* File System Information Classes */
/* See MS-FSCC 2.5 */
#define FS_VOLUME_INFORMATION		1 /* Query */
#define FS_LABEL_INFORMATION		2 /* Set */
#define FS_SIZE_INFORMATION		3 /* Query */
#define FS_DEVICE_INFORMATION		4 /* Query */
#define FS_ATTRIBUTE_INFORMATION	5 /* Query */
#define FS_CONTROL_INFORMATION		6 /* Query, Set */
#define FS_FULL_SIZE_INFORMATION	7 /* Query */
#define FS_OBJECT_ID_INFORMATION	8 /* Query, Set */
#define FS_DRIVER_PATH_INFORMATION	9 /* Query */
#define FS_SECTOR_SIZE_INFORMATION	11 /* SMB3 or later. Query */
/* See POSIX Extensions to MS-FSCC 2.3.1.1 */
#define FS_POSIX_INFORMATION		100 /* SMB3.1.1 POSIX. Query */

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

/*
 * File System Control Information
 * See MS-FSCC 2.5.2
 */
struct smb2_fs_control_info {
	__le64 FreeSpaceStartFiltering;
	__le64 FreeSpaceThreshold;
	__le64 FreeSpaceStopFiltering;
	__le64 DefaultQuotaThreshold;
	__le64 DefaultQuotaLimit;
	__le32 FileSystemControlFlags;
	__le32 Padding;
} __packed;

/* See MS-FSCC 2.5.4 */
struct smb2_fs_full_size_info {
	__le64 TotalAllocationUnits;
	__le64 CallerAvailableAllocationUnits;
	__le64 ActualAvailableAllocationUnits;
	__le32 SectorsPerAllocationUnit;
	__le32 BytesPerSector;
} __packed;

/* See MS-FSCC 2.5.7 */
#define SSINFO_FLAGS_ALIGNED_DEVICE		0x00000001
#define SSINFO_FLAGS_PARTITION_ALIGNED_ON_DEVICE 0x00000002
#define SSINFO_FLAGS_NO_SEEK_PENALTY		0x00000004
#define SSINFO_FLAGS_TRIM_ENABLED		0x00000008
/* sector size info struct */
struct smb3_fs_ss_info {
	__le32 LogicalBytesPerSector;
	__le32 PhysicalBytesPerSectorForAtomicity;
	__le32 PhysicalBytesPerSectorForPerf;
	__le32 FSEffPhysicalBytesPerSectorForAtomicity;
	__le32 Flags;
	__le32 ByteOffsetForSectorAlignment;
	__le32 ByteOffsetForPartitionAlignment;
} __packed;

/* See MS-FSCC 2.5.8 */
typedef struct {
	__le64 TotalAllocationUnits;
	__le64 AvailableAllocationUnits;
	__le32 SectorsPerAllocationUnit;
	__le32 BytesPerSector;
} __packed FILE_SYSTEM_SIZE_INFO;	/* size info, level 0x103 */

/* volume info struct - see MS-FSCC 2.5.9 */
#define MAX_VOL_LABEL_LEN	32
struct filesystem_vol_info {
	__le64	VolumeCreationTime;
	__le32	VolumeSerialNumber;
	__le32	VolumeLabelLength; /* includes trailing null */
	__u8	SupportsObjects; /* True if eg like NTFS, supports objects */
	__u8	Reserved;
	__u8	VolumeLabel[]; /* variable len */
} __packed;

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
 * SMB2 Notify Action Flags
 * See MS-FSCC 2.7.1
 */
#define FILE_ACTION_ADDED                       0x00000001
#define FILE_ACTION_REMOVED                     0x00000002
#define FILE_ACTION_MODIFIED                    0x00000003
#define FILE_ACTION_RENAMED_OLD_NAME            0x00000004
#define FILE_ACTION_RENAMED_NEW_NAME            0x00000005
#define FILE_ACTION_ADDED_STREAM                0x00000006
#define FILE_ACTION_REMOVED_STREAM              0x00000007
#define FILE_ACTION_MODIFIED_STREAM             0x00000008
#define FILE_ACTION_REMOVED_BY_DELETE           0x00000009
#define FILE_ACTION_ID_NOT_TUNNELLED            0x0000000A
#define FILE_ACTION_TUNNELLED_ID_COLLISION      0x0000000B

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
