/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2016 Namjae Jeon <linkinjeon@kernel.org>
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#ifndef _SMB2PDU_H
#define _SMB2PDU_H

#include "ntlmssp.h"
#include "smbacl.h"

/*Create Action Flags*/
#define FILE_SUPERSEDED                0x00000000
#define FILE_OPENED            0x00000001
#define FILE_CREATED           0x00000002
#define FILE_OVERWRITTEN       0x00000003

/*
 * Size of the session key (crypto key encrypted with the password
 */
#define SMB2_NTLMV2_SESSKEY_SIZE	16
#define SMB2_SIGNATURE_SIZE		16
#define SMB2_HMACSHA256_SIZE		32
#define SMB2_CMACAES_SIZE		16
#define SMB3_GCM128_CRYPTKEY_SIZE	16
#define SMB3_GCM256_CRYPTKEY_SIZE	32

/*
 * Size of the smb3 encryption/decryption keys
 */
#define SMB3_ENC_DEC_KEY_SIZE		32

/*
 * Size of the smb3 signing key
 */
#define SMB3_SIGN_KEY_SIZE		16

#define CIFS_CLIENT_CHALLENGE_SIZE	8
#define SMB_SERVER_CHALLENGE_SIZE	8

/* SMB2 Max Credits */
#define SMB2_MAX_CREDITS		8192

/* Maximum buffer size value we can send with 1 credit */
#define SMB2_MAX_BUFFER_SIZE 65536

#define NUMBER_OF_SMB2_COMMANDS	0x0013

/* BB FIXME - analyze following length BB */
#define MAX_SMB2_HDR_SIZE 0x78 /* 4 len + 64 hdr + (2*24 wct) + 2 bct + 2 pad */

#define SMB21_DEFAULT_IOSIZE	(1024 * 1024)
#define SMB3_DEFAULT_IOSIZE	(4 * 1024 * 1024)
#define SMB3_DEFAULT_TRANS_SIZE	(1024 * 1024)
#define SMB3_MIN_IOSIZE	(64 * 1024)
#define SMB3_MAX_IOSIZE	(8 * 1024 * 1024)

/*
 *	Definitions for SMB2 Protocol Data Units (network frames)
 *
 *  See MS-SMB2.PDF specification for protocol details.
 *  The Naming convention is the lower case version of the SMB2
 *  command code name for the struct. Note that structures must be packed.
 *
 */

#define SMB2_ERROR_STRUCTURE_SIZE2	9
#define SMB2_ERROR_STRUCTURE_SIZE2_LE	cpu_to_le16(SMB2_ERROR_STRUCTURE_SIZE2)

struct smb2_err_rsp {
	struct smb2_hdr hdr;
	__le16 StructureSize;
	__u8   ErrorContextCount;
	__u8   Reserved;
	__le32 ByteCount;  /* even if zero, at least one byte follows */
	__u8   ErrorData[1];  /* variable length */
} __packed;

struct preauth_integrity_info {
	/* PreAuth integrity Hash ID */
	__le16			Preauth_HashId;
	/* PreAuth integrity Hash Value */
	__u8			Preauth_HashValue[SMB2_PREAUTH_HASH_SIZE];
};

/* offset is sizeof smb2_negotiate_rsp but rounded up to 8 bytes. */
#ifdef CONFIG_SMB_SERVER_KERBEROS5
/* sizeof(struct smb2_negotiate_rsp) =
 * header(64) + response(64) + GSS_LENGTH(96) + GSS_PADDING(0)
 */
#define OFFSET_OF_NEG_CONTEXT	0xe0
#else
/* sizeof(struct smb2_negotiate_rsp) =
 * header(64) + response(64) + GSS_LENGTH(74) + GSS_PADDING(6)
 */
#define OFFSET_OF_NEG_CONTEXT	0xd0
#endif

#define SMB2_SESSION_EXPIRED		(0)
#define SMB2_SESSION_IN_PROGRESS	BIT(0)
#define SMB2_SESSION_VALID		BIT(1)

struct create_durable_req_v2 {
	struct create_context ccontext;
	__u8   Name[8];
	__le32 Timeout;
	__le32 Flags;
	__u8 Reserved[8];
	__u8 CreateGuid[16];
} __packed;

struct create_durable_reconn_req {
	struct create_context ccontext;
	__u8   Name[8];
	union {
		__u8  Reserved[16];
		struct {
			__le64 PersistentFileId;
			__le64 VolatileFileId;
		} Fid;
	} Data;
} __packed;

