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
#include <linux/parser.h>
#include <linux/slab.h>
#include <uapi/linux/fscrypt.h>

#define FS_CRYPTO_BLOCK_SIZE		16

union fscrypt_context;
struct fscrypt_info;
struct seq_file;

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
	bool is_ciphertext_name;
};

#define FSTR_INIT(n, l)		{ .name = n, .len = l }
#define FSTR_TO_QSTR(f)		QSTR_INIT((f)->name, (f)->len)
#define fname_name(p)		((p)->disk_name.name)
#define fname_len(p)		((p)->disk_name.len)

/* Maximum value for the third parameter of fscrypt_operations.set_context(). */
#define FSCRYPT_SET_CONTEXT_MAX_SIZE	40

#ifdef CONFIG_FS_ENCRYPTION
/*
 * fscrypt superblock flags
 */
#define FS_CFLG_OWN_PAGES (1U << 1)

/*
 * crypto operations for filesystems
 */
struct fscrypt_operations {
	unsigned int flags;
	const char *key_prefix;
	int (*get_context)(struct inode *inode, void *ctx, size_t len);
	int (*set_context)(struct inode *inode, const void *ctx, size_t len,
			   void *fs_data);
	const union fscrypt_context *(*get_dummy_context)(
		struct super_block *sb);
	bool (*empty_dir)(struct inode *inode);
	unsigned int max_namelen;
	bool (*has_stable_inodes)(struct super_block *sb);
	void (*get_ino_and_lblk_bits)(struct super_block *sb,
				      int *ino_bits_ret, int *lblk_bits_ret);
	bool (*inline_crypt_enabled)(struct super_block *sb);
	int (*get_num_devices)(struct super_block *sb);
	void (*get_devices)(struct super_block *sb,
			    struct request_queue **devs);
};

static inline bool fscrypt_has_encryption_key(const struct inode *inode)
{
	/* pairs with cmpxchg_release() in fscrypt_get_encryption_info() */
	return READ_ONCE(inode->i_crypt_info) != NULL;
}

/**
 * fscrypt_needs_contents_encryption() - check whether an inode needs
 *					 contents encryption
 * @inode: the inode to check
 *
 * Return: %true iff the inode is an encrypted regular file and the kernel was
 * built with fscrypt support.
 *
 * If you need to know whether the encrypt bit is set even when the kernel was
 * built without fscrypt support, you must use IS_ENCRYPTED() directly instead.
 */
static inline bool fscrypt_needs_contents_encryption(const struct inode *inode)
{
	return IS_ENCRYPTED(inode) && S_ISREG(inode->i_mode);
}

static inline const union fscrypt_context *
fscrypt_get_dummy_context(struct super_block *sb)
{
	if (!sb->s_cop->get_dummy_context)
		return NULL;
	return sb->s_cop->get_dummy_context(sb);
}

/*
 * When d_splice_alias() moves a directory's encrypted alias to its decrypted
 * alias as a result of the encryption key being added, DCACHE_ENCRYPTED_NAME
 * must be cleared.  Note that we don't have to support arbitrary moves of this
 * flag because fscrypt doesn't allow encrypted aliases to be the source or
 * target of a rename().
 */
static inline void fscrypt_handle_d_move(struct dentry *dentry)
{
	dentry->d_flags &= ~DCACHE_ENCRYPTED_NAME;
}

/* crypto.c */
void fscrypt_enqueue_decrypt_work(struct work_struct *);

struct page *fscrypt_encrypt_pagecache_blocks(struct page *page,
					      unsigned int len,
					      unsigned int offs,
					      gfp_t gfp_flags);
int fscrypt_encrypt_block_inplace(const struct inode *inode, struct page *page,
				  unsigned int len, unsigned int offs,
				  u64 lblk_num, gfp_t gfp_flags);

int fscrypt_decrypt_pagecache_blocks(struct page *page, unsigned int len,
				     unsigned int offs);
