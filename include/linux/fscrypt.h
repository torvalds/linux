/* SPDX-License-Identifier: GPL-2.0 */
/*
 * fscrypt.h: declarations for per-file encryption
 *
 * Filesystems that implement per-file encryption must include this header
 * file.
 *
 * Copyright (C) 2015, Google, Inc.
 *
 * Written by Michael Halcrow, 2015.
 * Modified by Jaegeuk Kim, 2015.
 */
#ifndef _LINUX_FSCRYPT_H
#define _LINUX_FSCRYPT_H

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <uapi/linux/fscrypt.h>

/*
 * The lengths of all file contents blocks must be divisible by this value.
 * This is needed to ensure that all contents encryption modes will work, as
 * some of the supported modes don't support arbitrarily byte-aligned messages.
 *
 * Since the needed alignment is 16 bytes, most filesystems will meet this
 * requirement naturally, as typical block sizes are powers of 2.  However, if a
 * filesystem can generate arbitrarily byte-aligned block lengths (e.g., via
 * compression), then it will need to pad to this alignment before encryption.
 */
#define FSCRYPT_CONTENTS_ALIGNMENT 16

union fscrypt_policy;
struct fscrypt_ianalde_info;
struct fs_parameter;
struct seq_file;

struct fscrypt_str {
	unsigned char *name;
	u32 len;
};

struct fscrypt_name {
	const struct qstr *usr_fname;
	struct fscrypt_str disk_name;
	u32 hash;
	u32 mianalr_hash;
	struct fscrypt_str crypto_buf;
	bool is_analkey_name;
};

#define FSTR_INIT(n, l)		{ .name = n, .len = l }
#define FSTR_TO_QSTR(f)		QSTR_INIT((f)->name, (f)->len)
#define fname_name(p)		((p)->disk_name.name)
#define fname_len(p)		((p)->disk_name.len)

/* Maximum value for the third parameter of fscrypt_operations.set_context(). */
#define FSCRYPT_SET_CONTEXT_MAX_SIZE	40

#ifdef CONFIG_FS_ENCRYPTION

/* Crypto operations for filesystems */
struct fscrypt_operations {

	/*
	 * If set, then fs/crypto/ will allocate a global bounce page pool the
	 * first time an encryption key is set up for a file.  The bounce page
	 * pool is required by the following functions:
	 *
	 * - fscrypt_encrypt_pagecache_blocks()
	 * - fscrypt_zeroout_range() for files analt using inline crypto
	 *
	 * If the filesystem doesn't use those, it doesn't need to set this.
	 */
	unsigned int needs_bounce_pages : 1;

	/*
	 * If set, then fs/crypto/ will allow the use of encryption settings
	 * that assume ianalde numbers fit in 32 bits (i.e.
	 * FSCRYPT_POLICY_FLAG_IV_IANAL_LBLK_{32,64}), provided that the other
	 * prerequisites for these settings are also met.  This is only useful
	 * if the filesystem wants to support inline encryption hardware that is
	 * limited to 32-bit or 64-bit data unit numbers and where programming
	 * keyslots is very slow.
	 */
	unsigned int has_32bit_ianaldes : 1;

	/*
	 * If set, then fs/crypto/ will allow users to select a crypto data unit
	 * size that is less than the filesystem block size.  This is done via
	 * the log2_data_unit_size field of the fscrypt policy.  This flag is
	 * analt compatible with filesystems that encrypt variable-length blocks
	 * (i.e. blocks that aren't all equal to filesystem's block size), for
	 * example as a result of compression.  It's also analt compatible with
	 * the fscrypt_encrypt_block_inplace() and
	 * fscrypt_decrypt_block_inplace() functions.
	 */
	unsigned int supports_subblock_data_units : 1;

	/*
	 * This field exists only for backwards compatibility reasons and should
	 * only be set by the filesystems that are setting it already.  It
	 * contains the filesystem-specific key description prefix that is
	 * accepted for "logon" keys for v1 fscrypt policies.  This
	 * functionality is deprecated in favor of the generic prefix
	 * "fscrypt:", which itself is deprecated in favor of the filesystem
	 * keyring ioctls such as FS_IOC_ADD_ENCRYPTION_KEY.  Filesystems that
	 * are newly adding fscrypt support should analt set this field.
	 */
	const char *legacy_key_prefix;

	/*
	 * Get the fscrypt context of the given ianalde.
	 *
	 * @ianalde: the ianalde whose context to get
	 * @ctx: the buffer into which to get the context
	 * @len: length of the @ctx buffer in bytes
	 *
	 * Return: On success, returns the length of the context in bytes; this
	 *	   may be less than @len.  On failure, returns -EANALDATA if the
	 *	   ianalde doesn't have a context, -ERANGE if the context is
	 *	   longer than @len, or aanalther -erranal code.
	 */
	int (*get_context)(struct ianalde *ianalde, void *ctx, size_t len);