struct create_durable_reconn_v2_req {
	struct create_context ccontext;
	__u8   Name[8];
	struct {
		__le64 PersistentFileId;
		__le64 VolatileFileId;
	} Fid;
	__u8 CreateGuid[16];
	__le32 Flags;
} __packed;

struct create_app_inst_id {
	struct create_context ccontext;
	__u8 Name[8];
	__u8 Reserved[8];
	__u8 AppInstanceId[16];
} __packed;

struct create_app_inst_id_vers {
	struct create_context ccontext;
	__u8 Name[8];
	__u8 Reserved[2];
	__u8 Padding[4];
	__le64 AppInstanceVersionHigh;
	__le64 AppInstanceVersionLow;
} __packed;

struct create_mxac_req {
	struct create_context ccontext;
	__u8   Name[8];
	__le64 Timestamp;
} __packed;

struct create_alloc_size_req {
	struct create_context ccontext;
	__u8   Name[8];
	__le64 AllocationSize;
} __packed;

struct create_posix {
	struct create_context ccontext;
	__u8    Name[16];
	__le32  Mode;
	__u32   Reserved;
} __packed;

struct create_durable_rsp {
	struct create_context ccontext;
	__u8   Name[8];
	union {
		__u8  Reserved[8];
		__u64 data;
	} Data;
} __packed;

struct create_durable_v2_rsp {
	struct create_context ccontext;
	__u8   Name[8];
	__le32 Timeout;
	__le32 Flags;
} __packed;

struct create_mxac_rsp {
	struct create_context ccontext;
	__u8   Name[8];
	__le32 QueryStatus;
	__le32 MaximalAccess;
} __packed;

struct create_disk_id_rsp {
	struct create_context ccontext;
	__u8   Name[8];
	__le64 DiskFileId;
	__le64 VolumeId;
	__u8  Reserved[16];
} __packed;

/* equivalent of the contents of SMB3.1.1 POSIX open context response */
struct create_posix_rsp {
	struct create_context ccontext;
	__u8    Name[16];
	__le32 nlink;
	__le32 reparse_tag;
	__le32 mode;
	u8 SidBuffer[40];
} __packed;

#define SMB2_LEASE_NONE_LE			cpu_to_le32(0x00)
#define SMB2_LEASE_READ_CACHING_LE		cpu_to_le32(0x01)
#define SMB2_LEASE_HANDLE_CACHING_LE		cpu_to_le32(0x02)
#define SMB2_LEASE_WRITE_CACHING_LE		cpu_to_le32(0x04)

#define SMB2_LEASE_FLAG_BREAK_IN_PROGRESS_LE	cpu_to_le32(0x02)

#define SMB2_LEASE_KEY_SIZE			16

struct lease_context {
	__u8 LeaseKey[SMB2_LEASE_KEY_SIZE];
	__le32 LeaseState;
	__le32 LeaseFlags;
	__le64 LeaseDuration;
} __packed;

struct lease_context_v2 {
	__u8 LeaseKey[SMB2_LEASE_KEY_SIZE];
	__le32 LeaseState;
	__le32 LeaseFlags;
	__le64 LeaseDuration;
	__u8 ParentLeaseKey[SMB2_LEASE_KEY_SIZE];
	__le16 Epoch;
	__le16 Reserved;
} __packed;

struct create_lease {
	struct create_context ccontext;
	__u8   Name[8];
	struct lease_context lcontext;
} __packed;

struct create_lease_v2 {
	struct create_context ccontext;
	__u8   Name[8];
	struct lease_context_v2 lcontext;
	__u8   Pad[4];
} __packed;

struct smb2_buffer_desc_v1 {
	__le64 offset;
	__le32 token;
	__le32 length;
} __packed;

#define SMB2_0_IOCTL_IS_FSCTL 0x00000001

struct duplicate_extents_to_file {
	__u64 PersistentFileHandle; /* source file handle, opaque endianness */
	__u64 VolatileFileHandle;
	__le64 SourceFileOffset;
	__le64 TargetFileOffset;
	__le64 ByteCount;  /* Bytes to be copied */
} __packed;

struct smb2_ioctl_req {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 57 */
	__le16 Reserved; /* offset from start of SMB2 header to write data */
	__le32 CntCode;
	__le64  PersistentFileId;
	__le64  VolatileFileId;
	__le32 InputOffset; /* Reserved MBZ */
	__le32 InputCount;
	__le32 MaxInputResponse;
	__le32 OutputOffset;
	__le32 OutputCount;
	__le32 MaxOutputResponse;
	__le32 Flags;
	__le32 Reserved2;
	__u8   Buffer[1];
} __packed;

