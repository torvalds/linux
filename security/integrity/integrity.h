/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2009-2010 IBM Corporation
 *
 * Authors:
 * Mimi Zohar <zohar@us.ibm.com>
 */

#include <linux/types.h>
#include <linux/integrity.h>
#include <crypto/sha.h>
#include <linux/key.h>
#include <linux/audit.h>

struct key_acl;

/* iint action cache flags */
#define IMA_MEASURE		0x00000001
#define IMA_MEASURED		0x00000002
#define IMA_APPRAISE		0x00000004
#define IMA_APPRAISED		0x00000008
/*#define IMA_COLLECT		0x00000010  do not use this flag */
#define IMA_COLLECTED		0x00000020
#define IMA_AUDIT		0x00000040
#define IMA_AUDITED		0x00000080
#define IMA_HASH		0x00000100
#define IMA_HASHED		0x00000200

/* iint cache flags */
#define IMA_ACTION_FLAGS	0xff000000
#define IMA_DIGSIG_REQUIRED	0x01000000
#define IMA_PERMIT_DIRECTIO	0x02000000
#define IMA_NEW_FILE		0x04000000
#define EVM_IMMUTABLE_DIGSIG	0x08000000
#define IMA_FAIL_UNVERIFIABLE_SIGS	0x10000000

#define IMA_DO_MASK		(IMA_MEASURE | IMA_APPRAISE | IMA_AUDIT | \
				 IMA_HASH | IMA_APPRAISE_SUBMASK)
#define IMA_DONE_MASK		(IMA_MEASURED | IMA_APPRAISED | IMA_AUDITED | \
				 IMA_HASHED | IMA_COLLECTED | \
				 IMA_APPRAISED_SUBMASK)

/* iint subaction appraise cache flags */
#define IMA_FILE_APPRAISE	0x00001000
#define IMA_FILE_APPRAISED	0x00002000
#define IMA_MMAP_APPRAISE	0x00004000
#define IMA_MMAP_APPRAISED	0x00008000
#define IMA_BPRM_APPRAISE	0x00010000
#define IMA_BPRM_APPRAISED	0x00020000
#define IMA_READ_APPRAISE	0x00040000
#define IMA_READ_APPRAISED	0x00080000
#define IMA_CREDS_APPRAISE	0x00100000
#define IMA_CREDS_APPRAISED	0x00200000
#define IMA_APPRAISE_SUBMASK	(IMA_FILE_APPRAISE | IMA_MMAP_APPRAISE | \
				 IMA_BPRM_APPRAISE | IMA_READ_APPRAISE | \
				 IMA_CREDS_APPRAISE)
#define IMA_APPRAISED_SUBMASK	(IMA_FILE_APPRAISED | IMA_MMAP_APPRAISED | \
				 IMA_BPRM_APPRAISED | IMA_READ_APPRAISED | \
				 IMA_CREDS_APPRAISED)

/* iint cache atomic_flags */
#define IMA_CHANGE_XATTR	0
#define IMA_UPDATE_XATTR	1
#define IMA_CHANGE_ATTR		2
#define IMA_DIGSIG		3
#define IMA_MUST_MEASURE	4

enum evm_ima_xattr_type {
	IMA_XATTR_DIGEST = 0x01,
	EVM_XATTR_HMAC,
	EVM_IMA_XATTR_DIGSIG,
	IMA_XATTR_DIGEST_NG,
	EVM_XATTR_PORTABLE_DIGSIG,
	IMA_XATTR_LAST
};

struct evm_ima_xattr_data {
	u8 type;
	u8 data[];
} __packed;

/* Only used in the EVM HMAC code. */
struct evm_xattr {
	struct evm_ima_xattr_data data;
	u8 digest[SHA1_DIGEST_SIZE];
} __packed;

#define IMA_MAX_DIGEST_SIZE	64

struct ima_digest_data {
	u8 algo;
	u8 length;
	union {
		struct {
			u8 unused;
			u8 type;
		} sha1;
		struct {
			u8 type;
			u8 algo;
		} ng;
		u8 data[2];
	} xattr;
	u8 digest[0];
} __packed;

