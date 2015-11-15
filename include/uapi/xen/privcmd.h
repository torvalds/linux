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

#endif /* __LINUX_PUBLIC_PRIVCMD_H__ */