struct smb2_ioctl_rsp {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 49 */
	__le16 Reserved; /* offset from start of SMB2 header to write data */
	__le32 CntCode;
	__le64  PersistentFileId;
	__le64  VolatileFileId;
	__le32 InputOffset; /* Reserved MBZ */
	__le32 InputCount;
	__le32 OutputOffset;
	__le32 OutputCount;
	__le32 Flags;
	__le32 Reserved2;
	__u8   Buffer[1];
} __packed;

struct validate_negotiate_info_req {
	__le32 Capabilities;
	__u8   Guid[SMB2_CLIENT_GUID_SIZE];
	__le16 SecurityMode;
	__le16 DialectCount;
	__le16 Dialects[1]; /* dialect (someday maybe list) client asked for */
} __packed;

struct validate_negotiate_info_rsp {
	__le32 Capabilities;
	__u8   Guid[SMB2_CLIENT_GUID_SIZE];
	__le16 SecurityMode;
	__le16 Dialect; /* Dialect in use for the connection */
} __packed;

struct smb_sockaddr_in {
	__be16 Port;
	__be32 IPv4address;
	__u8 Reserved[8];
} __packed;

struct smb_sockaddr_in6 {
	__be16 Port;
	__be32 FlowInfo;
	__u8 IPv6address[16];
	__be32 ScopeId;
} __packed;

#define INTERNETWORK	0x0002
#define INTERNETWORKV6	0x0017

struct sockaddr_storage_rsp {
	__le16 Family;
	union {
		struct smb_sockaddr_in addr4;
		struct smb_sockaddr_in6 addr6;
	};
} __packed;

#define RSS_CAPABLE	0x00000001
#define RDMA_CAPABLE	0x00000002

struct network_interface_info_ioctl_rsp {
	__le32 Next; /* next interface. zero if this is last one */
	__le32 IfIndex;
	__le32 Capability; /* RSS or RDMA Capable */
	__le32 Reserved;
	__le64 LinkSpeed;
	char	SockAddr_Storage[128];
} __packed;

struct file_object_buf_type1_ioctl_rsp {
	__u8 ObjectId[16];
	__u8 BirthVolumeId[16];
	__u8 BirthObjectId[16];
	__u8 DomainId[16];
} __packed;

struct resume_key_ioctl_rsp {
	__le64 ResumeKey[3];
	__le32 ContextLength;
	__u8 Context[4]; /* ignored, Windows sets to 4 bytes of zero */
} __packed;

struct copychunk_ioctl_req {
	__le64 ResumeKey[3];
	__le32 ChunkCount;
	__le32 Reserved;
	__u8 Chunks[1]; /* array of srv_copychunk */
} __packed;

struct srv_copychunk {
	__le64 SourceOffset;
	__le64 TargetOffset;
	__le32 Length;
	__le32 Reserved;
} __packed;

struct copychunk_ioctl_rsp {
	__le32 ChunksWritten;
	__le32 ChunkBytesWritten;
	__le32 TotalBytesWritten;
} __packed;

struct file_sparse {
	__u8	SetSparse;
} __packed;

struct file_zero_data_information {
	__le64	FileOffset;
	__le64	BeyondFinalZero;
} __packed;

struct file_allocated_range_buffer {
	__le64	file_offset;
	__le64	length;
} __packed;

struct reparse_data_buffer {
	__le32	ReparseTag;
	__le16	ReparseDataLength;
	__u16	Reserved;
	__u8	DataBuffer[]; /* Variable Length */
} __packed;

/* SMB2 Notify Action Flags */
#define FILE_ACTION_ADDED		0x00000001
#define FILE_ACTION_REMOVED		0x00000002
#define FILE_ACTION_MODIFIED		0x00000003
#define FILE_ACTION_RENAMED_OLD_NAME	0x00000004
#define FILE_ACTION_RENAMED_NEW_NAME	0x00000005
#define FILE_ACTION_ADDED_STREAM	0x00000006
#define FILE_ACTION_REMOVED_STREAM	0x00000007
#define FILE_ACTION_MODIFIED_STREAM	0x00000008
#define FILE_ACTION_REMOVED_BY_DELETE	0x00000009

