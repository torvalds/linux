/*
 * fscrypt_supp.h
 *
 * This is included by filesystems configured with encryption support.
 */

#ifndef _LINUX_FSCRYPT_SUPP_H
#define _LINUX_FSCRYPT_SUPP_H

#include <linux/fscrypt_common.h>

/* crypto.c */
extern struct kmem_cache *fscrypt_info_cachep;
extern struct fscrypt_ctx *fscrypt_get_ctx(const struct inode *, gfp_t);
extern void fscrypt_release_ctx(struct fscrypt_ctx *);
extern struct page *fscrypt_encrypt_page(const struct inode *, struct page *,
						unsigned int, unsigned int,
						u64, gfp_t);
extern int fscrypt_decrypt_page(const struct inode *, struct page *, unsigned int,
				unsigned int, u64);
extern void fscrypt_restore_control_page(struct page *);

extern const struct dentry_operations fscrypt_d_ops;

static inline void fscrypt_set_d_op(struct dentry *dentry)
{
	d_set_d_op(dentry, &fscrypt_d_ops);
}

static inline void fscrypt_set_encrypted_dentry(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	dentry->d_flags |= DCACHE_ENCRYPTED_WITH_KEY;
	spin_unlock(&dentry->d_lock);
}

/* policy.c */
extern int fscrypt_ioctl_set_policy(struct file *, const void __user *);
extern int fscrypt_ioctl_get_policy(struct file *, void __user *);
extern int fscrypt_has_permitted_context(struct inode *, struct inode *);
extern int fscrypt_inherit_context(struct inode *, struct inode *,
					void *, bool);
/* keyinfo.c */
extern int fscrypt_get_encryption_info(struct inode *);
extern void fscrypt_put_encryption_info(struct inode *, struct fscrypt_info *);

/* fname.c */
extern int fscrypt_setup_filename(struct inode *, const struct qstr *,
				int lookup, struct fscrypt_name *);
extern void fscrypt_free_filename(struct fscrypt_name *);
extern u32 fscrypt_fname_encrypted_size(const struct inode *, u32);
extern int fscrypt_fname_alloc_buffer(const struct inode *, u32,
				struct fscrypt_str *);
extern void fscrypt_fname_free_buffer(struct fscrypt_str *);
extern int fscrypt_fname_disk_to_usr(struct inode *, u32, u32,
			const struct fscrypt_str *, struct fscrypt_str *);
extern int fscrypt_fname_usr_to_disk(struct inode *, const struct qstr *,
			struct fscrypt_str *);

/* bio.c */
extern void fscrypt_decrypt_bio_pages(struct fscrypt_ctx *, struct bio *);
extern void fscrypt_pullback_bio_page(struct page **, bool);
extern int fscrypt_zeroout_range(const struct inode *, pgoff_t, sector_t,
				 unsigned int);

#endif	/* _LINUX_FSCRYPT_SUPP_H */
