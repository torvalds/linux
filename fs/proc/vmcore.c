/*
 *	fs/proc/vmcore.c Interface for accessing the crash
 * 				 dump from the system's previous life.
 * 	Heavily borrowed from fs/proc/kcore.c
 *	Created by: Hariprasad Nellitheertha (hari@in.ibm.com)
 *	Copyright (C) IBM Corporation, 2004. All rights reserved
 *
 */

#include <linux/mm.h>
#include <linux/kcore.h>
#include <linux/user.h>
#include <linux/elf.h>
#include <linux/elfcore.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/printk.h>
#include <linux/bootmem.h>
#include <linux/init.h>
#include <linux/crash_dump.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include "internal.h"

/* List representing chunks of contiguous memory areas and their offsets in
 * vmcore file.
 */
static LIST_HEAD(vmcore_list);

/* Stores the pointer to the buffer containing kernel elf core headers. */
static char *elfcorebuf;
static size_t elfcorebuf_sz;
static size_t elfcorebuf_sz_orig;

static char *elfnotes_buf;
static size_t elfnotes_sz;

/* Total size of vmcore file. */
static u64 vmcore_size;

static struct proc_dir_entry *proc_vmcore = NULL;

/*
 * Returns > 0 for RAM pages, 0 for non-RAM pages, < 0 on error
 * The called function has to take care of module refcounting.
 */
static int (*oldmem_pfn_is_ram)(unsigned long pfn);

int register_oldmem_pfn_is_ram(int (*fn)(unsigned long pfn))
{
	if (oldmem_pfn_is_ram)
		return -EBUSY;
	oldmem_pfn_is_ram = fn;
	return 0;
}
EXPORT_SYMBOL_GPL(register_oldmem_pfn_is_ram);

void unregister_oldmem_pfn_is_ram(void)
{
	oldmem_pfn_is_ram = NULL;
	wmb();
}
EXPORT_SYMBOL_GPL(unregister_oldmem_pfn_is_ram);

static int pfn_is_ram(unsigned long pfn)
{
	int (*fn)(unsigned long pfn);
	/* pfn is ram unless fn() checks pagetype */
	int ret = 1;

	/*
	 * Ask hypervisor if the pfn is really ram.
	 * A ballooned page contains no data and reading from such a page
	 * will cause high load in the hypervisor.
	 */
	fn = oldmem_pfn_is_ram;
	if (fn)
		ret = fn(pfn);

	return ret;
}

/* Reads a page from the oldmem device from given offset. */
static ssize_t read_from_oldmem(char *buf, size_t count,
				u64 *ppos, int userbuf)
{
	unsigned long pfn, offset;
	size_t nr_bytes;
	ssize_t read = 0, tmp;

	if (!count)
		return 0;

	offset = (unsigned long)(*ppos % PAGE_SIZE);
	pfn = (unsigned long)(*ppos / PAGE_SIZE);

	do {
		if (count > (PAGE_SIZE - offset))
			nr_bytes = PAGE_SIZE - offset;
		else
			nr_bytes = count;

		/* If pfn is not ram, return zeros for sparse dump files */
		if (pfn_is_ram(pfn) == 0)
			memset(buf, 0, nr_bytes);
		else {
			tmp = copy_oldmem_page(pfn, buf, nr_bytes,
						offset, userbuf);
			if (tmp < 0)
				return tmp;
		}
		*ppos += nr_bytes;
		count -= nr_bytes;
		buf += nr_bytes;
		read += nr_bytes;
		++pfn;
		offset = 0;
	} while (count);

	return read;
}

/* Read from the ELF header and then the crash dump. On error, negative value is
 * returned otherwise number of bytes read are returned.
 */