#define SMB2_LOCKFLAG_SHARED		0x0001
#define SMB2_LOCKFLAG_EXCLUSIVE		0x0002
#define SMB2_LOCKFLAG_UNLOCK		0x0004
#define SMB2_LOCKFLAG_FAIL_IMMEDIATELY	0x0010
#define SMB2_LOCKFLAG_MASK		0x0007

struct smb2_lock_element {
	__le64 Offset;
	__le64 Length;
	__le32 Flags;
	__le32 Reserved;
} __packed;

struct smb2_lock_req {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 48 */
	__le16 LockCount;
	__le32 Reserved;
	__le64  PersistentFileId;
	__le64  VolatileFileId;
	/* Followed by at least one */
	struct smb2_lock_element locks[1];
} __packed;

struct smb2_lock_rsp {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 4 */
	__le16 Reserved;
} __packed;

struct smb2_echo_req {
	struct smb2_hdr hdr;
	__le16 StructureSize;	/* Must be 4 */
	__u16  Reserved;
} __packed;

struct smb2_echo_rsp {
	struct smb2_hdr hdr;
	__le16 StructureSize;	/* Must be 4 */
	__u16  Reserved;
} __packed;

/* search (query_directory) Flags field */
#define SMB2_RESTART_SCANS		0x01
#define SMB2_RETURN_SINGLE_ENTRY	0x02
#define SMB2_INDEX_SPECIFIED		0x04
#define SMB2_REOPEN			0x10

struct smb2_query_directory_req {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 33 */
	__u8   FileInformationClass;
	__u8   Flags;
	__le32 FileIndex;
	__le64  PersistentFileId;
	__le64  VolatileFileId;
	__le16 FileNameOffset;
	__le16 FileNameLength;
	__le32 OutputBufferLength;
	__u8   Buffer[1];
} __packed;

struct smb2_query_directory_rsp {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 9 */
	__le16 OutputBufferOffset;
	__le32 OutputBufferLength;
	__u8   Buffer[1];
} __packed;

/* Possible InfoType values */
#define SMB2_O_INFO_FILE	0x01
#define SMB2_O_INFO_FILESYSTEM	0x02
#define SMB2_O_INFO_SECURITY	0x03
#define SMB2_O_INFO_QUOTA	0x04

/* Security info type additionalinfo flags. See MS-SMB2 (2.2.37) or MS-DTYP */
#define OWNER_SECINFO   0x00000001
#define GROUP_SECINFO   0x00000002
#define DACL_SECINFO   0x00000004
#define SACL_SECINFO   0x00000008
#define LABEL_SECINFO   0x00000010
#define ATTRIBUTE_SECINFO   0x00000020
#define SCOPE_SECINFO   0x00000040
#define BACKUP_SECINFO   0x00010000
#define UNPROTECTED_SACL_SECINFO   0x10000000
#define UNPROTECTED_DACL_SECINFO   0x20000000
#define PROTECTED_SACL_SECINFO   0x40000000
#define PROTECTED_DACL_SECINFO   0x80000000

struct smb2_query_info_req {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 41 */
	__u8   InfoType;
	__u8   FileInfoClass;
	__le32 OutputBufferLength;
	__le16 InputBufferOffset;
	__u16  Reserved;
	__le32 InputBufferLength;
	__le32 AdditionalInformation;
	__le32 Flags;
	__le64  PersistentFileId;
	__le64  VolatileFileId;
	__u8   Buffer[1];
} __packed;

struct smb2_query_info_rsp {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 9 */
	__le16 OutputBufferOffset;
	__le32 OutputBufferLength;
	__u8   Buffer[1];
} __packed;

struct smb2_set_info_req {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 33 */
	__u8   InfoType;
	__u8   FileInfoClass;
	__le32 BufferLength;
	__le16 BufferOffset;
	__u16  Reserved;
	__le32 AdditionalInformation;
	__le64  PersistentFileId;
	__le64  VolatileFileId;
	__u8   Buffer[1];
} __packed;

struct smb2_set_info_rsp {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 2 */
} __packed;

