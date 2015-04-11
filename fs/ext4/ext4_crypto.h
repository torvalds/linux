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
	char master_key_descriptor[EXT4_KEY_DESCRIPTOR_SIZE];
} __attribute__((__packed__));

#define EXT4_ENCRYPTION_CONTEXT_FORMAT_V1 1
#define EXT4_KEY_DERIVATION_NONCE_SIZE 16

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
	char reserved;
	char master_key_descriptor[EXT4_KEY_DESCRIPTOR_SIZE];
	char nonce[EXT4_KEY_DERIVATION_NONCE_SIZE];
} __attribute__((__packed__));

#endif	/* _EXT4_CRYPTO_H */
