/* SPDX-License-Identifier: LGPL-2.1 */
/*
 *
 *   Copyright (c) International Business Machines  Corp., 2009, 2013
 *                 Etersoft, 2012
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *              Pavel Shilovsky (pshilovsky@samba.org) 2012
 *
 */

#ifndef _SMB2PDU_H
#define _SMB2PDU_H

#include <net/sock.h>
#include "cifsacl.h"

/* 52 transform hdr + 64 hdr + 88 create rsp */
#define SMB2_TRANSFORM_HEADER_SIZE 52
#define MAX_SMB2_HDR_SIZE 204

/* The total header size for SMB2 read and write */
#define SMB2_READWRITE_PDU_HEADER_SIZE (48 + sizeof(struct smb2_hdr))

/* See MS-SMB2 2.2.43 */
struct smb2_rdma_transform {
	__le16 RdmaDescriptorOffset;
	__le16 RdmaDescriptorLength;
	__le32 Channel; /* for values see channel description in smb2 read above */
	__le16 TransformCount;
	__le16 Reserved1;
	__le32 Reserved2;
} __packed;

/* TransformType */
#define SMB2_RDMA_TRANSFORM_TYPE_ENCRYPTION	0x0001
#define SMB2_RDMA_TRANSFORM_TYPE_SIGNING	0x0002

struct smb2_rdma_crypto_transform {
	__le16	TransformType;
	__le16	SignatureLength;
	__le16	NonceLength;
	__u16	Reserved;
	__u8	Signature[]; /* variable length */
	/* u8 Nonce[] */
	/* followed by padding */
} __packed;

/*
 *	Definitions for SMB2 Protocol Data Units (network frames)
 *
 *  See MS-SMB2.PDF specification for protocol details.
 *  The Naming convention is the lower case version of the SMB2
 *  command code name for the struct. Note that structures must be packed.
 *
 */

#define COMPOUND_FID 0xFFFFFFFFFFFFFFFFULL

#define SMB2_ERROR_STRUCTURE_SIZE2 cpu_to_le16(9)

struct smb2_err_rsp {
	struct smb2_hdr hdr;
	__le16 StructureSize;
	__le16 Reserved; /* MBZ */
	__le32 ByteCount;  /* even if zero, at least one byte follows */
	__u8   ErrorData[1];  /* variable length */
} __packed;

#define SYMLINK_ERROR_TAG 0x4c4d5953

struct smb2_symlink_err_rsp {
	__le32 SymLinkLength;
	__le32 SymLinkErrorTag;
	__le32 ReparseTag;
	__le16 ReparseDataLength;
	__le16 UnparsedPathLength;
	__le16 SubstituteNameOffset;
	__le16 SubstituteNameLength;
	__le16 PrintNameOffset;
	__le16 PrintNameLength;
	__le32 Flags;
	__u8  PathBuffer[];
} __packed;

/* SMB 3.1.1 and later dialects. See MS-SMB2 section 2.2.2.1 */
struct smb2_error_context_rsp {
	__le32 ErrorDataLength;
	__le32 ErrorId;
	__u8  ErrorContextData; /* ErrorDataLength long array */
} __packed;

/* ErrorId values */
#define SMB2_ERROR_ID_DEFAULT		0x00000000
#define SMB2_ERROR_ID_SHARE_REDIRECT	cpu_to_le32(0x72645253)	/* "rdRS" */

/* Defines for Type field below (see MS-SMB2 2.2.2.2.2.1) */
#define MOVE_DST_IPADDR_V4	cpu_to_le32(0x00000001)
#define MOVE_DST_IPADDR_V6	cpu_to_le32(0x00000002)

struct move_dst_ipaddr {
	__le32 Type;
	__u32  Reserved;
	__u8   address[16]; /* IPv4 followed by 12 bytes rsvd or IPv6 address */
} __packed;