static ssize_t read_vmcore(struct file *file, char __user *buffer,
				size_t buflen, loff_t *fpos)
{
	ssize_t acc = 0, tmp;
	size_t tsz;
	u64 start;
	struct vmcore *m = NULL;

	if (buflen == 0 || *fpos >= vmcore_size)
		return 0;

	/* trim buflen to not go beyond EOF */
	if (buflen > vmcore_size - *fpos)
		buflen = vmcore_size - *fpos;

	/* Read ELF core header */
	if (*fpos < elfcorebuf_sz) {
		tsz = min(elfcorebuf_sz - (size_t)*fpos, buflen);
		if (copy_to_user(buffer, elfcorebuf + *fpos, tsz))
			return -EFAULT;
		buflen -= tsz;
		*fpos += tsz;
		buffer += tsz;
		acc += tsz;

		/* leave now if filled buffer already */
		if (buflen == 0)
			return acc;
	}

	/* Read Elf note segment */
	if (*fpos < elfcorebuf_sz + elfnotes_sz) {
		void *kaddr;

		tsz = min(elfcorebuf_sz + elfnotes_sz - (size_t)*fpos, buflen);
		kaddr = elfnotes_buf + *fpos - elfcorebuf_sz;
		if (copy_to_user(buffer, kaddr, tsz))
			return -EFAULT;
		buflen -= tsz;
		*fpos += tsz;
		buffer += tsz;
		acc += tsz;

		/* leave now if filled buffer already */
		if (buflen == 0)
			return acc;
	}

	list_for_each_entry(m, &vmcore_list, list) {
		if (*fpos < m->offset + m->size) {
			tsz = min_t(size_t, m->offset + m->size - *fpos, buflen);
			start = m->paddr + *fpos - m->offset;
			tmp = read_from_oldmem(buffer, tsz, &start, 1);
			if (tmp < 0)
				return tmp;
			buflen -= tsz;
			*fpos += tsz;
			buffer += tsz;
			acc += tsz;

			/* leave now if filled buffer already */
			if (buflen == 0)
				return acc;
		}
	}

	return acc;
}

static const struct file_operations proc_vmcore_operations = {
	.read		= read_vmcore,
	.llseek		= default_llseek,
};

static struct vmcore* __init get_new_element(void)
{
	return kzalloc(sizeof(struct vmcore), GFP_KERNEL);
}

static u64 __init get_vmcore_size_elf64(char *elfptr, size_t elfsz)
{
	int i;
	u64 size;
	Elf64_Ehdr *ehdr_ptr;
	Elf64_Phdr *phdr_ptr;

	ehdr_ptr = (Elf64_Ehdr *)elfptr;
	phdr_ptr = (Elf64_Phdr*)(elfptr + sizeof(Elf64_Ehdr));
	size = elfsz;
	for (i = 0; i < ehdr_ptr->e_phnum; i++) {
		size += phdr_ptr->p_memsz;
		phdr_ptr++;
	}
	return size;
}

static u64 __init get_vmcore_size_elf32(char *elfptr, size_t elfsz)
{
	int i;
	u64 size;
	Elf32_Ehdr *ehdr_ptr;
	Elf32_Phdr *phdr_ptr;

	ehdr_ptr = (Elf32_Ehdr *)elfptr;
	phdr_ptr = (Elf32_Phdr*)(elfptr + sizeof(Elf32_Ehdr));
	size = elfsz;
	for (i = 0; i < ehdr_ptr->e_phnum; i++) {
		size += phdr_ptr->p_memsz;
		phdr_ptr++;
	}
	return size;
}

/**
 * update_note_header_size_elf64 - update p_memsz member of each PT_NOTE entry
 *
 * @ehdr_ptr: ELF header
 *
 * This function updates p_memsz member of each PT_NOTE entry in the
 * program header table pointed to by @ehdr_ptr to real size of ELF
 * note segment.
 */
