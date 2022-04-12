// SPDX-License-Identifier: MIT
/*
 * VirtualBox Guest Shared Folders support: Regular file inode and file ops.
 *
 * Copyright (C) 2006-2018 Oracle Corporation
 */

#include <linux/mm.h>
#include <linux/page-flags.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/sizes.h>
#include "vfsmod.h"

struct vboxsf_handle {
	u64 handle;
	u32 root;
	u32 access_flags;
	struct kref refcount;
	struct list_head head;
};

struct vboxsf_handle *vboxsf_create_sf_handle(struct inode *inode,
					      u64 handle, u32 access_flags)
{
	struct vboxsf_inode *sf_i = VBOXSF_I(inode);
	struct vboxsf_handle *sf_handle;

	sf_handle = kmalloc(sizeof(*sf_handle), GFP_KERNEL);
	if (!sf_handle)
		return ERR_PTR(-ENOMEM);

	/* the host may have given us different attr then requested */
	sf_i->force_restat = 1;

	/* init our handle struct and add it to the inode's handles list */
	sf_handle->handle = handle;
	sf_handle->root = VBOXSF_SBI(inode->i_sb)->root;
	sf_handle->access_flags = access_flags;
	kref_init(&sf_handle->refcount);

	mutex_lock(&sf_i->handle_list_mutex);
	list_add(&sf_handle->head, &sf_i->handle_list);
	mutex_unlock(&sf_i->handle_list_mutex);

	return sf_handle;
}

static int vboxsf_file_open(struct inode *inode, struct file *file)
{
	struct vboxsf_sbi *sbi = VBOXSF_SBI(inode->i_sb);
	struct shfl_createparms params = {};
	struct vboxsf_handle *sf_handle;
	u32 access_flags = 0;
	int err;

	/*
	 * We check the value of params.handle afterwards to find out if
	 * the call succeeded or failed, as the API does not seem to cleanly
	 * distinguish error and informational messages.
	 *
	 * Furthermore, we must set params.handle to SHFL_HANDLE_NIL to
	 * make the shared folders host service use our mode parameter.
	 */
	params.handle = SHFL_HANDLE_NIL;
	if (file->f_flags & O_CREAT) {
		params.create_flags |= SHFL_CF_ACT_CREATE_IF_NEW;
		/*
		 * We ignore O_EXCL, as the Linux kernel seems to call create
		 * beforehand itself, so O_EXCL should always fail.
		 */
		if (file->f_flags & O_TRUNC)
			params.create_flags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS;
		else
			params.create_flags |= SHFL_CF_ACT_OPEN_IF_EXISTS;
	} else {
		params.create_flags |= SHFL_CF_ACT_FAIL_IF_NEW;
		if (file->f_flags & O_TRUNC)
			params.create_flags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS;
	}

	switch (file->f_flags & O_ACCMODE) {
	case O_RDONLY:
		access_flags |= SHFL_CF_ACCESS_READ;
		break;

	case O_WRONLY:
		access_flags |= SHFL_CF_ACCESS_WRITE;
		break;

	case O_RDWR:
		access_flags |= SHFL_CF_ACCESS_READWRITE;
		break;

	default:
		WARN_ON(1);
	}

	if (file->f_flags & O_APPEND)
		access_flags |= SHFL_CF_ACCESS_APPEND;

	params.create_flags |= access_flags;
	params.info.attr.mode = inode->i_mode;

	err = vboxsf_create_at_dentry(file_dentry(file), &params);
	if (err == 0 && params.handle == SHFL_HANDLE_NIL)
		err = (params.result == SHFL_FILE_EXISTS) ? -EEXIST : -ENOENT;
	if (err)
		return err;

	sf_handle = vboxsf_create_sf_handle(inode, params.handle, access_flags);
	if (IS_ERR(sf_handle)) {
		vboxsf_close(sbi->root, params.handle);
		return PTR_ERR(sf_handle);
	}

	file->private_data = sf_handle;
	return 0;
}

static void vboxsf_handle_release(struct kref *refcount)
{
	struct vboxsf_handle *sf_handle =
		container_of(refcount, struct vboxsf_handle, refcount);

	vboxsf_close(sf_handle->root, sf_handle->handle);
	kfree(sf_handle);
}