/* FILE Info response size */
#define FILE_DIRECTORY_INFORMATION_SIZE       1
#define FILE_FULL_DIRECTORY_INFORMATION_SIZE  2
#define FILE_BOTH_DIRECTORY_INFORMATION_SIZE  3
#define FILE_BASIC_INFORMATION_SIZE           40
#define FILE_STANDARD_INFORMATION_SIZE        24
#define FILE_INTERNAL_INFORMATION_SIZE        8
#define FILE_EA_INFORMATION_SIZE              4
#define FILE_ACCESS_INFORMATION_SIZE          4
#define FILE_NAME_INFORMATION_SIZE            9
#define FILE_RENAME_INFORMATION_SIZE          10
#define FILE_LINK_INFORMATION_SIZE            11
#define FILE_NAMES_INFORMATION_SIZE           12
#define FILE_DISPOSITION_INFORMATION_SIZE     13
#define FILE_POSITION_INFORMATION_SIZE        14
#define FILE_FULL_EA_INFORMATION_SIZE         15
#define FILE_MODE_INFORMATION_SIZE            4
#define FILE_ALIGNMENT_INFORMATION_SIZE       4
#define FILE_ALL_INFORMATION_SIZE             104
#define FILE_ALLOCATION_INFORMATION_SIZE      19
#define FILE_END_OF_FILE_INFORMATION_SIZE     20
#define FILE_ALTERNATE_NAME_INFORMATION_SIZE  8
#define FILE_STREAM_INFORMATION_SIZE          32
#define FILE_PIPE_INFORMATION_SIZE            23
#define FILE_PIPE_LOCAL_INFORMATION_SIZE      24
#define FILE_PIPE_REMOTE_INFORMATION_SIZE     25
#define FILE_MAILSLOT_QUERY_INFORMATION_SIZE  26
#define FILE_MAILSLOT_SET_INFORMATION_SIZE    27
#define FILE_COMPRESSION_INFORMATION_SIZE     16
#define FILE_OBJECT_ID_INFORMATION_SIZE       29
/* Number 30 not defined in documents */
#define FILE_MOVE_CLUSTER_INFORMATION_SIZE    31
#define FILE_QUOTA_INFORMATION_SIZE           32
#define FILE_REPARSE_POINT_INFORMATION_SIZE   33
#define FILE_NETWORK_OPEN_INFORMATION_SIZE    56
#define FILE_ATTRIBUTE_TAG_INFORMATION_SIZE   8

/* FS Info response  size */
#define FS_DEVICE_INFORMATION_SIZE     8
#define FS_ATTRIBUTE_INFORMATION_SIZE  16
#define FS_VOLUME_INFORMATION_SIZE     24
#define FS_SIZE_INFORMATION_SIZE       24
#define FS_FULL_SIZE_INFORMATION_SIZE  32
#define FS_SECTOR_SIZE_INFORMATION_SIZE 28
#define FS_OBJECT_ID_INFORMATION_SIZE 64
#define FS_CONTROL_INFORMATION_SIZE 48
#define FS_POSIX_INFORMATION_SIZE 56

/* FS_ATTRIBUTE_File_System_Name */
#define FS_TYPE_SUPPORT_SIZE   44
struct fs_type_info {
	char		*fs_name;
	long		magic_number;
} __packed;

struct smb2_oplock_break {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 24 */
	__u8   OplockLevel;
	__u8   Reserved;
	__le32 Reserved2;
	__le64  PersistentFid;
	__le64  VolatileFid;
} __packed;

#define SMB2_NOTIFY_BREAK_LEASE_FLAG_ACK_REQUIRED cpu_to_le32(0x01)

struct smb2_lease_break {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 44 */
	__le16 Epoch;
	__le32 Flags;
	__u8   LeaseKey[16];
	__le32 CurrentLeaseState;
	__le32 NewLeaseState;
	__le32 BreakReason;
	__le32 AccessMaskHint;
	__le32 ShareMaskHint;
} __packed;

struct smb2_lease_ack {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 36 */
	__le16 Reserved;
	__le32 Flags;
	__u8   LeaseKey[16];
	__le32 LeaseState;
	__le64 LeaseDuration;
} __packed;

/*
 *	PDU infolevel structure definitions
 *	BB consider moving to a different header
 */

/* File System Information Classes */
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
#define FS_POSIX_INFORMATION		100 /* SMB3.1.1 POSIX. Query */

struct smb2_fs_full_size_info {
	__le64 TotalAllocationUnits;
	__le64 CallerAvailableAllocationUnits;
	__le64 ActualAvailableAllocationUnits;
	__le32 SectorsPerAllocationUnit;
	__le32 BytesPerSector;
} __packed;

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

/* File System Control Information */
struct smb2_fs_control_info {
	__le64 FreeSpaceStartFiltering;
	__le64 FreeSpaceThreshold;
	__le64 FreeSpaceStopFiltering;
	__le64 DefaultQuotaThreshold;
	__le64 DefaultQuotaLimit;
	__le32 FileSystemControlFlags;
	__le32 Padding;
} __packed;