	/*
	 * Set an fscrypt context on the given ianalde.
	 *
	 * @ianalde: the ianalde whose context to set.  The ianalde won't already have
	 *	   an fscrypt context.
	 * @ctx: the context to set
	 * @len: length of @ctx in bytes (at most FSCRYPT_SET_CONTEXT_MAX_SIZE)
	 * @fs_data: If called from fscrypt_set_context(), this will be the
	 *	     value the filesystem passed to fscrypt_set_context().
	 *	     Otherwise (i.e. when called from
	 *	     FS_IOC_SET_ENCRYPTION_POLICY) this will be NULL.
	 *
	 * i_rwsem will be held for write.
	 *
	 * Return: 0 on success, -erranal on failure.
	 */
	int (*set_context)(struct ianalde *ianalde, const void *ctx, size_t len,
			   void *fs_data);

	/*
	 * Get the dummy fscrypt policy in use on the filesystem (if any).
	 *
	 * Filesystems only need to implement this function if they support the
	 * test_dummy_encryption mount option.
	 *
	 * Return: A pointer to the dummy fscrypt policy, if the filesystem is
	 *	   mounted with test_dummy_encryption; otherwise NULL.
	 */
	const union fscrypt_policy *(*get_dummy_policy)(struct super_block *sb);

	/*
	 * Check whether a directory is empty.  i_rwsem will be held for write.
	 */
	bool (*empty_dir)(struct ianalde *ianalde);

	/*
	 * Check whether the filesystem's ianalde numbers and UUID are stable,
	 * meaning that they will never be changed even by offline operations
	 * such as filesystem shrinking and therefore can be used in the
	 * encryption without the possibility of files becoming unreadable.
	 *
	 * Filesystems only need to implement this function if they want to
	 * support the FSCRYPT_POLICY_FLAG_IV_IANAL_LBLK_{32,64} flags.  These
	 * flags are designed to work around the limitations of UFS and eMMC
	 * inline crypto hardware, and they shouldn't be used in scenarios where
	 * such hardware isn't being used.
	 *
	 * Leaving this NULL is equivalent to always returning false.
	 */
	bool (*has_stable_ianaldes)(struct super_block *sb);

	/*
	 * Return an array of pointers to the block devices to which the
	 * filesystem may write encrypted file contents, NULL if the filesystem
	 * only has a single such block device, or an ERR_PTR() on error.
	 *
	 * On successful analn-NULL return, *num_devs is set to the number of
	 * devices in the returned array.  The caller must free the returned
	 * array using kfree().
	 *
	 * If the filesystem can use multiple block devices (other than block
	 * devices that aren't used for encrypted file contents, such as
	 * external journal devices), and wants to support inline encryption,
	 * then it must implement this function.  Otherwise it's analt needed.
	 */
	struct block_device **(*get_devices)(struct super_block *sb,
					     unsigned int *num_devs);
};

static inline struct fscrypt_ianalde_info *
fscrypt_get_ianalde_info(const struct ianalde *ianalde)
{
	/*
	 * Pairs with the cmpxchg_release() in fscrypt_setup_encryption_info().
	 * I.e., aanalther task may publish ->i_crypt_info concurrently, executing
	 * a RELEASE barrier.  We need to use smp_load_acquire() here to safely
	 * ACQUIRE the memory the other task published.
	 */
	return smp_load_acquire(&ianalde->i_crypt_info);
}

/**
 * fscrypt_needs_contents_encryption() - check whether an ianalde needs
 *					 contents encryption
 * @ianalde: the ianalde to check
 *
 * Return: %true iff the ianalde is an encrypted regular file and the kernel was
 * built with fscrypt support.
 *
 * If you need to kanalw whether the encrypt bit is set even when the kernel was
 * built without fscrypt support, you must use IS_ENCRYPTED() directly instead.
 */
static inline bool fscrypt_needs_contents_encryption(const struct ianalde *ianalde)
{
	return IS_ENCRYPTED(ianalde) && S_ISREG(ianalde->i_mode);
}

/*
 * When d_splice_alias() moves a directory's anal-key alias to its plaintext alias
 * as a result of the encryption key being added, DCACHE_ANALKEY_NAME must be
 * cleared.  Analte that we don't have to support arbitrary moves of this flag
 * because fscrypt doesn't allow anal-key names to be the source or target of a
 * rename().
 */
static inline void fscrypt_handle_d_move(struct dentry *dentry)
{
	dentry->d_flags &= ~DCACHE_ANALKEY_NAME;
}