struct share_redirect_error_context_rsp {
	__le32 StructureSize;
	__le32 NotificationType;
	__le32 ResourceNameOffset;
	__le32 ResourceNameLength;
	__le16 Reserved;
	__le16 TargetType;
	__le32 IPAddrCount;
	struct move_dst_ipaddr IpAddrMoveList[];
	/* __u8 ResourceName[] */ /* Name of share as counted Unicode string */
} __packed;

/*
 * Maximum number of iovs we need for an open/create request.
 * [0] : struct smb2_create_req
 * [1] : path
 * [2] : lease context
 * [3] : durable context
 * [4] : posix context
 * [5] : time warp context
 * [6] : query id context
 * [7] : compound padding
 */
#define SMB2_CREATE_IOV_SIZE 8

/*
 * Maximum size of a SMB2_CREATE response is 64 (smb2 header) +
 * 88 (fixed part of create response) + 520 (path) + 208 (contexts) +
 * 2 bytes of padding.
 */
#define MAX_SMB2_CREATE_RESPONSE_SIZE 880

#define SMB2_LEASE_READ_CACHING_HE	0x01
#define SMB2_LEASE_HANDLE_CACHING_HE	0x02
#define SMB2_LEASE_WRITE_CACHING_HE	0x04

#define SMB2_LEASE_NONE			cpu_to_le32(0x00)
#define SMB2_LEASE_READ_CACHING		cpu_to_le32(0x01)
#define SMB2_LEASE_HANDLE_CACHING	cpu_to_le32(0x02)
#define SMB2_LEASE_WRITE_CACHING	cpu_to_le32(0x04)

#define SMB2_LEASE_FLAG_BREAK_IN_PROGRESS cpu_to_le32(0x00000002)
#define SMB2_LEASE_FLAG_PARENT_LEASE_KEY_SET cpu_to_le32(0x00000004)

#define SMB2_LEASE_KEY_SIZE 16

struct lease_context {
	u8 LeaseKey[SMB2_LEASE_KEY_SIZE];
	__le32 LeaseState;
	__le32 LeaseFlags;
	__le64 LeaseDuration;
} __packed;

