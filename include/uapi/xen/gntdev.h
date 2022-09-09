/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR MIT) */
/******************************************************************************
 * gntdev.h
 * 
 * Interface to /dev/xen/gntdev.
 * 
 * Copyright (c) 2007, D G Murray
 * Copyright (c) 2018, Oleksandr Andrushchenko, EPAM Systems Inc.
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef __LINUX_PUBLIC_GNTDEV_H__
#define __LINUX_PUBLIC_GNTDEV_H__

#include <linux/types.h>

struct ioctl_gntdev_grant_ref {
	/* The domain ID of the grant to be mapped. */
	__u32 domid;
	/* The grant reference of the grant to be mapped. */
	__u32 ref;
};

/*
 * Inserts the grant references into the mapping table of an instance
 * of gntdev. N.B. This does not perform the mapping, which is deferred
 * until mmap() is called with @index as the offset. @index should be
 * considered opaque to userspace, with one exception: if no grant
 * references have ever been inserted into the mapping table of this
 * instance, @index will be set to 0. This is necessary to use gntdev
 * with userspace APIs that expect a file descriptor that can be
 * mmap()'d at offset 0, such as Wayland. If @count is set to 0, this
 * ioctl will fail.
 */
#define IOCTL_GNTDEV_MAP_GRANT_REF \
_IOC(_IOC_NONE, 'G', 0, sizeof(struct ioctl_gntdev_map_grant_ref))
struct ioctl_gntdev_map_grant_ref {
	/* IN parameters */
	/* The number of grants to be mapped. */
	__u32 count;
	__u32 pad;
	/* OUT parameters */
	/* The offset to be used on a subsequent call to mmap(). */
	__u64 index;
	/* Variable IN parameter. */
	/* Array of grant references, of size @count. */
	struct ioctl_gntdev_grant_ref refs[1];
};

/*
 * Removes the grant references from the mapping table of an instance of
 * gntdev. N.B. munmap() must be called on the relevant virtual address(es)
 * before this ioctl is called, or an error will result.
 */
#define IOCTL_GNTDEV_UNMAP_GRANT_REF \
_IOC(_IOC_NONE, 'G', 1, sizeof(struct ioctl_gntdev_unmap_grant_ref))
struct ioctl_gntdev_unmap_grant_ref {
	/* IN parameters */
	/* The offset was returned by the corresponding map operation. */
	__u64 index;
	/* The number of pages to be unmapped. */
	__u32 count;
	__u32 pad;
};

/*
 * Returns the offset in the driver's address space that corresponds
 * to @vaddr. This can be used to perform a munmap(), followed by an
 * UNMAP_GRANT_REF ioctl, where no state about the offset is retained by
 * the caller. The number of pages that were allocated at the same time as
 * @vaddr is returned in @count.
 *
 * N.B. Where more than one page has been mapped into a contiguous range, the
 *      supplied @vaddr must correspond to the start of the range; otherwise
 *      an error will result. It is only possible to munmap() the entire
 *      contiguously-allocated range at once, and not any subrange thereof.
 */
#define IOCTL_GNTDEV_GET_OFFSET_FOR_VADDR \
_IOC(_IOC_NONE, 'G', 2, sizeof(struct ioctl_gntdev_get_offset_for_vaddr))
struct ioctl_gntdev_get_offset_for_vaddr {
	/* IN parameters */
	/* The virtual address of the first mapped page in a range. */
	__u64 vaddr;
	/* OUT parameters */
	/* The offset that was used in the initial mmap() operation. */
	__u64 offset;
	/* The number of pages mapped in the VM area that begins at @vaddr. */
	__u32 count;
	__u32 pad;
};

/*
 * Sets the maximum number of grants that may mapped at once by this gntdev
 * instance.
 *
 * N.B. This must be called before any other ioctl is performed on the device.
 */
#define IOCTL_GNTDEV_SET_MAX_GRANTS \
_IOC(_IOC_NONE, 'G', 3, sizeof(struct ioctl_gntdev_set_max_grants))
struct ioctl_gntdev_set_max_grants {
	/* IN parameter */
	/* The maximum number of grants that may be mapped at once. */
	__u32 count;
};

/*
 * Sets up an unmap notification within the page, so that the other side can do
 * cleanup if this side crashes. Required to implement cross-domain robust
 * mutexes or close notification on communication channels.
 *
 * Each mapped page only supports one notification; multiple calls referring to
 * the same page overwrite the previous notification. You must clear the
 * notification prior to the IOCTL_GNTALLOC_DEALLOC_GREF if you do not want it
 * to occur.
 */
#define IOCTL_GNTDEV_SET_UNMAP_NOTIFY \
_IOC(_IOC_NONE, 'G', 7, sizeof(struct ioctl_gntdev_unmap_notify))
struct ioctl_gntdev_unmap_notify {
	/* IN parameters */
	/* Offset in the file descriptor for a byte within the page (same as
	 * used in mmap). If using UNMAP_NOTIFY_CLEAR_BYTE, this is the byte to
	 * be cleared. Otherwise, it can be any byte in the page whose
	 * notification we are adjusting.
	 */
	__u64 index;
	/* Action(s) to take on unmap */
	__u32 action;
	/* Event channel to notify */
	__u32 event_channel_port;
};

struct gntdev_grant_copy_segment {
	union {
		void __user *virt;
		struct {
			grant_ref_t ref;
			__u16 offset;
			domid_t domid;
		} foreign;
	} source, dest;
	__u16 len;

	__u16 flags;  /* GNTCOPY_* */
	__s16 status; /* GNTST_* */
};