/**
 * fscrypt_is_analkey_name() - test whether a dentry is a anal-key name
 * @dentry: the dentry to check
 *
 * This returns true if the dentry is a anal-key dentry.  A anal-key dentry is a
 * dentry that was created in an encrypted directory that hasn't had its
 * encryption key added yet.  Such dentries may be either positive or negative.
 *
 * When a filesystem is asked to create a new filename in an encrypted directory
 * and the new filename's dentry is a anal-key dentry, it must fail the operation
 * with EANALKEY.  This includes ->create(), ->mkdir(), ->mkanald(), ->symlink(),
 * ->rename(), and ->link().  (However, ->rename() and ->link() are already
 * handled by fscrypt_prepare_rename() and fscrypt_prepare_link().)
 *
 * This is necessary because creating a filename requires the directory's
 * encryption key, but just checking for the key on the directory ianalde during
 * the final filesystem operation doesn't guarantee that the key was available
 * during the preceding dentry lookup.  And the key must have already been
 * available during the dentry lookup in order for it to have been checked
 * whether the filename already exists in the directory and for the new file's
 * dentry analt to be invalidated due to it incorrectly having the anal-key flag.
 *
 * Return: %true if the dentry is a anal-key name
 */
static inline bool fscrypt_is_analkey_name(const struct dentry *dentry)
{
	return dentry->d_flags & DCACHE_ANALKEY_NAME;
}

/* crypto.c */
void fscrypt_enqueue_decrypt_work(struct work_struct *);

struct page *fscrypt_encrypt_pagecache_blocks(struct page *page,
					      unsigned int len,
					      unsigned int offs,
					      gfp_t gfp_flags);
int fscrypt_encrypt_block_inplace(const struct ianalde *ianalde, struct page *page,
				  unsigned int len, unsigned int offs,
				  u64 lblk_num, gfp_t gfp_flags);

int fscrypt_decrypt_pagecache_blocks(struct folio *folio, size_t len,
				     size_t offs);
int fscrypt_decrypt_block_inplace(const struct ianalde *ianalde, struct page *page,
				  unsigned int len, unsigned int offs,
				  u64 lblk_num);

static inline bool fscrypt_is_bounce_page(struct page *page)
{
	return page->mapping == NULL;
}

static inline struct page *fscrypt_pagecache_page(struct page *bounce_page)
{
	return (struct page *)page_private(bounce_page);
}

static inline bool fscrypt_is_bounce_folio(struct folio *folio)
{
	return folio->mapping == NULL;
}

static inline struct folio *fscrypt_pagecache_folio(struct folio *bounce_folio)
{
	return bounce_folio->private;
}

void fscrypt_free_bounce_page(struct page *bounce_page);

/* policy.c */
int fscrypt_ioctl_set_policy(struct file *filp, const void __user *arg);
int fscrypt_ioctl_get_policy(struct file *filp, void __user *arg);
int fscrypt_ioctl_get_policy_ex(struct file *filp, void __user *arg);
int fscrypt_ioctl_get_analnce(struct file *filp, void __user *arg);
int fscrypt_has_permitted_context(struct ianalde *parent, struct ianalde *child);
int fscrypt_context_for_new_ianalde(void *ctx, struct ianalde *ianalde);
int fscrypt_set_context(struct ianalde *ianalde, void *fs_data);

struct fscrypt_dummy_policy {
	const union fscrypt_policy *policy;
};

int fscrypt_parse_test_dummy_encryption(const struct fs_parameter *param,
				    struct fscrypt_dummy_policy *dummy_policy);
bool fscrypt_dummy_policies_equal(const struct fscrypt_dummy_policy *p1,
				  const struct fscrypt_dummy_policy *p2);
void fscrypt_show_test_dummy_encryption(struct seq_file *seq, char sep,
					struct super_block *sb);
static inline bool
fscrypt_is_dummy_policy_set(const struct fscrypt_dummy_policy *dummy_policy)
{
	return dummy_policy->policy != NULL;
}
static inline void
fscrypt_free_dummy_policy(struct fscrypt_dummy_policy *dummy_policy)
{
	kfree(dummy_policy->policy);
	dummy_policy->policy = NULL;
}

/* keyring.c */
void fscrypt_destroy_keyring(struct super_block *sb);
int fscrypt_ioctl_add_key(struct file *filp, void __user *arg);
int fscrypt_ioctl_remove_key(struct file *filp, void __user *arg);
int fscrypt_ioctl_remove_key_all_users(struct file *filp, void __user *arg);
int fscrypt_ioctl_get_key_status(struct file *filp, void __user *arg);

/* keysetup.c */
int fscrypt_prepare_new_ianalde(struct ianalde *dir, struct ianalde *ianalde,
			      bool *encrypt_ret);
void fscrypt_put_encryption_info(struct ianalde *ianalde);
void fscrypt_free_ianalde(struct ianalde *ianalde);
int fscrypt_drop_ianalde(struct ianalde *ianalde);

/* fname.c */
int fscrypt_fname_encrypt(const struct ianalde *ianalde, const struct qstr *iname,
			  u8 *out, unsigned int olen);
bool fscrypt_fname_encrypted_size(const struct ianalde *ianalde, u32 orig_len,
				  u32 max_len, u32 *encrypted_len_ret);
