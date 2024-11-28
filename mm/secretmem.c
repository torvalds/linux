// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corporation, 2021
 *
 * Author: Mike Rapoport <rppt@linux.ibm.com>
 */

#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/swap.h>
#include <linux/mount.h>
#include <linux/memfd.h>
#include <linux/bitops.h>
#include <linux/printk.h>
#include <linux/pagemap.h>
#include <linux/syscalls.h>
#include <linux/pseudo_fs.h>
#include <linux/secretmem.h>
#include <linux/set_memory.h>
#include <linux/sched/signal.h>

#include <uapi/linux/magic.h>

#include <asm/tlbflush.h>

#include "internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "secretmem: " fmt

/*
 * Define mode and flag masks to allow validation of the system call
 * parameters.
 */
#define SECRETMEM_MODE_MASK	(0x0)
#define SECRETMEM_FLAGS_MASK	SECRETMEM_MODE_MASK

static bool secretmem_enable __ro_after_init;
module_param_named(enable, secretmem_enable, bool, 0400);
MODULE_PARM_DESC(secretmem_enable,
		 "Enable secretmem and memfd_secret(2) system call");

static atomic_t secretmem_users;

bool secretmem_active(void)
{
	return !!atomic_read(&secretmem_users);
}

static vm_fault_t secretmem_fault(struct vm_fault *vmf)
{
	struct address_space *mapping = vmf->vma->vm_file->f_mapping;
	struct inode *inode = file_inode(vmf->vma->vm_file);
	pgoff_t offset = vmf->pgoff;
	gfp_t gfp = vmf->gfp_mask;
	unsigned long addr;
	struct page *page;
	vm_fault_t ret;
	int err;

	if (((loff_t)vmf->pgoff << PAGE_SHIFT) >= i_size_read(inode))
		return vmf_error(-EINVAL);

	filemap_invalidate_lock_shared(mapping);

retry:
	page = find_lock_page(mapping, offset);
	if (!page) {
		page = alloc_page(gfp | __GFP_ZERO);
		if (!page) {
			ret = VM_FAULT_OOM;
			goto out;
		}

		err = set_direct_map_invalid_noflush(page);
		if (err) {
			put_page(page);
			ret = vmf_error(err);
			goto out;
		}

		__SetPageUptodate(page);
		err = add_to_page_cache_lru(page, mapping, offset, gfp);
		if (unlikely(err)) {
			put_page(page);
			/*
			 * If a split of large page was required, it
			 * already happened when we marked the page invalid
			 * which guarantees that this call won't fail
			 */
			set_direct_map_default_noflush(page);
			if (err == -EEXIST)
				goto retry;

			ret = vmf_error(err);
			goto out;
		}

		addr = (unsigned long)page_address(page);
		flush_tlb_kernel_range(addr, addr + PAGE_SIZE);
	}

	vmf->page = page;
	ret = VM_FAULT_LOCKED;

out:
	filemap_invalidate_unlock_shared(mapping);
	return ret;
}

static const struct vm_operations_struct secretmem_vm_ops = {
	.fault = secretmem_fault,
};

static int secretmem_release(struct inode *inode, struct file *file)
{
	atomic_dec(&secretmem_users);
	return 0;
}

static int secretmem_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long len = vma->vm_end - vma->vm_start;

	if ((vma->vm_flags & (VM_SHARED | VM_MAYSHARE)) == 0)
		return -EINVAL;

	if (mlock_future_check(vma->vm_mm, vma->vm_flags | VM_LOCKED, len))
		return -EAGAIN;

	vm_flags_set(vma, VM_LOCKED | VM_DONTDUMP);
	vma->vm_ops = &secretmem_vm_ops;

	return 0;
}

bool vma_is_secretmem(struct vm_area_struct *vma)
{
	return vma->vm_ops == &secretmem_vm_ops;
}

static const struct file_operations secretmem_fops = {
	.release	= secretmem_release,
	.mmap		= secretmem_mmap,
};

static int secretmem_migrate_folio(struct address_space *mapping,
		struct folio *dst, struct folio *src, enum migrate_mode mode)
{
	return -EBUSY;
}