static int __init update_note_header_size_elf64(const Elf64_Ehdr *ehdr_ptr)
{
	int i, rc=0;
	Elf64_Phdr *phdr_ptr;
	Elf64_Nhdr *nhdr_ptr;

	phdr_ptr = (Elf64_Phdr *)(ehdr_ptr + 1);
	for (i = 0; i < ehdr_ptr->e_phnum; i++, phdr_ptr++) {
		void *notes_section;
		u64 offset, max_sz, sz, real_sz = 0;
		if (phdr_ptr->p_type != PT_NOTE)
			continue;
		max_sz = phdr_ptr->p_memsz;
		offset = phdr_ptr->p_offset;
		notes_section = kmalloc(max_sz, GFP_KERNEL);
		if (!notes_section)
			return -ENOMEM;
		rc = read_from_oldmem(notes_section, max_sz, &offset, 0);
		if (rc < 0) {
			kfree(notes_section);
			return rc;
		}
		nhdr_ptr = notes_section;
		while (real_sz < max_sz) {
			if (nhdr_ptr->n_namesz == 0)
				break;
			sz = sizeof(Elf64_Nhdr) +
				((nhdr_ptr->n_namesz + 3) & ~3) +
				((nhdr_ptr->n_descsz + 3) & ~3);
			real_sz += sz;
			nhdr_ptr = (Elf64_Nhdr*)((char*)nhdr_ptr + sz);
		}
		kfree(notes_section);
		phdr_ptr->p_memsz = real_sz;
	}

	return 0;
}

/**
 * get_note_number_and_size_elf64 - get the number of PT_NOTE program
 * headers and sum of real size of their ELF note segment headers and
 * data.
 *
 * @ehdr_ptr: ELF header
 * @nr_ptnote: buffer for the number of PT_NOTE program headers
 * @sz_ptnote: buffer for size of unique PT_NOTE program header
 *
 * This function is used to merge multiple PT_NOTE program headers
 * into a unique single one. The resulting unique entry will have
 * @sz_ptnote in its phdr->p_mem.
 *
 * It is assumed that program headers with PT_NOTE type pointed to by
 * @ehdr_ptr has already been updated by update_note_header_size_elf64
 * and each of PT_NOTE program headers has actual ELF note segment
 * size in its p_memsz member.
 */
static int __init get_note_number_and_size_elf64(const Elf64_Ehdr *ehdr_ptr,
						 int *nr_ptnote, u64 *sz_ptnote)
{
	int i;
	Elf64_Phdr *phdr_ptr;

	*nr_ptnote = *sz_ptnote = 0;

	phdr_ptr = (Elf64_Phdr *)(ehdr_ptr + 1);
	for (i = 0; i < ehdr_ptr->e_phnum; i++, phdr_ptr++) {
		if (phdr_ptr->p_type != PT_NOTE)
			continue;
		*nr_ptnote += 1;
		*sz_ptnote += phdr_ptr->p_memsz;
	}

	return 0;
}

/**
 * copy_notes_elf64 - copy ELF note segments in a given buffer
 *
 * @ehdr_ptr: ELF header
 * @notes_buf: buffer into which ELF note segments are copied
 *
 * This function is used to copy ELF note segment in the 1st kernel
 * into the buffer @notes_buf in the 2nd kernel. It is assumed that
 * size of the buffer @notes_buf is equal to or larger than sum of the
 * real ELF note segment headers and data.
 *
 * It is assumed that program headers with PT_NOTE type pointed to by
 * @ehdr_ptr has already been updated by update_note_header_size_elf64
 * and each of PT_NOTE program headers has actual ELF note segment
 * size in its p_memsz member.
 */
static int __init copy_notes_elf64(const Elf64_Ehdr *ehdr_ptr, char *notes_buf)
{
	int i, rc=0;
	Elf64_Phdr *phdr_ptr;

	phdr_ptr = (Elf64_Phdr*)(ehdr_ptr + 1);

	for (i = 0; i < ehdr_ptr->e_phnum; i++, phdr_ptr++) {
		u64 offset;
		if (phdr_ptr->p_type != PT_NOTE)
			continue;
		offset = phdr_ptr->p_offset;
		rc = read_from_oldmem(notes_buf, phdr_ptr->p_memsz, &offset, 0);
		if (rc < 0)
			return rc;
		notes_buf += phdr_ptr->p_memsz;
	}

	return 0;
}

