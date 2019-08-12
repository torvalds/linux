/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2008 IBM Corporation
 * Author: Mimi Zohar <zohar@us.ibm.com>
 */

#ifndef _LINUX_IMA_H
#define _LINUX_IMA_H

#include <linux/fs.h>
#include <linux/security.h>
#include <linux/kexec.h>
struct linux_binprm;

#ifdef CONFIG_IMA
extern int ima_bprm_check(struct linux_binprm *bprm);
extern int ima_file_check(struct file *file, int mask);
extern void ima_post_create_tmpfile(struct inode *inode);
extern void ima_file_free(struct file *file);
extern int ima_file_mmap(struct file *file, unsigned long prot);
extern int ima_load_data(enum kernel_load_data_id id);
extern int ima_read_file(struct file *file, enum kernel_read_file_id id);
extern int ima_post_read_file(struct file *file, void *buf, loff_t size,
			      enum kernel_read_file_id id);
extern void ima_post_path_mknod(struct dentry *dentry);
extern void ima_kexec_cmdline(const void *buf, int size);

#ifdef CONFIG_IMA_KEXEC
extern void ima_add_kexec_buffer(struct kimage *image);
#endif

#if (defined(CONFIG_X86) && defined(CONFIG_EFI)) || defined(CONFIG_S390)
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
static inline int ima_bprm_check(struct linux_binprm *bprm)
{
	return 0;
}

static inline int ima_file_check(struct file *file, int mask)
{
	return 0;
}

static inline void ima_post_create_tmpfile(struct inode *inode)
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

static inline int ima_load_data(enum kernel_load_data_id id)
{
	return 0;
}

static inline int ima_read_file(struct file *file, enum kernel_read_file_id id)
{
	return 0;
}

static inline int ima_post_read_file(struct file *file, void *buf, loff_t size,
				     enum kernel_read_file_id id)
{
	return 0;
}

static inline void ima_post_path_mknod(struct dentry *dentry)
{
	return;
}

static inline void ima_kexec_cmdline(const void *buf, int size) {}
#endif /* CONFIG_IMA */

#ifndef CONFIG_IMA_KEXEC
struct kimage;

static inline void ima_add_kexec_buffer(struct kimage *image)
{}
#endif

#ifdef CONFIG_IMA_APPRAISE
extern bool is_ima_appraise_enabled(void);
extern void ima_inode_post_setattr(struct dentry *dentry);
extern int ima_inode_setxattr(struct dentry *dentry, const char *xattr_name,
		       const void *xattr_value, size_t xattr_value_len);
extern int ima_inode_removexattr(struct dentry *dentry, const char *xattr_name);
#else
static inline bool is_ima_appraise_enabled(void)
{
	return 0;
}

static inline void ima_inode_post_setattr(struct dentry *dentry)
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
#endif /* _LINUX_IMA_H */
