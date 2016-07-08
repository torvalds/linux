/*
 * General per-file encryption definition
 *
 * Copyright (C) 2015, Google, Inc.
 *
 * Written by Michael Halcrow, 2015.
 * Modified by Jaegeuk Kim, 2015.
 */

#ifndef _LINUX_FSCRYPTO_H
#define _LINUX_FSCRYPTO_H

#include <linux/key.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/bio.h>
#include <linux/dcache.h>
#include <crypto/skcipher.h>
#include <uapi/linux/fs.h>

#define FS_KEY_DERIVATION_NONCE_SIZE		16
#define FS_ENCRYPTION_CONTEXT_FORMAT_V1		1

#define FS_POLICY_FLAGS_PAD_4		0x00
#define FS_POLICY_FLAGS_PAD_8		0x01
#define FS_POLICY_FLAGS_PAD_16		0x02
#define FS_POLICY_FLAGS_PAD_32		0x03
#define FS_POLICY_FLAGS_PAD_MASK	0x03
#define FS_POLICY_FLAGS_VALID		0x03

/* Encryption algorithms */
#define FS_ENCRYPTION_MODE_INVALID		0
#define FS_ENCRYPTION_MODE_AES_256_XTS		1
#define FS_ENCRYPTION_MODE_AES_256_GCM		2
#define FS_ENCRYPTION_MODE_AES_256_CBC		3
#define FS_ENCRYPTION_MODE_AES_256_CTS		4

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

/* This is passed in from userspace into the kernel keyring */
struct fscrypt_key {
	u32 mode;
	u8 raw[FS_MAX_KEY_SIZE];
	u32 size;
} __packed;

struct fscrypt_info {
	u8 ci_data_mode;
	u8 ci_filename_mode;
	u8 ci_flags;
	struct crypto_skcipher *ci_ctfm;
	struct key *ci_keyring_key;
	u8 ci_master_key[FS_KEY_DESCRIPTOR_SIZE];
};

#define FS_CTX_REQUIRES_FREE_ENCRYPT_FL		0x00000001
#define FS_WRITE_PATH_FL			0x00000002

struct fscrypt_ctx {
	union {
		struct {
			struct page *bounce_page;	/* Ciphertext page */
			struct page *control_page;	/* Original page  */
		} w;
		struct {
			struct bio *bio;
			struct work_struct work;
		} r;
		struct list_head free_list;	/* Free list */
	};
	u8 flags;				/* Flags */
	u8 mode;				/* Encryption mode for tfm */
};

struct fscrypt_completion_result {
	struct completion completion;
	int res;
};

#define DECLARE_FS_COMPLETION_RESULT(ecr) \
	struct fscrypt_completion_result ecr = { \
		COMPLETION_INITIALIZER((ecr).completion), 0 }

static inline int fscrypt_key_size(int mode)
{
	switch (mode) {
	case FS_ENCRYPTION_MODE_AES_256_XTS:
		return FS_AES_256_XTS_KEY_SIZE;
	case FS_ENCRYPTION_MODE_AES_256_GCM:
		return FS_AES_256_GCM_KEY_SIZE;
	case FS_ENCRYPTION_MODE_AES_256_CBC:
		return FS_AES_256_CBC_KEY_SIZE;
	case FS_ENCRYPTION_MODE_AES_256_CTS:
		return FS_AES_256_CTS_KEY_SIZE;
	default:
		BUG();
	}
	return 0;
}

#define FS_FNAME_NUM_SCATTER_ENTRIES	4
#define FS_CRYPTO_BLOCK_SIZE		16
#define FS_FNAME_CRYPTO_DIGEST_SIZE	32

/**
 * For encrypted symlinks, the ciphertext length is stored at the beginning
 * of the string in little-endian format.
 */
struct fscrypt_symlink_data {
	__le16 len;
	char encrypted_path[1];
} __packed;

/**
 * This function is used to calculate the disk space required to
 * store a filename of length l in encrypted symlink format.
 */
static inline u32 fscrypt_symlink_data_len(u32 l)
{
	if (l < FS_CRYPTO_BLOCK_SIZE)
		l = FS_CRYPTO_BLOCK_SIZE;
	return (l + sizeof(struct fscrypt_symlink_data) - 1);
}

struct fscrypt_str {
	unsigned char *name;
	u32 len;
};

