/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2016 Namjae Jeon <linkinjeon@kernel.org>
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#ifndef _SMB2PDU_H
#define _SMB2PDU_H

#include "ntlmssp.h"
#include "smbacl.h"

/*
 * Note that, due to trying to use names similar to the protocol specifications,
 * there are many mixed case field names in the structures below.  Although
 * this does not match typical Linux kernel style, it is necessary to be
 * able to match against the protocol specfication.
 *
 * SMB2 commands
 * Some commands have minimal (wct=0,bcc=0), or uninteresting, responses
 * (ie no useful data other than the SMB error code itself) and are marked such.
 * Knowing this helps avoid response buffer allocations and copy in some cases.
 */

/* List of commands in host endian */
#define SMB2_NEGOTIATE_HE	0x0000
#define SMB2_SESSION_SETUP_HE	0x0001
#define SMB2_LOGOFF_HE		0x0002 /* trivial request/resp */
#define SMB2_TREE_CONNECT_HE	0x0003
#define SMB2_TREE_DISCONNECT_HE	0x0004 /* trivial req/resp */
#define SMB2_CREATE_HE		0x0005
#define SMB2_CLOSE_HE		0x0006
#define SMB2_FLUSH_HE		0x0007 /* trivial resp */
#define SMB2_READ_HE		0x0008
#define SMB2_WRITE_HE		0x0009
#define SMB2_LOCK_HE		0x000A
#define SMB2_IOCTL_HE		0x000B
#define SMB2_CANCEL_HE		0x000C
#define SMB2_ECHO_HE		0x000D
#define SMB2_QUERY_DIRECTORY_HE	0x000E
#define SMB2_CHANGE_NOTIFY_HE	0x000F
#define SMB2_QUERY_INFO_HE	0x0010
#define SMB2_SET_INFO_HE	0x0011
#define SMB2_OPLOCK_BREAK_HE	0x0012

/* The same list in little endian */
#define SMB2_NEGOTIATE		cpu_to_le16(SMB2_NEGOTIATE_HE)
#define SMB2_SESSION_SETUP	cpu_to_le16(SMB2_SESSION_SETUP_HE)
#define SMB2_LOGOFF		cpu_to_le16(SMB2_LOGOFF_HE)
#define SMB2_TREE_CONNECT	cpu_to_le16(SMB2_TREE_CONNECT_HE)
#define SMB2_TREE_DISCONNECT	cpu_to_le16(SMB2_TREE_DISCONNECT_HE)
#define SMB2_CREATE		cpu_to_le16(SMB2_CREATE_HE)
#define SMB2_CLOSE		cpu_to_le16(SMB2_CLOSE_HE)
#define SMB2_FLUSH		cpu_to_le16(SMB2_FLUSH_HE)
#define SMB2_READ		cpu_to_le16(SMB2_READ_HE)
#define SMB2_WRITE		cpu_to_le16(SMB2_WRITE_HE)
#define SMB2_LOCK		cpu_to_le16(SMB2_LOCK_HE)
#define SMB2_IOCTL		cpu_to_le16(SMB2_IOCTL_HE)
#define SMB2_CANCEL		cpu_to_le16(SMB2_CANCEL_HE)
#define SMB2_ECHO		cpu_to_le16(SMB2_ECHO_HE)
#define SMB2_QUERY_DIRECTORY	cpu_to_le16(SMB2_QUERY_DIRECTORY_HE)
#define SMB2_CHANGE_NOTIFY	cpu_to_le16(SMB2_CHANGE_NOTIFY_HE)
#define SMB2_QUERY_INFO		cpu_to_le16(SMB2_QUERY_INFO_HE)
#define SMB2_SET_INFO		cpu_to_le16(SMB2_SET_INFO_HE)
#define SMB2_OPLOCK_BREAK	cpu_to_le16(SMB2_OPLOCK_BREAK_HE)

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

#define SMB2_CLIENT_GUID_SIZE		16
#define SMB2_CREATE_GUID_SIZE		16

/* Maximum buffer size value we can send with 1 credit */
#define SMB2_MAX_BUFFER_SIZE 65536

#define NUMBER_OF_SMB2_COMMANDS	0x0013

/* BB FIXME - analyze following length BB */
#define MAX_SMB2_HDR_SIZE 0x78 /* 4 len + 64 hdr + (2*24 wct) + 2 bct + 2 pad */

