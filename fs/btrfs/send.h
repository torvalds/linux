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
	BTRFS_SEND_C_UNSPEC		= 0,

	/* Version 1 */
	BTRFS_SEND_C_SUBVOL		= 1,
	BTRFS_SEND_C_SNAPSHOT		= 2,

	BTRFS_SEND_C_MKFILE		= 3,
	BTRFS_SEND_C_MKDIR		= 4,
	BTRFS_SEND_C_MKNOD		= 5,
	BTRFS_SEND_C_MKFIFO		= 6,
	BTRFS_SEND_C_MKSOCK		= 7,
	BTRFS_SEND_C_SYMLINK		= 8,

	BTRFS_SEND_C_RENAME		= 9,
	BTRFS_SEND_C_LINK		= 10,
	BTRFS_SEND_C_UNLINK		= 11,
	BTRFS_SEND_C_RMDIR		= 12,

	BTRFS_SEND_C_SET_XATTR		= 13,
	BTRFS_SEND_C_REMOVE_XATTR	= 14,

	BTRFS_SEND_C_WRITE		= 15,
	BTRFS_SEND_C_CLONE		= 16,

	BTRFS_SEND_C_TRUNCATE		= 17,
	BTRFS_SEND_C_CHMOD		= 18,
	BTRFS_SEND_C_CHOWN		= 19,
	BTRFS_SEND_C_UTIMES		= 20,

	BTRFS_SEND_C_END		= 21,
	BTRFS_SEND_C_UPDATE_EXTENT	= 22,
	BTRFS_SEND_C_MAX_V1		= 22,

	/* Version 2 */
	BTRFS_SEND_C_MAX_V2		= 22,

	/* End */
	BTRFS_SEND_C_MAX		= 22,
};

/* attributes in send stream */
enum {
	BTRFS_SEND_A_UNSPEC		= 0,

	BTRFS_SEND_A_UUID		= 1,
	BTRFS_SEND_A_CTRANSID		= 2,

	BTRFS_SEND_A_INO		= 3,
	BTRFS_SEND_A_SIZE		= 4,
	BTRFS_SEND_A_MODE		= 5,
	BTRFS_SEND_A_UID		= 6,
	BTRFS_SEND_A_GID		= 7,
	BTRFS_SEND_A_RDEV		= 8,
	BTRFS_SEND_A_CTIME		= 9,
	BTRFS_SEND_A_MTIME		= 10,
	BTRFS_SEND_A_ATIME		= 11,
	BTRFS_SEND_A_OTIME		= 12,

	BTRFS_SEND_A_XATTR_NAME		= 13,
	BTRFS_SEND_A_XATTR_DATA		= 14,

	BTRFS_SEND_A_PATH		= 15,
	BTRFS_SEND_A_PATH_TO		= 16,
	BTRFS_SEND_A_PATH_LINK		= 17,

	BTRFS_SEND_A_FILE_OFFSET	= 18,
	BTRFS_SEND_A_DATA		= 19,

	BTRFS_SEND_A_CLONE_UUID		= 20,
	BTRFS_SEND_A_CLONE_CTRANSID	= 21,
	BTRFS_SEND_A_CLONE_PATH		= 22,
	BTRFS_SEND_A_CLONE_OFFSET	= 23,
	BTRFS_SEND_A_CLONE_LEN		= 24,

	BTRFS_SEND_A_MAX		= 24,
};

#ifdef __KERNEL__
long btrfs_ioctl_send(struct inode *inode, struct btrfs_ioctl_send_args *arg);
#endif

#endif
