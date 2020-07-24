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

#define FS_CRYPTO_BLOCK_SIZE		16

union fscrypt_policy;
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
	bool is_nokey_name;
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
	const union fscrypt_policy *(*get_dummy_policy)(struct super_block *sb);
	bool (*empty_dir)(struct inode *inode);
	unsigned int max_namelen;
	bool (*has_stable_inodes)(struct super_block *sb);
	void (*get_ino_and_lblk_bits)(struct super_block *sb,
				      int *ino_bits_ret, int *lblk_bits_ret);
	int (*get_num_devices)(struct super_block *sb);
	void (*get_devices)(struct super_block *sb,
			    struct request_queue **devs);
};

static inline struct fscrypt_info *fscrypt_get_info(const struct inode *inode)
{
	/*
	 * Pairs with the cmpxchg_release() in fscrypt_setup_encryption_info().
	 * I.e., another task may publish ->i_crypt_info concurrently, executing
	 * a RELEASE barrier.  We need to use smp_load_acquire() here to safely
	 * ACQUIRE the memory the other task published.
	 */
	return smp_load_acquire(&inode->i_crypt_info);
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

/*
 * When d_splice_alias() moves a directory's no-key alias to its plaintext alias
 * as a result of the encryption key being added, DCACHE_NOKEY_NAME must be
 * cleared.  Note that we don't have to support arbitrary moves of this flag
 * because fscrypt doesn't allow no-key names to be the source or target of a
 * rename().
 */
static inline void fscrypt_handle_d_move(struct dentry *dentry)
{
	dentry->d_flags &= ~DCACHE_NOKEY_NAME;
}

/**
 * fscrypt_is_nokey_name() - test whether a dentry is a no-key name
 * @dentry: the dentry to check
 *
 * This returns true if the dentry is a no-key dentry.  A no-key dentry is a
 * dentry that was created in an encrypted directory that hasn't had its
 * encryption key added yet.  Such dentries may be either positive or negative.
 *
 * When a filesystem is asked to create a new filename in an encrypted directory
 * and the new filename's dentry is a no-key dentry, it must fail the operation
 * with ENOKEY.  This includes ->create(), ->mkdir(), ->mknod(), ->symlink(),
 * ->rename(), and ->link().  (However, ->rename() and ->link() are already
 * handled by fscrypt_prepare_rename() and fscrypt_prepare_link().)
 *
 * This is necessary because creating a filename requires the directory's
 * encryption key, but just checking for the key on the directory inode during
 * the final filesystem operation doesn't guarantee that the key was available
 * during the preceding dentry lookup.  And the key must have already been
 * available during the dentry lookup in order for it to have been checked
 * whether the filename already exists in the directory and for the new file's
 * dentry not to be invalidated due to it incorrectly having the no-key flag.
 *
 * Return: %true if the dentry is a no-key name
 */
static inline bool fscrypt_is_nokey_name(const struct dentry *dentry)
{
	return dentry->d_flags & DCACHE_NOKEY_NAME;
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

/* policy.c */
int fscrypt_ioctl_set_policy(struct file *filp, const void __user *arg);
int fscrypt_ioctl_get_policy(struct file *filp, void __user *arg);
int fscrypt_ioctl_get_policy_ex(struct file *filp, void __user *arg);
int fscrypt_ioctl_get_nonce(struct file *filp, void __user *arg);
int fscrypt_has_permitted_context(struct inode *parent, struct inode *child);
int fscrypt_set_context(struct inode *inode, void *fs_data);

struct fscrypt_dummy_policy {
	const union fscrypt_policy *policy;
};

int fscrypt_set_test_dummy_encryption(struct super_block *sb, const char *arg,
				struct fscrypt_dummy_policy *dummy_policy);
void fscrypt_show_test_dummy_encryption(struct seq_file *seq, char sep,
					struct super_block *sb);
static inline void
fscrypt_free_dummy_policy(struct fscrypt_dummy_policy *dummy_policy)
{
	kfree(dummy_policy->policy);
	dummy_policy->policy = NULL;
}

/* keyring.c */
void fscrypt_sb_free(struct super_block *sb);
int fscrypt_ioctl_add_key(struct file *filp, void __user *arg);
int fscrypt_ioctl_remove_key(struct file *filp, void __user *arg);
int fscrypt_ioctl_remove_key_all_users(struct file *filp, void __user *arg);
int fscrypt_ioctl_get_key_status(struct file *filp, void __user *arg);

/* keysetup.c */
int fscrypt_prepare_new_inode(struct inode *dir, struct inode *inode,
			      bool *encrypt_ret);
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

int fscrypt_fname_alloc_buffer(u32 max_encrypted_len,
			       struct fscrypt_str *crypto_str);
void fscrypt_fname_free_buffer(struct fscrypt_str *crypto_str);
int fscrypt_fname_disk_to_usr(const struct inode *inode,
			      u32 hash, u32 minor_hash,
			      const struct fscrypt_str *iname,
			      struct fscrypt_str *oname);
bool fscrypt_match_name(const struct fscrypt_name *fname,
			const u8 *de_name, u32 de_name_len);
u64 fscrypt_fname_siphash(const struct inode *dir, const struct qstr *name);
int fscrypt_d_revalidate(struct dentry *dentry, unsigned int flags);

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
int __fscrypt_prepare_readdir(struct inode *dir);
int __fscrypt_prepare_setattr(struct dentry *dentry, struct iattr *attr);
int fscrypt_prepare_setflags(struct inode *inode,
			     unsigned int oldflags, unsigned int flags);
int fscrypt_prepare_symlink(struct inode *dir, const char *target,
			    unsigned int len, unsigned int max_len,
			    struct fscrypt_str *disk_link);
int __fscrypt_encrypt_symlink(struct inode *inode, const char *target,
			      unsigned int len, struct fscrypt_str *disk_link);
const char *fscrypt_get_symlink(struct inode *inode, const void *caddr,
				unsigned int max_size,
				struct delayed_call *done);
static inline void fscrypt_set_ops(struct super_block *sb,
				   const struct fscrypt_operations *s_cop)
{
	sb->s_cop = s_cop;
}
#else  /* !CONFIG_FS_ENCRYPTION */

static inline struct fscrypt_info *fscrypt_get_info(const struct inode *inode)
{
	return NULL;
}

static inline bool fscrypt_needs_contents_encryption(const struct inode *inode)
{
	return false;
}

static inline void fscrypt_handle_d_move(struct dentry *dentry)
{
}

static inline bool fscrypt_is_nokey_name(const struct dentry *dentry)
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

static inline int fscrypt_set_context(struct inode *inode, void *fs_data)
{
	return -EOPNOTSUPP;
}

struct fscrypt_dummy_policy {
};

static inline void fscrypt_show_test_dummy_encryption(struct seq_file *seq,
						      char sep,
						      struct super_block *sb)
{
}

static inline void
fscrypt_free_dummy_policy(struct fscrypt_dummy_policy *dummy_policy)
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

/* keysetup.c */

static inline int fscrypt_prepare_new_inode(struct inode *dir,
					    struct inode *inode,
					    bool *encrypt_ret)
{
	if (IS_ENCRYPTED(dir))
		return -EOPNOTSUPP;
	return 0;
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

static inline int fscrypt_fname_alloc_buffer(u32 max_encrypted_len,
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

static inline int fscrypt_d_revalidate(struct dentry *dentry,
				       unsigned int flags)
{
	return 1;
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

static inline int __fscrypt_prepare_readdir(struct inode *dir)
{
	return -EOPNOTSUPP;
}

static inline int __fscrypt_prepare_setattr(struct dentry *dentry,
					    struct iattr *attr)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_prepare_setflags(struct inode *inode,
					   unsigned int oldflags,
					   unsigned int flags)
{
	return 0;
}

static inline int fscrypt_prepare_symlink(struct inode *dir,
					  const char *target,
					  unsigned int len,
					  unsigned int max_len,
					  struct fscrypt_str *disk_link)
{
	if (IS_ENCRYPTED(dir))
		return -EOPNOTSUPP;
	disk_link->name = (unsigned char *)target;
	disk_link->len = len + 1;
	if (disk_link->len > max_len)
		return -ENAMETOOLONG;
	return 0;
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

static inline void fscrypt_set_ops(struct super_block *sb,
				   const struct fscrypt_operations *s_cop)
{
}

#endif	/* !CONFIG_FS_ENCRYPTION */

/* inline_crypt.c */
#ifdef CONFIG_FS_ENCRYPTION_INLINE_CRYPT

bool __fscrypt_inode_uses_inline_crypto(const struct inode *inode);

void fscrypt_set_bio_crypt_ctx(struct bio *bio,
			       const struct inode *inode, u64 first_lblk,
			       gfp_t gfp_mask);

void fscrypt_set_bio_crypt_ctx_bh(struct bio *bio,
				  const struct buffer_head *first_bh,
				  gfp_t gfp_mask);

bool fscrypt_mergeable_bio(struct bio *bio, const struct inode *inode,
			   u64 next_lblk);

bool fscrypt_mergeable_bio_bh(struct bio *bio,
			      const struct buffer_head *next_bh);

bool fscrypt_dio_supported(struct kiocb *iocb, struct iov_iter *iter);

u64 fscrypt_limit_io_blocks(const struct inode *inode, u64 lblk, u64 nr_blocks);

#else /* CONFIG_FS_ENCRYPTION_INLINE_CRYPT */

static inline bool __fscrypt_inode_uses_inline_crypto(const struct inode *inode)
{
	return false;
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

static inline u64 fscrypt_limit_io_blocks(const struct inode *inode, u64 lblk,
					  u64 nr_blocks)
{
	return nr_blocks;
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
 * fscrypt_inode_uses_inline_crypto() - test whether an inode uses inline
 *					encryption
 * @inode: an inode. If encrypted, its key must be set up.
 *
 * Return: true if the inode requires file contents encryption and if the
 *	   encryption should be done in the block layer via blk-crypto rather
 *	   than in the filesystem layer.
 */
static inline bool fscrypt_inode_uses_inline_crypto(const struct inode *inode)
{
	return fscrypt_needs_contents_encryption(inode) &&
	       __fscrypt_inode_uses_inline_crypto(inode);
}

/**
 * fscrypt_inode_uses_fs_layer_crypto() - test whether an inode uses fs-layer
 *					  encryption
 * @inode: an inode. If encrypted, its key must be set up.
 *
 * Return: true if the inode requires file contents encryption and if the
 *	   encryption should be done in the filesystem layer rather than in the
 *	   block layer via blk-crypto.
 */
static inline bool fscrypt_inode_uses_fs_layer_crypto(const struct inode *inode)
{
	return fscrypt_needs_contents_encryption(inode) &&
	       !__fscrypt_inode_uses_inline_crypto(inode);
}

/**
 * fscrypt_has_encryption_key() - check whether an inode has had its key set up
 * @inode: the inode to check
 *
 * Return: %true if the inode has had its encryption key set up, else %false.
 *
 * Usually this should be preceded by fscrypt_get_encryption_info() to try to
 * set up the key first.
 */
static inline bool fscrypt_has_encryption_key(const struct inode *inode)
{
	return fscrypt_get_info(inode) != NULL;
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
 * the filename.
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
 * the name that will actually be used to search the directory on-disk.  If the
 * directory's encryption policy is supported by this kernel and its encryption
 * key is available, then the lookup is assumed to be by plaintext name;
 * otherwise, it is assumed to be by no-key name.
 *
 * This will set DCACHE_NOKEY_NAME on the dentry if the lookup is by no-key
 * name.  In this case the filesystem must assign the dentry a dentry_operations
 * which contains fscrypt_d_revalidate (or contains a d_revalidate method that
 * calls fscrypt_d_revalidate), so that the dentry will be invalidated if the
 * directory's encryption key is later added.
 *
 * Return: 0 on success; -ENOENT if the directory's key is unavailable but the
 * filename isn't a valid no-key name, so a negative dentry should be created;
 * or another -errno code.
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
 * fscrypt_prepare_readdir() - prepare to read a possibly-encrypted directory
 * @dir: the directory inode
 *
 * If the directory is encrypted and it doesn't already have its encryption key
 * set up, try to set it up so that the filenames will be listed in plaintext
 * form rather than in no-key form.
 *
 * Return: 0 on success; -errno on error.  Note that the encryption key being
 *	   unavailable is not considered an error.  It is also not an error if
 *	   the encryption policy is unsupported by this kernel; that is treated
 *	   like the key being unavailable, so that files can still be deleted.
 */
static inline int fscrypt_prepare_readdir(struct inode *dir)
{
	if (IS_ENCRYPTED(dir))
		return __fscrypt_prepare_readdir(dir);
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
	if (IS_ENCRYPTED(d_inode(dentry)))
		return __fscrypt_prepare_setattr(dentry, attr);
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