#define SMB2_PROTO_NUMBER cpu_to_le32(0x424d53fe) /* 'B''M''S' */
#define SMB2_TRANSFORM_PROTO_NUM cpu_to_le32(0x424d53fd)

#define SMB21_DEFAULT_IOSIZE	(1024 * 1024)
#define SMB3_DEFAULT_IOSIZE	(4 * 1024 * 1024)
#define SMB3_DEFAULT_TRANS_SIZE	(1024 * 1024)

/*
 * SMB2 Header Definition
 *
 * "MBZ" :  Must be Zero
 * "BB"  :  BugBug, Something to check/review/analyze later
 * "PDU" :  "Protocol Data Unit" (ie a network "frame")
 *
 */

#define __SMB2_HEADER_STRUCTURE_SIZE	64
#define SMB2_HEADER_STRUCTURE_SIZE				\
	cpu_to_le16(__SMB2_HEADER_STRUCTURE_SIZE)

struct smb2_hdr {
	__be32 smb2_buf_length;	/* big endian on wire */
				/*
				 * length is only two or three bytes - with
				 * one or two byte type preceding it that MBZ
				 */
	__le32 ProtocolId;	/* 0xFE 'S' 'M' 'B' */
	__le16 StructureSize;	/* 64 */
	__le16 CreditCharge;	/* MBZ */
	__le32 Status;		/* Error from server */
	__le16 Command;
	__le16 CreditRequest;	/* CreditResponse */
	__le32 Flags;
	__le32 NextCommand;
	__le64 MessageId;
	union {
		struct {
			__le32 ProcessId;
			__le32  TreeId;
		} __packed SyncId;
		__le64  AsyncId;
	} __packed Id;
	__le64  SessionId;
	__u8   Signature[16];
} __packed;

struct smb2_pdu {
	struct smb2_hdr hdr;
	__le16 StructureSize2; /* size of wct area (varies, request specific) */
} __packed;

#define SMB3_AES_CCM_NONCE 11
#define SMB3_AES_GCM_NONCE 12

struct smb2_transform_hdr {
	__be32 smb2_buf_length; /* big endian on wire */
	/*
	 * length is only two or three bytes - with
	 * one or two byte type preceding it that MBZ
	 */
	__le32 ProtocolId;      /* 0xFD 'S' 'M' 'B' */
	__u8   Signature[16];
	__u8   Nonce[16];
	__le32 OriginalMessageSize;
	__u16  Reserved1;
	__le16 Flags; /* EncryptionAlgorithm */
	__le64  SessionId;
} __packed;

/*
 *	SMB2 flag definitions
 */
#define SMB2_FLAGS_SERVER_TO_REDIR	cpu_to_le32(0x00000001)
#define SMB2_FLAGS_ASYNC_COMMAND	cpu_to_le32(0x00000002)
#define SMB2_FLAGS_RELATED_OPERATIONS	cpu_to_le32(0x00000004)
#define SMB2_FLAGS_SIGNED		cpu_to_le32(0x00000008)
#define SMB2_FLAGS_DFS_OPERATIONS	cpu_to_le32(0x10000000)
#define SMB2_FLAGS_REPLAY_OPERATIONS	cpu_to_le32(0x20000000)

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

struct smb2_negotiate_req {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 36 */
	__le16 DialectCount;
	__le16 SecurityMode;
	__le16 Reserved;	/* MBZ */
	__le32 Capabilities;
	__u8   ClientGUID[SMB2_CLIENT_GUID_SIZE];
	/* In SMB3.02 and earlier next three were MBZ le64 ClientStartTime */
	__le32 NegotiateContextOffset; /* SMB3.1.1 only. MBZ earlier */
	__le16 NegotiateContextCount;  /* SMB3.1.1 only. MBZ earlier */
	__le16 Reserved2;
	__le16 Dialects[1]; /* One dialect (vers=) at a time for now */
} __packed;

/* SecurityMode flags */
#define SMB2_NEGOTIATE_SIGNING_ENABLED_LE	cpu_to_le16(0x0001)
#define SMB2_NEGOTIATE_SIGNING_REQUIRED		0x0002
#define SMB2_NEGOTIATE_SIGNING_REQUIRED_LE	cpu_to_le16(0x0002)
/* Capabilities flags */
#define SMB2_GLOBAL_CAP_DFS		0x00000001
#define SMB2_GLOBAL_CAP_LEASING		0x00000002 /* Resp only New to SMB2.1 */
#define SMB2_GLOBAL_CAP_LARGE_MTU	0X00000004 /* Resp only New to SMB2.1 */
#define SMB2_GLOBAL_CAP_MULTI_CHANNEL	0x00000008 /* New to SMB3 */
#define SMB2_GLOBAL_CAP_PERSISTENT_HANDLES 0x00000010 /* New to SMB3 */
#define SMB2_GLOBAL_CAP_DIRECTORY_LEASING  0x00000020 /* New to SMB3 */
#define SMB2_GLOBAL_CAP_ENCRYPTION	0x00000040 /* New to SMB3 */
/* Internal types */
#define SMB2_NT_FIND			0x00100000
#define SMB2_LARGE_FILES		0x00200000