void vboxsf_release_sf_handle(struct inode *inode, struct vboxsf_handle *sf_handle)
{
	struct vboxsf_inode *sf_i = VBOXSF_I(inode);

	mutex_lock(&sf_i->handle_list_mutex);
	list_del(&sf_handle->head);
	mutex_unlock(&sf_i->handle_list_mutex);

	kref_put(&sf_handle->refcount, vboxsf_handle_release);
}

static int vboxsf_file_release(struct inode *inode, struct file *file)
{
	/*
	 * When a file is closed on our (the guest) side, we want any subsequent
	 * accesses done on the host side to see all changes done from our side.
	 */
	filemap_write_and_wait(inode->i_mapping);

	vboxsf_release_sf_handle(inode, file->private_data);
	return 0;
}

/*
 * Write back dirty pages now, because there may not be any suitable
 * open files later
 */
static void vboxsf_vma_close(struct vm_area_struct *vma)
{
	filemap_write_and_wait(vma->vm_file->f_mapping);
}

static const struct vm_operations_struct vboxsf_file_vm_ops = {
	.close		= vboxsf_vma_close,
	.fault		= filemap_fault,
	.map_pages	= filemap_map_pages,
};

static int vboxsf_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	int err;

	err = generic_file_mmap(file, vma);
	if (!err)
		vma->vm_ops = &vboxsf_file_vm_ops;

	return err;
}

/*
 * Note that since we are accessing files on the host's filesystem, files
 * may always be changed underneath us by the host!
 *
 * The vboxsf API between the guest and the host does not offer any functions
 * to deal with this. There is no inode-generation to check for changes, no
 * events / callback on changes and no way to lock files.
 *
 * To avoid returning stale data when a file gets *opened* on our (the guest)
 * side, we do a "stat" on the host side, then compare the mtime with the
 * last known mtime and invalidate the page-cache if they differ.
 * This is done from vboxsf_inode_revalidate().
 *
 * When reads are done through the read_iter fop, it is possible to do
 * further cache revalidation then, there are 3 options to deal with this:
 *
 * 1)  Rely solely on the revalidation done at open time
 * 2)  Do another "stat" and compare mtime again. Unfortunately the vboxsf
 *     host API does not allow stat on handles, so we would need to use
 *     file->f_path.dentry and the stat will then fail if the file was unlinked
 *     or renamed (and there is no thing like NFS' silly-rename). So we get:
 * 2a) "stat" and compare mtime, on stat failure invalidate the cache
 * 2b) "stat" and compare mtime, on stat failure do nothing
 * 3)  Simply always call invalidate_inode_pages2_range on the range of the read
 *
 * Currently we are keeping things KISS and using option 1. this allows
 * directly using generic_file_read_iter without wrapping it.
 *
 * This means that only data written on the host side before open() on
 * the guest side is guaranteed to be seen by the guest. If necessary
 * we may provide other read-cache strategies in the future and make this
 * configurable through a mount option.
 */
const struct file_operations vboxsf_reg_fops = {
	.llseek = generic_file_llseek,
	.read_iter = generic_file_read_iter,
	.write_iter = generic_file_write_iter,
	.mmap = vboxsf_file_mmap,
	.open = vboxsf_file_open,
	.release = vboxsf_file_release,
	.fsync = noop_fsync,
	.splice_read = generic_file_splice_read,
};

const struct inode_operations vboxsf_reg_iops = {
	.getattr = vboxsf_getattr,
	.setattr = vboxsf_setattr
};

static int vboxsf_readpage(struct file *file, struct page *page)
{
	struct vboxsf_handle *sf_handle = file->private_data;
	loff_t off = page_offset(page);
	u32 nread = PAGE_SIZE;
	u8 *buf;
	int err;

	buf = kmap(page);

	err = vboxsf_read(sf_handle->root, sf_handle->handle, off, &nread, buf);
	if (err == 0) {
		memset(&buf[nread], 0, PAGE_SIZE - nread);
		flush_dcache_page(page);
		SetPageUptodate(page);
	} else {
		SetPageError(page);
	}

	kunmap(page);
	unlock_page(page);
	return err;
}