struct lease_context_v2 {
	u8 LeaseKey[SMB2_LEASE_KEY_SIZE];
	__le32 LeaseState;
	__le32 LeaseFlags;
	__le64 LeaseDuration;
	__le64 ParentLeaseKeyLow;
	__le64 ParentLeaseKeyHigh;
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

struct create_durable {
	struct create_context ccontext;
	__u8   Name[8];
	union {
		__u8  Reserved[16];
		struct {
			__u64 PersistentFileId;
			__u64 VolatileFileId;
		} Fid;
	} Data;
} __packed;

struct create_posix {
	struct create_context ccontext;
	__u8	Name[16];
	__le32  Mode;
	__u32	Reserved;
} __packed;

/* See MS-SMB2 2.2.13.2.11 */
/* Flags */
#define SMB2_DHANDLE_FLAG_PERSISTENT	0x00000002
struct durable_context_v2 {
	__le32 Timeout;
	__le32 Flags;
	__u64 Reserved;
	__u8 CreateGuid[16];
} __packed;

struct create_durable_v2 {
	struct create_context ccontext;
	__u8   Name[8];
	struct durable_context_v2 dcontext;
} __packed;

/* See MS-SMB2 2.2.13.2.12 */
struct durable_reconnect_context_v2 {
	struct {
		__u64 PersistentFileId;
		__u64 VolatileFileId;
	} Fid;
	__u8 CreateGuid[16];
	__le32 Flags; /* see above DHANDLE_FLAG_PERSISTENT */
} __packed;

/* See MS-SMB2 2.2.14.2.9 */
struct create_on_disk_id {
	struct create_context ccontext;
	__u8   Name[8];
	__le64 DiskFileId;
	__le64 VolumeId;
	__u32  Reserved[4];
} __packed;

/* See MS-SMB2 2.2.14.2.12 */
struct durable_reconnect_context_v2_rsp {
	__le32 Timeout;
	__le32 Flags; /* see above DHANDLE_FLAG_PERSISTENT */
} __packed;

struct create_durable_handle_reconnect_v2 {
	struct create_context ccontext;
	__u8   Name[8];
	struct durable_reconnect_context_v2 dcontext;
	__u8   Pad[4];
} __packed;

/* See MS-SMB2 2.2.13.2.5 */
struct crt_twarp_ctxt {
	struct create_context ccontext;
	__u8	Name[8];
	__le64	Timestamp;

} __packed;

/* See MS-SMB2 2.2.13.2.9 */
struct crt_query_id_ctxt {
	struct create_context ccontext;
	__u8	Name[8];
} __packed;

struct crt_sd_ctxt {
	struct create_context ccontext;
	__u8	Name[8];
	struct smb3_sd sd;
} __packed;


#define COPY_CHUNK_RES_KEY_SIZE	24
struct resume_key_req {
	char ResumeKey[COPY_CHUNK_RES_KEY_SIZE];
	__le32	ContextLength;	/* MBZ */
	char	Context[];	/* ignored, Windows sets to 4 bytes of zero */
} __packed;

/* this goes in the ioctl buffer when doing a copychunk request */
struct copychunk_ioctl {
	char SourceKey[COPY_CHUNK_RES_KEY_SIZE];
	__le32 ChunkCount; /* we are only sending 1 */
	__le32 Reserved;
	/* array will only be one chunk long for us */
	__le64 SourceOffset;
	__le64 TargetOffset;
	__le32 Length; /* how many bytes to copy */
	__u32 Reserved2;
} __packed;

/* this goes in the ioctl buffer when doing FSCTL_SET_ZERO_DATA */
struct file_zero_data_information {
	__le64	FileOffset;
	__le64	BeyondFinalZero;
} __packed;

struct copychunk_ioctl_rsp {
	__le32 ChunksWritten;
	__le32 ChunkBytesWritten;
	__le32 TotalBytesWritten;
} __packed;

/* See MS-FSCC 2.3.29 and 2.3.30 */
struct get_retrieval_pointer_count_req {
	__le64 StartingVcn; /* virtual cluster number (signed) */
} __packed;

struct get_retrieval_pointer_count_rsp {
	__le32 ExtentCount;
} __packed;

/*
 * See MS-FSCC 2.3.33 and 2.3.34
 * request is the same as get_retrieval_point_count_req struct above
 */
struct smb3_extents {
	__le64 NextVcn;
	__le64 Lcn; /* logical cluster number */
} __packed;

struct get_retrieval_pointers_refcount_rsp {
	__le32 ExtentCount;
	__u32  Reserved;
	__le64 StartingVcn;
	struct smb3_extents extents[];
} __packed;

struct fsctl_set_integrity_information_req {
	__le16	ChecksumAlgorithm;
	__le16	Reserved;
	__le32	Flags;
} __packed;

struct fsctl_get_integrity_information_rsp {
	__le16	ChecksumAlgorithm;
	__le16	Reserved;
	__le32	Flags;
	__le32	ChecksumChunkSizeInBytes;
	__le32	ClusterSizeInBytes;
} __packed;

struct file_allocated_range_buffer {
	__le64	file_offset;
	__le64	length;
} __packed;

/* Integrity ChecksumAlgorithm choices for above */
#define	CHECKSUM_TYPE_NONE	0x0000
#define	CHECKSUM_TYPE_CRC64	0x0002
#define CHECKSUM_TYPE_UNCHANGED	0xFFFF	/* set only */

/* Integrity flags for above */
#define FSCTL_INTEGRITY_FLAG_CHECKSUM_ENFORCEMENT_OFF	0x00000001

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

/* See MS-FSCC 2.1.2.6 and cifspdu.h for struct reparse_posix_data */


/* See MS-DFSC 2.2.2 */
struct fsctl_get_dfs_referral_req {
	__le16 MaxReferralLevel;
	__u8 RequestFileName[];
} __packed;

/* DFS response is struct get_dfs_refer_rsp */

/* See MS-SMB2 2.2.31.3 */
struct network_resiliency_req {
	__le32 Timeout;
	__le32 Reserved;
} __packed;
/* There is no buffer for the response ie no struct network_resiliency_rsp */


struct validate_negotiate_info_req {
	__le32 Capabilities;
	__u8   Guid[SMB2_CLIENT_GUID_SIZE];
	__le16 SecurityMode;
	__le16 DialectCount;
	__le16 Dialects[4]; /* BB expand this if autonegotiate > 4 dialects */
} __packed;

struct validate_negotiate_info_rsp {
	__le32 Capabilities;
	__u8   Guid[SMB2_CLIENT_GUID_SIZE];
	__le16 SecurityMode;
	__le16 Dialect; /* Dialect in use for the connection */
} __packed;

#define RSS_CAPABLE	cpu_to_le32(0x00000001)
#define RDMA_CAPABLE	cpu_to_le32(0x00000002)

#define INTERNETWORK	cpu_to_le16(0x0002)
#define INTERNETWORKV6	cpu_to_le16(0x0017)

struct network_interface_info_ioctl_rsp {
	__le32 Next; /* next interface. zero if this is last one */
	__le32 IfIndex;
	__le32 Capability; /* RSS or RDMA Capable */
	__le32 Reserved;
	__le64 LinkSpeed;
	__le16 Family;
	__u8 Buffer[126];
} __packed;

struct iface_info_ipv4 {
	__be16 Port;
	__be32 IPv4Address;
	__be64 Reserved;
} __packed;

struct iface_info_ipv6 {
	__be16 Port;
	__be32 FlowInfo;
	__u8   IPv6Address[16];
	__be32 ScopeId;
} __packed;

#define NO_FILE_ID 0xFFFFFFFFFFFFFFFFULL /* general ioctls to srv not to file */

struct compress_ioctl {
	__le16 CompressionState; /* See cifspdu.h for possible flag values */
} __packed;

struct duplicate_extents_to_file {
	__u64 PersistentFileHandle; /* source file handle, opaque endianness */
	__u64 VolatileFileHandle;
	__le64 SourceFileOffset;
	__le64 TargetFileOffset;
	__le64 ByteCount;  /* Bytes to be copied */
} __packed;

/*
 * Maximum number of iovs we need for an ioctl request.
 * [0] : struct smb2_ioctl_req
 * [1] : in_data
 */
#define SMB2_IOCTL_IOV_SIZE 2

struct smb2_ioctl_req {
	struct smb2_hdr hdr;
	__le16 StructureSize;	/* Must be 57 */
	__u16 Reserved;
	__le32 CtlCode;
	__u64  PersistentFileId; /* opaque endianness */
	__u64  VolatileFileId; /* opaque endianness */
	__le32 InputOffset;
	__le32 InputCount;
	__le32 MaxInputResponse;
	__le32 OutputOffset;
	__le32 OutputCount;
	__le32 MaxOutputResponse;
	__le32 Flags;
	__u32  Reserved2;
	__u8   Buffer[];
} __packed;

struct smb2_ioctl_rsp {
	struct smb2_hdr hdr;
	__le16 StructureSize;	/* Must be 57 */
	__u16 Reserved;
	__le32 CtlCode;
	__u64  PersistentFileId; /* opaque endianness */
	__u64  VolatileFileId; /* opaque endianness */
	__le32 InputOffset;
	__le32 InputCount;
	__le32 OutputOffset;
	__le32 OutputCount;
	__le32 Flags;
	__u32  Reserved2;
	/* char * buffer[] */
} __packed;

#define SMB2_LOCKFLAG_SHARED_LOCK	0x0001
#define SMB2_LOCKFLAG_EXCLUSIVE_LOCK	0x0002
#define SMB2_LOCKFLAG_UNLOCK		0x0004
#define SMB2_LOCKFLAG_FAIL_IMMEDIATELY	0x0010

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
	/*
	 * The least significant four bits are the index, the other 28 bits are
	 * the lock sequence number (0 to 64). See MS-SMB2 2.2.26
	 */
	__le32 LockSequenceNumber;
	__u64  PersistentFileId; /* opaque endianness */
	__u64  VolatileFileId; /* opaque endianness */
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

#define SMB2_QUERY_DIRECTORY_IOV_SIZE 2

/*
 * Valid FileInformation classes.
 *
 * Note that these are a subset of the (file) QUERY_INFO levels defined
 * later in this file (but since QUERY_DIRECTORY uses equivalent numbers
 * we do not redefine them here)
 *
 * FileDirectoryInfomation		0x01
 * FileFullDirectoryInformation		0x02
 * FileIdFullDirectoryInformation	0x26
 * FileBothDirectoryInformation		0x03
 * FileIdBothDirectoryInformation	0x25
 * FileNamesInformation			0x0C
 * FileIdExtdDirectoryInformation	0x3C
 */

struct smb2_query_directory_req {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 33 */
	__u8   FileInformationClass;
	__u8   Flags;
	__le32 FileIndex;
	__u64  PersistentFileId; /* opaque endianness */
	__u64  VolatileFileId; /* opaque endianness */
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

/* Flags used for FileFullEAinfo */
#define SL_RESTART_SCAN		0x00000001
#define SL_RETURN_SINGLE_ENTRY	0x00000002
#define SL_INDEX_SPECIFIED	0x00000004

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
	__u64  PersistentFileId; /* opaque endianness */
	__u64  VolatileFileId; /* opaque endianness */
	__u8   Buffer[1];
} __packed;

struct smb2_query_info_rsp {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 9 */
	__le16 OutputBufferOffset;
	__le32 OutputBufferLength;
	__u8   Buffer[1];
} __packed;

/*
 * Maximum number of iovs we need for a set-info request.
 * The largest one is rename/hardlink
 * [0] : struct smb2_set_info_req + smb2_file_[rename|link]_info
 * [1] : path
 * [2] : compound padding
 */
#define SMB2_SET_INFO_IOV_SIZE 3

struct smb2_set_info_req {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 33 */
	__u8   InfoType;
	__u8   FileInfoClass;
	__le32 BufferLength;
	__le16 BufferOffset;
	__u16  Reserved;
	__le32 AdditionalInformation;
	__u64  PersistentFileId; /* opaque endianness */
	__u64  VolatileFileId; /* opaque endianness */
	__u8   Buffer[1];
} __packed;

struct smb2_set_info_rsp {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 2 */
} __packed;

struct smb2_oplock_break {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 24 */
	__u8   OplockLevel;
	__u8   Reserved;
	__le32 Reserved2;
	__u64  PersistentFid;
	__u64  VolatileFid;
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
#define FS_LABEL_INFORMATION		2 /* Local only */
#define FS_SIZE_INFORMATION		3 /* Query */
#define FS_DEVICE_INFORMATION		4 /* Query */
#define FS_ATTRIBUTE_INFORMATION	5 /* Query */
#define FS_CONTROL_INFORMATION		6 /* Query, Set */
#define FS_FULL_SIZE_INFORMATION	7 /* Query */
#define FS_OBJECT_ID_INFORMATION	8 /* Query, Set */
#define FS_DRIVER_PATH_INFORMATION	9 /* Local only */
#define FS_VOLUME_FLAGS_INFORMATION	10 /* Local only */
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
	__le32 FileSystemEffectivePhysicalBytesPerSectorForAtomicity;
	__le32 Flags;
	__le32 ByteOffsetForSectorAlignment;
	__le32 ByteOffsetForPartitionAlignment;
} __packed;

/* volume info struct - see MS-FSCC 2.5.9 */
#define MAX_VOL_LABEL_LEN	32
struct smb3_fs_vol_info {
	__le64	VolumeCreationTime;
	__u32	VolumeSerialNumber;
	__le32	VolumeLabelLength; /* includes trailing null */
	__u8	SupportsObjects; /* True if eg like NTFS, supports objects */
	__u8	Reserved;
	__u8	VolumeLabel[]; /* variable len */
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
#define FILE_ID_INFORMATION		59
#define FILE_ID_EXTD_DIRECTORY_INFORMATION 60

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
	/* padding - overall struct size must be >= 24 so filename + pad >= 6 */
} __packed; /* level 10 Set */

struct smb2_file_link_info { /* encoding of request for level 11 */
	__u8   ReplaceIfExists; /* 1 = replace existing link with new */
				/* 0 = fail if link already exists */
	__u8   Reserved[7];
	__u64  RootDirectory;  /* MBZ for network operations (why says spec?) */
	__le32 FileNameLength;
	char   FileName[];     /* Name to be assigned to new link */
} __packed; /* level 11 Set */

struct smb2_file_full_ea_info { /* encoding of response for level 15 */
	__le32 next_entry_offset;
	__u8   flags;
	__u8   ea_name_length;
	__le16 ea_value_length;
	char   ea_data[]; /* \0 terminated name plus value */
} __packed; /* level 15 Set */

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

struct smb2_file_eof_info { /* encoding of request for level 10 */
	__le64 EndOfFile; /* new end of file value */
} __packed; /* level 20 Set */

struct smb2_file_reparse_point_info {
	__le64 IndexNumber;
	__le32 Tag;
} __packed;

struct smb2_file_network_open_info {
	__le64 CreationTime;
	__le64 LastAccessTime;
	__le64 LastWriteTime;
	__le64 ChangeTime;
	__le64 AllocationSize;
	__le64 EndOfFile;
	__le32 Attributes;
	__le32 Reserved;
} __packed; /* level 34 Query also similar returned in close rsp and open rsp */

/* See MS-FSCC 2.4.21 */
struct smb2_file_id_information {
	__le64	VolumeSerialNumber;
	__u64  PersistentFileId; /* opaque endianness */
	__u64  VolatileFileId; /* opaque endianness */
} __packed; /* level 59 */

/* See MS-FSCC 2.4.18 */
struct smb2_file_id_extd_directory_info {
	__le32 NextEntryOffset;
	__u32 FileIndex;
	__le64 CreationTime;
	__le64 LastAccessTime;
	__le64 LastWriteTime;
	__le64 ChangeTime;
	__le64 EndOfFile;
	__le64 AllocationSize;
	__le32 FileAttributes;
	__le32 FileNameLength;
	__le32 EaSize; /* EA size */
	__le32 ReparsePointTag; /* valid if FILE_ATTR_REPARSE_POINT set in FileAttributes */
	__le64 UniqueId; /* inode num - le since Samba puts ino in low 32 bit */
	char FileName[1];
} __packed; /* level 60 */

extern char smb2_padding[7];

/* equivalent of the contents of SMB3.1.1 POSIX open context response */
struct create_posix_rsp {
	u32 nlink;
	u32 reparse_tag;
	u32 mode;
	struct cifs_sid owner; /* var-sized on the wire */
	struct cifs_sid group; /* var-sized on the wire */
} __packed;

/*
 * SMB2-only POSIX info level for query dir
 *
 * See posix_info_sid_size(), posix_info_extra_size() and
 * posix_info_parse() to help with the handling of this struct.
 */
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
	/*
	 * var sized owner SID
	 * var sized group SID
	 * le32 filenamelength
	 * u8  filename[]
	 */
} __packed;

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

/*
 * Parsed version of the above struct. Allows direct access to the
 * variable length fields
 */
struct smb2_posix_info_parsed {
	const struct smb2_posix_info *base;
	size_t size;
	struct cifs_sid owner;
	struct cifs_sid group;
	int name_len;
	const u8 *name;
};

#endif				/* _SMB2PDU_H */