#define SMB311_SALT_SIZE			32
/* Hash Algorithm Types */
#define SMB2_PREAUTH_INTEGRITY_SHA512	cpu_to_le16(0x0001)

#define PREAUTH_HASHVALUE_SIZE		64

struct preauth_integrity_info {
	/* PreAuth integrity Hash ID */
	__le16			Preauth_HashId;
	/* PreAuth integrity Hash Value */
	__u8			Preauth_HashValue[PREAUTH_HASHVALUE_SIZE];
};

/* offset is sizeof smb2_negotiate_rsp - 4 but rounded up to 8 bytes. */
#ifdef CONFIG_SMB_SERVER_KERBEROS5
/* sizeof(struct smb2_negotiate_rsp) - 4 =
 * header(64) + response(64) + GSS_LENGTH(96) + GSS_PADDING(0)
 */
#define OFFSET_OF_NEG_CONTEXT	0xe0
#else
/* sizeof(struct smb2_negotiate_rsp) - 4 =
 * header(64) + response(64) + GSS_LENGTH(74) + GSS_PADDING(6)
 */
#define OFFSET_OF_NEG_CONTEXT	0xd0
#endif

#define SMB2_PREAUTH_INTEGRITY_CAPABILITIES	cpu_to_le16(1)
#define SMB2_ENCRYPTION_CAPABILITIES		cpu_to_le16(2)
#define SMB2_COMPRESSION_CAPABILITIES		cpu_to_le16(3)
#define SMB2_NETNAME_NEGOTIATE_CONTEXT_ID	cpu_to_le16(5)
#define SMB2_SIGNING_CAPABILITIES		cpu_to_le16(8)
#define SMB2_POSIX_EXTENSIONS_AVAILABLE		cpu_to_le16(0x100)

struct smb2_neg_context {
	__le16  ContextType;
	__le16  DataLength;
	__le32  Reserved;
	/* Followed by array of data */
} __packed;

struct smb2_preauth_neg_context {
	__le16	ContextType; /* 1 */
	__le16	DataLength;
	__le32	Reserved;
	__le16	HashAlgorithmCount; /* 1 */
	__le16	SaltLength;
	__le16	HashAlgorithms; /* HashAlgorithms[0] since only one defined */
	__u8	Salt[SMB311_SALT_SIZE];
} __packed;

/* Encryption Algorithms Ciphers */
#define SMB2_ENCRYPTION_AES128_CCM	cpu_to_le16(0x0001)
#define SMB2_ENCRYPTION_AES128_GCM	cpu_to_le16(0x0002)
#define SMB2_ENCRYPTION_AES256_CCM	cpu_to_le16(0x0003)
#define SMB2_ENCRYPTION_AES256_GCM	cpu_to_le16(0x0004)

struct smb2_encryption_neg_context {
	__le16	ContextType; /* 2 */
	__le16	DataLength;
	__le32	Reserved;
	/* CipherCount usally 2, but can be 3 when AES256-GCM enabled */
	__le16	CipherCount; /* AES-128-GCM and AES-128-CCM by default */
	__le16	Ciphers[];
} __packed;

#define SMB3_COMPRESS_NONE	cpu_to_le16(0x0000)
#define SMB3_COMPRESS_LZNT1	cpu_to_le16(0x0001)
#define SMB3_COMPRESS_LZ77	cpu_to_le16(0x0002)
#define SMB3_COMPRESS_LZ77_HUFF	cpu_to_le16(0x0003)

struct smb2_compression_ctx {
	__le16	ContextType; /* 3 */
	__le16  DataLength;
	__le32	Reserved;
	__le16	CompressionAlgorithmCount;
	__u16	Padding;
	__le32	Reserved1;
	__le16	CompressionAlgorithms[];
} __packed;