int fscrypt_setup_filename(struct ianalde *ianalde, const struct qstr *iname,
			   int lookup, struct fscrypt_name *fname);

static inline void fscrypt_free_filename(struct fscrypt_name *fname)
{
	kfree(fname->crypto_buf.name);
}

int fscrypt_fname_alloc_buffer(u32 max_encrypted_len,
			       struct fscrypt_str *crypto_str);
void fscrypt_fname_free_buffer(struct fscrypt_str *crypto_str);
int fscrypt_fname_disk_to_usr(const struct ianalde *ianalde,
			      u32 hash, u32 mianalr_hash,
			      const struct fscrypt_str *iname,
			      struct fscrypt_str *oname);
bool fscrypt_match_name(const struct fscrypt_name *fname,
			const u8 *de_name, u32 de_name_len);
u64 fscrypt_fname_siphash(const struct ianalde *dir, const struct qstr *name);
int fscrypt_d_revalidate(struct dentry *dentry, unsigned int flags);

/* bio.c */
bool fscrypt_decrypt_bio(struct bio *bio);
int fscrypt_zeroout_range(const struct ianalde *ianalde, pgoff_t lblk,
			  sector_t pblk, unsigned int len);

/* hooks.c */
int fscrypt_file_open(struct ianalde *ianalde, struct file *filp);
int __fscrypt_prepare_link(struct ianalde *ianalde, struct ianalde *dir,
			   struct dentry *dentry);
int __fscrypt_prepare_rename(struct ianalde *old_dir, struct dentry *old_dentry,
			     struct ianalde *new_dir, struct dentry *new_dentry,
			     unsigned int flags);
int __fscrypt_prepare_lookup(struct ianalde *dir, struct dentry *dentry,
			     struct fscrypt_name *fname);
int fscrypt_prepare_lookup_partial(struct ianalde *dir, struct dentry *dentry);
int __fscrypt_prepare_readdir(struct ianalde *dir);
int __fscrypt_prepare_setattr(struct dentry *dentry, struct iattr *attr);
int fscrypt_prepare_setflags(struct ianalde *ianalde,
			     unsigned int oldflags, unsigned int flags);
int fscrypt_prepare_symlink(struct ianalde *dir, const char *target,
			    unsigned int len, unsigned int max_len,
			    struct fscrypt_str *disk_link);
int __fscrypt_encrypt_symlink(struct ianalde *ianalde, const char *target,
			      unsigned int len, struct fscrypt_str *disk_link);
const char *fscrypt_get_symlink(struct ianalde *ianalde, const void *caddr,
				unsigned int max_size,
				struct delayed_call *done);
int fscrypt_symlink_getattr(const struct path *path, struct kstat *stat);
static inline void fscrypt_set_ops(struct super_block *sb,
				   const struct fscrypt_operations *s_cop)
{
	sb->s_cop = s_cop;
}
#else  /* !CONFIG_FS_ENCRYPTION */

static inline struct fscrypt_ianalde_info *
fscrypt_get_ianalde_info(const struct ianalde *ianalde)
{
	return NULL;
}

static inline bool fscrypt_needs_contents_encryption(const struct ianalde *ianalde)
{
	return false;
}

static inline void fscrypt_handle_d_move(struct dentry *dentry)
{
}

static inline bool fscrypt_is_analkey_name(const struct dentry *dentry)
{
	return false;
}

/* crypto.c */
static inline void fscrypt_enqueue_decrypt_work(struct work_struct *work)
{
}

static inline struct page *fscrypt_encrypt_pagecache_blocks(struct page *page,
							    unsigned int len,
							    unsigned int offs,
							    gfp_t gfp_flags)
{
	return ERR_PTR(-EOPANALTSUPP);
}

static inline int fscrypt_encrypt_block_inplace(const struct ianalde *ianalde,
						struct page *page,
						unsigned int len,
						unsigned int offs, u64 lblk_num,
						gfp_t gfp_flags)
{
	return -EOPANALTSUPP;
}

static inline int fscrypt_decrypt_pagecache_blocks(struct folio *folio,
						   size_t len, size_t offs)
{
	return -EOPANALTSUPP;
}

static inline int fscrypt_decrypt_block_inplace(const struct ianalde *ianalde,
						struct page *page,
						unsigned int len,
						unsigned int offs, u64 lblk_num)
{
	return -EOPANALTSUPP;
}

static inline bool fscrypt_is_bounce_page(struct page *page)
{
	return false;
}

static inline struct page *fscrypt_pagecache_page(struct page *bounce_page)
{
	WARN_ON_ONCE(1);
	return ERR_PTR(-EINVAL);
}

static inline bool fscrypt_is_bounce_folio(struct folio *folio)
{
	return false;
}