/* partial list of QUERY INFO levels */
#define FILE_DIRECTORY_INFORMATION	1
#define FILE_FULL_DIRECTORY_INFORMATION 2
#define FILE_BOTH_DIRECTORY_INFORMATION 3
#define FILE_BASIC_INFORMATION		4
#define FILE_STANDARD_INFORMATION	5
#define FILE_INTERNAL_INFORMATION	6
#define FILE_EA_INFORMATION	        7
#define FILE_ACCESS_INFORMATION		8
#define FILE_NAME_INFORMATION		9
#define FILE_RENAME_INFORMATION		10
#define FILE_LINK_INFORMATION		11
#define FILE_NAMES_INFORMATION		12
#define FILE_DISPOSITION_INFORMATION	13
#define FILE_POSITION_INFORMATION	14
#define FILE_FULL_EA_INFORMATION	15
#define FILE_MODE_INFORMATION		16
#define FILE_ALIGNMENT_INFORMATION	17
#define FILE_ALL_INFORMATION		18
#define FILE_ALLOCATION_INFORMATION	19
#define FILE_END_OF_FILE_INFORMATION	20
#define FILE_ALTERNATE_NAME_INFORMATION 21
#define FILE_STREAM_INFORMATION		22
#define FILE_PIPE_INFORMATION		23
#define FILE_PIPE_LOCAL_INFORMATION	24
#define FILE_PIPE_REMOTE_INFORMATION	25
#define FILE_MAILSLOT_QUERY_INFORMATION 26
#define FILE_MAILSLOT_SET_INFORMATION	27
#define FILE_COMPRESSION_INFORMATION	28
#define FILE_OBJECT_ID_INFORMATION	29
/* Number 30 not defined in documents */
#define FILE_MOVE_CLUSTER_INFORMATION	31
#define FILE_QUOTA_INFORMATION		32
#define FILE_REPARSE_POINT_INFORMATION	33
#define FILE_NETWORK_OPEN_INFORMATION	34
#define FILE_ATTRIBUTE_TAG_INFORMATION	35
#define FILE_TRACKING_INFORMATION	36
#define FILEID_BOTH_DIRECTORY_INFORMATION 37
#define FILEID_FULL_DIRECTORY_INFORMATION 38
#define FILE_VALID_DATA_LENGTH_INFORMATION 39
#define FILE_SHORT_NAME_INFORMATION	40
#define FILE_SFIO_RESERVE_INFORMATION	44
#define FILE_SFIO_VOLUME_INFORMATION	45
#define FILE_HARD_LINK_INFORMATION	46
#define FILE_NORMALIZED_NAME_INFORMATION 48
#define FILEID_GLOBAL_TX_DIRECTORY_INFORMATION 50
#define FILE_STANDARD_LINK_INFORMATION	54

#define OP_BREAK_STRUCT_SIZE_20		24
#define OP_BREAK_STRUCT_SIZE_21		36

struct smb2_file_access_info {
	__le32 AccessFlags;
} __packed;

struct smb2_file_alignment_info {
	__le32 AlignmentRequirement;
} __packed;

struct smb2_file_internal_info {
	__le64 IndexNumber;
} __packed; /* level 6 Query */

struct smb2_file_rename_info { /* encoding of request for level 10 */
	__u8   ReplaceIfExists; /* 1 = replace existing target with new */
				/* 0 = fail if target already exists */
	__u8   Reserved[7];
	__u64  RootDirectory;  /* MBZ for network operations (why says spec?) */
	__le32 FileNameLength;
	char   FileName[];     /* New name to be assigned */
} __packed; /* level 10 Set */

struct smb2_file_link_info { /* encoding of request for level 11 */
	__u8   ReplaceIfExists; /* 1 = replace existing link with new */
				/* 0 = fail if link already exists */
	__u8   Reserved[7];
	__u64  RootDirectory;  /* MBZ for network operations (why says spec?) */
	__le32 FileNameLength;
	char   FileName[];     /* Name to be assigned to new link */
} __packed; /* level 11 Set */

/*
 * This level 18, although with struct with same name is different from cifs
 * level 0x107. Level 0x107 has an extra u64 between AccessFlags and
 * CurrentByteOffset.
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
	char   FileName[1];
} __packed; /* level 18 Query */

