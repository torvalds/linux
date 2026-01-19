/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#ifndef __SMB_SERVER_COMMON_H__
#define __SMB_SERVER_COMMON_H__

#include <linux/kernel.h>

#include "glob.h"
#include "../common/smbglob.h"
#include "../common/smb1pdu.h"
#include "../common/smb2pdu.h"
#include "../common/fscc.h"
#include "smb2pdu.h"

/* ksmbd's Specific ERRNO */
#define ESHARE			50000

#define SMB1_PROT		0
#define SMB2_PROT		1
#define SMB21_PROT		2
/* multi-protocol negotiate request */
#define SMB2X_PROT		3
#define SMB30_PROT		4
#define SMB302_PROT		5
#define SMB311_PROT		6
#define BAD_PROT		0xFFFF

#define SMB_ECHO_INTERVAL	(60 * HZ)

#define MAX_STREAM_PROT_LEN	0x00FFFFFF

/* Responses when opening a file. */
#define F_SUPERSEDED	0
#define F_OPENED	1
#define F_CREATED	2
#define F_OVERWRITTEN	3

/* Combinations of file access permission bits */
#define SET_FILE_READ_RIGHTS (FILE_READ_DATA | FILE_READ_EA \
		| FILE_READ_ATTRIBUTES \
		| DELETE | READ_CONTROL | WRITE_DAC \
		| WRITE_OWNER | SYNCHRONIZE)
#define SET_FILE_WRITE_RIGHTS (FILE_WRITE_DATA | FILE_APPEND_DATA \
		| FILE_WRITE_EA \
		| FILE_DELETE_CHILD \
		| FILE_WRITE_ATTRIBUTES \
		| DELETE | READ_CONTROL | WRITE_DAC \
		| WRITE_OWNER | SYNCHRONIZE)

/* generic flags for file open */
#define GENERIC_READ_FLAGS	(READ_CONTROL | FILE_READ_DATA | \
		FILE_READ_ATTRIBUTES | \
		FILE_READ_EA | SYNCHRONIZE)

#define GENERIC_WRITE_FLAGS	(READ_CONTROL | FILE_WRITE_DATA | \
		FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | \
		FILE_APPEND_DATA | SYNCHRONIZE)

#define GENERIC_EXECUTE_FLAGS	(READ_CONTROL | FILE_EXECUTE | \
		FILE_READ_ATTRIBUTES | SYNCHRONIZE)

#define GENERIC_ALL_FLAGS	(DELETE | READ_CONTROL | WRITE_DAC | \
		WRITE_OWNER | SYNCHRONIZE | FILE_READ_DATA | \
		FILE_WRITE_DATA | FILE_APPEND_DATA | \
		FILE_READ_EA | FILE_WRITE_EA | \
		FILE_EXECUTE | FILE_DELETE_CHILD | \
		FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES)

#define SMB_COM_NEGOTIATE		0x72 /* See MS-CIFS 2.2.2.1 */

/* See MS-CIFS 2.2.3.1 */
#define SMBFLG_RESPONSE 0x80	/* this PDU is a response from server */

/*
 * See MS-CIFS 2.2.3.1
 *     MS-SMB 2.2.3.1
 */
#define SMBFLG2_IS_LONG_NAME	cpu_to_le16(0x40)
#define SMBFLG2_EXT_SEC		cpu_to_le16(0x800)
#define SMBFLG2_ERR_STATUS	cpu_to_le16(0x4000)
#define SMBFLG2_UNICODE		cpu_to_le16(0x8000)

/* See MS-CIFS 2.2.4.52.2 */
struct smb_negotiate_rsp {
	struct smb_hdr hdr;     /* wct = 17 */
	__le16 DialectIndex; /* 0xFFFF = no dialect acceptable */
	__le16 ByteCount;
} __packed;

struct filesystem_vol_info {
	__le64 VolumeCreationTime;
	__le32 SerialNumber;
	__le32 VolumeLabelSize;
	__le16 Reserved;
	__le16 VolumeLabel[];
} __packed;