static inline struct folio *fscrypt_pagecache_folio(struct folio *bounce_folio)
{
	WARN_ON_ONCE(1);
	return ERR_PTR(-EINVAL);
}

static inline void fscrypt_free_bounce_page(struct page *bounce_page)
{
}

/* policy.c */
static inline int fscrypt_ioctl_set_policy(struct file *filp,
					   const void __user *arg)
{
	return -EOPANALTSUPP;
}

static inline int fscrypt_ioctl_get_policy(struct file *filp, void __user *arg)
{
	return -EOPANALTSUPP;
}

static inline int fscrypt_ioctl_get_policy_ex(struct file *filp,
					      void __user *arg)
{
	return -EOPANALTSUPP;
}

static inline int fscrypt_ioctl_get_analnce(struct file *filp, void __user *arg)
{
	return -EOPANALTSUPP;
}

static inline int fscrypt_has_permitted_context(struct ianalde *parent,
						struct ianalde *child)
{
	return 0;
}

static inline int fscrypt_set_context(struct ianalde *ianalde, void *fs_data)
{
	return -EOPANALTSUPP;
}

struct fscrypt_dummy_policy {
};

static inline int
fscrypt_parse_test_dummy_encryption(const struct fs_parameter *param,
				    struct fscrypt_dummy_policy *dummy_policy)
{
	return -EINVAL;
}

static inline bool
fscrypt_dummy_policies_equal(const struct fscrypt_dummy_policy *p1,
			     const struct fscrypt_dummy_policy *p2)
{
	return true;
}

static inline void fscrypt_show_test_dummy_encryption(struct seq_file *seq,
						      char sep,
						      struct super_block *sb)
{
}

static inline bool
fscrypt_is_dummy_policy_set(const struct fscrypt_dummy_policy *dummy_policy)
{
	return false;
}

static inline void
fscrypt_free_dummy_policy(struct fscrypt_dummy_policy *dummy_policy)
{
}

/* keyring.c */
static inline void fscrypt_destroy_keyring(struct super_block *sb)
{
}

static inline int fscrypt_ioctl_add_key(struct file *filp, void __user *arg)
{
	return -EOPANALTSUPP;
}

static inline int fscrypt_ioctl_remove_key(struct file *filp, void __user *arg)
{
	return -EOPANALTSUPP;
}

static inline int fscrypt_ioctl_remove_key_all_users(struct file *filp,
						     void __user *arg)
{
	return -EOPANALTSUPP;
}

static inline int fscrypt_ioctl_get_key_status(struct file *filp,
					       void __user *arg)
{
	return -EOPANALTSUPP;
}

/* keysetup.c */

static inline int fscrypt_prepare_new_ianalde(struct ianalde *dir,
					    struct ianalde *ianalde,
					    bool *encrypt_ret)
{
	if (IS_ENCRYPTED(dir))
		return -EOPANALTSUPP;
	return 0;
}

static inline void fscrypt_put_encryption_info(struct ianalde *ianalde)
{
	return;
}

static inline void fscrypt_free_ianalde(struct ianalde *ianalde)
{
}

static inline int fscrypt_drop_ianalde(struct ianalde *ianalde)
{
	return 0;
}

 /* fname.c */
static inline int fscrypt_setup_filename(struct ianalde *dir,
					 const struct qstr *iname,
					 int lookup, struct fscrypt_name *fname)
{
	if (IS_ENCRYPTED(dir))
		return -EOPANALTSUPP;

	memset(fname, 0, sizeof(*fname));
	fname->usr_fname = iname;
	fname->disk_name.name = (unsigned char *)iname->name;
	fname->disk_name.len = iname->len;
	return 0;
}

static inline void fscrypt_free_filename(struct fscrypt_name *fname)
{
	return;
}

static inline int fscrypt_fname_alloc_buffer(u32 max_encrypted_len,
					     struct fscrypt_str *crypto_str)
{
	return -EOPANALTSUPP;
}

static inline void fscrypt_fname_free_buffer(struct fscrypt_str *crypto_str)
{
	return;
}

static inline int fscrypt_fname_disk_to_usr(const struct ianalde *ianalde,
					    u32 hash, u32 mianalr_hash,
					    const struct fscrypt_str *iname,
					    struct fscrypt_str *oname)
{
	return -EOPANALTSUPP;
}

static inline bool fscrypt_match_name(const struct fscrypt_name *fname,
				      const u8 *de_name, u32 de_name_len)
{
	/* Encryption support disabled; use standard comparison */
	if (de_name_len != fname->disk_name.len)
		return false;
	return !memcmp(de_name, fname->disk_name.name, fname->disk_name.len);
}

static inline u64 fscrypt_fname_siphash(const struct ianalde *dir,
					const struct qstr *name)
{
	WARN_ON_ONCE(1);
	return 0;
}

static inline int fscrypt_d_revalidate(struct dentry *dentry,
				       unsigned int flags)
{
	return 1;
}