/* Merges all the PT_NOTE headers into one. */
static int __init merge_note_headers_elf64(char *elfptr, size_t *elfsz,
					   char **notes_buf, size_t *notes_sz)
{
	int i, nr_ptnote=0, rc=0;
	char *tmp;
	Elf64_Ehdr *ehdr_ptr;
	Elf64_Phdr phdr;
	u64 phdr_sz = 0, note_off;
	struct vm_struct *vm;

	ehdr_ptr = (Elf64_Ehdr *)elfptr;

	rc = update_note_header_size_elf64(ehdr_ptr);
	if (rc < 0)
		return rc;

	rc = get_note_number_and_size_elf64(ehdr_ptr, &nr_ptnote, &phdr_sz);
	if (rc < 0)
		return rc;

	*notes_sz = roundup(phdr_sz, PAGE_SIZE);
	*notes_buf = vzalloc(*notes_sz);
	if (!*notes_buf)
		return -ENOMEM;

	/*
	 * Allow users to remap ELF note segment buffer on vmalloc memory using
	 * remap_vmalloc_range.()
	 */
	vm = find_vm_area(*notes_buf);
	BUG_ON(!vm);
	vm->flags |= VM_USERMAP;

	rc = copy_notes_elf64(ehdr_ptr, *notes_buf);
	if (rc < 0)
		return rc;

	/* Prepare merged PT_NOTE program header. */
	phdr.p_type    = PT_NOTE;
	phdr.p_flags   = 0;
	note_off = sizeof(Elf64_Ehdr) +
			(ehdr_ptr->e_phnum - nr_ptnote +1) * sizeof(Elf64_Phdr);
	phdr.p_offset  = roundup(note_off, PAGE_SIZE);
	phdr.p_vaddr   = phdr.p_paddr = 0;
	phdr.p_filesz  = phdr.p_memsz = phdr_sz;
	phdr.p_align   = 0;

	/* Add merged PT_NOTE program header*/
	tmp = elfptr + sizeof(Elf64_Ehdr);
	memcpy(tmp, &phdr, sizeof(phdr));
	tmp += sizeof(phdr);

	/* Remove unwanted PT_NOTE program headers. */
	i = (nr_ptnote - 1) * sizeof(Elf64_Phdr);
	*elfsz = *elfsz - i;
	memmove(tmp, tmp+i, ((*elfsz)-sizeof(Elf64_Ehdr)-sizeof(Elf64_Phdr)));
	memset(elfptr + *elfsz, 0, i);
	*elfsz = roundup(*elfsz, PAGE_SIZE);

	/* Modify e_phnum to reflect merged headers. */
	ehdr_ptr->e_phnum = ehdr_ptr->e_phnum - nr_ptnote + 1;

	return 0;
}

/**
 * update_note_header_size_elf32 - update p_memsz member of each PT_NOTE entry
 *
 * @ehdr_ptr: ELF header
 *
 * This function updates p_memsz member of each PT_NOTE entry in the
 * program header table pointed to by @ehdr_ptr to real size of ELF
 * note segment.
 */
static int __init update_note_header_size_elf32(const Elf32_Ehdr *ehdr_ptr)
{
	int i, rc=0;
	Elf32_Phdr *phdr_ptr;
	Elf32_Nhdr *nhdr_ptr;

	phdr_ptr = (Elf32_Phdr *)(ehdr_ptr + 1);
	for (i = 0; i < ehdr_ptr->e_phnum; i++, phdr_ptr++) {
		void *notes_section;
		u64 offset, max_sz, sz, real_sz = 0;
		if (phdr_ptr->p_type != PT_NOTE)
			continue;
		max_sz = phdr_ptr->p_memsz;
		offset = phdr_ptr->p_offset;
		notes_section = kmalloc(max_sz, GFP_KERNEL);
		if (!notes_section)
			return -ENOMEM;
		rc = read_from_oldmem(notes_section, max_sz, &offset, 0);
		if (rc < 0) {
			kfree(notes_section);
			return rc;
		}
		nhdr_ptr = notes_section;
		while (real_sz < max_sz) {
			if (nhdr_ptr->n_namesz == 0)
				break;
			sz = sizeof(Elf32_Nhdr) +
				((nhdr_ptr->n_namesz + 3) & ~3) +
				((nhdr_ptr->n_descsz + 3) & ~3);
			real_sz += sz;
			nhdr_ptr = (Elf32_Nhdr*)((char*)nhdr_ptr + sz);
		}
		kfree(notes_section);
		phdr_ptr->p_memsz = real_sz;
	}

	return 0;
}

