/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2010 IBM Corporation
 * Author: David Safford <safford@us.ibm.com>
 */

#ifndef _KEYS_TRUSTED_TYPE_H
#define _KEYS_TRUSTED_TYPE_H

#include <linux/key.h>
#include <linux/rcupdate.h>
#include <linux/tpm.h>

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) "trusted_key: " fmt

#define MIN_KEY_SIZE			32
#define MAX_KEY_SIZE			128
#define MAX_BLOB_SIZE			512
#define MAX_PCRINFO_SIZE		64
#define MAX_DIGEST_SIZE			64

struct trusted_key_payload {
	struct rcu_head rcu;
	unsigned int key_len;
	unsigned int blob_len;
	unsigned char migratable;
	unsigned char old_format;
	unsigned char key[MAX_KEY_SIZE + 1];
	unsigned char blob[MAX_BLOB_SIZE];
};

struct trusted_key_options {
	uint16_t keytype;
	uint32_t keyhandle;
	unsigned char keyauth[TPM_DIGEST_SIZE];
	uint32_t blobauth_len;
	unsigned char blobauth[TPM_DIGEST_SIZE];
	uint32_t pcrinfo_len;
	unsigned char pcrinfo[MAX_PCRINFO_SIZE];
	int pcrlock;
	uint32_t hash;
	uint32_t policydigest_len;
	unsigned char policydigest[MAX_DIGEST_SIZE];
	uint32_t policyhandle;
};

struct trusted_key_ops {
	/*
	 * flag to indicate if trusted key implementation supports migration
	 * or not.
	 */
	unsigned char migratable;

	/* Initialize key interface. */
	int (*init)(void);

	/* Seal a key. */
	int (*seal)(struct trusted_key_payload *p, char *datablob);

	/* Unseal a key. */
	int (*unseal)(struct trusted_key_payload *p, char *datablob);

	/* Optional: Get a randomized key. */
	int (*get_random)(unsigned char *key, size_t key_len);

	/* Exit key interface. */
	void (*exit)(void);
};

struct trusted_key_source {
	char *name;
	struct trusted_key_ops *ops;
};

extern struct key_type key_type_trusted;

#define TRUSTED_DEBUG 0

#if TRUSTED_DEBUG
static inline void dump_payload(struct trusted_key_payload *p)
{
	pr_info("key_len %d\n", p->key_len);
	print_hex_dump(KERN_INFO, "key ", DUMP_PREFIX_NONE,
		       16, 1, p->key, p->key_len, 0);
	pr_info("bloblen %d\n", p->blob_len);
	print_hex_dump(KERN_INFO, "blob ", DUMP_PREFIX_NONE,
		       16, 1, p->blob, p->blob_len, 0);
	pr_info("migratable %d\n", p->migratable);
}
#else
static inline void dump_payload(struct trusted_key_payload *p)
{
}
#endif

#endif /* _KEYS_TRUSTED_TYPE_H */