#define POSIX_CTXT_DATA_LEN     16
struct smb2_posix_neg_context {
	__le16	ContextType; /* 0x100 */
	__le16	DataLength;
	__le32	Reserved;
	__u8	Name[16]; /* POSIX ctxt GUID 93AD25509CB411E7B42383DE968BCD7C */
} __packed;

struct smb2_netname_neg_context {
	__le16	ContextType; /* 0x100 */
	__le16	DataLength;
	__le32	Reserved;
	__le16	NetName[]; /* hostname of target converted to UCS-2 */
} __packed;

/* Signing algorithms */
#define SIGNING_ALG_HMAC_SHA256		cpu_to_le16(0)
#define SIGNING_ALG_AES_CMAC		cpu_to_le16(1)
#define SIGNING_ALG_AES_GMAC		cpu_to_le16(2)

struct smb2_signing_capabilities {
	__le16	ContextType; /* 8 */
	__le16	DataLength;
	__le32	Reserved;
	__le16	SigningAlgorithmCount;
	__le16	SigningAlgorithms[];
} __packed;

struct smb2_negotiate_rsp {
	struct smb2_hdr hdr;
	__le16 StructureSize;	/* Must be 65 */
	__le16 SecurityMode;
	__le16 DialectRevision;
	__le16 NegotiateContextCount; /* Prior to SMB3.1.1 was Reserved & MBZ */
	__u8   ServerGUID[16];
	__le32 Capabilities;
	__le32 MaxTransactSize;
	__le32 MaxReadSize;
	__le32 MaxWriteSize;
	__le64 SystemTime;	/* MBZ */
	__le64 ServerStartTime;
	__le16 SecurityBufferOffset;
	__le16 SecurityBufferLength;
	__le32 NegotiateContextOffset;	/* Pre:SMB3.1.1 was reserved/ignored */
	__u8   Buffer[1];	/* variable length GSS security buffer */
} __packed;

/* Flags */
#define SMB2_SESSION_REQ_FLAG_BINDING		0x01
#define SMB2_SESSION_REQ_FLAG_ENCRYPT_DATA	0x04

#define SMB2_SESSION_EXPIRED		(0)
#define SMB2_SESSION_IN_PROGRESS	BIT(0)
#define SMB2_SESSION_VALID		BIT(1)

/* Flags */
#define SMB2_SESSION_REQ_FLAG_BINDING		0x01
#define SMB2_SESSION_REQ_FLAG_ENCRYPT_DATA	0x04

struct smb2_sess_setup_req {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 25 */
	__u8   Flags;
	__u8   SecurityMode;
	__le32 Capabilities;
	__le32 Channel;
	__le16 SecurityBufferOffset;
	__le16 SecurityBufferLength;
	__le64 PreviousSessionId;
	__u8   Buffer[1];	/* variable length GSS security buffer */
} __packed;

/* Flags/Reserved for SMB3.1.1 */
#define SMB2_SHAREFLAG_CLUSTER_RECONNECT	0x0001

/* Currently defined SessionFlags */
#define SMB2_SESSION_FLAG_IS_GUEST_LE		cpu_to_le16(0x0001)
#define SMB2_SESSION_FLAG_IS_NULL_LE		cpu_to_le16(0x0002)
#define SMB2_SESSION_FLAG_ENCRYPT_DATA_LE	cpu_to_le16(0x0004)
struct smb2_sess_setup_rsp {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 9 */
	__le16 SessionFlags;
	__le16 SecurityBufferOffset;
	__le16 SecurityBufferLength;
	__u8   Buffer[1];	/* variable length GSS security buffer */
} __packed;

struct smb2_logoff_req {
	struct smb2_hdr hdr;
	__le16 StructureSize;	/* Must be 4 */
	__le16 Reserved;
} __packed;

struct smb2_logoff_rsp {
	struct smb2_hdr hdr;
	__le16 StructureSize;	/* Must be 4 */
	__le16 Reserved;
} __packed;

struct smb2_tree_connect_req {
	struct smb2_hdr hdr;
	__le16 StructureSize;	/* Must be 9 */
	__le16 Reserved;	/* Flags in SMB3.1.1 */
	__le16 PathOffset;
	__le16 PathLength;
	__u8   Buffer[1];	/* variable length */
} __packed;

struct smb2_tree_connect_rsp {
	struct smb2_hdr hdr;
	__le16 StructureSize;	/* Must be 16 */
	__u8   ShareType;  /* see below */
	__u8   Reserved;
	__le32 ShareFlags; /* see below */
	__le32 Capabilities; /* see below */
	__le32 MaximalAccess;
} __packed;