struct smb2_file_basic_info { /* data block encoding of response to level 18 */
	__le64 CreationTime;	/* Beginning of FILE_BASIC_INFO equivalent */
	__le64 LastAccessTime;
	__le64 LastWriteTime;
	__le64 ChangeTime;
	__le32 Attributes;
	__u32  Pad1;		/* End of FILE_BASIC_INFO_INFO equivalent */
} __packed;

struct smb2_file_alt_name_info {
	__le32 FileNameLength;
	char FileName[];
} __packed;

struct smb2_file_stream_info {
	__le32  NextEntryOffset;
	__le32  StreamNameLength;
	__le64 StreamSize;
	__le64 StreamAllocationSize;
	char   StreamName[];
} __packed;

struct smb2_file_eof_info { /* encoding of request for level 10 */
	__le64 EndOfFile; /* new end of file value */
} __packed; /* level 20 Set */

struct smb2_file_ntwrk_info {
	__le64 CreationTime;
	__le64 LastAccessTime;
	__le64 LastWriteTime;
	__le64 ChangeTime;
	__le64 AllocationSize;
	__le64 EndOfFile;
	__le32 Attributes;
	__le32 Reserved;
} __packed;

struct smb2_file_standard_info {
	__le64 AllocationSize;
	__le64 EndOfFile;
	__le32 NumberOfLinks;	/* hard links */
	__u8   DeletePending;
	__u8   Directory;
	__le16 Reserved;
} __packed; /* level 18 Query */

struct smb2_file_ea_info {
	__le32 EASize;
} __packed;

struct smb2_file_alloc_info {
	__le64 AllocationSize;
} __packed;

struct smb2_file_disposition_info {
	__u8 DeletePending;
} __packed;

struct smb2_file_pos_info {
	__le64 CurrentByteOffset;
} __packed;

#define FILE_MODE_INFO_MASK cpu_to_le32(0x0000100e)

struct smb2_file_mode_info {
	__le32 Mode;
} __packed;

#define COMPRESSION_FORMAT_NONE 0x0000
#define COMPRESSION_FORMAT_LZNT1 0x0002

struct smb2_file_comp_info {
	__le64 CompressedFileSize;
	__le16 CompressionFormat;
	__u8 CompressionUnitShift;
	__u8 ChunkShift;
	__u8 ClusterShift;
	__u8 Reserved[3];
} __packed;

struct smb2_file_attr_tag_info {
	__le32 FileAttributes;
	__le32 ReparseTag;
} __packed;

#define SL_RESTART_SCAN	0x00000001
#define SL_RETURN_SINGLE_ENTRY	0x00000002
#define SL_INDEX_SPECIFIED	0x00000004

struct smb2_ea_info_req {
	__le32 NextEntryOffset;
	__u8   EaNameLength;
	char name[1];
} __packed; /* level 15 Query */

struct smb2_ea_info {
	__le32 NextEntryOffset;
	__u8   Flags;
	__u8   EaNameLength;
	__le16 EaValueLength;
	char name[1];
	/* optionally followed by value */
} __packed; /* level 15 Query */

struct create_ea_buf_req {
	struct create_context ccontext;
	__u8   Name[8];
	struct smb2_ea_info ea;
} __packed;

struct create_sd_buf_req {
	struct create_context ccontext;
	__u8   Name[8];
	struct smb_ntsd ntsd;
} __packed;

/* Find File infolevels */
#define SMB_FIND_FILE_POSIX_INFO	0x064

/* Level 100 query info */
struct smb311_posix_qinfo {
	__le64 CreationTime;
	__le64 LastAccessTime;
	__le64 LastWriteTime;
	__le64 ChangeTime;
	__le64 EndOfFile;
	__le64 AllocationSize;
	__le32 DosAttributes;
	__le64 Inode;
	__le32 DeviceId;
	__le32 Zero;
	/* beginning of POSIX Create Context Response */
	__le32 HardLinks;
	__le32 ReparseTag;
	__le32 Mode;
	u8     Sids[];
	/*
	 * var sized owner SID
	 * var sized group SID
	 * le32 filenamelength
	 * u8  filename[]
	 */
} __packed;