static void secretmem_free_folio(struct folio *folio)
{
	set_direct_map_default_noflush(&folio->page);
	folio_zero_segment(folio, 0, folio_size(folio));
}

const struct address_space_operations secretmem_aops = {
	.dirty_folio	= noop_dirty_folio,
	.free_folio	= secretmem_free_folio,
	.migrate_folio	= secretmem_migrate_folio,
};

static int secretmem_setattr(struct user_namespace *mnt_userns,
			     struct dentry *dentry, struct iattr *iattr)
{
	struct inode *inode = d_inode(dentry);
	struct address_space *mapping = inode->i_mapping;
	unsigned int ia_valid = iattr->ia_valid;
	int ret;

	filemap_invalidate_lock(mapping);

	if ((ia_valid & ATTR_SIZE) && inode->i_size)
		ret = -EINVAL;
	else
		ret = simple_setattr(mnt_userns, dentry, iattr);

	filemap_invalidate_unlock(mapping);

	return ret;
}

static const struct inode_operations secretmem_iops = {
	.setattr = secretmem_setattr,
};

static struct vfsmount *secretmem_mnt;

static struct file *secretmem_file_create(unsigned long flags)
{
	struct file *file = ERR_PTR(-ENOMEM);
	struct inode *inode;
	const char *anon_name = "[secretmem]";
	const struct qstr qname = QSTR_INIT(anon_name, strlen(anon_name));
	int err;

	inode = alloc_anon_inode(secretmem_mnt->mnt_sb);
	if (IS_ERR(inode))
		return ERR_CAST(inode);

	err = security_inode_init_security_anon(inode, &qname, NULL);
	if (err) {
		file = ERR_PTR(err);
		goto err_free_inode;
	}

	file = alloc_file_pseudo(inode, secretmem_mnt, "secretmem",
				 O_RDWR, &secretmem_fops);
	if (IS_ERR(file))
		goto err_free_inode;

	mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
	mapping_set_unevictable(inode->i_mapping);

	inode->i_op = &secretmem_iops;
	inode->i_mapping->a_ops = &secretmem_aops;

	/* pretend we are a normal file with zero size */
	inode->i_mode |= S_IFREG;
	inode->i_size = 0;

	return file;

err_free_inode:
	iput(inode);
	return file;
}

SYSCALL_DEFINE1(memfd_secret, unsigned int, flags)
{
	struct file *file;
	int fd, err;

	/* make sure local flags do not confict with global fcntl.h */
	BUILD_BUG_ON(SECRETMEM_FLAGS_MASK & O_CLOEXEC);

	if (!secretmem_enable || !can_set_direct_map())
		return -ENOSYS;

	if (flags & ~(SECRETMEM_FLAGS_MASK | O_CLOEXEC))
		return -EINVAL;
	if (atomic_read(&secretmem_users) < 0)
		return -ENFILE;

	fd = get_unused_fd_flags(flags & O_CLOEXEC);
	if (fd < 0)
		return fd;

	file = secretmem_file_create(flags);
	if (IS_ERR(file)) {
		err = PTR_ERR(file);
		goto err_put_fd;
	}

	file->f_flags |= O_LARGEFILE;

	atomic_inc(&secretmem_users);
	fd_install(fd, file);
	return fd;

err_put_fd:
	put_unused_fd(fd);
	return err;
}

static int secretmem_init_fs_context(struct fs_context *fc)
{
	return init_pseudo(fc, SECRETMEM_MAGIC) ? 0 : -ENOMEM;
}

static struct file_system_type secretmem_fs = {
	.name		= "secretmem",
	.init_fs_context = secretmem_init_fs_context,
	.kill_sb	= kill_anon_super,
};

static int __init secretmem_init(void)
{
	if (!secretmem_enable || !can_set_direct_map())
		return 0;

	secretmem_mnt = kern_mount(&secretmem_fs);
	if (IS_ERR(secretmem_mnt))
		return PTR_ERR(secretmem_mnt);

	/* prevent secretmem mappings from ever getting PROT_EXEC */
	secretmem_mnt->mnt_flags |= MNT_NOEXEC;

	return 0;
}
fs_initcall(secretmem_init);