int fscrypt_decrypt_block_inplace(const struct inode *inode, struct page *page,
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

void fscrypt_free_bounce_page(struct page *bounce_page);
int fscrypt_d_revalidate(struct dentry *dentry, unsigned int flags);

/* policy.c */
int fscrypt_ioctl_set_policy(struct file *filp, const void __user *arg);
int fscrypt_ioctl_get_policy(struct file *filp, void __user *arg);
int fscrypt_ioctl_get_policy_ex(struct file *filp, void __user *arg);
int fscrypt_ioctl_get_nonce(struct file *filp, void __user *arg);
int fscrypt_has_permitted_context(struct inode *parent, struct inode *child);
int fscrypt_inherit_context(struct inode *parent, struct inode *child,
			    void *fs_data, bool preload);

struct fscrypt_dummy_context {
	const union fscrypt_context *ctx;
};

int fscrypt_set_test_dummy_encryption(struct super_block *sb,
				      const substring_t *arg,
				      struct fscrypt_dummy_context *dummy_ctx);
void fscrypt_show_test_dummy_encryption(struct seq_file *seq, char sep,
					struct super_block *sb);
static inline void
fscrypt_free_dummy_context(struct fscrypt_dummy_context *dummy_ctx)
{
	kfree(dummy_ctx->ctx);
	dummy_ctx->ctx = NULL;
}

/* keyring.c */
void fscrypt_sb_free(struct super_block *sb);
int fscrypt_ioctl_add_key(struct file *filp, void __user *arg);
int fscrypt_ioctl_remove_key(struct file *filp, void __user *arg);
int fscrypt_ioctl_remove_key_all_users(struct file *filp, void __user *arg);
int fscrypt_ioctl_get_key_status(struct file *filp, void __user *arg);
int fscrypt_register_key_removal_notifier(struct notifier_block *nb);
int fscrypt_unregister_key_removal_notifier(struct notifier_block *nb);

/* keysetup.c */
int fscrypt_get_encryption_info(struct inode *inode);
void fscrypt_put_encryption_info(struct inode *inode);
void fscrypt_free_inode(struct inode *inode);
int fscrypt_drop_inode(struct inode *inode);

/* fname.c */
int fscrypt_setup_filename(struct inode *inode, const struct qstr *iname,
			   int lookup, struct fscrypt_name *fname);

static inline void fscrypt_free_filename(struct fscrypt_name *fname)
{
	kfree(fname->crypto_buf.name);
}

int fscrypt_fname_alloc_buffer(const struct inode *inode, u32 max_encrypted_len,
			       struct fscrypt_str *crypto_str);
void fscrypt_fname_free_buffer(struct fscrypt_str *crypto_str);
int fscrypt_fname_disk_to_usr(const struct inode *inode,
			      u32 hash, u32 minor_hash,
			      const struct fscrypt_str *iname,
			      struct fscrypt_str *oname);
bool fscrypt_match_name(const struct fscrypt_name *fname,
			const u8 *de_name, u32 de_name_len);
u64 fscrypt_fname_siphash(const struct inode *dir, const struct qstr *name);

/* bio.c */
void fscrypt_decrypt_bio(struct bio *bio);
int fscrypt_zeroout_range(const struct inode *inode, pgoff_t lblk,
			  sector_t pblk, unsigned int len);

/* hooks.c */
int fscrypt_file_open(struct inode *inode, struct file *filp);
int __fscrypt_prepare_link(struct inode *inode, struct inode *dir,
			   struct dentry *dentry);
int __fscrypt_prepare_rename(struct inode *old_dir, struct dentry *old_dentry,
			     struct inode *new_dir, struct dentry *new_dentry,
			     unsigned int flags);
int __fscrypt_prepare_lookup(struct inode *dir, struct dentry *dentry,
			     struct fscrypt_name *fname);
int fscrypt_prepare_setflags(struct inode *inode,
			     unsigned int oldflags, unsigned int flags);
int __fscrypt_prepare_symlink(struct inode *dir, unsigned int len,
			      unsigned int max_len,
			      struct fscrypt_str *disk_link);
int __fscrypt_encrypt_symlink(struct inode *inode, const char *target,
			      unsigned int len, struct fscrypt_str *disk_link);
const char *fscrypt_get_symlink(struct inode *inode, const void *caddr,
				unsigned int max_size,
				struct delayed_call *done);
#else  /* !CONFIG_FS_ENCRYPTION */

static inline bool fscrypt_has_encryption_key(const struct inode *inode)
{
	return false;
}

static inline bool fscrypt_needs_contents_encryption(const struct inode *inode)
{
	return false;
}

static inline const union fscrypt_context *
fscrypt_get_dummy_context(struct super_block *sb)
{
	return NULL;
}

static inline void fscrypt_handle_d_move(struct dentry *dentry)
{
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
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int fscrypt_encrypt_block_inplace(const struct inode *inode,
						struct page *page,
						unsigned int len,
						unsigned int offs, u64 lblk_num,
						gfp_t gfp_flags)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_decrypt_pagecache_blocks(struct page *page,
						   unsigned int len,
						   unsigned int offs)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_decrypt_block_inplace(const struct inode *inode,
						struct page *page,
						unsigned int len,
						unsigned int offs, u64 lblk_num)
{
	return -EOPNOTSUPP;
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

static inline void fscrypt_free_bounce_page(struct page *bounce_page)
{
}

/* policy.c */
static inline int fscrypt_ioctl_set_policy(struct file *filp,
					   const void __user *arg)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_ioctl_get_policy(struct file *filp, void __user *arg)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_ioctl_get_policy_ex(struct file *filp,
					      void __user *arg)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_ioctl_get_nonce(struct file *filp, void __user *arg)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_has_permitted_context(struct inode *parent,
						struct inode *child)
{
	return 0;
}

static inline int fscrypt_inherit_context(struct inode *parent,
					  struct inode *child,
					  void *fs_data, bool preload)
{
	return -EOPNOTSUPP;
}

struct fscrypt_dummy_context {
};

static inline void fscrypt_show_test_dummy_encryption(struct seq_file *seq,
						      char sep,
						      struct super_block *sb)
{
}

static inline void
fscrypt_free_dummy_context(struct fscrypt_dummy_context *dummy_ctx)
{
}

/* keyring.c */
static inline void fscrypt_sb_free(struct super_block *sb)
{
}

static inline int fscrypt_ioctl_add_key(struct file *filp, void __user *arg)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_ioctl_remove_key(struct file *filp, void __user *arg)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_ioctl_remove_key_all_users(struct file *filp,
						     void __user *arg)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_ioctl_get_key_status(struct file *filp,
					       void __user *arg)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_register_key_removal_notifier(
						struct notifier_block *nb)
{
	return 0;
}

static inline int fscrypt_unregister_key_removal_notifier(
						struct notifier_block *nb)
{
	return 0;
}

/* keysetup.c */
static inline int fscrypt_get_encryption_info(struct inode *inode)
{
	return -EOPNOTSUPP;
}

static inline void fscrypt_put_encryption_info(struct inode *inode)
{
	return;
}

static inline void fscrypt_free_inode(struct inode *inode)
{
}

static inline int fscrypt_drop_inode(struct inode *inode)
{
	return 0;
}

 /* fname.c */
static inline int fscrypt_setup_filename(struct inode *dir,
					 const struct qstr *iname,
					 int lookup, struct fscrypt_name *fname)
{
	if (IS_ENCRYPTED(dir))
		return -EOPNOTSUPP;

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

static inline int fscrypt_fname_alloc_buffer(const struct inode *inode,
					     u32 max_encrypted_len,
					     struct fscrypt_str *crypto_str)
{
	return -EOPNOTSUPP;
}

static inline void fscrypt_fname_free_buffer(struct fscrypt_str *crypto_str)
{
	return;
}

static inline int fscrypt_fname_disk_to_usr(const struct inode *inode,
					    u32 hash, u32 minor_hash,
					    const struct fscrypt_str *iname,
					    struct fscrypt_str *oname)
{
	return -EOPNOTSUPP;
}

static inline bool fscrypt_match_name(const struct fscrypt_name *fname,
				      const u8 *de_name, u32 de_name_len)
{
	/* Encryption support disabled; use standard comparison */
	if (de_name_len != fname->disk_name.len)
		return false;
	return !memcmp(de_name, fname->disk_name.name, fname->disk_name.len);
}

static inline u64 fscrypt_fname_siphash(const struct inode *dir,
					const struct qstr *name)
{
	WARN_ON_ONCE(1);
	return 0;
}

/* bio.c */
static inline void fscrypt_decrypt_bio(struct bio *bio)
{
}

static inline int fscrypt_zeroout_range(const struct inode *inode, pgoff_t lblk,
					sector_t pblk, unsigned int len)
{
	return -EOPNOTSUPP;
}

/* hooks.c */

static inline int fscrypt_file_open(struct inode *inode, struct file *filp)
{
	if (IS_ENCRYPTED(inode))
		return -EOPNOTSUPP;
	return 0;
}

static inline int __fscrypt_prepare_link(struct inode *inode, struct inode *dir,
					 struct dentry *dentry)
{
	return -EOPNOTSUPP;
}

static inline int __fscrypt_prepare_rename(struct inode *old_dir,
					   struct dentry *old_dentry,
					   struct inode *new_dir,
					   struct dentry *new_dentry,
					   unsigned int flags)
{
	return -EOPNOTSUPP;
}

static inline int __fscrypt_prepare_lookup(struct inode *dir,
					   struct dentry *dentry,
					   struct fscrypt_name *fname)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_prepare_setflags(struct inode *inode,
					   unsigned int oldflags,
					   unsigned int flags)
{
	return 0;
}

static inline int __fscrypt_prepare_symlink(struct inode *dir,
					    unsigned int len,
					    unsigned int max_len,
					    struct fscrypt_str *disk_link)
{
	return -EOPNOTSUPP;
}


static inline int __fscrypt_encrypt_symlink(struct inode *inode,
					    const char *target,
					    unsigned int len,
					    struct fscrypt_str *disk_link)
{
	return -EOPNOTSUPP;
}

static inline const char *fscrypt_get_symlink(struct inode *inode,
					      const void *caddr,
					      unsigned int max_size,
					      struct delayed_call *done)
{
	return ERR_PTR(-EOPNOTSUPP);
}
#endif	/* !CONFIG_FS_ENCRYPTION */

/* inline_crypt.c */
#ifdef CONFIG_FS_ENCRYPTION_INLINE_CRYPT
extern bool fscrypt_inode_uses_inline_crypto(const struct inode *inode);

extern bool fscrypt_inode_uses_fs_layer_crypto(const struct inode *inode);

extern void fscrypt_set_bio_crypt_ctx(struct bio *bio,
				      const struct inode *inode,
				      u64 first_lblk, gfp_t gfp_mask);

extern void fscrypt_set_bio_crypt_ctx_bh(struct bio *bio,
					 const struct buffer_head *first_bh,
					 gfp_t gfp_mask);

extern bool fscrypt_mergeable_bio(struct bio *bio, const struct inode *inode,
				  u64 next_lblk);

extern bool fscrypt_mergeable_bio_bh(struct bio *bio,
				     const struct buffer_head *next_bh);

bool fscrypt_dio_supported(struct kiocb *iocb, struct iov_iter *iter);

int fscrypt_limit_dio_pages(const struct inode *inode, loff_t pos,
			    int nr_pages);

#else /* CONFIG_FS_ENCRYPTION_INLINE_CRYPT */
static inline bool fscrypt_inode_uses_inline_crypto(const struct inode *inode)
{
	return false;
}

static inline bool fscrypt_inode_uses_fs_layer_crypto(const struct inode *inode)
{
	return IS_ENCRYPTED(inode) && S_ISREG(inode->i_mode);
}

static inline void fscrypt_set_bio_crypt_ctx(struct bio *bio,
					     const struct inode *inode,
					     u64 first_lblk, gfp_t gfp_mask) { }

static inline void fscrypt_set_bio_crypt_ctx_bh(
					 struct bio *bio,
					 const struct buffer_head *first_bh,
					 gfp_t gfp_mask) { }

static inline bool fscrypt_mergeable_bio(struct bio *bio,
					 const struct inode *inode,
					 u64 next_lblk)
{
	return true;
}

static inline bool fscrypt_mergeable_bio_bh(struct bio *bio,
					    const struct buffer_head *next_bh)
{
	return true;
}

static inline bool fscrypt_dio_supported(struct kiocb *iocb,
					 struct iov_iter *iter)
{
	const struct inode *inode = file_inode(iocb->ki_filp);

	return !fscrypt_needs_contents_encryption(inode);
}

static inline int fscrypt_limit_dio_pages(const struct inode *inode, loff_t pos,
					  int nr_pages)
{
	return nr_pages;
}
#endif /* !CONFIG_FS_ENCRYPTION_INLINE_CRYPT */

#if IS_ENABLED(CONFIG_FS_ENCRYPTION) && IS_ENABLED(CONFIG_DM_DEFAULT_KEY)
static inline bool
fscrypt_inode_should_skip_dm_default_key(const struct inode *inode)
{
	return IS_ENCRYPTED(inode) && S_ISREG(inode->i_mode);
}
#else
static inline bool
fscrypt_inode_should_skip_dm_default_key(const struct inode *inode)
{
	return false;
}
#endif

/**
 * fscrypt_require_key() - require an inode's encryption key
 * @inode: the inode we need the key for
 *
 * If the inode is encrypted, set up its encryption key if not already done.
 * Then require that the key be present and return -ENOKEY otherwise.
 *
 * No locks are needed, and the key will live as long as the struct inode --- so
 * it won't go away from under you.
 *
 * Return: 0 on success, -ENOKEY if the key is missing, or another -errno code
 * if a problem occurred while setting up the encryption key.
 */
static inline int fscrypt_require_key(struct inode *inode)
{
	if (IS_ENCRYPTED(inode)) {
		int err = fscrypt_get_encryption_info(inode);

		if (err)
			return err;
		if (!fscrypt_has_encryption_key(inode))
			return -ENOKEY;
	}
	return 0;
}

/**
 * fscrypt_prepare_link() - prepare to link an inode into a possibly-encrypted
 *			    directory
 * @old_dentry: an existing dentry for the inode being linked
 * @dir: the target directory
 * @dentry: negative dentry for the target filename
 *
 * A new link can only be added to an encrypted directory if the directory's
 * encryption key is available --- since otherwise we'd have no way to encrypt
 * the filename.  Therefore, we first set up the directory's encryption key (if
 * not already done) and return an error if it's unavailable.
 *
 * We also verify that the link will not violate the constraint that all files
 * in an encrypted directory tree use the same encryption policy.
 *
 * Return: 0 on success, -ENOKEY if the directory's encryption key is missing,
 * -EXDEV if the link would result in an inconsistent encryption policy, or
 * another -errno code.
 */
static inline int fscrypt_prepare_link(struct dentry *old_dentry,
				       struct inode *dir,
				       struct dentry *dentry)
{
	if (IS_ENCRYPTED(dir))
		return __fscrypt_prepare_link(d_inode(old_dentry), dir, dentry);
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
 * directory's encryption key is available --- since otherwise we'd have no way
 * to encrypt the filename.  A rename to an existing name, on the other hand,
 * *is* cryptographically possible without the key.  However, we take the more
 * conservative approach and just forbid all no-key renames.
 *
 * We also verify that the rename will not violate the constraint that all files
 * in an encrypted directory tree use the same encryption policy.
 *
 * Return: 0 on success, -ENOKEY if an encryption key is missing, -EXDEV if the
 * rename would cause inconsistent encryption policies, or another -errno code.
 */
static inline int fscrypt_prepare_rename(struct inode *old_dir,
					 struct dentry *old_dentry,
					 struct inode *new_dir,
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
 * the name that will actually be used to search the directory on-disk.  Lookups
 * can be done with or without the directory's encryption key; without the key,
 * filenames are presented in encrypted form.  Therefore, we'll try to set up
 * the directory's encryption key, but even without it the lookup can continue.
 *
 * After calling this function, a filesystem should ensure that it's dentry
 * operations contain fscrypt_d_revalidate if DCACHE_ENCRYPTED_NAME was set,
 * so that the dentry can be invalidated if the key is later added.
 *
 * Return: 0 on success; -ENOENT if key is unavailable but the filename isn't a
 * correctly formed encoded ciphertext name, so a negative dentry should be
 * created; or another -errno code.
 */
static inline int fscrypt_prepare_lookup(struct inode *dir,
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
 * fscrypt_prepare_setattr() - prepare to change a possibly-encrypted inode's
 *			       attributes
 * @dentry: dentry through which the inode is being changed
 * @attr: attributes to change
 *
 * Prepare for ->setattr() on a possibly-encrypted inode.  On an encrypted file,
 * most attribute changes are allowed even without the encryption key.  However,
 * without the encryption key we do have to forbid truncates.  This is needed
 * because the size being truncated to may not be a multiple of the filesystem
 * block size, and in that case we'd have to decrypt the final block, zero the
 * portion past i_size, and re-encrypt it.  (We *could* allow truncating to a
 * filesystem block boundary, but it's simpler to just forbid all truncates ---
 * and we already forbid all other contents modifications without the key.)
 *
 * Return: 0 on success, -ENOKEY if the key is missing, or another -errno code
 * if a problem occurred while setting up the encryption key.
 */
static inline int fscrypt_prepare_setattr(struct dentry *dentry,
					  struct iattr *attr)
{
	if (attr->ia_valid & ATTR_SIZE)
		return fscrypt_require_key(d_inode(dentry));
	return 0;
}

/**
 * fscrypt_prepare_symlink() - prepare to create a possibly-encrypted symlink
 * @dir: directory in which the symlink is being created
 * @target: plaintext symlink target
 * @len: length of @target excluding null terminator
 * @max_len: space the filesystem has available to store the symlink target
 * @disk_link: (out) the on-disk symlink target being prepared
 *
 * This function computes the size the symlink target will require on-disk,
 * stores it in @disk_link->len, and validates it against @max_len.  An
 * encrypted symlink may be longer than the original.
 *
 * Additionally, @disk_link->name is set to @target if the symlink will be
 * unencrypted, but left NULL if the symlink will be encrypted.  For encrypted
 * symlinks, the filesystem must call fscrypt_encrypt_symlink() to create the
 * on-disk target later.  (The reason for the two-step process is that some
 * filesystems need to know the size of the symlink target before creating the
 * inode, e.g. to determine whether it will be a "fast" or "slow" symlink.)
 *
 * Return: 0 on success, -ENAMETOOLONG if the symlink target is too long,
 * -ENOKEY if the encryption key is missing, or another -errno code if a problem
 * occurred while setting up the encryption key.
 */
static inline int fscrypt_prepare_symlink(struct inode *dir,
					  const char *target,
					  unsigned int len,
					  unsigned int max_len,
					  struct fscrypt_str *disk_link)
{
	if (IS_ENCRYPTED(dir) || fscrypt_get_dummy_context(dir->i_sb) != NULL)
		return __fscrypt_prepare_symlink(dir, len, max_len, disk_link);

	disk_link->name = (unsigned char *)target;
	disk_link->len = len + 1;
	if (disk_link->len > max_len)
		return -ENAMETOOLONG;
	return 0;
}

/**
 * fscrypt_encrypt_symlink() - encrypt the symlink target if needed
 * @inode: symlink inode
 * @target: plaintext symlink target
 * @len: length of @target excluding null terminator
 * @disk_link: (in/out) the on-disk symlink target being prepared
 *
 * If the symlink target needs to be encrypted, then this function encrypts it
 * into @disk_link->name.  fscrypt_prepare_symlink() must have been called
 * previously to compute @disk_link->len.  If the filesystem did not allocate a
 * buffer for @disk_link->name after calling fscrypt_prepare_link(), then one
 * will be kmalloc()'ed and the filesystem will be responsible for freeing it.
 *
 * Return: 0 on success, -errno on failure
 */
static inline int fscrypt_encrypt_symlink(struct inode *inode,
					  const char *target,
					  unsigned int len,
					  struct fscrypt_str *disk_link)
{
	if (IS_ENCRYPTED(inode))
		return __fscrypt_encrypt_symlink(inode, target, len, disk_link);
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
