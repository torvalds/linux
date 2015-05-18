/*
 * linux/fs/ext4/ext4_crypto.h
 *
 * Copyright (C) 2015, Google, Inc.
 *
 * This contains encryption header content for ext4
 *
 * Written by Michael Halcrow, 2015.
 */

#ifndef _EXT4_CRYPTO_H
#define _EXT4_CRYPTO_H

#include <linux/fs.h>

#define EXT4_KEY_DESCRIPTOR_SIZE 8

/* Policy provided via an ioctl on the topmost directory */
struct ext4_encryption_policy {
	char version;
	char contents_encryption_mode;
	char filenames_encryption_mode;
	char flags;
	char master_key_descriptor[EXT4_KEY_DESCRIPTOR_SIZE];
} __attribute__((__packed__));

#define EXT4_ENCRYPTION_CONTEXT_FORMAT_V1 1
#define EXT4_KEY_DERIVATION_NONCE_SIZE 16

#define EXT4_POLICY_FLAGS_PAD_4		0x00
#define EXT4_POLICY_FLAGS_PAD_8		0x01
#define EXT4_POLICY_FLAGS_PAD_16	0x02
#define EXT4_POLICY_FLAGS_PAD_32	0x03
#define EXT4_POLICY_FLAGS_PAD_MASK	0x03
#define EXT4_POLICY_FLAGS_VALID		0x03

/**
 * Encryption context for inode
 *
 * Protector format:
 *  1 byte: Protector format (1 = this version)
 *  1 byte: File contents encryption mode
 *  1 byte: File names encryption mode
 *  1 byte: Reserved
 *  8 bytes: Master Key descriptor
 *  16 bytes: Encryption Key derivation nonce
 */
struct ext4_encryption_context {
	char format;
	char contents_encryption_mode;
	char filenames_encryption_mode;
	char flags;
	char master_key_descriptor[EXT4_KEY_DESCRIPTOR_SIZE];
	char nonce[EXT4_KEY_DERIVATION_NONCE_SIZE];
} __attribute__((__packed__));

/* Encryption parameters */
#define EXT4_XTS_TWEAK_SIZE 16
#define EXT4_AES_128_ECB_KEY_SIZE 16
#define EXT4_AES_256_GCM_KEY_SIZE 32
#define EXT4_AES_256_CBC_KEY_SIZE 32
#define EXT4_AES_256_CTS_KEY_SIZE 32
#define EXT4_AES_256_XTS_KEY_SIZE 64
#define EXT4_MAX_KEY_SIZE 64

#define EXT4_KEY_DESC_PREFIX "ext4:"
#define EXT4_KEY_DESC_PREFIX_SIZE 5

struct ext4_encryption_key {
	uint32_t mode;
	char raw[EXT4_MAX_KEY_SIZE];
	uint32_t size;
};

#define EXT4_CTX_REQUIRES_FREE_ENCRYPT_FL             0x00000001
#define EXT4_BOUNCE_PAGE_REQUIRES_FREE_ENCRYPT_FL     0x00000002

struct ext4_crypto_ctx {
	struct crypto_tfm *tfm;         /* Crypto API context */
	struct page *bounce_page;       /* Ciphertext page on write path */
	struct page *control_page;      /* Original page on write path */
	struct bio *bio;                /* The bio for this context */
	struct work_struct work;        /* Work queue for read complete path */
	struct list_head free_list;     /* Free list */
	int flags;                      /* Flags */
	int mode;                       /* Encryption mode for tfm */
};

struct ext4_completion_result {
	struct completion completion;
	int res;
};

#define DECLARE_EXT4_COMPLETION_RESULT(ecr) \
	struct ext4_completion_result ecr = { \
		COMPLETION_INITIALIZER((ecr).completion), 0 }

static inline int ext4_encryption_key_size(int mode)
{
	switch (mode) {
	case EXT4_ENCRYPTION_MODE_AES_256_XTS:
		return EXT4_AES_256_XTS_KEY_SIZE;
	case EXT4_ENCRYPTION_MODE_AES_256_GCM:
		return EXT4_AES_256_GCM_KEY_SIZE;
	case EXT4_ENCRYPTION_MODE_AES_256_CBC:
		return EXT4_AES_256_CBC_KEY_SIZE;
	case EXT4_ENCRYPTION_MODE_AES_256_CTS:
		return EXT4_AES_256_CTS_KEY_SIZE;
	default:
		BUG();
	}
	return 0;
}

#define EXT4_FNAME_NUM_SCATTER_ENTRIES	4
#define EXT4_CRYPTO_BLOCK_SIZE		16
#define EXT4_FNAME_CRYPTO_DIGEST_SIZE	32

struct ext4_str {
	unsigned char *name;
	u32 len;
};

struct ext4_fname_crypto_ctx {
	u32 lim;
	struct crypto_ablkcipher *ctfm;
	struct crypto_hash *htfm;
	struct ext4_encryption_key key;
	unsigned flags : 8;
	unsigned has_valid_key : 1;
	unsigned ctfm_key_is_ready : 1;
};

/**
 * For encrypted symlinks, the ciphertext length is stored at the beginning
 * of the string in little-endian format.
 */
struct ext4_encrypted_symlink_data {
	__le16 len;
	char encrypted_path[1];
} __attribute__((__packed__));

/**
 * This function is used to calculate the disk space required to
 * store a filename of length l in encrypted symlink format.
 */
static inline u32 encrypted_symlink_data_len(u32 l)
{
	if (l < EXT4_CRYPTO_BLOCK_SIZE)
		l = EXT4_CRYPTO_BLOCK_SIZE;
	return (l + sizeof(struct ext4_encrypted_symlink_data) - 1);
}

#endif	/* _EXT4_CRYPTO_H */