/* Possible ShareType values */
#define SMB2_SHARE_TYPE_DISK	0x01
#define SMB2_SHARE_TYPE_PIPE	0x02
#define	SMB2_SHARE_TYPE_PRINT	0x03

/*
 * Possible ShareFlags - exactly one and only one of the first 4 caching flags
 * must be set (any of the remaining, SHI1005, flags may be set individually
 * or in combination.
 */
#define SMB2_SHAREFLAG_MANUAL_CACHING			0x00000000
#define SMB2_SHAREFLAG_AUTO_CACHING			0x00000010
#define SMB2_SHAREFLAG_VDO_CACHING			0x00000020
#define SMB2_SHAREFLAG_NO_CACHING			0x00000030
#define SHI1005_FLAGS_DFS				0x00000001
#define SHI1005_FLAGS_DFS_ROOT				0x00000002
#define SHI1005_FLAGS_RESTRICT_EXCLUSIVE_OPENS		0x00000100
#define SHI1005_FLAGS_FORCE_SHARED_DELETE		0x00000200
#define SHI1005_FLAGS_ALLOW_NAMESPACE_CACHING		0x00000400
#define SHI1005_FLAGS_ACCESS_BASED_DIRECTORY_ENUM	0x00000800
#define SHI1005_FLAGS_FORCE_LEVELII_OPLOCK		0x00001000
#define SHI1005_FLAGS_ENABLE_HASH			0x00002000

/* Possible share capabilities */
#define SMB2_SHARE_CAP_DFS	cpu_to_le32(0x00000008)

struct smb2_tree_disconnect_req {
	struct smb2_hdr hdr;
	__le16 StructureSize;	/* Must be 4 */
	__le16 Reserved;
} __packed;

struct smb2_tree_disconnect_rsp {
	struct smb2_hdr hdr;
	__le16 StructureSize;	/* Must be 4 */
	__le16 Reserved;
} __packed;

#define ATTR_READONLY_LE	cpu_to_le32(ATTR_READONLY)
#define ATTR_HIDDEN_LE		cpu_to_le32(ATTR_HIDDEN)
#define ATTR_SYSTEM_LE		cpu_to_le32(ATTR_SYSTEM)
#define ATTR_DIRECTORY_LE	cpu_to_le32(ATTR_DIRECTORY)
#define ATTR_ARCHIVE_LE		cpu_to_le32(ATTR_ARCHIVE)
#define ATTR_NORMAL_LE		cpu_to_le32(ATTR_NORMAL)
#define ATTR_TEMPORARY_LE	cpu_to_le32(ATTR_TEMPORARY)
#define ATTR_SPARSE_FILE_LE	cpu_to_le32(ATTR_SPARSE)
#define ATTR_REPARSE_POINT_LE	cpu_to_le32(ATTR_REPARSE)
#define ATTR_COMPRESSED_LE	cpu_to_le32(ATTR_COMPRESSED)
#define ATTR_OFFLINE_LE		cpu_to_le32(ATTR_OFFLINE)
#define ATTR_NOT_CONTENT_INDEXED_LE	cpu_to_le32(ATTR_NOT_CONTENT_INDEXED)
#define ATTR_ENCRYPTED_LE	cpu_to_le32(ATTR_ENCRYPTED)
#define ATTR_INTEGRITY_STREAML_LE	cpu_to_le32(0x00008000)
#define ATTR_NO_SCRUB_DATA_LE	cpu_to_le32(0x00020000)
#define ATTR_MASK_LE		cpu_to_le32(0x00007FB7)

/* Oplock levels */
#define SMB2_OPLOCK_LEVEL_NONE		0x00
#define SMB2_OPLOCK_LEVEL_II		0x01
#define SMB2_OPLOCK_LEVEL_EXCLUSIVE	0x08
#define SMB2_OPLOCK_LEVEL_BATCH		0x09
#define SMB2_OPLOCK_LEVEL_LEASE		0xFF
/* Non-spec internal type */
#define SMB2_OPLOCK_LEVEL_NOCHANGE	0x99