static struct vboxsf_handle *vboxsf_get_write_handle(struct vboxsf_inode *sf_i)
{
	struct vboxsf_handle *h, *sf_handle = NULL;

	mutex_lock(&sf_i->handle_list_mutex);
	list_for_each_entry(h, &sf_i->handle_list, head) {
		if (h->access_flags == SHFL_CF_ACCESS_WRITE ||
		    h->access_flags == SHFL_CF_ACCESS_READWRITE) {
			kref_get(&h->refcount);
			sf_handle = h;
			break;
		}
	}
	mutex_unlock(&sf_i->handle_list_mutex);

	return sf_handle;
}

static int vboxsf_writepage(struct page *page, struct writeback_control *wbc)
{
	struct inode *inode = page->mapping->host;
	struct vboxsf_inode *sf_i = VBOXSF_I(inode);
	struct vboxsf_handle *sf_handle;
	loff_t off = page_offset(page);
	loff_t size = i_size_read(inode);
	u32 nwrite = PAGE_SIZE;
	u8 *buf;
	int err;

	if (off + PAGE_SIZE > size)
		nwrite = size & ~PAGE_MASK;

	sf_handle = vboxsf_get_write_handle(sf_i);
	if (!sf_handle)
		return -EBADF;

	buf = kmap(page);
	err = vboxsf_write(sf_handle->root, sf_handle->handle,
			   off, &nwrite, buf);
	kunmap(page);

	kref_put(&sf_handle->refcount, vboxsf_handle_release);

	if (err == 0) {
		ClearPageError(page);
		/* mtime changed */
		sf_i->force_restat = 1;
	} else {
		ClearPageUptodate(page);
	}

	unlock_page(page);
	return err;
}

static int vboxsf_write_end(struct file *file, struct address_space *mapping,
			    loff_t pos, unsigned int len, unsigned int copied,
			    struct page *page, void *fsdata)
{
	struct inode *inode = mapping->host;
	struct vboxsf_handle *sf_handle = file->private_data;
	unsigned int from = pos & ~PAGE_MASK;
	u32 nwritten = len;
	u8 *buf;
	int err;

	/* zero the stale part of the page if we did a short copy */
	if (!PageUptodate(page) && copied < len)
		zero_user(page, from + copied, len - copied);

	buf = kmap(page);
	err = vboxsf_write(sf_handle->root, sf_handle->handle,
			   pos, &nwritten, buf + from);
	kunmap(page);

	if (err) {
		nwritten = 0;
		goto out;
	}

	/* mtime changed */
	VBOXSF_I(inode)->force_restat = 1;

	if (!PageUptodate(page) && nwritten == PAGE_SIZE)
		SetPageUptodate(page);

	pos += nwritten;
	if (pos > inode->i_size)
		i_size_write(inode, pos);

out:
	unlock_page(page);
	put_page(page);

	return nwritten;
}

/*
 * Note simple_write_begin does not read the page from disk on partial writes
 * this is ok since vboxsf_write_end only writes the written parts of the
 * page and it does not call SetPageUptodate for partial writes.
 */
const struct address_space_operations vboxsf_reg_aops = {
	.readpage = vboxsf_readpage,
	.writepage = vboxsf_writepage,
	.dirty_folio = filemap_dirty_folio,
	.write_begin = simple_write_begin,
	.write_end = vboxsf_write_end,
};

static const char *vboxsf_get_link(struct dentry *dentry, struct inode *inode,
				   struct delayed_call *done)
{
	struct vboxsf_sbi *sbi = VBOXSF_SBI(inode->i_sb);
	struct shfl_string *path;
	char *link;
	int err;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	path = vboxsf_path_from_dentry(sbi, dentry);
	if (IS_ERR(path))
		return ERR_CAST(path);

	link = kzalloc(PATH_MAX, GFP_KERNEL);
	if (!link) {
		__putname(path);
		return ERR_PTR(-ENOMEM);
	}

	err = vboxsf_readlink(sbi->root, path, PATH_MAX, link);
	__putname(path);
	if (err) {
		kfree(link);
		return ERR_PTR(err);
	}

	set_delayed_call(done, kfree_link, link);
	return link;
}

const struct inode_operations vboxsf_lnk_iops = {
	.get_link = vboxsf_get_link
};