/*
 * Copy between grant references and local buffers.
 *
 * The copy is split into @count @segments, each of which can copy
 * to/from one grant reference.
 *
 * Each segment is similar to struct gnttab_copy in the hypervisor ABI
 * except the local buffer is specified using a virtual address
 * (instead of a GFN and offset).
 *
 * The local buffer may cross a Xen page boundary -- the driver will
 * split segments into multiple ops if required.
 *
 * Returns 0 if all segments have been processed and @status in each
 * segment is valid.  Note that one or more segments may have failed
 * (status != GNTST_okay).
 *
 * If the driver had to split a segment into two or more ops, @status
 * includes the status of the first failed op for that segment (or
 * GNTST_okay if all ops were successful).
 *
 * If -1 is returned, the status of all segments is undefined.
 *
 * EINVAL: A segment has local buffers for both source and
 *         destination.
 * EINVAL: A segment crosses the boundary of a foreign page.
 * EFAULT: A segment's local buffer is not accessible.
 */
#define IOCTL_GNTDEV_GRANT_COPY \
	_IOC(_IOC_NONE, 'G', 8, sizeof(struct ioctl_gntdev_grant_copy))
struct ioctl_gntdev_grant_copy {
	unsigned int count;
	struct gntdev_grant_copy_segment __user *segments;
};

/* Clear (set to zero) the byte specified by index */
#define UNMAP_NOTIFY_CLEAR_BYTE 0x1
/* Send an interrupt on the indicated event channel */
#define UNMAP_NOTIFY_SEND_EVENT 0x2

/*
 * Flags to be used while requesting memory mapping's backing storage
 * to be allocated with DMA API.
 */

/*
 * The buffer is backed with memory allocated with dma_alloc_wc.
 */
#define GNTDEV_DMA_FLAG_WC		(1 << 0)

/*
 * The buffer is backed with memory allocated with dma_alloc_coherent.
 */
#define GNTDEV_DMA_FLAG_COHERENT	(1 << 1)

/*
 * Create a dma-buf [1] from grant references @refs of count @count provided
 * by the foreign domain @domid with flags @flags.
 *
 * By default dma-buf is backed by system memory pages, but by providing
 * one of the GNTDEV_DMA_FLAG_XXX flags it can also be created as
 * a DMA write-combine or coherent buffer, e.g. allocated with dma_alloc_wc/
 * dma_alloc_coherent.
 *
 * Returns 0 if dma-buf was successfully created and the corresponding
 * dma-buf's file descriptor is returned in @fd.
 *
 * [1] Documentation/driver-api/dma-buf.rst
 */

#define IOCTL_GNTDEV_DMABUF_EXP_FROM_REFS \
	_IOC(_IOC_NONE, 'G', 9, \
	     sizeof(struct ioctl_gntdev_dmabuf_exp_from_refs))
struct ioctl_gntdev_dmabuf_exp_from_refs {
	/* IN parameters. */
	/* Specific options for this dma-buf: see GNTDEV_DMA_FLAG_XXX. */
	__u32 flags;
	/* Number of grant references in @refs array. */
	__u32 count;
	/* OUT parameters. */
	/* File descriptor of the dma-buf. */
	__u32 fd;
	/* The domain ID of the grant references to be mapped. */
	__u32 domid;
	/* Variable IN parameter. */
	/* Array of grant references of size @count. */
	__u32 refs[1];
};

/*
 * This will block until the dma-buf with the file descriptor @fd is
 * released. This is only valid for buffers created with
 * IOCTL_GNTDEV_DMABUF_EXP_FROM_REFS.
 *
 * If within @wait_to_ms milliseconds the buffer is not released
 * then -ETIMEDOUT error is returned.
 * If the buffer with the file descriptor @fd does not exist or has already
 * been released, then -ENOENT is returned. For valid file descriptors
 * this must not be treated as error.
 */
#define IOCTL_GNTDEV_DMABUF_EXP_WAIT_RELEASED \
	_IOC(_IOC_NONE, 'G', 10, \
	     sizeof(struct ioctl_gntdev_dmabuf_exp_wait_released))
struct ioctl_gntdev_dmabuf_exp_wait_released {
	/* IN parameters */
	__u32 fd;
	__u32 wait_to_ms;
};

/*
 * Import a dma-buf with file descriptor @fd and export granted references
 * to the pages of that dma-buf into array @refs of size @count.
 */
#define IOCTL_GNTDEV_DMABUF_IMP_TO_REFS \
	_IOC(_IOC_NONE, 'G', 11, \
	     sizeof(struct ioctl_gntdev_dmabuf_imp_to_refs))
struct ioctl_gntdev_dmabuf_imp_to_refs {
	/* IN parameters. */
	/* File descriptor of the dma-buf. */
	__u32 fd;
	/* Number of grant references in @refs array. */
	__u32 count;
	/* The domain ID for which references to be granted. */
	__u32 domid;
	/* Reserved - must be zero. */
	__u32 reserved;
	/* OUT parameters. */
	/* Array of grant references of size @count. */
	__u32 refs[1];
};

/*
 * This will close all references to the imported buffer with file descriptor
 * @fd, so it can be released by the owner. This is only valid for buffers
 * created with IOCTL_GNTDEV_DMABUF_IMP_TO_REFS.
 */
#define IOCTL_GNTDEV_DMABUF_IMP_RELEASE \
	_IOC(_IOC_NONE, 'G', 12, \
	     sizeof(struct ioctl_gntdev_dmabuf_imp_release))
struct ioctl_gntdev_dmabuf_imp_release {
	/* IN parameters */
	__u32 fd;
	__u32 reserved;
};

#endif /* __LINUX_PUBLIC_GNTDEV_H__ */
