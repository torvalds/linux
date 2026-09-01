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

/* SMB2 Max Credits */
#define SMB2_MAX_CREDITS		8192

/* BB FIXME - analyze following length BB */
#define MAX_SMB2_HDR_SIZE 0x78 /* 4 len + 64 hdr + (2*24 wct) + 2 bct + 2 pad */

#define SMB21_DEFAULT_IOSIZE	(1024 * 1024)
#define SMB3_DEFAULT_TRANS_SIZE	(4 * 1024 * 1024)
#define SMB3_MIN_IOSIZE		(64 * 1024)
#define SMB3_MAX_IOSIZE		(8 * 1024 * 1024)
#define SMB3_MAX_MSGSIZE	(4 * 4096)

/*
 *	Definitions for SMB2 Protocol Data Units (network frames)
 *
 *  See MS-SMB2.PDF specification for protocol details.
 *  The Naming convention is the lower case version of the SMB2
 *  command code name for the struct. Note that structures must be packed.
 *
 */

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

#define SMB2_SESSION_TIMEOUT		(10 * HZ)

/* Apple Defined Contexts */
#define SMB2_CREATE_AAPL		"AAPL"

/*
 * AAPL SMB2 extension -- kAAPL_SERVER_QUERY create context.
 *
 * Command code and bitmap values are the existing
 * SMB2_CRTCTX_AAPL_* constants in fs/smb/common/smb2pdu.h.
 *
 * Omitting the model string when reply_bitmap includes
 * SMB2_CRTCTX_AAPL_MODEL_INFO causes smbfs.kext to enter a broken
 * disconnect path requiring a reboot.
 *
 * Layout: ccontext(16) + Name[4] + Pad[4] + cmd(4) + reserved(4) +
 *         reply_bitmap(8) + server_caps(8) + vol_caps(8)
 * When MODEL_INFO requested, appended: pad2(4) + model_bytes(4) + UTF-16LE
 */
#define SMB2_CREATE_AAPL_LEN	4

/*
 * Server capability flags (server_caps field) -- SMB2_CRTCTX_AAPL_UNIX_BASED:
 * prevents macOS Windows-compat mode (question-mark icons).
 * SMB2_CRTCTX_AAPL_SUPPORTS_OSX_COPYFILE: enables server-side file copy via
 * FSCTL_SRV_COPYCHUNK. SMB2_CRTCTX_AAPL_SUPPORTS_READ_DIR_ATTR: inline
 * FinderInfo per FIND entry, set when client also advertises the bit;
 * format: EaSize=max_access, ShortName[0..7]=rfork_size,
 * ShortName[8..23]=FinderInfo(16B), Reserved2=unix_mode.
 */
#define AAPL_SERVER_CAPS_KSMBD	(SMB2_CRTCTX_AAPL_UNIX_BASED | \
				 SMB2_CRTCTX_AAPL_SUPPORTS_OSX_COPYFILE | \
				 SMB2_CRTCTX_AAPL_SUPPORTS_READ_DIR_ATTR)

/*
 * READDIR_ATTR_V2 (SMB2_CRTCTX_AAPL_SUPPORTS_READ_DIR_ATTR_V2, see
 * fs/smb/common/smb2pdu.h) extends the same inline-FinderInfo mechanism
 * above with a flags field, confirmed byte-identical to V1 otherwise
 * against AAPL's actual public client behavior. When a client's own
 * client_caps requests V2, the server advertises V2 instead of V1 in
 * its own server_caps reply; V1 and V2 are mutually exclusive on the
 * wire, not both set together. The wire format's ShortNameLength+Reserved
 * (ignored in V1) become a single flags field in V2 --
 * AAPL_READDIR_ATTR_V2_NO_XATTR is the only flag bit currently defined,
 * signaling the item has no xattrs/streams so the client can skip a
 * separate query.
 */
#define AAPL_READDIR_ATTR_V2_NO_XATTR	0x01

/* Model string: up to 31 ASCII chars */
#define AAPL_MODEL_MAX_CHARS	31
#define AAPL_MODEL_UTF16_BYTES	(AAPL_MODEL_MAX_CHARS * 2)

/*
 * Max AAPL response: header(24) + base data(32) + pad2(4) + model_bytes(4)
 * + model(62), 8-byte aligned: ALIGN(126, 8) = 128 bytes.
 */
#define AAPL_RSP_MAX_SIZE	128

/* AAPL server query request (client->server) */
struct aapl_server_query_req {
	__le32 cmd;
	__le32 reserved;
	__le64 req_bitmap;
	__le64 client_caps;
} __packed;