/**
 * get_note_number_and_size_elf32 - get the number of PT_NOTE program
 * headers and sum of real size of their ELF note segment headers and
 * data.
 *
 * @ehdr_ptr: ELF header
 * @nr_ptnote: buffer for the number of PT_NOTE program headers
 * @sz_ptnote: buffer for size of unique PT_NOTE program header
 *
 * This function is used to merge multiple PT_NOTE program headers
 * into a unique single one. The resulting unique entry will have
 * @sz_ptnote in its phdr->p_mem.
 *
 * It is assumed that program headers with PT_NOTE type pointed to by
 * @ehdr_ptr has already been updated by update_note_header_size_elf32
 * and each of PT_NOTE program headers has actual ELF note segment
 * size in its p_memsz member.
 */
static int __init get_note_number_and_size_elf32(const Elf32_Ehdr *ehdr_ptr,
						 int *nr_ptnote, u64 *sz_ptnote)
{
	int i;
	Elf32_Phdr *phdr_ptr;

	*nr_ptnote = *sz_ptnote = 0;

	phdr_ptr = (Elf32_Phdr *)(ehdr_ptr + 1);
	for (i = 0; i < ehdr_ptr->e_phnum; i++, phdr_ptr++) {
		if (phdr_ptr->p_type != PT_NOTE)
			continue;
		*nr_ptnote += 1;
		*sz_ptnote += phdr_ptr->p_memsz;
	}

	return 0;
}

/**
 * copy_notes_elf32 - copy ELF note segments in a given buffer
 *
 * @ehdr_ptr: ELF header
 * @notes_buf: buffer into which ELF note segments are copied
 *
 * This function is used to copy ELF note segment in the 1st kernel
 * into the buffer @notes_buf in the 2nd kernel. It is assumed that
 * size of the buffer @notes_buf is equal to or larger than sum of the
 * real ELF note segment headers and data.
 *
 * It is assumed that program headers with PT_NOTE type pointed to by
 * @ehdr_ptr has already been updated by update_note_header_size_elf32
 * and each of PT_NOTE program headers has actual ELF note segment
 * size in its p_memsz member.
 */
static int __init copy_notes_elf32(const Elf32_Ehdr *ehdr_ptr, char *notes_buf)
{
	int i, rc=0;
	Elf32_Phdr *phdr_ptr;

	phdr_ptr = (Elf32_Phdr*)(ehdr_ptr + 1);

	for (i = 0; i < ehdr_ptr->e_phnum; i++, phdr_ptr++) {
		u64 offset;
		if (phdr_ptr->p_type != PT_NOTE)
			continue;
		offset = phdr_ptr->p_offset;
		rc = read_from_oldmem(notes_buf, phdr_ptr->p_memsz, &offset, 0);
		if (rc < 0)
			return rc;
		notes_buf += phdr_ptr->p_memsz;
	}

	return 0;
}

