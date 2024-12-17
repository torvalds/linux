// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
 */

#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/types.h>
#include <linux/binfmts.h>
#include <linux/mman.h>
#include <linux/blk_types.h>

#include "ipe.h"
#include "hooks.h"
#include "eval.h"
#include "digest.h"

/**
 * ipe_bprm_check_security() - ipe security hook function for bprm check.
 * @bprm: Supplies a pointer to a linux_binprm structure to source the file
 *	  being evaluated.
 *
 * This LSM hook is called when a binary is loaded through the exec
 * family of system calls.
 *
 * Return:
 * * %0		- Success
 * * %-EACCES	- Did not pass IPE policy
 */
int ipe_bprm_check_security(struct linux_binprm *bprm)
{
	struct ipe_eval_ctx ctx = IPE_EVAL_CTX_INIT;

	ipe_build_eval_ctx(&ctx, bprm->file, IPE_OP_EXEC, IPE_HOOK_BPRM_CHECK);
	return ipe_evaluate_event(&ctx);
}

/**
 * ipe_mmap_file() - ipe security hook function for mmap check.
 * @f: File being mmap'd. Can be NULL in the case of anonymous memory.
 * @reqprot: The requested protection on the mmap, passed from usermode.
 * @prot: The effective protection on the mmap, resolved from reqprot and
 *	  system configuration.
 * @flags: Unused.
 *
 * This hook is called when a file is loaded through the mmap
 * family of system calls.
 *
 * Return:
 * * %0		- Success
 * * %-EACCES	- Did not pass IPE policy
 */
int ipe_mmap_file(struct file *f, unsigned long reqprot __always_unused,
		  unsigned long prot, unsigned long flags)
{
	struct ipe_eval_ctx ctx = IPE_EVAL_CTX_INIT;

	if (prot & PROT_EXEC) {
		ipe_build_eval_ctx(&ctx, f, IPE_OP_EXEC, IPE_HOOK_MMAP);
		return ipe_evaluate_event(&ctx);
	}

	return 0;
}

/**
 * ipe_file_mprotect() - ipe security hook function for mprotect check.
 * @vma: Existing virtual memory area created by mmap or similar.
 * @reqprot: The requested protection on the mmap, passed from usermode.
 * @prot: The effective protection on the mmap, resolved from reqprot and
 *	  system configuration.
 *
 * This LSM hook is called when a mmap'd region of memory is changing
 * its protections via mprotect.
 *
 * Return:
 * * %0		- Success
 * * %-EACCES	- Did not pass IPE policy
 */
int ipe_file_mprotect(struct vm_area_struct *vma,
		      unsigned long reqprot __always_unused,
		      unsigned long prot)
{
	struct ipe_eval_ctx ctx = IPE_EVAL_CTX_INIT;

	/* Already Executable */
	if (vma->vm_flags & VM_EXEC)
		return 0;

	if (prot & PROT_EXEC) {
		ipe_build_eval_ctx(&ctx, vma->vm_file, IPE_OP_EXEC, IPE_HOOK_MPROTECT);
		return ipe_evaluate_event(&ctx);
	}

	return 0;
}

/**
 * ipe_kernel_read_file() - ipe security hook function for kernel read.
 * @file: Supplies a pointer to the file structure being read in from disk.
 * @id: Supplies the enumeration identifying the purpose of the read.
 * @contents: Unused.
 *
 * This LSM hook is called when a file is read from disk in the kernel.
 *
 * Return:
 * * %0		- Success
 * * %-EACCES	- Did not pass IPE policy
 */
int ipe_kernel_read_file(struct file *file, enum kernel_read_file_id id,
			 bool contents)
{
	struct ipe_eval_ctx ctx = IPE_EVAL_CTX_INIT;
	enum ipe_op_type op;

	switch (id) {
	case READING_FIRMWARE:
		op = IPE_OP_FIRMWARE;
		break;
	case READING_MODULE:
		op = IPE_OP_KERNEL_MODULE;
		break;
	case READING_KEXEC_INITRAMFS:
		op = IPE_OP_KEXEC_INITRAMFS;
		break;
	case READING_KEXEC_IMAGE:
		op = IPE_OP_KEXEC_IMAGE;
		break;
	case READING_POLICY:
		op = IPE_OP_POLICY;
		break;
	case READING_X509_CERTIFICATE:
		op = IPE_OP_X509;
		break;
	default:
		op = IPE_OP_INVALID;
		WARN(1, "no rule setup for kernel_read_file enum %d", id);
	}

	ipe_build_eval_ctx(&ctx, file, op, IPE_HOOK_KERNEL_READ);
	return ipe_evaluate_event(&ctx);
}

/**
 * ipe_kernel_load_data() - ipe security hook function for kernel load data.
 * @id: Supplies the enumeration identifying the purpose of the load.
 * @contents: Unused.
 *
 * This LSM hook is called when a data buffer provided by userspace is loading
 * into the kernel.
 *
 * Return:
 * * %0		- Success
 * * %-EACCES	- Did not pass IPE policy
 */
