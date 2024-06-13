/* SPDX-License-Identifier: LGPL-2.1 */
/*
 *
 *   Structure definitions for io control for cifs/smb3
 *
 *   Copyright (c) 2015 Steve French <steve.french@primarydata.com>
 *
 */

struct smb_mnt_fs_info {
	__u32	version; /* 0001 */
	__u16	protocol_id;
	__u16	tcon_flags;
	__u32	vol_serial_number;
	__u32	vol_create_time;
	__u32	share_caps;
	__u32	share_flags;
	__u32	sector_flags;
	__u32	optimal_sector_size;
	__u32	max_bytes_chunk;
	__u32	fs_attributes;
	__u32	max_path_component;
	__u32	device_type;
	__u32	device_characteristics;
	__u32	maximal_access;
	__u64   cifs_posix_caps;
} __packed;

struct smb_mnt_tcon_info {
	__u32	tid;
	__u64	session_id;
} __packed;

struct smb_snapshot_array {
	__u32	number_of_snapshots;
	__u32	number_of_snapshots_returned;
	__u32	snapshot_array_size;
	/*	snapshots[]; */
} __packed;

/* query_info flags */
#define PASSTHRU_QUERY_INFO	0x00000000
#define PASSTHRU_FSCTL		0x00000001
#define PASSTHRU_SET_INFO	0x00000002
struct smb_query_info {
	__u32   info_type;
	__u32   file_info_class;
	__u32   additional_information;
	__u32   flags;
	__u32	input_buffer_length;
	__u32	output_buffer_length;
	/* char buffer[]; */
} __packed;

/*
 * Dumping the commonly used 16 byte (e.g. CCM and GCM128) keys still supported
 * for backlevel compatibility, but is not sufficient for dumping the less
 * frequently used GCM256 (32 byte) keys (see the newer "CIFS_DUMP_FULL_KEY"
 * ioctl for dumping decryption info for GCM256 mounts)
 */
struct smb3_key_debug_info {
	__u64	Suid;
	__u16	cipher_type;
	__u8	auth_key[16]; /* SMB2_NTLMV2_SESSKEY_SIZE */
	__u8	smb3encryptionkey[SMB3_SIGN_KEY_SIZE];
	__u8	smb3decryptionkey[SMB3_SIGN_KEY_SIZE];
} __packed;

/*
 * Dump variable-sized keys
 */
struct smb3_full_key_debug_info {
	/* INPUT: size of userspace buffer */
	__u32   in_size;

	/*
	 * INPUT: 0 for current user, otherwise session to dump
	 * OUTPUT: session id that was dumped
	 */
	__u64	session_id;
	__u16	cipher_type;
	__u8    session_key_length;
	__u8    server_in_key_length;
	__u8    server_out_key_length;
	__u8    data[];
	/*
	 * return this struct with the keys appended at the end:
	 * __u8 session_key[session_key_length];
	 * __u8 server_in_key[server_in_key_length];
	 * __u8 server_out_key[server_out_key_length];
	 */
} __packed;

struct smb3_notify {
	__u32	completion_filter;
	bool	watch_tree;
} __packed;

struct smb3_notify_info {
	__u32	completion_filter;
	bool	watch_tree;
	__u32   data_len; /* size of notify data below */
	__u8	notify_data[];
} __packed;

#define CIFS_IOCTL_MAGIC	0xCF
#define CIFS_IOC_COPYCHUNK_FILE	_IOW(CIFS_IOCTL_MAGIC, 3, int)
#define CIFS_IOC_SET_INTEGRITY  _IO(CIFS_IOCTL_MAGIC, 4)
#define CIFS_IOC_GET_MNT_INFO _IOR(CIFS_IOCTL_MAGIC, 5, struct smb_mnt_fs_info)
#define CIFS_ENUMERATE_SNAPSHOTS _IOR(CIFS_IOCTL_MAGIC, 6, struct smb_snapshot_array)
#define CIFS_QUERY_INFO _IOWR(CIFS_IOCTL_MAGIC, 7, struct smb_query_info)
#define CIFS_DUMP_KEY _IOWR(CIFS_IOCTL_MAGIC, 8, struct smb3_key_debug_info)
#define CIFS_IOC_NOTIFY _IOW(CIFS_IOCTL_MAGIC, 9, struct smb3_notify)
#define CIFS_DUMP_FULL_KEY _IOWR(CIFS_IOCTL_MAGIC, 10, struct smb3_full_key_debug_info)
#define CIFS_IOC_NOTIFY_INFO _IOWR(CIFS_IOCTL_MAGIC, 11, struct smb3_notify_info)
#define CIFS_IOC_GET_TCON_INFO _IOR(CIFS_IOCTL_MAGIC, 12, struct smb_mnt_tcon_info)
#define CIFS_IOC_SHUTDOWN _IOR('X', 125, __u32)

/*
 * Flags for going down operation
 */
#define CIFS_GOING_FLAGS_DEFAULT                0x0     /* going down */
#define CIFS_GOING_FLAGS_LOGFLUSH               0x1     /* flush log but not data */
#define CIFS_GOING_FLAGS_NOLOGFLUSH             0x2     /* don't flush log nor data */

static inline bool cifs_forced_shutdown(struct cifs_sb_info *sbi)
{
	if (CIFS_MOUNT_SHUTDOWN & sbi->mnt_cifs_flags)
		return true;
	else
		return false;
}