struct smb2_posix_info {
	__le32 NextEntryOffset;
	__u32 Ignored;
	__le64 CreationTime;
	__le64 LastAccessTime;
	__le64 LastWriteTime;
	__le64 ChangeTime;
	__le64 EndOfFile;
	__le64 AllocationSize;
	__le32 DosAttributes;
	__le64 Inode;
	__le32 DeviceId;
	__le32 Zero;
	/* beginning of POSIX Create Context Response */
	__le32 HardLinks;
	__le32 ReparseTag;
	__le32 Mode;
	u8 SidBuffer[40];
	__le32 name_len;
	u8 name[1];
	/*
	 * var sized owner SID
	 * var sized group SID
	 * le32 filenamelength
	 * u8  filename[]
	 */
} __packed;

/* functions */
void init_smb2_1_server(struct ksmbd_conn *conn);
void init_smb3_0_server(struct ksmbd_conn *conn);
void init_smb3_02_server(struct ksmbd_conn *conn);
int init_smb3_11_server(struct ksmbd_conn *conn);

void init_smb2_max_read_size(unsigned int sz);
void init_smb2_max_write_size(unsigned int sz);
void init_smb2_max_trans_size(unsigned int sz);
void init_smb2_max_credits(unsigned int sz);

bool is_smb2_neg_cmd(struct ksmbd_work *work);
bool is_smb2_rsp(struct ksmbd_work *work);

u16 get_smb2_cmd_val(struct ksmbd_work *work);
void set_smb2_rsp_status(struct ksmbd_work *work, __le32 err);
int init_smb2_rsp_hdr(struct ksmbd_work *work);
int smb2_allocate_rsp_buf(struct ksmbd_work *work);
bool is_chained_smb2_message(struct ksmbd_work *work);
int init_smb2_neg_rsp(struct ksmbd_work *work);
void smb2_set_err_rsp(struct ksmbd_work *work);
int smb2_check_user_session(struct ksmbd_work *work);
int smb2_get_ksmbd_tcon(struct ksmbd_work *work);
bool smb2_is_sign_req(struct ksmbd_work *work, unsigned int command);
int smb2_check_sign_req(struct ksmbd_work *work);
void smb2_set_sign_rsp(struct ksmbd_work *work);
int smb3_check_sign_req(struct ksmbd_work *work);
void smb3_set_sign_rsp(struct ksmbd_work *work);
int find_matching_smb2_dialect(int start_index, __le16 *cli_dialects,
			       __le16 dialects_count);
struct file_lock *smb_flock_init(struct file *f);
int setup_async_work(struct ksmbd_work *work, void (*fn)(void **),
		     void **arg);
void smb2_send_interim_resp(struct ksmbd_work *work, __le32 status);
struct channel *lookup_chann_list(struct ksmbd_session *sess,
				  struct ksmbd_conn *conn);
void smb3_preauth_hash_rsp(struct ksmbd_work *work);
bool smb3_is_transform_hdr(void *buf);
int smb3_decrypt_req(struct ksmbd_work *work);
int smb3_encrypt_resp(struct ksmbd_work *work);
bool smb3_11_final_sess_setup_resp(struct ksmbd_work *work);
int smb2_set_rsp_credits(struct ksmbd_work *work);

/* smb2 misc functions */
int ksmbd_smb2_check_message(struct ksmbd_work *work);

/* smb2 command handlers */
int smb2_handle_negotiate(struct ksmbd_work *work);
int smb2_negotiate_request(struct ksmbd_work *work);
int smb2_sess_setup(struct ksmbd_work *work);
int smb2_tree_connect(struct ksmbd_work *work);
int smb2_tree_disconnect(struct ksmbd_work *work);
int smb2_session_logoff(struct ksmbd_work *work);
int smb2_open(struct ksmbd_work *work);
int smb2_query_info(struct ksmbd_work *work);
int smb2_query_dir(struct ksmbd_work *work);
int smb2_close(struct ksmbd_work *work);
int smb2_echo(struct ksmbd_work *work);
int smb2_set_info(struct ksmbd_work *work);
int smb2_read(struct ksmbd_work *work);
int smb2_write(struct ksmbd_work *work);
int smb2_flush(struct ksmbd_work *work);
int smb2_cancel(struct ksmbd_work *work);
int smb2_lock(struct ksmbd_work *work);
int smb2_ioctl(struct ksmbd_work *work);
int smb2_oplock_break(struct ksmbd_work *work);
int smb2_notify(struct ksmbd_work *ksmbd_work);

/*
 * Get the body of the smb2 message excluding the 4 byte rfc1002 headers
 * from request/response buffer.
 */
static inline void *smb2_get_msg(void *buf)
{
	return buf + 4;
}

#endif	/* _SMB2PDU_H */