int ipe_kernel_load_data(enum kernel_load_data_id id, bool contents)
{
	struct ipe_eval_ctx ctx = IPE_EVAL_CTX_INIT;
	enum ipe_op_type op;

	switch (id) {
	case LOADING_FIRMWARE:
		op = IPE_OP_FIRMWARE;
		break;
	case LOADING_MODULE:
		op = IPE_OP_KERNEL_MODULE;
		break;
	case LOADING_KEXEC_INITRAMFS:
		op = IPE_OP_KEXEC_INITRAMFS;
		break;
	case LOADING_KEXEC_IMAGE:
		op = IPE_OP_KEXEC_IMAGE;
		break;
	case LOADING_POLICY:
		op = IPE_OP_POLICY;
		break;
	case LOADING_X509_CERTIFICATE:
		op = IPE_OP_X509;
		break;
	default:
		op = IPE_OP_INVALID;
		WARN(1, "no rule setup for kernel_load_data enum %d", id);
	}

	ipe_build_eval_ctx(&ctx, NULL, op, IPE_HOOK_KERNEL_LOAD);
	return ipe_evaluate_event(&ctx);
}

/**
 * ipe_unpack_initramfs() - Mark the current rootfs as initramfs.
 */
void ipe_unpack_initramfs(void)
{
	ipe_sb(current->fs->root.mnt->mnt_sb)->initramfs = true;
}

#ifdef CONFIG_IPE_PROP_DM_VERITY
/**
 * ipe_bdev_free_security() - Free IPE's LSM blob of block_devices.
 * @bdev: Supplies a pointer to a block_device that contains the structure
 *	  to free.
 */
void ipe_bdev_free_security(struct block_device *bdev)
{
	struct ipe_bdev *blob = ipe_bdev(bdev);

	ipe_digest_free(blob->root_hash);
}

#ifdef CONFIG_IPE_PROP_DM_VERITY_SIGNATURE
static void ipe_set_dmverity_signature(struct ipe_bdev *blob,
				       const void *value,
				       size_t size)
{
	blob->dm_verity_signed = size > 0 && value;
}
#else
static inline void ipe_set_dmverity_signature(struct ipe_bdev *blob,
					      const void *value,
					      size_t size)
{
}
#endif /* CONFIG_IPE_PROP_DM_VERITY_SIGNATURE */

/**
 * ipe_bdev_setintegrity() - Save integrity data from a bdev to IPE's LSM blob.
 * @bdev: Supplies a pointer to a block_device that contains the LSM blob.
 * @type: Supplies the integrity type.
 * @value: Supplies the value to store.
 * @size: The size of @value.
 *
 * This hook is currently used to save dm-verity's root hash or the existence
 * of a validated signed dm-verity root hash into LSM blob.
 *
 * Return: %0 on success. If an error occurs, the function will return the
 * -errno.
 */
int ipe_bdev_setintegrity(struct block_device *bdev, enum lsm_integrity_type type,
			  const void *value, size_t size)
{
	const struct dm_verity_digest *digest = NULL;
	struct ipe_bdev *blob = ipe_bdev(bdev);
	struct digest_info *info = NULL;

	if (type == LSM_INT_DMVERITY_SIG_VALID) {
		ipe_set_dmverity_signature(blob, value, size);

		return 0;
	}

	if (type != LSM_INT_DMVERITY_ROOTHASH)
		return -EINVAL;

	if (!value) {
		ipe_digest_free(blob->root_hash);
		blob->root_hash = NULL;

		return 0;
	}
	digest = value;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->digest = kmemdup(digest->digest, digest->digest_len, GFP_KERNEL);
	if (!info->digest)
		goto err;

	info->alg = kstrdup(digest->alg, GFP_KERNEL);
	if (!info->alg)
		goto err;

	info->digest_len = digest->digest_len;

	ipe_digest_free(blob->root_hash);
	blob->root_hash = info;

	return 0;
err:
	ipe_digest_free(info);

	return -ENOMEM;
}
#endif /* CONFIG_IPE_PROP_DM_VERITY */

#ifdef CONFIG_IPE_PROP_FS_VERITY_BUILTIN_SIG
/**
 * ipe_inode_setintegrity() - save integrity data from a inode to IPE's LSM blob.
 * @inode: The inode to source the security blob from.
 * @type: Supplies the integrity type.
 * @value: The value to be stored.
 * @size: The size of @value.
 *
 * This hook is currently used to save the existence of a validated fs-verity
 * builtin signature into LSM blob.
 *
 * Return: %0 on success. If an error occurs, the function will return the
 * -errno.
 */
int ipe_inode_setintegrity(const struct inode *inode,
			   enum lsm_integrity_type type,
			   const void *value, size_t size)
{
	struct ipe_inode *inode_sec = ipe_inode(inode);

	if (type == LSM_INT_FSVERITY_BUILTINSIG_VALID) {
		inode_sec->fs_verity_signed = size > 0 && value;
		return 0;
	}

	return -EINVAL;
}
#endif /* CONFIG_CONFIG_IPE_PROP_FS_VERITY_BUILTIN_SIG */
