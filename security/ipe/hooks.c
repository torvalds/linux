// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
 */

#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/types.h>
#include <linux/binfmts.h>
#include <linux/mman.h>

#include "ipe.h"
#include "hooks.h"
#include "eval.h"

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

	ipe_build_eval_ctx(&ctx, bprm->file, IPE_OP_EXEC);
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
		ipe_build_eval_ctx(&ctx, f, IPE_OP_EXEC);
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
		ipe_build_eval_ctx(&ctx, vma->vm_file, IPE_OP_EXEC);
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

	ipe_build_eval_ctx(&ctx, file, op);
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

	ipe_build_eval_ctx(&ctx, NULL, op);
	return ipe_evaluate_event(&ctx);
}

/**
 * ipe_unpack_initramfs() - Mark the current rootfs as initramfs.
 */
void ipe_unpack_initramfs(void)
{
	ipe_sb(current->fs->root.mnt->mnt_sb)->initramfs = true;
}