#define EXTENDED_INFO_MAGIC 0x43667364	/* Cfsd */
#define STRING_LENGTH 28

struct fs_extended_info {
	__le32 magic;
	__le32 version;
	__le32 release;
	__u64 rel_date;
	char    version_string[STRING_LENGTH];
} __packed;

struct object_id_info {
	char objid[16];
	struct fs_extended_info extended_info;
} __packed;

struct file_names_info {
	__le32 NextEntryOffset;
	__u32 FileIndex;
	__le32 FileNameLength;
	char FileName[];
} __packed;   /* level 0xc FF resp data */

struct file_id_both_directory_info {
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
	__le16 Reserved2;
	__le64 UniqueId;
	char FileName[];
} __packed;

struct smb_version_ops {
	u16 (*get_cmd_val)(struct ksmbd_work *swork);
	int (*init_rsp_hdr)(struct ksmbd_work *swork);
	void (*set_rsp_status)(struct ksmbd_work *swork, __le32 err);
	int (*allocate_rsp_buf)(struct ksmbd_work *work);
	int (*set_rsp_credits)(struct ksmbd_work *work);
	int (*check_user_session)(struct ksmbd_work *work);
	int (*get_ksmbd_tcon)(struct ksmbd_work *work);
	bool (*is_sign_req)(struct ksmbd_work *work, unsigned int command);
	int (*check_sign_req)(struct ksmbd_work *work);
	void (*set_sign_rsp)(struct ksmbd_work *work);
	int (*generate_signingkey)(struct ksmbd_session *sess, struct ksmbd_conn *conn);
	void (*generate_encryptionkey)(struct ksmbd_conn *conn, struct ksmbd_session *sess);
	bool (*is_transform_hdr)(void *buf);
	int (*decrypt_req)(struct ksmbd_work *work);
	int (*encrypt_resp)(struct ksmbd_work *work);
};

struct smb_version_cmds {
	int (*proc)(struct ksmbd_work *swork);
};

int ksmbd_min_protocol(void);
int ksmbd_max_protocol(void);

int ksmbd_lookup_protocol_idx(char *str);

int ksmbd_verify_smb_message(struct ksmbd_work *work);
bool ksmbd_smb_request(struct ksmbd_conn *conn);

int ksmbd_lookup_dialect_by_id(__le16 *cli_dialects, __le16 dialects_count);

int ksmbd_init_smb_server(struct ksmbd_conn *conn);

struct ksmbd_kstat;
int ksmbd_populate_dot_dotdot_entries(struct ksmbd_work *work,
				      int info_level,
				      struct ksmbd_file *dir,
				      struct ksmbd_dir_info *d_info,
				      char *search_pattern,
				      int (*fn)(struct ksmbd_conn *,
						int,
						struct ksmbd_dir_info *,
						struct ksmbd_kstat *));

int ksmbd_extract_shortname(struct ksmbd_conn *conn,
			    const char *longname,
			    char *shortname);

int ksmbd_smb_negotiate_common(struct ksmbd_work *work, unsigned int command);

int ksmbd_smb_check_shared_mode(struct file *filp, struct ksmbd_file *curr_fp);
int __ksmbd_override_fsids(struct ksmbd_work *work,
			   struct ksmbd_share_config *share);
int ksmbd_override_fsids(struct ksmbd_work *work);
void ksmbd_revert_fsids(struct ksmbd_work *work);

unsigned int ksmbd_server_side_copy_max_chunk_count(void);
unsigned int ksmbd_server_side_copy_max_chunk_size(void);
unsigned int ksmbd_server_side_copy_max_total_size(void);
bool is_asterisk(char *p);
__le32 smb_map_generic_desired_access(__le32 daccess);

/*
 * Get the body of the smb message excluding the 4 byte rfc1002 headers
 * from request/response buffer.
 */
static inline void *smb_get_msg(void *buf)
{
	return buf + 4;
}
#endif /* __SMB_SERVER_COMMON_H__ */