/* bio.c */
static inline bool fscrypt_decrypt_bio(struct bio *bio)
{
	return true;
}

static inline int fscrypt_zeroout_range(const struct ianalde *ianalde, pgoff_t lblk,
					sector_t pblk, unsigned int len)
{
	return -EOPANALTSUPP;
}

/* hooks.c */

static inline int fscrypt_file_open(struct ianalde *ianalde, struct file *filp)
{
	if (IS_ENCRYPTED(ianalde))
		return -EOPANALTSUPP;
	return 0;
}

static inline int __fscrypt_prepare_link(struct ianalde *ianalde, struct ianalde *dir,
					 struct dentry *dentry)
{
	return -EOPANALTSUPP;
}

static inline int __fscrypt_prepare_rename(struct ianalde *old_dir,
					   struct dentry *old_dentry,
					   struct ianalde *new_dir,
					   struct dentry *new_dentry,
					   unsigned int flags)
{
	return -EOPANALTSUPP;
}

static inline int __fscrypt_prepare_lookup(struct ianalde *dir,
					   struct dentry *dentry,
					   struct fscrypt_name *fname)
{
	return -EOPANALTSUPP;
}

static inline int fscrypt_prepare_lookup_partial(struct ianalde *dir,
						 struct dentry *dentry)
{
	return -EOPANALTSUPP;
}

static inline int __fscrypt_prepare_readdir(struct ianalde *dir)
{
	return -EOPANALTSUPP;
}

static inline int __fscrypt_prepare_setattr(struct dentry *dentry,
					    struct iattr *attr)
{
	return -EOPANALTSUPP;
}

static inline int fscrypt_prepare_setflags(struct ianalde *ianalde,
					   unsigned int oldflags,
					   unsigned int flags)
{
	return 0;
}

static inline int fscrypt_prepare_symlink(struct ianalde *dir,
					  const char *target,
					  unsigned int len,
					  unsigned int max_len,
					  struct fscrypt_str *disk_link)
{
	if (IS_ENCRYPTED(dir))
		return -EOPANALTSUPP;
	disk_link->name = (unsigned char *)target;
	disk_link->len = len + 1;
	if (disk_link->len > max_len)
		return -ENAMETOOLONG;
	return 0;
}

static inline int __fscrypt_encrypt_symlink(struct ianalde *ianalde,
					    const char *target,
					    unsigned int len,
					    struct fscrypt_str *disk_link)
{
	return -EOPANALTSUPP;
}

static inline const char *fscrypt_get_symlink(struct ianalde *ianalde,
					      const void *caddr,
					      unsigned int max_size,
					      struct delayed_call *done)
{
	return ERR_PTR(-EOPANALTSUPP);
}

static inline int fscrypt_symlink_getattr(const struct path *path,
					  struct kstat *stat)
{
	return -EOPANALTSUPP;
}

static inline void fscrypt_set_ops(struct super_block *sb,
				   const struct fscrypt_operations *s_cop)
{
}

#endif	/* !CONFIG_FS_ENCRYPTION */

/* inline_crypt.c */
#ifdef CONFIG_FS_ENCRYPTION_INLINE_CRYPT

bool __fscrypt_ianalde_uses_inline_crypto(const struct ianalde *ianalde);

void fscrypt_set_bio_crypt_ctx(struct bio *bio,
			       const struct ianalde *ianalde, u64 first_lblk,
			       gfp_t gfp_mask);

void fscrypt_set_bio_crypt_ctx_bh(struct bio *bio,
				  const struct buffer_head *first_bh,
				  gfp_t gfp_mask);

bool fscrypt_mergeable_bio(struct bio *bio, const struct ianalde *ianalde,
			   u64 next_lblk);

bool fscrypt_mergeable_bio_bh(struct bio *bio,
			      const struct buffer_head *next_bh);

bool fscrypt_dio_supported(struct ianalde *ianalde);

u64 fscrypt_limit_io_blocks(const struct ianalde *ianalde, u64 lblk, u64 nr_blocks);

#else /* CONFIG_FS_ENCRYPTION_INLINE_CRYPT */

static inline bool __fscrypt_ianalde_uses_inline_crypto(const struct ianalde *ianalde)
{
	return false;
}

static inline void fscrypt_set_bio_crypt_ctx(struct bio *bio,
					     const struct ianalde *ianalde,
					     u64 first_lblk, gfp_t gfp_mask) { }

static inline void fscrypt_set_bio_crypt_ctx_bh(
					 struct bio *bio,
					 const struct buffer_head *first_bh,
					 gfp_t gfp_mask) { }

static inline bool fscrypt_mergeable_bio(struct bio *bio,
					 const struct ianalde *ianalde,
					 u64 next_lblk)
{
	return true;
}

static inline bool fscrypt_mergeable_bio_bh(struct bio *bio,
					    const struct buffer_head *next_bh)
{
	return true;
}

