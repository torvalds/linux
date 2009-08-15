/*
 *	fs/proc/vmcore.c Interface for accessing the crash
 * 				 dump from the system's previous life.
 * 	Heavily borrowed from fs/proc/kcore.c
 *	Created by: Hariprasad Nellitheertha (hari@in.ibm.com)
 *	Copyright (C) IBM Corporation, 2004. All rights reserved
 *
 */

#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/user.h>
#include <linux/elf.h>
#include <linux/elfcore.h>
#include <linux/highmem.h>
#include <linux/bootmem.h>
#include <linux/init.h>
#include <linux/crash_dump.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <asm/io.h>

/* List representing chunks of contiguous memory areas and their offsets in
 * vmcore file.
 */
static LIST_HEAD(vmcore_list);

/* Stores the pointer to the buffer containing kernel elf core headers. */
static char *elfcorebuf;
static size_t elfcorebuf_sz;

/* Total size of vmcore file. */
static u64 vmcore_size;

static struct proc_dir_entry *proc_vmcore = NULL;

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

		tmp = copy_oldmem_page(pfn, buf, nr_bytes, offset, userbuf);
		if (tmp < 0)
			return tmp;
		*ppos += nr_bytes;
		count -= nr_bytes;
		buf += nr_bytes;
		read += nr_bytes;
		++pfn;
		offset = 0;
	} while (count);

	return read;
}

/* Maps vmcore file offset to respective physical address in memroy. */
static u64 map_offset_to_paddr(loff_t offset, struct list_head *vc_list,
					struct vmcore **m_ptr)
{
	struct vmcore *m;
	u64 paddr;

	list_for_each_entry(m, vc_list, list) {
		u64 start, end;
		start = m->offset;
		end = m->offset + m->size - 1;
		if (offset >= start && offset <= end) {
			paddr = m->paddr + offset - start;
			*m_ptr = m;
			return paddr;
		}
	}
	*m_ptr = NULL;
	return 0;
}

/* Read from the ELF header and then the crash dump. On error, negative value is
 * returned otherwise number of bytes read are returned.
 */
