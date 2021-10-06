/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2008 IBM Corporation
 * Author: Mimi Zohar <zohar@us.ibm.com>
 */

#ifndef _LINUX_IMA_H
#define _LINUX_IMA_H

#include <linux/kernel_read_file.h>
#include <linux/fs.h>
#include <linux/security.h>
#include <linux/kexec.h>
#include <crypto/hash_info.h>
struct linux_binprm;

#ifdef CONFIG_IMA
extern enum hash_algo ima_get_current_hash_algo(void);
extern int ima_bprm_check(struct linux_binprm *bprm);
extern int ima_file_check(struct file *file, int mask);
extern void ima_post_create_tmpfile(struct user_namespace *mnt_userns,
				    struct inode *inode);
extern void ima_file_free(struct file *file);
extern int ima_file_mmap(struct file *file, unsigned long prot);
extern int ima_file_mprotect(struct vm_area_struct *vma, unsigned long prot);
extern int ima_load_data(enum kernel_load_data_id id, bool contents);
extern int ima_post_load_data(char *buf, loff_t size,
			      enum kernel_load_data_id id, char *description);
extern int ima_read_file(struct file *file, enum kernel_read_file_id id,
			 bool contents);
extern int ima_post_read_file(struct file *file, void *buf, loff_t size,
			      enum kernel_read_file_id id);
extern void ima_post_path_mknod(struct user_namespace *mnt_userns,
				struct dentry *dentry);
extern int ima_file_hash(struct file *file, char *buf, size_t buf_size);
extern int ima_inode_hash(struct inode *inode, char *buf, size_t buf_size);
extern void ima_kexec_cmdline(int kernel_fd, const void *buf, int size);
extern int ima_measure_critical_data(const char *event_label,
				     const char *event_name,
				     const void *buf, size_t buf_len,
				     bool hash, u8 *digest, size_t digest_len);

#ifdef CONFIG_IMA_APPRAISE_BOOTPARAM
extern void ima_appraise_parse_cmdline(void);
#else
static inline void ima_appraise_parse_cmdline(void) {}
#endif

#ifdef CONFIG_IMA_KEXEC
extern void ima_add_kexec_buffer(struct kimage *image);
#endif

#ifdef CONFIG_IMA_SECURE_AND_OR_TRUSTED_BOOT
extern bool arch_ima_get_secureboot(void);
extern const char * const *arch_get_ima_policy(void);
#else
static inline bool arch_ima_get_secureboot(void)
{
	return false;
}

static inline const char * const *arch_get_ima_policy(void)
{
	return NULL;
}
#endif

#else
static inline enum hash_algo ima_get_current_hash_algo(void)
{
	return HASH_ALGO__LAST;
}

static inline int ima_bprm_check(struct linux_binprm *bprm)
{
	return 0;
}

static inline int ima_file_check(struct file *file, int mask)
{
	return 0;
}

static inline void ima_post_create_tmpfile(struct user_namespace *mnt_userns,
					   struct inode *inode)
{
}

static inline void ima_file_free(struct file *file)
{
	return;
}

static inline int ima_file_mmap(struct file *file, unsigned long prot)
{
	return 0;
}

static inline int ima_file_mprotect(struct vm_area_struct *vma,
				    unsigned long prot)
{
	return 0;
}

static inline int ima_load_data(enum kernel_load_data_id id, bool contents)
{
	return 0;
}

static inline int ima_post_load_data(char *buf, loff_t size,
				     enum kernel_load_data_id id,
				     char *description)
{
	return 0;
}

static inline int ima_read_file(struct file *file, enum kernel_read_file_id id,
				bool contents)
{
	return 0;
}

static inline int ima_post_read_file(struct file *file, void *buf, loff_t size,
				     enum kernel_read_file_id id)
{
	return 0;
}

static inline void ima_post_path_mknod(struct user_namespace *mnt_userns,
				       struct dentry *dentry)
{
	return;
}

static inline int ima_file_hash(struct file *file, char *buf, size_t buf_size)
{
	return -EOPNOTSUPP;
}

static inline int ima_inode_hash(struct inode *inode, char *buf, size_t buf_size)
{
	return -EOPNOTSUPP;
}

static inline void ima_kexec_cmdline(int kernel_fd, const void *buf, int size) {}

static inline int ima_measure_critical_data(const char *event_label,
					     const char *event_name,
					     const void *buf, size_t buf_len,
					     bool hash, u8 *digest,
					     size_t digest_len)
{
	return -ENOENT;
}

#endif /* CONFIG_IMA */

#ifndef CONFIG_IMA_KEXEC
struct kimage;

static inline void ima_add_kexec_buffer(struct kimage *image)
{}
#endif

#ifdef CONFIG_IMA_MEASURE_ASYMMETRIC_KEYS
extern void ima_post_key_create_or_update(struct key *keyring,
					  struct key *key,
					  const void *payload, size_t plen,
					  unsigned long flags, bool create);
#else
static inline void ima_post_key_create_or_update(struct key *keyring,
						 struct key *key,
						 const void *payload,
						 size_t plen,
						 unsigned long flags,
						 bool create) {}
#endif  /* CONFIG_IMA_MEASURE_ASYMMETRIC_KEYS */

#ifdef CONFIG_IMA_APPRAISE
extern bool is_ima_appraise_enabled(void);
extern void ima_inode_post_setattr(struct user_namespace *mnt_userns,
				   struct dentry *dentry);
extern int ima_inode_setxattr(struct dentry *dentry, const char *xattr_name,
		       const void *xattr_value, size_t xattr_value_len);
extern int ima_inode_removexattr(struct dentry *dentry, const char *xattr_name);
#else
static inline bool is_ima_appraise_enabled(void)
{
	return 0;
}

static inline void ima_inode_post_setattr(struct user_namespace *mnt_userns,
					  struct dentry *dentry)
{
	return;
}

static inline int ima_inode_setxattr(struct dentry *dentry,
				     const char *xattr_name,
				     const void *xattr_value,
				     size_t xattr_value_len)
{
	return 0;
}

static inline int ima_inode_removexattr(struct dentry *dentry,
					const char *xattr_name)
{
	return 0;
}
#endif /* CONFIG_IMA_APPRAISE */

#if defined(CONFIG_IMA_APPRAISE) && defined(CONFIG_INTEGRITY_TRUSTED_KEYRING)
extern bool ima_appraise_signature(enum kernel_read_file_id func);
#else
static inline bool ima_appraise_signature(enum kernel_read_file_id func)
{
	return false;
}
#endif /* CONFIG_IMA_APPRAISE && CONFIG_INTEGRITY_TRUSTED_KEYRING */
#endif /* _LINUX_IMA_H */