struct create_aapl_rsp {
	struct create_context_hdr ccontext;
	__u8   Name[4];
	__u8   Pad[4];
	__le32 cmd;
	__le32 reserved;
	__le64 reply_bitmap;
	__le64 server_caps;
	__le64 vol_caps;
	/* when MODEL_INFO requested: __le32 pad2; __le32 model_bytes; __le16 model[] */
} __packed;

#define DURABLE_HANDLE_MAX_TIMEOUT	300000

struct create_alloc_size_req {
	struct create_context_hdr ccontext;
	__u8   Name[8];
	__le64 AllocationSize;
} __packed;

struct create_durable_rsp {
	struct create_context_hdr ccontext;
	__u8   Name[8];
	union {
		__u8  Reserved[8];
		__u64 data;
	} Data;
} __packed;

/*
 * See POSIX-SMB2 2.2.14.2.16
 * Link: https://gitlab.com/samba-team/smb3-posix-spec/-/blob/master/smb3_posix_extensions.md
 */
struct create_posix_rsp {
	struct create_context_hdr ccontext;
	__u8    Name[16];
	__le32 nlink;
	__le32 reparse_tag;
	__le32 mode;
	/* SidBuffer contain two sids(Domain sid(28), UNIX group sid(16)) */
	u8 SidBuffer[44];
} __packed;

#define SMB2_0_IOCTL_IS_FSCTL 0x00000001

struct sockaddr_storage_rsp {
	__le16 Family;
	union {
		struct smb_sockaddr_in addr4;
		struct smb_sockaddr_in6 addr6;
	};
} __packed;

struct file_object_buf_type1_ioctl_rsp {
	__u8 ObjectId[16];
	__u8 BirthVolumeId[16];
	__u8 BirthObjectId[16];
	__u8 DomainId[16];
} __packed;

struct file_sparse {
	__u8	SetSparse;
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

/*
 *	PDU query infolevel structure definitions
 *	BB consider moving to a different header
 */

struct smb2_file_access_info {
	__le32 AccessFlags;
} __packed;

struct smb2_file_alignment_info {
	__le32 AlignmentRequirement;
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

struct srv_snapshot_array {
	__le32 NumberOfSnapShots;
	__le32 NumberOfSnapShotsReturned;
	__le32 SnapShotArraySize;
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
	char name[];
} __packed; /* level 15 Query */

struct smb2_ea_info {
	__le32 NextEntryOffset;
	__u8   Flags;
	__u8   EaNameLength;
	__le16 EaValueLength;
	char name[];
	/* optionally followed by value */
} __packed; /* level 15 Query */

struct create_ea_buf_req {
	struct create_context_hdr ccontext;
	__u8   Name[8];
	struct smb2_ea_info ea;
} __packed;

struct create_sd_buf_req {
	struct create_context_hdr ccontext;
	__u8   Name[8];
	struct smb_ntsd ntsd;
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
	/* SidBuffer contain two sids (UNIX user sid(16), UNIX group sid(16)) */
	u8 SidBuffer[32];
	__le32 name_len;
	u8 name[];
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
void release_async_work(struct ksmbd_work *work);
void smb2_send_interim_resp(struct ksmbd_work *work, __le32 status);
struct channel *lookup_chann_list(struct ksmbd_session *sess,
				  struct ksmbd_conn *conn);
void smb3_preauth_hash_rsp(struct ksmbd_work *work);
bool smb3_is_transform_hdr(void *buf);
int smb3_decrypt_req(struct ksmbd_work *work);
int smb3_encrypt_resp(struct ksmbd_work *work);
bool smb3_11_final_sess_setup_resp(struct ksmbd_work *work);
int smb2_set_rsp_credits(struct ksmbd_work *work);
bool smb3_encryption_negotiated(struct ksmbd_conn *conn);

/* smb2 misc functions */
int ksmbd_smb2_check_message(struct ksmbd_work *work);
void smb2_complete_request_open(struct ksmbd_work *work);

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

#define POSIX_TYPE_FILE		0
#define POSIX_TYPE_DIR		1
#define POSIX_TYPE_SYMLINK	2
#define POSIX_TYPE_CHARDEV	3
#define POSIX_TYPE_BLKDEV	4
#define POSIX_TYPE_FIFO		5
#define POSIX_TYPE_SOCKET	6

#define POSIX_FILETYPE_SHIFT	12

#endif	/* _SMB2PDU_H */
