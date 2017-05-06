/*
 * linux/fs/f2fs/f2fs_crypto.h
 *
 * Copied from linux/fs/ext4/ext4_crypto.h
 *
 * Copyright (C) 2015, Google, Inc.
 *
 * This contains encryption header content for f2fs
 *
 * Written by Michael Halcrow, 2015.
 * Modified by Jaegeuk Kim, 2015.
 */
#ifndef _F2FS_CRYPTO_H
#define _F2FS_CRYPTO_H

#include <linux/fs.h>

#define F2FS_KEY_DESCRIPTOR_SIZE	8

/* Policy provided via an ioctl on the topmost directory */
struct f2fs_encryption_policy {
	char version;
	char contents_encryption_mode;
	char filenames_encryption_mode;
	char flags;
	char master_key_descriptor[F2FS_KEY_DESCRIPTOR_SIZE];
} __attribute__((__packed__));

#define F2FS_ENCRYPTION_CONTEXT_FORMAT_V1	1
#define F2FS_KEY_DERIVATION_NONCE_SIZE		16

#define F2FS_POLICY_FLAGS_PAD_4		0x00
#define F2FS_POLICY_FLAGS_PAD_8		0x01
#define F2FS_POLICY_FLAGS_PAD_16	0x02
#define F2FS_POLICY_FLAGS_PAD_32	0x03
#define F2FS_POLICY_FLAGS_PAD_MASK	0x03
#define F2FS_POLICY_FLAGS_VALID		0x03

/**
 * Encryption context for inode
 *
 * Protector format:
 *  1 byte: Protector format (1 = this version)
 *  1 byte: File contents encryption mode
 *  1 byte: File names encryption mode
 *  1 byte: Flags
 *  8 bytes: Master Key descriptor
 *  16 bytes: Encryption Key derivation nonce
 */
struct f2fs_encryption_context {
	char format;
	char contents_encryption_mode;
	char filenames_encryption_mode;
	char flags;
	char master_key_descriptor[F2FS_KEY_DESCRIPTOR_SIZE];
	char nonce[F2FS_KEY_DERIVATION_NONCE_SIZE];
} __attribute__((__packed__));

/* Encryption parameters */
#define F2FS_XTS_TWEAK_SIZE 16
#define F2FS_AES_128_ECB_KEY_SIZE 16
#define F2FS_AES_256_GCM_KEY_SIZE 32
#define F2FS_AES_256_CBC_KEY_SIZE 32
#define F2FS_AES_256_CTS_KEY_SIZE 32
#define F2FS_AES_256_XTS_KEY_SIZE 64
#define F2FS_MAX_KEY_SIZE 64

#define F2FS_KEY_DESC_PREFIX "f2fs:"
#define F2FS_KEY_DESC_PREFIX_SIZE 5

struct f2fs_encryption_key {
	__u32 mode;
	char raw[F2FS_MAX_KEY_SIZE];
	__u32 size;
} __attribute__((__packed__));

struct f2fs_crypt_info {
	char		ci_data_mode;
	char		ci_filename_mode;
	char		ci_flags;
	struct crypto_ablkcipher *ci_ctfm;
	char		ci_master_key[F2FS_KEY_DESCRIPTOR_SIZE];
};

#define F2FS_CTX_REQUIRES_FREE_ENCRYPT_FL             0x00000001
#define F2FS_WRITE_PATH_FL			      0x00000002

struct f2fs_crypto_ctx {
	union {
		struct {
			struct page *bounce_page;       /* Ciphertext page */
			struct page *control_page;      /* Original page  */
		} w;
		struct {
			struct bio *bio;
			struct work_struct work;
		} r;
		struct list_head free_list;     /* Free list */
	};
	char flags;                      /* Flags */
};

struct f2fs_completion_result {
	struct completion completion;
	int res;
};

#define DECLARE_F2FS_COMPLETION_RESULT(ecr) \
	struct f2fs_completion_result ecr = { \
		COMPLETION_INITIALIZER((ecr).completion), 0 }

static inline int f2fs_encryption_key_size(int mode)
{
	switch (mode) {
	case F2FS_ENCRYPTION_MODE_AES_256_XTS:
		return F2FS_AES_256_XTS_KEY_SIZE;
	case F2FS_ENCRYPTION_MODE_AES_256_GCM:
		return F2FS_AES_256_GCM_KEY_SIZE;
	case F2FS_ENCRYPTION_MODE_AES_256_CBC:
		return F2FS_AES_256_CBC_KEY_SIZE;
	case F2FS_ENCRYPTION_MODE_AES_256_CTS:
		return F2FS_AES_256_CTS_KEY_SIZE;
	default:
		BUG();
	}
	return 0;
}

#define F2FS_FNAME_NUM_SCATTER_ENTRIES	4
#define F2FS_CRYPTO_BLOCK_SIZE		16
#define F2FS_FNAME_CRYPTO_DIGEST_SIZE	32

/**
 * For encrypted symlinks, the ciphertext length is stored at the beginning
 * of the string in little-endian format.
 */
struct f2fs_encrypted_symlink_data {
	__le16 len;
	char encrypted_path[1];
} __attribute__((__packed__));

/**
 * This function is used to calculate the disk space required to
 * store a filename of length l in encrypted symlink format.
 */
static inline u32 encrypted_symlink_data_len(u32 l)
{
	return (l + sizeof(struct f2fs_encrypted_symlink_data) - 1);
}
#endif	/* _F2FS_CRYPTO_H */