/* Desired Access Flags */
#define FILE_READ_DATA_LE		cpu_to_le32(0x00000001)
#define FILE_LIST_DIRECTORY_LE		cpu_to_le32(0x00000001)
#define FILE_WRITE_DATA_LE		cpu_to_le32(0x00000002)
#define FILE_ADD_FILE_LE		cpu_to_le32(0x00000002)
#define FILE_APPEND_DATA_LE		cpu_to_le32(0x00000004)
#define FILE_ADD_SUBDIRECTORY_LE	cpu_to_le32(0x00000004)
#define FILE_READ_EA_LE			cpu_to_le32(0x00000008)
#define FILE_WRITE_EA_LE		cpu_to_le32(0x00000010)
#define FILE_EXECUTE_LE			cpu_to_le32(0x00000020)
#define FILE_TRAVERSE_LE		cpu_to_le32(0x00000020)
#define FILE_DELETE_CHILD_LE		cpu_to_le32(0x00000040)
#define FILE_READ_ATTRIBUTES_LE		cpu_to_le32(0x00000080)
#define FILE_WRITE_ATTRIBUTES_LE	cpu_to_le32(0x00000100)
#define FILE_DELETE_LE			cpu_to_le32(0x00010000)
#define FILE_READ_CONTROL_LE		cpu_to_le32(0x00020000)
#define FILE_WRITE_DAC_LE		cpu_to_le32(0x00040000)
#define FILE_WRITE_OWNER_LE		cpu_to_le32(0x00080000)
#define FILE_SYNCHRONIZE_LE		cpu_to_le32(0x00100000)
#define FILE_ACCESS_SYSTEM_SECURITY_LE	cpu_to_le32(0x01000000)
#define FILE_MAXIMAL_ACCESS_LE		cpu_to_le32(0x02000000)
#define FILE_GENERIC_ALL_LE		cpu_to_le32(0x10000000)
#define FILE_GENERIC_EXECUTE_LE		cpu_to_le32(0x20000000)
#define FILE_GENERIC_WRITE_LE		cpu_to_le32(0x40000000)
#define FILE_GENERIC_READ_LE		cpu_to_le32(0x80000000)
#define DESIRED_ACCESS_MASK		cpu_to_le32(0xF21F01FF)

/* ShareAccess Flags */
#define FILE_SHARE_READ_LE		cpu_to_le32(0x00000001)
#define FILE_SHARE_WRITE_LE		cpu_to_le32(0x00000002)
#define FILE_SHARE_DELETE_LE		cpu_to_le32(0x00000004)
#define FILE_SHARE_ALL_LE		cpu_to_le32(0x00000007)

/* CreateDisposition Flags */
#define FILE_SUPERSEDE_LE		cpu_to_le32(0x00000000)
#define FILE_OPEN_LE			cpu_to_le32(0x00000001)
#define FILE_CREATE_LE			cpu_to_le32(0x00000002)
#define	FILE_OPEN_IF_LE			cpu_to_le32(0x00000003)
#define FILE_OVERWRITE_LE		cpu_to_le32(0x00000004)
#define FILE_OVERWRITE_IF_LE		cpu_to_le32(0x00000005)
#define FILE_CREATE_MASK_LE		cpu_to_le32(0x00000007)

#define FILE_READ_DESIRED_ACCESS_LE	(FILE_READ_DATA_LE |		\
					FILE_READ_EA_LE |		\
					FILE_GENERIC_READ_LE)
#define FILE_WRITE_DESIRE_ACCESS_LE	(FILE_WRITE_DATA_LE |		\
					FILE_APPEND_DATA_LE |		\
					FILE_WRITE_EA_LE |		\
					FILE_WRITE_ATTRIBUTES_LE |	\
					FILE_GENERIC_WRITE_LE)

/* Impersonation Levels */
#define IL_ANONYMOUS_LE		cpu_to_le32(0x00000000)
#define IL_IDENTIFICATION_LE	cpu_to_le32(0x00000001)
#define IL_IMPERSONATION_LE	cpu_to_le32(0x00000002)
#define IL_DELEGATE_LE		cpu_to_le32(0x00000003)

