/*
 * fscrypt_private.h
 *
 * Copyright (C) 2015, Google, Inc.
 *
 * This contains encryption key functions.
 *
 * Written by Michael Halcrow, Ildar Muslukhov, and Uday Savagaonkar, 2015.
 */

#ifndef _FSCRYPT_PRIVATE_H
#define _FSCRYPT_PRIVATE_H

#include <linux/fscrypto.h>

#define FS_FNAME_CRYPTO_DIGEST_SIZE	32

/* Encryption parameters */
#define FS_XTS_TWEAK_SIZE		16
#define FS_AES_128_ECB_KEY_SIZE		16
#define FS_AES_256_GCM_KEY_SIZE		32
#define FS_AES_256_CBC_KEY_SIZE		32
#define FS_AES_256_CTS_KEY_SIZE		32
#define FS_AES_256_XTS_KEY_SIZE		64
#define FS_MAX_KEY_SIZE			64

#define FS_KEY_DESC_PREFIX		"fscrypt:"
#define FS_KEY_DESC_PREFIX_SIZE		8

#define FS_KEY_DERIVATION_NONCE_SIZE		16

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
struct fscrypt_context {
	u8 format;
	u8 contents_encryption_mode;
	u8 filenames_encryption_mode;
	u8 flags;
	u8 master_key_descriptor[FS_KEY_DESCRIPTOR_SIZE];
	u8 nonce[FS_KEY_DERIVATION_NONCE_SIZE];
} __packed;

#define FS_ENCRYPTION_CONTEXT_FORMAT_V1		1

/* This is passed in from userspace into the kernel keyring */
struct fscrypt_key {
	u32 mode;
	u8 raw[FS_MAX_KEY_SIZE];
	u32 size;
} __packed;

/*
 * A pointer to this structure is stored in the file system's in-core
 * representation of an inode.
 */
struct fscrypt_info {
	u8 ci_data_mode;
	u8 ci_filename_mode;
	u8 ci_flags;
	struct crypto_skcipher *ci_ctfm;
	struct key *ci_keyring_key;
	u8 ci_master_key[FS_KEY_DESCRIPTOR_SIZE];
};

#define FS_CTX_REQUIRES_FREE_ENCRYPT_FL		0x00000001
#define FS_CTX_HAS_BOUNCE_BUFFER_FL		0x00000002

struct fscrypt_completion_result {
	struct completion completion;
	int res;
};

#define DECLARE_FS_COMPLETION_RESULT(ecr) \
	struct fscrypt_completion_result ecr = { \
		COMPLETION_INITIALIZER((ecr).completion), 0 }


/* crypto.c */
int fscrypt_initialize(unsigned int cop_flags);

/* keyinfo.c */
extern int fscrypt_get_crypt_info(struct inode *);

#endif /* _FSCRYPT_PRIVATE_H */