/* Merges all the PT_NOTE headers into one. */
static int __init merge_note_headers_elf32(char *elfptr, size_t *elfsz,
					   char **notes_buf, size_t *notes_sz)
{
	int i, nr_ptnote=0, rc=0;
	char *tmp;
	Elf32_Ehdr *ehdr_ptr;
	Elf32_Phdr phdr;
	u64 phdr_sz = 0, note_off;
	struct vm_struct *vm;

	ehdr_ptr = (Elf32_Ehdr *)elfptr;

	rc = update_note_header_size_elf32(ehdr_ptr);
	if (rc < 0)
		return rc;

	rc = get_note_number_and_size_elf32(ehdr_ptr, &nr_ptnote, &phdr_sz);
	if (rc < 0)
		return rc;

	*notes_sz = roundup(phdr_sz, PAGE_SIZE);
	*notes_buf = vzalloc(*notes_sz);
	if (!*notes_buf)
		return -ENOMEM;

	/*
	 * Allow users to remap ELF note segment buffer on vmalloc memory using
	 * remap_vmalloc_range()
	 */
	vm = find_vm_area(*notes_buf);
	BUG_ON(!vm);
	vm->flags |= VM_USERMAP;

	rc = copy_notes_elf32(ehdr_ptr, *notes_buf);
	if (rc < 0)
		return rc;

	/* Prepare merged PT_NOTE program header. */
	phdr.p_type    = PT_NOTE;
	phdr.p_flags   = 0;
	note_off = sizeof(Elf32_Ehdr) +
			(ehdr_ptr->e_phnum - nr_ptnote +1) * sizeof(Elf32_Phdr);
	phdr.p_offset  = roundup(note_off, PAGE_SIZE);
	phdr.p_vaddr   = phdr.p_paddr = 0;
	phdr.p_filesz  = phdr.p_memsz = phdr_sz;
	phdr.p_align   = 0;

	/* Add merged PT_NOTE program header*/
	tmp = elfptr + sizeof(Elf32_Ehdr);
	memcpy(tmp, &phdr, sizeof(phdr));
	tmp += sizeof(phdr);

	/* Remove unwanted PT_NOTE program headers. */
	i = (nr_ptnote - 1) * sizeof(Elf32_Phdr);
	*elfsz = *elfsz - i;
	memmove(tmp, tmp+i, ((*elfsz)-sizeof(Elf32_Ehdr)-sizeof(Elf32_Phdr)));
	memset(elfptr + *elfsz, 0, i);
	*elfsz = roundup(*elfsz, PAGE_SIZE);

	/* Modify e_phnum to reflect merged headers. */
	ehdr_ptr->e_phnum = ehdr_ptr->e_phnum - nr_ptnote + 1;

	return 0;
}

/* Add memory chunks represented by program headers to vmcore list. Also update
 * the new offset fields of exported program headers. */
static int __init process_ptload_program_headers_elf64(char *elfptr,
						size_t elfsz,
						size_t elfnotes_sz,
						struct list_head *vc_list)
{
	int i;
	Elf64_Ehdr *ehdr_ptr;
	Elf64_Phdr *phdr_ptr;
	loff_t vmcore_off;
	struct vmcore *new;

	ehdr_ptr = (Elf64_Ehdr *)elfptr;
	phdr_ptr = (Elf64_Phdr*)(elfptr + sizeof(Elf64_Ehdr)); /* PT_NOTE hdr */

	/* Skip Elf header, program headers and Elf note segment. */
	vmcore_off = elfsz + elfnotes_sz;

	for (i = 0; i < ehdr_ptr->e_phnum; i++, phdr_ptr++) {
		u64 paddr, start, end, size;

		if (phdr_ptr->p_type != PT_LOAD)
			continue;

		paddr = phdr_ptr->p_offset;
		start = rounddown(paddr, PAGE_SIZE);
		end = roundup(paddr + phdr_ptr->p_memsz, PAGE_SIZE);
		size = end - start;

		/* Add this contiguous chunk of memory to vmcore list.*/
		new = get_new_element();
		if (!new)
			return -ENOMEM;
		new->paddr = start;
		new->size = size;
		list_add_tail(&new->list, vc_list);

		/* Update the program header offset. */
		phdr_ptr->p_offset = vmcore_off + (paddr - start);
		vmcore_off = vmcore_off + size;
	}
	return 0;
}

static int __init process_ptload_program_headers_elf32(char *elfptr,
						size_t elfsz,
						size_t elfnotes_sz,
						struct list_head *vc_list)
{
	int i;
	Elf32_Ehdr *ehdr_ptr;
	Elf32_Phdr *phdr_ptr;
	loff_t vmcore_off;
	struct vmcore *new;

	ehdr_ptr = (Elf32_Ehdr *)elfptr;
	phdr_ptr = (Elf32_Phdr*)(elfptr + sizeof(Elf32_Ehdr)); /* PT_NOTE hdr */

	/* Skip Elf header, program headers and Elf note segment. */
	vmcore_off = elfsz + elfnotes_sz;

