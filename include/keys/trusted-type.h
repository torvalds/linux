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

extern struct key_type key_type_trusted;

#endif /* _KEYS_TRUSTED_TYPE_H */
