/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR MIT) */
/******************************************************************************
 * privcmd.h
 *
 * Interface to /proc/xen/privcmd.
 *
 * Copyright (c) 2003-2005, K A Fraser
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

#ifndef __LINUX_PUBLIC_PRIVCMD_H__
#define __LINUX_PUBLIC_PRIVCMD_H__

#include <linux/types.h>
#include <linux/compiler.h>
#include <xen/interface/xen.h>

struct privcmd_hypercall {
	__u64 op;
	__u64 arg[5];
};

struct privcmd_mmap_entry {
	__u64 va;
	/*
	 * This should be a GFN. It's not possible to change the name because
	 * it's exposed to the user-space.
	 */
	__u64 mfn;
	__u64 npages;
};

struct privcmd_mmap {
	int num;
	domid_t dom; /* target domain */
	struct privcmd_mmap_entry __user *entry;
};

struct privcmd_mmapbatch {
	int num;     /* number of pages to populate */
	domid_t dom; /* target domain */
	__u64 addr;  /* virtual address */
	xen_pfn_t __user *arr; /* array of mfns - or'd with
				  PRIVCMD_MMAPBATCH_*_ERROR on err */
};

#define PRIVCMD_MMAPBATCH_MFN_ERROR     0xf0000000U
#define PRIVCMD_MMAPBATCH_PAGED_ERROR   0x80000000U

struct privcmd_mmapbatch_v2 {
	unsigned int num; /* number of pages to populate */
	domid_t dom;      /* target domain */
	__u64 addr;       /* virtual address */
	const xen_pfn_t __user *arr; /* array of mfns */
	int __user *err;  /* array of error codes */
};

struct privcmd_dm_op_buf {
	void __user *uptr;
	size_t size;
};

struct privcmd_dm_op {
	domid_t dom;
	__u16 num;
	const struct privcmd_dm_op_buf __user *ubufs;
};

struct privcmd_mmap_resource {
	domid_t dom;
	__u32 type;
	__u32 id;
	__u32 idx;
	__u64 num;
	__u64 addr;
};

/* For privcmd_irqfd::flags */
#define PRIVCMD_IRQFD_FLAG_DEASSIGN (1 << 0)

struct privcmd_irqfd {
	__u64 dm_op;
	__u32 size; /* Size of structure pointed by dm_op */
	__u32 fd;
	__u32 flags;
	domid_t dom;
	__u8 pad[2];
};

/* For privcmd_ioeventfd::flags */
#define PRIVCMD_IOEVENTFD_FLAG_DEASSIGN (1 << 0)

struct privcmd_ioeventfd {
	__u64 ioreq;
	__u64 ports;
	__u64 addr;
	__u32 addr_len;
	__u32 event_fd;
	__u32 vcpus;
	__u32 vq;
	__u32 flags;
	domid_t dom;
	__u8 pad[2];
};

/*
 * @cmd: IOCTL_PRIVCMD_HYPERCALL
 * @arg: &privcmd_hypercall_t
 * Return: Value returned from execution of the specified hypercall.
 *
 * @cmd: IOCTL_PRIVCMD_MMAPBATCH_V2
 * @arg: &struct privcmd_mmapbatch_v2
 * Return: 0 on success (i.e., arg->err contains valid error codes for
 * each frame).  On an error other than a failed frame remap, -1 is
 * returned and errno is set to EINVAL, EFAULT etc.  As an exception,
 * if the operation was otherwise successful but any frame failed with
 * -ENOENT, then -1 is returned and errno is set to ENOENT.
 */
#define IOCTL_PRIVCMD_HYPERCALL					\
	_IOC(_IOC_NONE, 'P', 0, sizeof(struct privcmd_hypercall))
#define IOCTL_PRIVCMD_MMAP					\
	_IOC(_IOC_NONE, 'P', 2, sizeof(struct privcmd_mmap))
#define IOCTL_PRIVCMD_MMAPBATCH					\
	_IOC(_IOC_NONE, 'P', 3, sizeof(struct privcmd_mmapbatch))
#define IOCTL_PRIVCMD_MMAPBATCH_V2				\
	_IOC(_IOC_NONE, 'P', 4, sizeof(struct privcmd_mmapbatch_v2))
#define IOCTL_PRIVCMD_DM_OP					\
	_IOC(_IOC_NONE, 'P', 5, sizeof(struct privcmd_dm_op))
#define IOCTL_PRIVCMD_RESTRICT					\
	_IOC(_IOC_NONE, 'P', 6, sizeof(domid_t))
#define IOCTL_PRIVCMD_MMAP_RESOURCE				\
	_IOC(_IOC_NONE, 'P', 7, sizeof(struct privcmd_mmap_resource))
#define IOCTL_PRIVCMD_IRQFD					\
	_IOW('P', 8, struct privcmd_irqfd)
#define IOCTL_PRIVCMD_IOEVENTFD					\
	_IOW('P', 9, struct privcmd_ioeventfd)

#endif /* __LINUX_PUBLIC_PRIVCMD_H__ */
