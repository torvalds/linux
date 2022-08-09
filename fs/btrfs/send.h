/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2012 Alexander Block.  All rights reserved.
 * Copyright (C) 2012 STRATO.  All rights reserved.
 */

#ifndef BTRFS_SEND_H
#define BTRFS_SEND_H

#include "ctree.h"

#define BTRFS_SEND_STREAM_MAGIC "btrfs-stream"
#define BTRFS_SEND_STREAM_VERSION 1

#define BTRFS_SEND_BUF_SIZE SZ_64K

enum btrfs_tlv_type {
	BTRFS_TLV_U8,
	BTRFS_TLV_U16,
	BTRFS_TLV_U32,
	BTRFS_TLV_U64,
	BTRFS_TLV_BINARY,
	BTRFS_TLV_STRING,
	BTRFS_TLV_UUID,
	BTRFS_TLV_TIMESPEC,
};

struct btrfs_stream_header {
	char magic[sizeof(BTRFS_SEND_STREAM_MAGIC)];
	__le32 version;
} __attribute__ ((__packed__));

struct btrfs_cmd_header {
	/* len excluding the header */
	__le32 len;
	__le16 cmd;
	/* crc including the header with zero crc field */
	__le32 crc;
} __attribute__ ((__packed__));

struct btrfs_tlv_header {
	__le16 tlv_type;
	/* len excluding the header */
	__le16 tlv_len;
} __attribute__ ((__packed__));

/* commands */
enum btrfs_send_cmd {
	BTRFS_SEND_C_UNSPEC,

	/* Version 1 */
	BTRFS_SEND_C_SUBVOL,
	BTRFS_SEND_C_SNAPSHOT,

	BTRFS_SEND_C_MKFILE,
	BTRFS_SEND_C_MKDIR,
	BTRFS_SEND_C_MKNOD,
	BTRFS_SEND_C_MKFIFO,
	BTRFS_SEND_C_MKSOCK,
	BTRFS_SEND_C_SYMLINK,

	BTRFS_SEND_C_RENAME,
	BTRFS_SEND_C_LINK,
	BTRFS_SEND_C_UNLINK,
	BTRFS_SEND_C_RMDIR,

	BTRFS_SEND_C_SET_XATTR,
	BTRFS_SEND_C_REMOVE_XATTR,

	BTRFS_SEND_C_WRITE,
	BTRFS_SEND_C_CLONE,

	BTRFS_SEND_C_TRUNCATE,
	BTRFS_SEND_C_CHMOD,
	BTRFS_SEND_C_CHOWN,
	BTRFS_SEND_C_UTIMES,

	BTRFS_SEND_C_END,
	BTRFS_SEND_C_UPDATE_EXTENT,
	__BTRFS_SEND_C_MAX_V1,

	/* Version 2 */
	__BTRFS_SEND_C_MAX_V2,

	/* End */
	__BTRFS_SEND_C_MAX,
};
#define BTRFS_SEND_C_MAX (__BTRFS_SEND_C_MAX - 1)

/* attributes in send stream */
enum {
	BTRFS_SEND_A_UNSPEC,

	BTRFS_SEND_A_UUID,
	BTRFS_SEND_A_CTRANSID,

	BTRFS_SEND_A_INO,
	BTRFS_SEND_A_SIZE,
	BTRFS_SEND_A_MODE,
	BTRFS_SEND_A_UID,
	BTRFS_SEND_A_GID,
	BTRFS_SEND_A_RDEV,
	BTRFS_SEND_A_CTIME,
	BTRFS_SEND_A_MTIME,
	BTRFS_SEND_A_ATIME,
	BTRFS_SEND_A_OTIME,

	BTRFS_SEND_A_XATTR_NAME,
	BTRFS_SEND_A_XATTR_DATA,

	BTRFS_SEND_A_PATH,
	BTRFS_SEND_A_PATH_TO,
	BTRFS_SEND_A_PATH_LINK,

	BTRFS_SEND_A_FILE_OFFSET,
	BTRFS_SEND_A_DATA,

	BTRFS_SEND_A_CLONE_UUID,
	BTRFS_SEND_A_CLONE_CTRANSID,
	BTRFS_SEND_A_CLONE_PATH,
	BTRFS_SEND_A_CLONE_OFFSET,
	BTRFS_SEND_A_CLONE_LEN,

	__BTRFS_SEND_A_MAX,
};
#define BTRFS_SEND_A_MAX (__BTRFS_SEND_A_MAX - 1)

#ifdef __KERNEL__
long btrfs_ioctl_send(struct file *mnt_file, struct btrfs_ioctl_send_args *arg);
#endif

#endif