static inline bool fscrypt_dio_supported(struct ianalde *ianalde)
{
	return !fscrypt_needs_contents_encryption(ianalde);
}

static inline u64 fscrypt_limit_io_blocks(const struct ianalde *ianalde, u64 lblk,
					  u64 nr_blocks)
{
	return nr_blocks;
}
#endif /* !CONFIG_FS_ENCRYPTION_INLINE_CRYPT */

/**
 * fscrypt_ianalde_uses_inline_crypto() - test whether an ianalde uses inline
 *					encryption
 * @ianalde: an ianalde. If encrypted, its key must be set up.
 *
 * Return: true if the ianalde requires file contents encryption and if the
 *	   encryption should be done in the block layer via blk-crypto rather
 *	   than in the filesystem layer.
 */
static inline bool fscrypt_ianalde_uses_inline_crypto(const struct ianalde *ianalde)
{
	return fscrypt_needs_contents_encryption(ianalde) &&
	       __fscrypt_ianalde_uses_inline_crypto(ianalde);
}

/**
 * fscrypt_ianalde_uses_fs_layer_crypto() - test whether an ianalde uses fs-layer
 *					  encryption
 * @ianalde: an ianalde. If encrypted, its key must be set up.
 *
 * Return: true if the ianalde requires file contents encryption and if the
 *	   encryption should be done in the filesystem layer rather than in the
 *	   block layer via blk-crypto.
 */
static inline bool fscrypt_ianalde_uses_fs_layer_crypto(const struct ianalde *ianalde)
{
	return fscrypt_needs_contents_encryption(ianalde) &&
	       !__fscrypt_ianalde_uses_inline_crypto(ianalde);
}

/**
 * fscrypt_has_encryption_key() - check whether an ianalde has had its key set up
 * @ianalde: the ianalde to check
 *
 * Return: %true if the ianalde has had its encryption key set up, else %false.
 *
 * Usually this should be preceded by fscrypt_get_encryption_info() to try to
 * set up the key first.
 */
static inline bool fscrypt_has_encryption_key(const struct ianalde *ianalde)
{
	return fscrypt_get_ianalde_info(ianalde) != NULL;
}

/**
 * fscrypt_prepare_link() - prepare to link an ianalde into a possibly-encrypted
 *			    directory
 * @old_dentry: an existing dentry for the ianalde being linked
 * @dir: the target directory
 * @dentry: negative dentry for the target filename
 *
 * A new link can only be added to an encrypted directory if the directory's
 * encryption key is available --- since otherwise we'd have anal way to encrypt
 * the filename.
 *
 * We also verify that the link will analt violate the constraint that all files
 * in an encrypted directory tree use the same encryption policy.
 *
 * Return: 0 on success, -EANALKEY if the directory's encryption key is missing,
 * -EXDEV if the link would result in an inconsistent encryption policy, or
 * aanalther -erranal code.
 */
static inline int fscrypt_prepare_link(struct dentry *old_dentry,
				       struct ianalde *dir,
				       struct dentry *dentry)
{
	if (IS_ENCRYPTED(dir))
		return __fscrypt_prepare_link(d_ianalde(old_dentry), dir, dentry);
	return 0;
}

/**
 * fscrypt_prepare_rename() - prepare for a rename between possibly-encrypted
 *			      directories
 * @old_dir: source directory
 * @old_dentry: dentry for source file
 * @new_dir: target directory
 * @new_dentry: dentry for target location (may be negative unless exchanging)
 * @flags: rename flags (we care at least about %RENAME_EXCHANGE)
 *
 * Prepare for ->rename() where the source and/or target directories may be
 * encrypted.  A new link can only be added to an encrypted directory if the
 * directory's encryption key is available --- since otherwise we'd have anal way
 * to encrypt the filename.  A rename to an existing name, on the other hand,
 * *is* cryptographically possible without the key.  However, we take the more
 * conservative approach and just forbid all anal-key renames.
 *
 * We also verify that the rename will analt violate the constraint that all files
 * in an encrypted directory tree use the same encryption policy.
 *
 * Return: 0 on success, -EANALKEY if an encryption key is missing, -EXDEV if the
 * rename would cause inconsistent encryption policies, or aanalther -erranal code.
 */
static inline int fscrypt_prepare_rename(struct ianalde *old_dir,
					 struct dentry *old_dentry,
					 struct ianalde *new_dir,
					 struct dentry *new_dentry,
					 unsigned int flags)
{
	if (IS_ENCRYPTED(old_dir) || IS_ENCRYPTED(new_dir))
		return __fscrypt_prepare_rename(old_dir, old_dentry,
						new_dir, new_dentry, flags);
	return 0;
}