	for (i = 0; i < ehdr_ptr->e_phnum; i++, phdr_ptr++) {
		u64 paddr, start, end, size;

		if (phdr_ptr->p_type != PT_LOAD)
			continue;

		paddr = phdr_ptr->p_offset;
		start = rounddown(paddr, PAGE_SIZE);
		end = roundup(paddr + phdr_ptr->p_memsz, PAGE_SIZE);
		size = end - start;

		/* Add this contiguous chunk of memory to vmcore list.*/
		new = get_new_element();
		if (!new)
			return -ENOMEM;
		new->paddr = start;
		new->size = size;
		list_add_tail(&new->list, vc_list);

		/* Update the program header offset */
		phdr_ptr->p_offset = vmcore_off + (paddr - start);
		vmcore_off = vmcore_off + size;
	}
	return 0;
}

/* Sets offset fields of vmcore elements. */
static void __init set_vmcore_list_offsets(size_t elfsz, size_t elfnotes_sz,
					   struct list_head *vc_list)
{
	loff_t vmcore_off;
	struct vmcore *m;

	/* Skip Elf header, program headers and Elf note segment. */
	vmcore_off = elfsz + elfnotes_sz;

	list_for_each_entry(m, vc_list, list) {
		m->offset = vmcore_off;
		vmcore_off += m->size;
	}
}

static void free_elfcorebuf(void)
{
	free_pages((unsigned long)elfcorebuf, get_order(elfcorebuf_sz_orig));
	elfcorebuf = NULL;
	vfree(elfnotes_buf);
	elfnotes_buf = NULL;
}

static int __init parse_crash_elf64_headers(void)
{
	int rc=0;
	Elf64_Ehdr ehdr;
	u64 addr;

	addr = elfcorehdr_addr;

	/* Read Elf header */
	rc = read_from_oldmem((char*)&ehdr, sizeof(Elf64_Ehdr), &addr, 0);
	if (rc < 0)
		return rc;

	/* Do some basic Verification. */
	if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 ||
		(ehdr.e_type != ET_CORE) ||
		!vmcore_elf64_check_arch(&ehdr) ||
		ehdr.e_ident[EI_CLASS] != ELFCLASS64 ||
		ehdr.e_ident[EI_VERSION] != EV_CURRENT ||
		ehdr.e_version != EV_CURRENT ||
		ehdr.e_ehsize != sizeof(Elf64_Ehdr) ||
		ehdr.e_phentsize != sizeof(Elf64_Phdr) ||
		ehdr.e_phnum == 0) {
		pr_warn("Warning: Core image elf header is not sane\n");
		return -EINVAL;
	}

	/* Read in all elf headers. */
	elfcorebuf_sz_orig = sizeof(Elf64_Ehdr) +
				ehdr.e_phnum * sizeof(Elf64_Phdr);
	elfcorebuf_sz = elfcorebuf_sz_orig;
	elfcorebuf = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					      get_order(elfcorebuf_sz_orig));
	if (!elfcorebuf)
		return -ENOMEM;
	addr = elfcorehdr_addr;
	rc = read_from_oldmem(elfcorebuf, elfcorebuf_sz_orig, &addr, 0);
	if (rc < 0)
		goto fail;

	/* Merge all PT_NOTE headers into one. */
	rc = merge_note_headers_elf64(elfcorebuf, &elfcorebuf_sz,
				      &elfnotes_buf, &elfnotes_sz);
	if (rc)
		goto fail;
	rc = process_ptload_program_headers_elf64(elfcorebuf, elfcorebuf_sz,
						  elfnotes_sz, &vmcore_list);
	if (rc)
		goto fail;
	set_vmcore_list_offsets(elfcorebuf_sz, elfnotes_sz, &vmcore_list);
	return 0;
fail:
	free_elfcorebuf();
	return rc;
}

