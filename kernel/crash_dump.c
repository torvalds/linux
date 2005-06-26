/*
 *	kernel/crash_dump.c - Memory preserving reboot related code.
 *
 *	Created by: Hariprasad Nellitheertha (hari@in.ibm.com)
 *	Copyright (C) IBM Corporation, 2004. All rights reserved
 */

#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>
#include <linux/crash_dump.h>

#include <asm/io.h>
#include <asm/uaccess.h>

/* Stores the physical address of elf header of crash image. */
unsigned long long elfcorehdr_addr = ELFCORE_ADDR_MAX;

/*
 * Copy a page from "oldmem". For this page, there is no pte mapped
 * in the current kernel. We stitch up a pte, similar to kmap_atomic.
 */
ssize_t copy_oldmem_page(unsigned long pfn, char *buf,
				size_t csize, unsigned long offset, int userbuf)
{
	void *page, *vaddr;

	if (!csize)
		return 0;

	page = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	vaddr = kmap_atomic_pfn(pfn, KM_PTE0);
	copy_page(page, vaddr);
	kunmap_atomic(vaddr, KM_PTE0);

	if (userbuf) {
		if (copy_to_user(buf, (page + offset), csize)) {
			kfree(page);
			return -EFAULT;
		}
	} else {
		memcpy(buf, (page + offset), csize);
	}

	kfree(page);
	return csize;
}