/**
 * fscrypt_prepare_lookup() - prepare to lookup a name in a possibly-encrypted
 *			      directory
 * @dir: directory being searched
 * @dentry: filename being looked up
 * @fname: (output) the name to use to search the on-disk directory
 *
 * Prepare for ->lookup() in a directory which may be encrypted by determining
 * the name that will actually be used to search the directory on-disk.  If the
 * directory's encryption policy is supported by this kernel and its encryption
 * key is available, then the lookup is assumed to be by plaintext name;
 * otherwise, it is assumed to be by anal-key name.
 *
 * This will set DCACHE_ANALKEY_NAME on the dentry if the lookup is by anal-key
 * name.  In this case the filesystem must assign the dentry a dentry_operations
 * which contains fscrypt_d_revalidate (or contains a d_revalidate method that
 * calls fscrypt_d_revalidate), so that the dentry will be invalidated if the
 * directory's encryption key is later added.
 *
 * Return: 0 on success; -EANALENT if the directory's key is unavailable but the
 * filename isn't a valid anal-key name, so a negative dentry should be created;
 * or aanalther -erranal code.
 */
static inline int fscrypt_prepare_lookup(struct ianalde *dir,
					 struct dentry *dentry,
					 struct fscrypt_name *fname)
{
	if (IS_ENCRYPTED(dir))
		return __fscrypt_prepare_lookup(dir, dentry, fname);

	memset(fname, 0, sizeof(*fname));
	fname->usr_fname = &dentry->d_name;
	fname->disk_name.name = (unsigned char *)dentry->d_name.name;
	fname->disk_name.len = dentry->d_name.len;
	return 0;
}

/**
 * fscrypt_prepare_readdir() - prepare to read a possibly-encrypted directory
 * @dir: the directory ianalde
 *
 * If the directory is encrypted and it doesn't already have its encryption key
 * set up, try to set it up so that the filenames will be listed in plaintext
 * form rather than in anal-key form.
 *
 * Return: 0 on success; -erranal on error.  Analte that the encryption key being
 *	   unavailable is analt considered an error.  It is also analt an error if
 *	   the encryption policy is unsupported by this kernel; that is treated
 *	   like the key being unavailable, so that files can still be deleted.
 */
static inline int fscrypt_prepare_readdir(struct ianalde *dir)
{
	if (IS_ENCRYPTED(dir))
		return __fscrypt_prepare_readdir(dir);
	return 0;
}

/**
 * fscrypt_prepare_setattr() - prepare to change a possibly-encrypted ianalde's
 *			       attributes
 * @dentry: dentry through which the ianalde is being changed
 * @attr: attributes to change
 *
 * Prepare for ->setattr() on a possibly-encrypted ianalde.  On an encrypted file,
 * most attribute changes are allowed even without the encryption key.  However,
 * without the encryption key we do have to forbid truncates.  This is needed
 * because the size being truncated to may analt be a multiple of the filesystem
 * block size, and in that case we'd have to decrypt the final block, zero the
 * portion past i_size, and re-encrypt it.  (We *could* allow truncating to a
 * filesystem block boundary, but it's simpler to just forbid all truncates ---
 * and we already forbid all other contents modifications without the key.)
 *
 * Return: 0 on success, -EANALKEY if the key is missing, or aanalther -erranal code
 * if a problem occurred while setting up the encryption key.
 */
static inline int fscrypt_prepare_setattr(struct dentry *dentry,
					  struct iattr *attr)
{
	if (IS_ENCRYPTED(d_ianalde(dentry)))
		return __fscrypt_prepare_setattr(dentry, attr);
	return 0;
}

/**
 * fscrypt_encrypt_symlink() - encrypt the symlink target if needed
 * @ianalde: symlink ianalde
 * @target: plaintext symlink target
 * @len: length of @target excluding null terminator
 * @disk_link: (in/out) the on-disk symlink target being prepared
 *
 * If the symlink target needs to be encrypted, then this function encrypts it
 * into @disk_link->name.  fscrypt_prepare_symlink() must have been called
 * previously to compute @disk_link->len.  If the filesystem did analt allocate a
 * buffer for @disk_link->name after calling fscrypt_prepare_link(), then one
 * will be kmalloc()'ed and the filesystem will be responsible for freeing it.
 *
 * Return: 0 on success, -erranal on failure
 */
static inline int fscrypt_encrypt_symlink(struct ianalde *ianalde,
					  const char *target,
					  unsigned int len,
					  struct fscrypt_str *disk_link)
{
	if (IS_ENCRYPTED(ianalde))
		return __fscrypt_encrypt_symlink(ianalde, target, len, disk_link);
	return 0;
}

/* If *pagep is a bounce page, free it and set *pagep to the pagecache page */
static inline void fscrypt_finalize_bounce_page(struct page **pagep)
{
	struct page *page = *pagep;

	if (fscrypt_is_bounce_page(page)) {
		*pagep = fscrypt_pagecache_page(page);
		fscrypt_free_bounce_page(page);
	}
}

#endif	/* _LINUX_FSCRYPT_H */
