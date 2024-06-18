/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2009-2010 IBM Corporation
 *
 * Authors:
 * Mimi Zohar <zohar@us.ibm.com>
 */

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/integrity.h>
#include <crypto/sha1.h>
#include <crypto/hash.h>
#include <linux/key.h>
#include <linux/audit.h>
#include <linux/lsm_hooks.h>

enum evm_ima_xattr_type {
	IMA_XATTR_DIGEST = 0x01,
	EVM_XATTR_HMAC,
	EVM_IMA_XATTR_DIGSIG,
	IMA_XATTR_DIGEST_NG,
	EVM_XATTR_PORTABLE_DIGSIG,
	IMA_VERITY_DIGSIG,
	IMA_XATTR_LAST
};

struct evm_ima_xattr_data {
	/* New members must be added within the __struct_group() macro below. */
	__struct_group(evm_ima_xattr_data_hdr, hdr, __packed,
		u8 type;
	);
	u8 data[];
} __packed;

/* Only used in the EVM HMAC code. */
struct evm_xattr {
	struct evm_ima_xattr_data_hdr data;
	u8 digest[SHA1_DIGEST_SIZE];
} __packed;

#define IMA_MAX_DIGEST_SIZE	HASH_MAX_DIGESTSIZE

struct ima_digest_data {
	/* New members must be added within the __struct_group() macro below. */
	__struct_group(ima_digest_data_hdr, hdr, __packed,
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
	);
	u8 digest[];
} __packed;

/*
 * Instead of wrapping the ima_digest_data struct inside a local structure
 * with the maximum hash size, define ima_max_digest_data struct.
 */
struct ima_max_digest_data {
	struct ima_digest_data_hdr hdr;
	u8 digest[HASH_MAX_DIGESTSIZE];
} __packed;

/*
 * signature header format v2 - for using with asymmetric keys
 *
 * The signature_v2_hdr struct includes a signature format version
 * to simplify defining new signature formats.
 *
 * signature format:
 * version 2: regular file data hash based signature
 * version 3: struct ima_file_id data based signature
 */
struct signature_v2_hdr {
	uint8_t type;		/* xattr type */
	uint8_t version;	/* signature format version */
	uint8_t	hash_algo;	/* Digest algorithm [enum hash_algo] */
	__be32 keyid;		/* IMA key identifier - not X509/PGP specific */
	__be16 sig_size;	/* signature size */
	uint8_t sig[];		/* signature payload */
} __packed;

/*
 * IMA signature version 3 disambiguates the data that is signed, by
 * indirectly signing the hash of the ima_file_id structure data,
 * containing either the fsverity_descriptor struct digest or, in the
 * future, the regular IMA file hash.
 *
 * (The hash of the ima_file_id structure is only of the portion used.)
 */
struct ima_file_id {
	__u8 hash_type;		/* xattr type [enum evm_ima_xattr_type] */
	__u8 hash_algorithm;	/* Digest algorithm [enum hash_algo] */
	__u8 hash[HASH_MAX_DIGESTSIZE];
} __packed;

int integrity_kernel_read(struct file *file, loff_t offset,
			  void *addr, unsigned long count);

#define INTEGRITY_KEYRING_EVM		0
#define INTEGRITY_KEYRING_IMA		1
#define INTEGRITY_KEYRING_PLATFORM	2
#define INTEGRITY_KEYRING_MACHINE	3
#define INTEGRITY_KEYRING_MAX		4

extern struct dentry *integrity_dir;

struct modsig;

#ifdef CONFIG_INTEGRITY_SIGNATURE

int integrity_digsig_verify(const unsigned int id, const char *sig, int siglen,
			    const char *digest, int digestlen);
int integrity_modsig_verify(unsigned int id, const struct modsig *modsig);

int __init integrity_init_keyring(const unsigned int id);
int __init integrity_load_x509(const unsigned int id, const char *path);
int __init integrity_load_cert(const unsigned int id, const char *source,
			       const void *data, size_t len, key_perm_t perm);
#else

static inline int integrity_digsig_verify(const unsigned int id,
					  const char *sig, int siglen,
					  const char *digest, int digestlen)
{
	return -EOPNOTSUPP;
}

static inline int integrity_modsig_verify(unsigned int id,
					  const struct modsig *modsig)
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
					     key_perm_t perm)
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

#ifdef CONFIG_IMA_APPRAISE_MODSIG
int ima_modsig_verify(struct key *keyring, const struct modsig *modsig);
#else
static inline int ima_modsig_verify(struct key *keyring,
				    const struct modsig *modsig)
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

void integrity_audit_message(int audit_msgno, struct inode *inode,
			     const unsigned char *fname, const char *op,
			     const char *cause, int result, int info,
			     int errno);

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

static inline void integrity_audit_message(int audit_msgno,
					   struct inode *inode,
					   const unsigned char *fname,
					   const char *op, const char *cause,
					   int result, int info, int errno)
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

#ifdef CONFIG_INTEGRITY_MACHINE_KEYRING
void __init add_to_machine_keyring(const char *source, const void *data, size_t len);
bool __init imputed_trust_enabled(void);
#else
static inline void __init add_to_machine_keyring(const char *source,
						  const void *data, size_t len)
{
}

static inline bool __init imputed_trust_enabled(void)
{
	return false;
}
#endif
