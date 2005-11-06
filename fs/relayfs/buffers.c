/*
 * RelayFS buffer management code.
 *
 * Copyright (C) 2002-2005 - Tom Zanussi (zanussi@us.ibm.com), IBM Corp
 * Copyright (C) 1999-2005 - Karim Yaghmour (karim@opersys.com)
 *
 * This file is released under the GPL.
 */

#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/relayfs_fs.h>
#include "relay.h"
#include "buffers.h"

/*
 * close() vm_op implementation for relayfs file mapping.
 */
static void relay_file_mmap_close(struct vm_area_struct *vma)
{
	struct rchan_buf *buf = vma->vm_private_data;
	buf->chan->cb->buf_unmapped(buf, vma->vm_file);
}

/*
 * nopage() vm_op implementation for relayfs file mapping.
 */
static struct page *relay_buf_nopage(struct vm_area_struct *vma,
				     unsigned long address,
				     int *type)
{
	struct page *page;
	struct rchan_buf *buf = vma->vm_private_data;
	unsigned long offset = address - vma->vm_start;

	if (address > vma->vm_end)
		return NOPAGE_SIGBUS; /* Disallow mremap */
	if (!buf)
		return NOPAGE_OOM;

	page = vmalloc_to_page(buf->start + offset);
	if (!page)
		return NOPAGE_OOM;
	get_page(page);

	if (type)
		*type = VM_FAULT_MINOR;

	return page;
}

/*
 * vm_ops for relay file mappings.
 */
static struct vm_operations_struct relay_file_mmap_ops = {
	.nopage = relay_buf_nopage,
	.close = relay_file_mmap_close,
};

/**
 *	relay_mmap_buf: - mmap channel buffer to process address space
 *	@buf: relay channel buffer
 *	@vma: vm_area_struct describing memory to be mapped
 *
 *	Returns 0 if ok, negative on error
 *
 *	Caller should already have grabbed mmap_sem.
 */
int relay_mmap_buf(struct rchan_buf *buf, struct vm_area_struct *vma)
{
	unsigned long length = vma->vm_end - vma->vm_start;
	struct file *filp = vma->vm_file;

	if (!buf)
		return -EBADF;

	if (length != (unsigned long)buf->chan->alloc_size)
		return -EINVAL;

	vma->vm_ops = &relay_file_mmap_ops;
	vma->vm_private_data = buf;
	buf->chan->cb->buf_mapped(buf, filp);

	return 0;
}

/**
 *	relay_alloc_buf - allocate a channel buffer
 *	@buf: the buffer struct
 *	@size: total size of the buffer
 *
 *	Returns a pointer to the resulting buffer, NULL if unsuccessful
 */
static void *relay_alloc_buf(struct rchan_buf *buf, unsigned long size)
{
	void *mem;
	unsigned int i, j, n_pages;

	size = PAGE_ALIGN(size);
	n_pages = size >> PAGE_SHIFT;

	buf->page_array = kcalloc(n_pages, sizeof(struct page *), GFP_KERNEL);
	if (!buf->page_array)
		return NULL;

	for (i = 0; i < n_pages; i++) {
		buf->page_array[i] = alloc_page(GFP_KERNEL);
		if (unlikely(!buf->page_array[i]))
			goto depopulate;
	}
	mem = vmap(buf->page_array, n_pages, VM_MAP, PAGE_KERNEL);
	if (!mem)
		goto depopulate;

	memset(mem, 0, size);
	buf->page_count = n_pages;
	return mem;

depopulate:
	for (j = 0; j < i; j++)
		__free_page(buf->page_array[j]);
	kfree(buf->page_array);
	return NULL;
}

/**
 *	relay_create_buf - allocate and initialize a channel buffer
 *	@alloc_size: size of the buffer to allocate
 *	@n_subbufs: number of sub-buffers in the channel
 *
 *	Returns channel buffer if successful, NULL otherwise
 */
struct rchan_buf *relay_create_buf(struct rchan *chan)
{
	struct rchan_buf *buf = kcalloc(1, sizeof(struct rchan_buf), GFP_KERNEL);
	if (!buf)
		return NULL;

	buf->padding = kmalloc(chan->n_subbufs * sizeof(size_t *), GFP_KERNEL);
	if (!buf->padding)
		goto free_buf;

	buf->start = relay_alloc_buf(buf, chan->alloc_size);
	if (!buf->start)
		goto free_buf;

	buf->chan = chan;
	kref_get(&buf->chan->kref);
	return buf;

free_buf:
	kfree(buf->padding);
	kfree(buf);
	return NULL;
}

/**
 *	relay_destroy_buf - destroy an rchan_buf struct and associated buffer
 *	@buf: the buffer struct
 */
void relay_destroy_buf(struct rchan_buf *buf)
{
	struct rchan *chan = buf->chan;
	unsigned int i;

	if (likely(buf->start)) {
		vunmap(buf->start);
		for (i = 0; i < buf->page_count; i++)
			__free_page(buf->page_array[i]);
		kfree(buf->page_array);
	}
	kfree(buf->padding);
	kfree(buf);
	kref_put(&chan->kref, relay_destroy_channel);
}

/**
 *	relay_remove_buf - remove a channel buffer
 *
 *	Removes the file from the relayfs fileystem, which also frees the
 *	rchan_buf_struct and the channel buffer.  Should only be called from
 *	kref_put().
 */
void relay_remove_buf(struct kref *kref)
{
	struct rchan_buf *buf = container_of(kref, struct rchan_buf, kref);
	relayfs_remove(buf->dentry);
}