struct fscrypt_name {
	const struct qstr *usr_fname;
	struct fscrypt_str disk_name;
	u32 hash;
	u32 minor_hash;
	struct fscrypt_str crypto_buf;
};

#define FSTR_INIT(n, l)		{ .name = n, .len = l }
#define FSTR_TO_QSTR(f)		QSTR_INIT((f)->name, (f)->len)
#define fname_name(p)		((p)->disk_name.name)
#define fname_len(p)		((p)->disk_name.len)

/*
 * crypto opertions for filesystems
 */
struct fscrypt_operations {
	int (*get_context)(struct inode *, void *, size_t);
	int (*key_prefix)(struct inode *, u8 **);
	int (*prepare_context)(struct inode *);
	int (*set_context)(struct inode *, const void *, size_t, void *);
	int (*dummy_context)(struct inode *);
	bool (*is_encrypted)(struct inode *);
	bool (*empty_dir)(struct inode *);
	unsigned (*max_namelen)(struct inode *);
};

static inline bool fscrypt_dummy_context_enabled(struct inode *inode)
{
	if (inode->i_sb->s_cop->dummy_context &&
				inode->i_sb->s_cop->dummy_context(inode))
		return true;
	return false;
}

static inline bool fscrypt_valid_contents_enc_mode(u32 mode)
{
	return (mode == FS_ENCRYPTION_MODE_AES_256_XTS);
}

static inline bool fscrypt_valid_filenames_enc_mode(u32 mode)
{
	return (mode == FS_ENCRYPTION_MODE_AES_256_CTS);
}

static inline u32 fscrypt_validate_encryption_key_size(u32 mode, u32 size)
{
	if (size == fscrypt_key_size(mode))
		return size;
	return 0;
}

static inline bool fscrypt_is_dot_dotdot(const struct qstr *str)
{
	if (str->len == 1 && str->name[0] == '.')
		return true;

	if (str->len == 2 && str->name[0] == '.' && str->name[1] == '.')
		return true;

	return false;
}

static inline struct page *fscrypt_control_page(struct page *page)
{
#if IS_ENABLED(CONFIG_FS_ENCRYPTION)
	return ((struct fscrypt_ctx *)page_private(page))->w.control_page;
#else
	WARN_ON_ONCE(1);
	return ERR_PTR(-EINVAL);
#endif
}

static inline int fscrypt_has_encryption_key(struct inode *inode)
{
#if IS_ENABLED(CONFIG_FS_ENCRYPTION)
	return (inode->i_crypt_info != NULL);
#else
	return 0;
#endif
}

static inline void fscrypt_set_encrypted_dentry(struct dentry *dentry)
{
#if IS_ENABLED(CONFIG_FS_ENCRYPTION)
	spin_lock(&dentry->d_lock);
	dentry->d_flags |= DCACHE_ENCRYPTED_WITH_KEY;
	spin_unlock(&dentry->d_lock);
#endif
}

#if IS_ENABLED(CONFIG_FS_ENCRYPTION)
extern const struct dentry_operations fscrypt_d_ops;
#endif

static inline void fscrypt_set_d_op(struct dentry *dentry)
{
#if IS_ENABLED(CONFIG_FS_ENCRYPTION)
	d_set_d_op(dentry, &fscrypt_d_ops);
#endif
}

#if IS_ENABLED(CONFIG_FS_ENCRYPTION)
/* crypto.c */
extern struct kmem_cache *fscrypt_info_cachep;
int fscrypt_initialize(void);

extern struct fscrypt_ctx *fscrypt_get_ctx(struct inode *, gfp_t);
extern void fscrypt_release_ctx(struct fscrypt_ctx *);
extern struct page *fscrypt_encrypt_page(struct inode *, struct page *, gfp_t);
extern int fscrypt_decrypt_page(struct page *);
extern void fscrypt_decrypt_bio_pages(struct fscrypt_ctx *, struct bio *);
extern void fscrypt_pullback_bio_page(struct page **, bool);
extern void fscrypt_restore_control_page(struct page *);
extern int fscrypt_zeroout_range(struct inode *, pgoff_t, sector_t,
						unsigned int);
/* policy.c */
extern int fscrypt_process_policy(struct inode *,
					const struct fscrypt_policy *);
extern int fscrypt_get_policy(struct inode *, struct fscrypt_policy *);
extern int fscrypt_has_permitted_context(struct inode *, struct inode *);
extern int fscrypt_inherit_context(struct inode *, struct inode *,
					void *, bool);