/* Create Context Values */
#define SMB2_CREATE_EA_BUFFER			"ExtA" /* extended attributes */
#define SMB2_CREATE_SD_BUFFER			"SecD" /* security descriptor */
#define SMB2_CREATE_DURABLE_HANDLE_REQUEST	"DHnQ"
#define SMB2_CREATE_DURABLE_HANDLE_RECONNECT	"DHnC"
#define SMB2_CREATE_ALLOCATION_SIZE		"AlSi"
#define SMB2_CREATE_QUERY_MAXIMAL_ACCESS_REQUEST "MxAc"
#define SMB2_CREATE_TIMEWARP_REQUEST		"TWrp"
#define SMB2_CREATE_QUERY_ON_DISK_ID		"QFid"
#define SMB2_CREATE_REQUEST_LEASE		"RqLs"
#define SMB2_CREATE_DURABLE_HANDLE_REQUEST_V2   "DH2Q"
#define SMB2_CREATE_DURABLE_HANDLE_RECONNECT_V2 "DH2C"
#define SMB2_CREATE_APP_INSTANCE_ID     "\x45\xBC\xA6\x6A\xEF\xA7\xF7\x4A\x90\x08\xFA\x46\x2E\x14\x4D\x74"
 #define SMB2_CREATE_APP_INSTANCE_VERSION	"\xB9\x82\xD0\xB7\x3B\x56\x07\x4F\xA0\x7B\x52\x4A\x81\x16\xA0\x10"
#define SVHDX_OPEN_DEVICE_CONTEXT       0x83CE6F1AD851E0986E34401CC9BCFCE9
#define SMB2_CREATE_TAG_POSIX		"\x93\xAD\x25\x50\x9C\xB4\x11\xE7\xB4\x23\x83\xDE\x96\x8B\xCD\x7C"

struct smb2_create_req {
	struct smb2_hdr hdr;
	__le16 StructureSize;	/* Must be 57 */
	__u8   SecurityFlags;
	__u8   RequestedOplockLevel;
	__le32 ImpersonationLevel;
	__le64 SmbCreateFlags;
	__le64 Reserved;
	__le32 DesiredAccess;
	__le32 FileAttributes;
	__le32 ShareAccess;
	__le32 CreateDisposition;
	__le32 CreateOptions;
	__le16 NameOffset;
	__le16 NameLength;
	__le32 CreateContextsOffset;
	__le32 CreateContextsLength;
	__u8   Buffer[0];
} __packed;

struct smb2_create_rsp {
	struct smb2_hdr hdr;
	__le16 StructureSize;	/* Must be 89 */
	__u8   OplockLevel;
	__u8   Reserved;
	__le32 CreateAction;
	__le64 CreationTime;
	__le64 LastAccessTime;
	__le64 LastWriteTime;
	__le64 ChangeTime;
	__le64 AllocationSize;
	__le64 EndofFile;
	__le32 FileAttributes;
	__le32 Reserved2;
	__le64  PersistentFileId;
	__le64  VolatileFileId;
	__le32 CreateContextsOffset;
	__le32 CreateContextsLength;
	__u8   Buffer[1];
} __packed;

struct create_context {
	__le32 Next;
	__le16 NameOffset;
	__le16 NameLength;
	__le16 Reserved;
	__le16 DataOffset;
	__le32 DataLength;
	__u8 Buffer[0];
} __packed;

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

struct lease_context {
	__le64 LeaseKeyLow;
	__le64 LeaseKeyHigh;
	__le32 LeaseState;
	__le32 LeaseFlags;
	__le64 LeaseDuration;
} __packed;