/*
 * signature format v2 - for using with asymmetric keys
 */
struct signature_v2_hdr {
	uint8_t type;		/* xattr type */
	uint8_t version;	/* signature format version */
	uint8_t	hash_algo;	/* Digest algorithm [enum hash_algo] */
	__be32 keyid;		/* IMA key identifier - not X509/PGP specific */
	__be16 sig_size;	/* signature size */
	uint8_t sig[0];		/* signature payload */
} __packed;

/* integrity data associated with an inode */
struct integrity_iint_cache {
	struct rb_node rb_node;	/* rooted in integrity_iint_tree */
	struct mutex mutex;	/* protects: version, flags, digest */
	struct inode *inode;	/* back pointer to inode in question */
	u64 version;		/* track inode changes */
	unsigned long flags;
	unsigned long measured_pcrs;
	unsigned long atomic_flags;
	enum integrity_status ima_file_status:4;
	enum integrity_status ima_mmap_status:4;
	enum integrity_status ima_bprm_status:4;
	enum integrity_status ima_read_status:4;
	enum integrity_status ima_creds_status:4;
	enum integrity_status evm_status:4;
	struct ima_digest_data *ima_hash;
};

/* rbtree tree calls to lookup, insert, delete
 * integrity data associated with an inode.
 */
struct integrity_iint_cache *integrity_iint_find(struct inode *inode);

int integrity_kernel_read(struct file *file, loff_t offset,
			  void *addr, unsigned long count);

#define INTEGRITY_KEYRING_EVM		0
#define INTEGRITY_KEYRING_IMA		1
#define INTEGRITY_KEYRING_PLATFORM	2
#define INTEGRITY_KEYRING_MAX		3

extern struct dentry *integrity_dir;

#ifdef CONFIG_INTEGRITY_SIGNATURE

int integrity_digsig_verify(const unsigned int id, const char *sig, int siglen,
			    const char *digest, int digestlen);

int __init integrity_init_keyring(const unsigned int id);
int __init integrity_load_x509(const unsigned int id, const char *path);
int __init integrity_load_cert(const unsigned int id, const char *source,
			       const void *data, size_t len, struct key_acl *acl);
#else

static inline int integrity_digsig_verify(const unsigned int id,
					  const char *sig, int siglen,
					  const char *digest, int digestlen)
{
	return -EOPNOTSUPP;
}

static inline int integrity_init_keyring(const unsigned int id)
{
	return 0;
}

static inline int __init integrity_load_cert(const unsigned int id,
					     const char *source,
					     const void *data, size_t len,
					     struct key_acl *acl)
{
	return 0;
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

#ifdef CONFIG_IMA_LOAD_X509
void __init ima_load_x509(void);
#else
static inline void ima_load_x509(void)
{
}
#endif

#ifdef CONFIG_EVM_LOAD_X509
void __init evm_load_x509(void);
#else
static inline void evm_load_x509(void)
{
}
#endif

#ifdef CONFIG_INTEGRITY_AUDIT
/* declarations */
void integrity_audit_msg(int audit_msgno, struct inode *inode,
			 const unsigned char *fname, const char *op,
			 const char *cause, int result, int info);

static inline struct audit_buffer *
integrity_audit_log_start(struct audit_context *ctx, gfp_t gfp_mask, int type)
{
	return audit_log_start(ctx, gfp_mask, type);
}

#else
static inline void integrity_audit_msg(int audit_msgno, struct inode *inode,
				       const unsigned char *fname,
				       const char *op, const char *cause,
				       int result, int info)
{
}

static inline struct audit_buffer *
integrity_audit_log_start(struct audit_context *ctx, gfp_t gfp_mask, int type)
{
	return NULL;
}

#endif

#ifdef CONFIG_INTEGRITY_PLATFORM_KEYRING
void __init add_to_platform_keyring(const char *source, const void *data,
				    size_t len);
#else
static inline void __init add_to_platform_keyring(const char *source,
						  const void *data, size_t len)
{
}
#endif