/* keyinfo.c */
extern int get_crypt_info(struct inode *);
extern int fscrypt_get_encryption_info(struct inode *);
extern void fscrypt_put_encryption_info(struct inode *, struct fscrypt_info *);

/* fname.c */
extern int fscrypt_setup_filename(struct inode *, const struct qstr *,
				int lookup, struct fscrypt_name *);
extern void fscrypt_free_filename(struct fscrypt_name *);
extern u32 fscrypt_fname_encrypted_size(struct inode *, u32);
extern int fscrypt_fname_alloc_buffer(struct inode *, u32,
				struct fscrypt_str *);
extern void fscrypt_fname_free_buffer(struct fscrypt_str *);
extern int fscrypt_fname_disk_to_usr(struct inode *, u32, u32,
			const struct fscrypt_str *, struct fscrypt_str *);
extern int fscrypt_fname_usr_to_disk(struct inode *, const struct qstr *,
			struct fscrypt_str *);
#endif

/* crypto.c */
static inline struct fscrypt_ctx *fscrypt_notsupp_get_ctx(struct inode *i,
							gfp_t f)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void fscrypt_notsupp_release_ctx(struct fscrypt_ctx *c)
{
	return;
}

static inline struct page *fscrypt_notsupp_encrypt_page(struct inode *i,
						struct page *p, gfp_t f)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int fscrypt_notsupp_decrypt_page(struct page *p)
{
	return -EOPNOTSUPP;
}

static inline void fscrypt_notsupp_decrypt_bio_pages(struct fscrypt_ctx *c,
						struct bio *b)
{
	return;
}

static inline void fscrypt_notsupp_pullback_bio_page(struct page **p, bool b)
{
	return;
}

static inline void fscrypt_notsupp_restore_control_page(struct page *p)
{
	return;
}

static inline int fscrypt_notsupp_zeroout_range(struct inode *i, pgoff_t p,
					sector_t s, unsigned int f)
{
	return -EOPNOTSUPP;
}

/* policy.c */
static inline int fscrypt_notsupp_process_policy(struct inode *i,
				const struct fscrypt_policy *p)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_notsupp_get_policy(struct inode *i,
				struct fscrypt_policy *p)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_notsupp_has_permitted_context(struct inode *p,
				struct inode *i)
{
	return 0;
}

static inline int fscrypt_notsupp_inherit_context(struct inode *p,
				struct inode *i, void *v, bool b)
{
	return -EOPNOTSUPP;
}

/* keyinfo.c */
static inline int fscrypt_notsupp_get_encryption_info(struct inode *i)
{
	return -EOPNOTSUPP;
}

static inline void fscrypt_notsupp_put_encryption_info(struct inode *i,
					struct fscrypt_info *f)
{
	return;
}

 /* fname.c */
static inline int fscrypt_notsupp_setup_filename(struct inode *dir,
			const struct qstr *iname,
			int lookup, struct fscrypt_name *fname)
{
	if (dir->i_sb->s_cop->is_encrypted(dir))
		return -EOPNOTSUPP;

	memset(fname, 0, sizeof(struct fscrypt_name));
	fname->usr_fname = iname;
	fname->disk_name.name = (unsigned char *)iname->name;
	fname->disk_name.len = iname->len;
	return 0;
}

static inline void fscrypt_notsupp_free_filename(struct fscrypt_name *fname)
{
	return;
}

static inline u32 fscrypt_notsupp_fname_encrypted_size(struct inode *i, u32 s)
{
	/* never happens */
	WARN_ON(1);
	return 0;
}

static inline int fscrypt_notsupp_fname_alloc_buffer(struct inode *inode,
				u32 ilen, struct fscrypt_str *crypto_str)
{
	return -EOPNOTSUPP;
}

static inline void fscrypt_notsupp_fname_free_buffer(struct fscrypt_str *c)
{
	return;
}

static inline int fscrypt_notsupp_fname_disk_to_usr(struct inode *inode,
			u32 hash, u32 minor_hash,
			const struct fscrypt_str *iname,
			struct fscrypt_str *oname)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_notsupp_fname_usr_to_disk(struct inode *inode,
			const struct qstr *iname,
			struct fscrypt_str *oname)
{
	return -EOPNOTSUPP;
}
#endif	/* _LINUX_FSCRYPTO_H */