struct lease_context_v2 {
	__le64 LeaseKeyLow;
	__le64 LeaseKeyHigh;
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

/* Currently defined values for close flags */
#define SMB2_CLOSE_FLAG_POSTQUERY_ATTRIB	cpu_to_le16(0x0001)
struct smb2_close_req {
	struct smb2_hdr hdr;
	__le16 StructureSize;	/* Must be 24 */
	__le16 Flags;
	__le32 Reserved;
	__le64  PersistentFileId;
	__le64  VolatileFileId;
} __packed;

struct smb2_close_rsp {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* 60 */
	__le16 Flags;
	__le32 Reserved;
	__le64 CreationTime;
	__le64 LastAccessTime;
	__le64 LastWriteTime;
	__le64 ChangeTime;
	__le64 AllocationSize;	/* Beginning of FILE_STANDARD_INFO equivalent */
	__le64 EndOfFile;
	__le32 Attributes;
} __packed;

struct smb2_flush_req {
	struct smb2_hdr hdr;
	__le16 StructureSize;	/* Must be 24 */
	__le16 Reserved1;
	__le32 Reserved2;
	__le64  PersistentFileId;
	__le64  VolatileFileId;
} __packed;

struct smb2_flush_rsp {
	struct smb2_hdr hdr;
	__le16 StructureSize;
	__le16 Reserved;
} __packed;

struct smb2_buffer_desc_v1 {
	__le64 offset;
	__le32 token;
	__le32 length;
} __packed;

#define SMB2_CHANNEL_NONE		cpu_to_le32(0x00000000)
#define SMB2_CHANNEL_RDMA_V1		cpu_to_le32(0x00000001)
#define SMB2_CHANNEL_RDMA_V1_INVALIDATE cpu_to_le32(0x00000002)

struct smb2_read_req {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 49 */
	__u8   Padding; /* offset from start of SMB2 header to place read */
	__u8   Reserved;
	__le32 Length;
	__le64 Offset;
	__le64  PersistentFileId;
	__le64  VolatileFileId;
	__le32 MinimumCount;
	__le32 Channel; /* Reserved MBZ */
	__le32 RemainingBytes;
	__le16 ReadChannelInfoOffset; /* Reserved MBZ */
	__le16 ReadChannelInfoLength; /* Reserved MBZ */
	__u8   Buffer[1];
} __packed;

struct smb2_read_rsp {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 17 */
	__u8   DataOffset;
	__u8   Reserved;
	__le32 DataLength;
	__le32 DataRemaining;
	__u32  Reserved2;
	__u8   Buffer[1];
} __packed;

/* For write request Flags field below the following flag is defined: */
#define SMB2_WRITEFLAG_WRITE_THROUGH 0x00000001

struct smb2_write_req {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 49 */
	__le16 DataOffset; /* offset from start of SMB2 header to write data */
	__le32 Length;
	__le64 Offset;
	__le64  PersistentFileId;
	__le64  VolatileFileId;
	__le32 Channel; /* Reserved MBZ */
	__le32 RemainingBytes;
	__le16 WriteChannelInfoOffset; /* Reserved MBZ */
	__le16 WriteChannelInfoLength; /* Reserved MBZ */
	__le32 Flags;
	__u8   Buffer[1];
} __packed;

struct smb2_write_rsp {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 17 */
	__u8   DataOffset;
	__u8   Reserved;
	__le32 DataLength;
	__le32 DataRemaining;
	__u32  Reserved2;
	__u8   Buffer[1];
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

/* Completion Filter flags for Notify */
#define FILE_NOTIFY_CHANGE_FILE_NAME	0x00000001
#define FILE_NOTIFY_CHANGE_DIR_NAME	0x00000002
#define FILE_NOTIFY_CHANGE_NAME		0x00000003
#define FILE_NOTIFY_CHANGE_ATTRIBUTES	0x00000004
#define FILE_NOTIFY_CHANGE_SIZE		0x00000008
#define FILE_NOTIFY_CHANGE_LAST_WRITE	0x00000010
#define FILE_NOTIFY_CHANGE_LAST_ACCESS	0x00000020
#define FILE_NOTIFY_CHANGE_CREATION	0x00000040
#define FILE_NOTIFY_CHANGE_EA		0x00000080
#define FILE_NOTIFY_CHANGE_SECURITY	0x00000100
#define FILE_NOTIFY_CHANGE_STREAM_NAME	0x00000200
#define FILE_NOTIFY_CHANGE_STREAM_SIZE	0x00000400
#define FILE_NOTIFY_CHANGE_STREAM_WRITE	0x00000800

/* Flags */
#define SMB2_WATCH_TREE	0x0001

struct smb2_notify_req {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 32 */
	__le16 Flags;
	__le32 OutputBufferLength;
	__le64 PersistentFileId;
	__le64 VolatileFileId;
	__u32 CompletionFileter;
	__u32 Reserved;
} __packed;

struct smb2_notify_rsp {
	struct smb2_hdr hdr;
	__le16 StructureSize; /* Must be 9 */
	__le16 OutputBufferOffset;
	__le32 OutputBufferLength;
	__u8 Buffer[1];
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
	char   FileName[0];     /* New name to be assigned */
} __packed; /* level 10 Set */

struct smb2_file_link_info { /* encoding of request for level 11 */
	__u8   ReplaceIfExists; /* 1 = replace existing link with new */
				/* 0 = fail if link already exists */
	__u8   Reserved[7];
	__u64  RootDirectory;  /* MBZ for network operations (why says spec?) */
	__le32 FileNameLength;
	char   FileName[0];     /* Name to be assigned to new link */
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
	char FileName[0];
} __packed;

struct smb2_file_stream_info {
	__le32  NextEntryOffset;
	__le32  StreamNameLength;
	__le64 StreamSize;
	__le64 StreamAllocationSize;
	char   StreamName[0];
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

#define FILE_MODE_INFO_MASK cpu_to_le32(0x0000103e)

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

#endif	/* _SMB2PDU_H */