static int __init parse_crash_elf32_headers(void)
{
	int rc=0;
	Elf32_Ehdr ehdr;
	u64 addr;

	addr = elfcorehdr_addr;

	/* Read Elf header */
	rc = read_from_oldmem((char*)&ehdr, sizeof(Elf32_Ehdr), &addr, 0);
	if (rc < 0)
		return rc;

	/* Do some basic Verification. */
	if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 ||
		(ehdr.e_type != ET_CORE) ||
		!elf_check_arch(&ehdr) ||
		ehdr.e_ident[EI_CLASS] != ELFCLASS32||
		ehdr.e_ident[EI_VERSION] != EV_CURRENT ||
		ehdr.e_version != EV_CURRENT ||
		ehdr.e_ehsize != sizeof(Elf32_Ehdr) ||
		ehdr.e_phentsize != sizeof(Elf32_Phdr) ||
		ehdr.e_phnum == 0) {
		pr_warn("Warning: Core image elf header is not sane\n");
		return -EINVAL;
	}

	/* Read in all elf headers. */
	elfcorebuf_sz_orig = sizeof(Elf32_Ehdr) + ehdr.e_phnum * sizeof(Elf32_Phdr);
	elfcorebuf_sz = elfcorebuf_sz_orig;
	elfcorebuf = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					      get_order(elfcorebuf_sz_orig));
	if (!elfcorebuf)
		return -ENOMEM;
	addr = elfcorehdr_addr;
	rc = read_from_oldmem(elfcorebuf, elfcorebuf_sz_orig, &addr, 0);
	if (rc < 0)
		goto fail;

	/* Merge all PT_NOTE headers into one. */
	rc = merge_note_headers_elf32(elfcorebuf, &elfcorebuf_sz,
				      &elfnotes_buf, &elfnotes_sz);
	if (rc)
		goto fail;
	rc = process_ptload_program_headers_elf32(elfcorebuf, elfcorebuf_sz,
						  elfnotes_sz, &vmcore_list);
	if (rc)
		goto fail;
	set_vmcore_list_offsets(elfcorebuf_sz, elfnotes_sz, &vmcore_list);
	return 0;
fail:
	free_elfcorebuf();
	return rc;
}

static int __init parse_crash_elf_headers(void)
{
	unsigned char e_ident[EI_NIDENT];
	u64 addr;
	int rc=0;

	addr = elfcorehdr_addr;
	rc = read_from_oldmem(e_ident, EI_NIDENT, &addr, 0);
	if (rc < 0)
		return rc;
	if (memcmp(e_ident, ELFMAG, SELFMAG) != 0) {
		pr_warn("Warning: Core image elf header not found\n");
		return -EINVAL;
	}

	if (e_ident[EI_CLASS] == ELFCLASS64) {
		rc = parse_crash_elf64_headers();
		if (rc)
			return rc;

		/* Determine vmcore size. */
		vmcore_size = get_vmcore_size_elf64(elfcorebuf, elfcorebuf_sz);
	} else if (e_ident[EI_CLASS] == ELFCLASS32) {
		rc = parse_crash_elf32_headers();
		if (rc)
			return rc;

		/* Determine vmcore size. */
		vmcore_size = get_vmcore_size_elf32(elfcorebuf, elfcorebuf_sz);
	} else {
		pr_warn("Warning: Core image elf header is not sane\n");
		return -EINVAL;
	}
	return 0;
}

/* Init function for vmcore module. */
static int __init vmcore_init(void)
{
	int rc = 0;

	/* If elfcorehdr= has been passed in cmdline, then capture the dump.*/
	if (!(is_vmcore_usable()))
		return rc;
	rc = parse_crash_elf_headers();
	if (rc) {
		pr_warn("Kdump: vmcore not initialized\n");
		return rc;
	}

	proc_vmcore = proc_create("vmcore", S_IRUSR, NULL, &proc_vmcore_operations);
	if (proc_vmcore)
		proc_vmcore->size = vmcore_size;
	return 0;
}
module_init(vmcore_init)

/* Cleanup function for vmcore module. */
void vmcore_cleanup(void)
{
	struct list_head *pos, *next;

	if (proc_vmcore) {
		proc_remove(proc_vmcore);
		proc_vmcore = NULL;
	}

	/* clear the vmcore list. */
	list_for_each_safe(pos, next, &vmcore_list) {
		struct vmcore *m;

		m = list_entry(pos, struct vmcore, list);
		list_del(&m->list);
		kfree(m);
	}
	free_elfcorebuf();
}
EXPORT_SYMBOL_GPL(vmcore_cleanup);
