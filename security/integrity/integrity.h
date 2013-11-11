/*
 * Copyright (C) 2009-2010 IBM Corporation
 *
 * Authors:
 * Mimi Zohar <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 */

#include <linux/types.h>
#include <linux/integrity.h>
#include <crypto/sha.h>
#include <linux/key.h>

/* iint action cache flags */
#define IMA_MEASURE		0x00000001
#define IMA_MEASURED		0x00000002
#define IMA_APPRAISE		0x00000004
#define IMA_APPRAISED		0x00000008
/*#define IMA_COLLECT		0x00000010  do not use this flag */
#define IMA_COLLECTED		0x00000020
#define IMA_AUDIT		0x00000040
#define IMA_AUDITED		0x00000080

/* iint cache flags */
#define IMA_ACTION_FLAGS	0xff000000
#define IMA_DIGSIG		0x01000000
#define IMA_DIGSIG_REQUIRED	0x02000000

#define IMA_DO_MASK		(IMA_MEASURE | IMA_APPRAISE | IMA_AUDIT | \
				 IMA_APPRAISE_SUBMASK)
#define IMA_DONE_MASK		(IMA_MEASURED | IMA_APPRAISED | IMA_AUDITED | \
				 IMA_COLLECTED | IMA_APPRAISED_SUBMASK)

/* iint subaction appraise cache flags */
#define IMA_FILE_APPRAISE	0x00000100
#define IMA_FILE_APPRAISED	0x00000200
#define IMA_MMAP_APPRAISE	0x00000400
#define IMA_MMAP_APPRAISED	0x00000800
#define IMA_BPRM_APPRAISE	0x00001000
#define IMA_BPRM_APPRAISED	0x00002000
#define IMA_MODULE_APPRAISE	0x00004000
#define IMA_MODULE_APPRAISED	0x00008000
#define IMA_APPRAISE_SUBMASK	(IMA_FILE_APPRAISE | IMA_MMAP_APPRAISE | \
				 IMA_BPRM_APPRAISE | IMA_MODULE_APPRAISE)
#define IMA_APPRAISED_SUBMASK	(IMA_FILE_APPRAISED | IMA_MMAP_APPRAISED | \
				 IMA_BPRM_APPRAISED | IMA_MODULE_APPRAISED)

enum evm_ima_xattr_type {
	IMA_XATTR_DIGEST = 0x01,
	EVM_XATTR_HMAC,
	EVM_IMA_XATTR_DIGSIG,
};

struct evm_ima_xattr_data {
	u8 type;
	u8 digest[SHA1_DIGEST_SIZE];
}  __attribute__((packed));

/* integrity data associated with an inode */
struct integrity_iint_cache {
	struct rb_node rb_node; /* rooted in integrity_iint_tree */
	struct inode *inode;	/* back pointer to inode in question */
	u64 version;		/* track inode changes */
	unsigned long flags;
	struct evm_ima_xattr_data ima_xattr;
	enum integrity_status ima_file_status:4;
	enum integrity_status ima_mmap_status:4;
	enum integrity_status ima_bprm_status:4;
	enum integrity_status ima_module_status:4;
	enum integrity_status evm_status:4;
};

/* rbtree tree calls to lookup, insert, delete
 * integrity data associated with an inode.
 */
struct integrity_iint_cache *integrity_iint_insert(struct inode *inode);
struct integrity_iint_cache *integrity_iint_find(struct inode *inode);

#define INTEGRITY_KEYRING_EVM		0
#define INTEGRITY_KEYRING_MODULE	1
#define INTEGRITY_KEYRING_IMA		2
#define INTEGRITY_KEYRING_MAX		3

#ifdef CONFIG_INTEGRITY_SIGNATURE

int integrity_digsig_verify(const unsigned int id, const char *sig, int siglen,
					const char *digest, int digestlen);

#else

static inline int integrity_digsig_verify(const unsigned int id,
					  const char *sig, int siglen,
					  const char *digest, int digestlen)
{
	return -EOPNOTSUPP;
}

#endif /* CONFIG_INTEGRITY_SIGNATURE */

#ifdef CONFIG_INTEGRITY_ASYMMETRIC_KEYS
int asymmetric_verify(struct key *keyring, const char *sig,
		      int siglen, const char *data, int datalen);
#else
static inline int asymmetric_verify(struct key *keyring, const char *sig,
				    int siglen, const char *data, int datalen)
{
	return -EOPNOTSUPP;
}
#endif

/* set during initialization */
extern int iint_initialized;