static ssize_t read_vmcore(struct file *file, char __user *buffer,
				size_t buflen, loff_t *fpos)
{
	ssize_t acc = 0, tmp;
	size_t tsz;
	u64 start, nr_bytes;
	struct vmcore *curr_m = NULL;

	if (buflen == 0 || *fpos >= vmcore_size)
		return 0;

	/* trim buflen to not go beyond EOF */
	if (buflen > vmcore_size - *fpos)
		buflen = vmcore_size - *fpos;

	/* Read ELF core header */
	if (*fpos < elfcorebuf_sz) {
		tsz = elfcorebuf_sz - *fpos;
		if (buflen < tsz)
			tsz = buflen;
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

	start = map_offset_to_paddr(*fpos, &vmcore_list, &curr_m);
	if (!curr_m)
        	return -EINVAL;
	if ((tsz = (PAGE_SIZE - (start & ~PAGE_MASK))) > buflen)
		tsz = buflen;

	/* Calculate left bytes in current memory segment. */
	nr_bytes = (curr_m->size - (start - curr_m->paddr));
	if (tsz > nr_bytes)
		tsz = nr_bytes;

	while (buflen) {
		tmp = read_from_oldmem(buffer, tsz, &start, 1);
		if (tmp < 0)
			return tmp;
		buflen -= tsz;
		*fpos += tsz;
		buffer += tsz;
		acc += tsz;
		if (start >= (curr_m->paddr + curr_m->size)) {
			if (curr_m->list.next == &vmcore_list)
				return acc;	/*EOF*/
			curr_m = list_entry(curr_m->list.next,
						struct vmcore, list);
			start = curr_m->paddr;
		}
		if ((tsz = (PAGE_SIZE - (start & ~PAGE_MASK))) > buflen)
			tsz = buflen;
		/* Calculate left bytes in current memory segment. */
		nr_bytes = (curr_m->size - (start - curr_m->paddr));
		if (tsz > nr_bytes)
			tsz = nr_bytes;
	}
	return acc;
}

static const struct file_operations proc_vmcore_operations = {
	.read		= read_vmcore,
};

static struct vmcore* __init get_new_element(void)
{
	return kzalloc(sizeof(struct vmcore), GFP_KERNEL);
}

static u64 __init get_vmcore_size_elf64(char *elfptr)
{
	int i;
	u64 size;
	Elf64_Ehdr *ehdr_ptr;
	Elf64_Phdr *phdr_ptr;

	ehdr_ptr = (Elf64_Ehdr *)elfptr;
	phdr_ptr = (Elf64_Phdr*)(elfptr + sizeof(Elf64_Ehdr));
	size = sizeof(Elf64_Ehdr) + ((ehdr_ptr->e_phnum) * sizeof(Elf64_Phdr));
	for (i = 0; i < ehdr_ptr->e_phnum; i++) {
		size += phdr_ptr->p_memsz;
		phdr_ptr++;
	}
	return size;
}

static u64 __init get_vmcore_size_elf32(char *elfptr)
{
	int i;
	u64 size;
	Elf32_Ehdr *ehdr_ptr;
	Elf32_Phdr *phdr_ptr;

	ehdr_ptr = (Elf32_Ehdr *)elfptr;
	phdr_ptr = (Elf32_Phdr*)(elfptr + sizeof(Elf32_Ehdr));
	size = sizeof(Elf32_Ehdr) + ((ehdr_ptr->e_phnum) * sizeof(Elf32_Phdr));
	for (i = 0; i < ehdr_ptr->e_phnum; i++) {
		size += phdr_ptr->p_memsz;
		phdr_ptr++;
	}
	return size;
}

/* Merges all the PT_NOTE headers into one. */
static int __init merge_note_headers_elf64(char *elfptr, size_t *elfsz,
						struct list_head *vc_list)
{
	int i, nr_ptnote=0, rc=0;
	char *tmp;
	Elf64_Ehdr *ehdr_ptr;
	Elf64_Phdr phdr, *phdr_ptr;
	Elf64_Nhdr *nhdr_ptr;
	u64 phdr_sz = 0, note_off;

	ehdr_ptr = (Elf64_Ehdr *)elfptr;
	phdr_ptr = (Elf64_Phdr*)(elfptr + sizeof(Elf64_Ehdr));
	for (i = 0; i < ehdr_ptr->e_phnum; i++, phdr_ptr++) {
		int j;
		void *notes_section;
		struct vmcore *new;
		u64 offset, max_sz, sz, real_sz = 0;
		if (phdr_ptr->p_type != PT_NOTE)
			continue;
		nr_ptnote++;
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
		for (j = 0; j < max_sz; j += sz) {
			if (nhdr_ptr->n_namesz == 0)
				break;
			sz = sizeof(Elf64_Nhdr) +
				((nhdr_ptr->n_namesz + 3) & ~3) +
				((nhdr_ptr->n_descsz + 3) & ~3);
			real_sz += sz;
			nhdr_ptr = (Elf64_Nhdr*)((char*)nhdr_ptr + sz);
		}

		/* Add this contiguous chunk of notes section to vmcore list.*/
		new = get_new_element();
		if (!new) {
			kfree(notes_section);
			return -ENOMEM;
		}
		new->paddr = phdr_ptr->p_offset;
		new->size = real_sz;
		list_add_tail(&new->list, vc_list);
		phdr_sz += real_sz;
		kfree(notes_section);
	}

	/* Prepare merged PT_NOTE program header. */
	phdr.p_type    = PT_NOTE;
	phdr.p_flags   = 0;
	note_off = sizeof(Elf64_Ehdr) +
			(ehdr_ptr->e_phnum - nr_ptnote +1) * sizeof(Elf64_Phdr);
	phdr.p_offset  = note_off;
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

	/* Modify e_phnum to reflect merged headers. */
	ehdr_ptr->e_phnum = ehdr_ptr->e_phnum - nr_ptnote + 1;

	return 0;
}

/* Merges all the PT_NOTE headers into one. */
static int __init merge_note_headers_elf32(char *elfptr, size_t *elfsz,
						struct list_head *vc_list)
{
	int i, nr_ptnote=0, rc=0;
	char *tmp;
	Elf32_Ehdr *ehdr_ptr;
	Elf32_Phdr phdr, *phdr_ptr;
	Elf32_Nhdr *nhdr_ptr;
	u64 phdr_sz = 0, note_off;

	ehdr_ptr = (Elf32_Ehdr *)elfptr;
	phdr_ptr = (Elf32_Phdr*)(elfptr + sizeof(Elf32_Ehdr));
	for (i = 0; i < ehdr_ptr->e_phnum; i++, phdr_ptr++) {
		int j;
		void *notes_section;
		struct vmcore *new;
		u64 offset, max_sz, sz, real_sz = 0;
		if (phdr_ptr->p_type != PT_NOTE)
			continue;
		nr_ptnote++;
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
		for (j = 0; j < max_sz; j += sz) {
			if (nhdr_ptr->n_namesz == 0)
				break;
			sz = sizeof(Elf32_Nhdr) +
				((nhdr_ptr->n_namesz + 3) & ~3) +
				((nhdr_ptr->n_descsz + 3) & ~3);
			real_sz += sz;
			nhdr_ptr = (Elf32_Nhdr*)((char*)nhdr_ptr + sz);
		}

		/* Add this contiguous chunk of notes section to vmcore list.*/
		new = get_new_element();
		if (!new) {
			kfree(notes_section);
			return -ENOMEM;
		}
		new->paddr = phdr_ptr->p_offset;
		new->size = real_sz;
		list_add_tail(&new->list, vc_list);
		phdr_sz += real_sz;
		kfree(notes_section);
	}

	/* Prepare merged PT_NOTE program header. */
	phdr.p_type    = PT_NOTE;
	phdr.p_flags   = 0;
	note_off = sizeof(Elf32_Ehdr) +
			(ehdr_ptr->e_phnum - nr_ptnote +1) * sizeof(Elf32_Phdr);
	phdr.p_offset  = note_off;
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

	/* Modify e_phnum to reflect merged headers. */
	ehdr_ptr->e_phnum = ehdr_ptr->e_phnum - nr_ptnote + 1;

	return 0;
}

/* Add memory chunks represented by program headers to vmcore list. Also update
 * the new offset fields of exported program headers. */
static int __init process_ptload_program_headers_elf64(char *elfptr,
						size_t elfsz,
						struct list_head *vc_list)
{
	int i;
	Elf64_Ehdr *ehdr_ptr;
	Elf64_Phdr *phdr_ptr;
	loff_t vmcore_off;
	struct vmcore *new;

	ehdr_ptr = (Elf64_Ehdr *)elfptr;
	phdr_ptr = (Elf64_Phdr*)(elfptr + sizeof(Elf64_Ehdr)); /* PT_NOTE hdr */

	/* First program header is PT_NOTE header. */
	vmcore_off = sizeof(Elf64_Ehdr) +
			(ehdr_ptr->e_phnum) * sizeof(Elf64_Phdr) +
			phdr_ptr->p_memsz; /* Note sections */

	for (i = 0; i < ehdr_ptr->e_phnum; i++, phdr_ptr++) {
		if (phdr_ptr->p_type != PT_LOAD)
			continue;

		/* Add this contiguous chunk of memory to vmcore list.*/
		new = get_new_element();
		if (!new)
			return -ENOMEM;
		new->paddr = phdr_ptr->p_offset;
		new->size = phdr_ptr->p_memsz;
		list_add_tail(&new->list, vc_list);

		/* Update the program header offset. */
		phdr_ptr->p_offset = vmcore_off;
		vmcore_off = vmcore_off + phdr_ptr->p_memsz;
	}
	return 0;
}

static int __init process_ptload_program_headers_elf32(char *elfptr,
						size_t elfsz,
						struct list_head *vc_list)
{
	int i;
	Elf32_Ehdr *ehdr_ptr;
	Elf32_Phdr *phdr_ptr;
	loff_t vmcore_off;
	struct vmcore *new;

	ehdr_ptr = (Elf32_Ehdr *)elfptr;
	phdr_ptr = (Elf32_Phdr*)(elfptr + sizeof(Elf32_Ehdr)); /* PT_NOTE hdr */

	/* First program header is PT_NOTE header. */
	vmcore_off = sizeof(Elf32_Ehdr) +
			(ehdr_ptr->e_phnum) * sizeof(Elf32_Phdr) +
			phdr_ptr->p_memsz; /* Note sections */

	for (i = 0; i < ehdr_ptr->e_phnum; i++, phdr_ptr++) {
		if (phdr_ptr->p_type != PT_LOAD)
			continue;

		/* Add this contiguous chunk of memory to vmcore list.*/
		new = get_new_element();
		if (!new)
			return -ENOMEM;
		new->paddr = phdr_ptr->p_offset;
		new->size = phdr_ptr->p_memsz;
		list_add_tail(&new->list, vc_list);

		/* Update the program header offset */
		phdr_ptr->p_offset = vmcore_off;
		vmcore_off = vmcore_off + phdr_ptr->p_memsz;
	}
	return 0;
}

/* Sets offset fields of vmcore elements. */
static void __init set_vmcore_list_offsets_elf64(char *elfptr,
						struct list_head *vc_list)
{
	loff_t vmcore_off;
	Elf64_Ehdr *ehdr_ptr;
	struct vmcore *m;

	ehdr_ptr = (Elf64_Ehdr *)elfptr;

	/* Skip Elf header and program headers. */
	vmcore_off = sizeof(Elf64_Ehdr) +
			(ehdr_ptr->e_phnum) * sizeof(Elf64_Phdr);

	list_for_each_entry(m, vc_list, list) {
		m->offset = vmcore_off;
		vmcore_off += m->size;
	}
}

/* Sets offset fields of vmcore elements. */
static void __init set_vmcore_list_offsets_elf32(char *elfptr,
						struct list_head *vc_list)
{
	loff_t vmcore_off;
	Elf32_Ehdr *ehdr_ptr;
	struct vmcore *m;

	ehdr_ptr = (Elf32_Ehdr *)elfptr;

	/* Skip Elf header and program headers. */
	vmcore_off = sizeof(Elf32_Ehdr) +
			(ehdr_ptr->e_phnum) * sizeof(Elf32_Phdr);

	list_for_each_entry(m, vc_list, list) {
		m->offset = vmcore_off;
		vmcore_off += m->size;
	}
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
		!vmcore_elf_check_arch(&ehdr) ||
		ehdr.e_ident[EI_CLASS] != ELFCLASS64 ||
		ehdr.e_ident[EI_VERSION] != EV_CURRENT ||
		ehdr.e_version != EV_CURRENT ||
		ehdr.e_ehsize != sizeof(Elf64_Ehdr) ||
		ehdr.e_phentsize != sizeof(Elf64_Phdr) ||
		ehdr.e_phnum == 0) {
		printk(KERN_WARNING "Warning: Core image elf header is not"
					"sane\n");
		return -EINVAL;
	}

	/* Read in all elf headers. */
	elfcorebuf_sz = sizeof(Elf64_Ehdr) + ehdr.e_phnum * sizeof(Elf64_Phdr);
	elfcorebuf = kmalloc(elfcorebuf_sz, GFP_KERNEL);
	if (!elfcorebuf)
		return -ENOMEM;
	addr = elfcorehdr_addr;
	rc = read_from_oldmem(elfcorebuf, elfcorebuf_sz, &addr, 0);
	if (rc < 0) {
		kfree(elfcorebuf);
		return rc;
	}

	/* Merge all PT_NOTE headers into one. */
	rc = merge_note_headers_elf64(elfcorebuf, &elfcorebuf_sz, &vmcore_list);
	if (rc) {
		kfree(elfcorebuf);
		return rc;
	}
	rc = process_ptload_program_headers_elf64(elfcorebuf, elfcorebuf_sz,
							&vmcore_list);
	if (rc) {
		kfree(elfcorebuf);
		return rc;
	}
	set_vmcore_list_offsets_elf64(elfcorebuf, &vmcore_list);
	return 0;
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
		printk(KERN_WARNING "Warning: Core image elf header is not"
					"sane\n");
		return -EINVAL;
	}

	/* Read in all elf headers. */
	elfcorebuf_sz = sizeof(Elf32_Ehdr) + ehdr.e_phnum * sizeof(Elf32_Phdr);
	elfcorebuf = kmalloc(elfcorebuf_sz, GFP_KERNEL);
	if (!elfcorebuf)
		return -ENOMEM;
	addr = elfcorehdr_addr;
	rc = read_from_oldmem(elfcorebuf, elfcorebuf_sz, &addr, 0);
	if (rc < 0) {
		kfree(elfcorebuf);
		return rc;
	}

	/* Merge all PT_NOTE headers into one. */
	rc = merge_note_headers_elf32(elfcorebuf, &elfcorebuf_sz, &vmcore_list);
	if (rc) {
		kfree(elfcorebuf);
		return rc;
	}
	rc = process_ptload_program_headers_elf32(elfcorebuf, elfcorebuf_sz,
								&vmcore_list);
	if (rc) {
		kfree(elfcorebuf);
		return rc;
	}
	set_vmcore_list_offsets_elf32(elfcorebuf, &vmcore_list);
	return 0;
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
		printk(KERN_WARNING "Warning: Core image elf header"
					" not found\n");
		return -EINVAL;
	}

	if (e_ident[EI_CLASS] == ELFCLASS64) {
		rc = parse_crash_elf64_headers();
		if (rc)
			return rc;

		/* Determine vmcore size. */
		vmcore_size = get_vmcore_size_elf64(elfcorebuf);
	} else if (e_ident[EI_CLASS] == ELFCLASS32) {
		rc = parse_crash_elf32_headers();
		if (rc)
			return rc;

		/* Determine vmcore size. */
		vmcore_size = get_vmcore_size_elf32(elfcorebuf);
	} else {
		printk(KERN_WARNING "Warning: Core image elf header is not"
					" sane\n");
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
		printk(KERN_WARNING "Kdump: vmcore not initialized\n");
		return rc;
	}

	proc_vmcore = proc_create("vmcore", S_IRUSR, NULL, &proc_vmcore_operations);
	if (proc_vmcore)
		proc_vmcore->size = vmcore_size;
	return 0;
}
module_init(vmcore_init)
