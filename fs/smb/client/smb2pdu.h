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

#define SMB2_SYMLINK_STRUCT_SIZE \
	(sizeof(struct smb2_err_rsp) + sizeof(struct smb2_symlink_err_rsp))

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

/*
 * Maximum number of iovs we need for an ioctl request.
 * [0] : struct smb2_ioctl_req
 * [1] : in_data
 */
#define SMB2_IOCTL_IOV_SIZE 2

/*
 *	PDU query infolevel structure definitions
 *	BB consider moving to a different header
 */

struct smb2_file_full_ea_info { /* encoding of response for level 15 */
	__le32 next_entry_offset;
	__u8   flags;
	__u8   ea_name_length;
	__le16 ea_value_length;
	char   ea_data[]; /* \0 terminated name plus value */
} __packed; /* level 15 Set */

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
	char FileName[];
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

#define SMB2_QUERY_DIRECTORY_IOV_SIZE 2

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
