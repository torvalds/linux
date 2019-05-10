// SPDX-License-Identifier: GPL-2.0
/*
 *	fs/proc/kcore.c kernel ELF core dumper
 *
 *	Modelled on fs/exec.c:aout_core_dump()
 *	Jeremy Fitzhardinge <jeremy@sw.oz.au>
 *	ELF version written by David Howells <David.Howells@nexor.co.uk>
 *	Modified and incorporated into 2.3.x by Tigran Aivazian <tigran@veritas.com>
 *	Support to dump vmalloc'd areas (ELF only), Tigran Aivazian <tigran@veritas.com>
 *	Safe accesses to vmalloc/direct-mapped discontiguous areas, Kanoj Sarcar <kanoj@sgi.com>
 */

#include <linux/crash_core.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/kcore.h>
#include <linux/user.h>
#include <linux/capability.h>
#include <linux/elf.h>
#include <linux/elfcore.h>
#include <linux/notifier.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>
#include <linux/printk.h>
#include <linux/memblock.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/io.h>
#include <linux/list.h>
#include <linux/ioport.h>
#include <linux/memory.h>
#include <linux/sched/task.h>
#include <asm/sections.h>
#include "internal.h"

#define CORE_STR "CORE"

#ifndef ELF_CORE_EFLAGS
#define ELF_CORE_EFLAGS	0
#endif

static struct proc_dir_entry *proc_root_kcore;


#ifndef kc_vaddr_to_offset
#define	kc_vaddr_to_offset(v) ((v) - PAGE_OFFSET)
#endif
#ifndef	kc_offset_to_vaddr
#define	kc_offset_to_vaddr(o) ((o) + PAGE_OFFSET)
#endif

static LIST_HEAD(kclist_head);
static DECLARE_RWSEM(kclist_lock);
static int kcore_need_update = 1;

/*
 * Returns > 0 for RAM pages, 0 for non-RAM pages, < 0 on error
 * Same as oldmem_pfn_is_ram in vmcore
 */
static int (*mem_pfn_is_ram)(unsigned long pfn);

int __init register_mem_pfn_is_ram(int (*fn)(unsigned long pfn))
{
	if (mem_pfn_is_ram)
		return -EBUSY;
	mem_pfn_is_ram = fn;
	return 0;
}

static int pfn_is_ram(unsigned long pfn)
{
	if (mem_pfn_is_ram)
		return mem_pfn_is_ram(pfn);
	else
		return 1;
}

/* This doesn't grab kclist_lock, so it should only be used at init time. */
void __init kclist_add(struct kcore_list *new, void *addr, size_t size,
		       int type)
{
	new->addr = (unsigned long)addr;
	new->size = size;
	new->type = type;

	list_add_tail(&new->list, &kclist_head);
}

static size_t get_kcore_size(int *nphdr, size_t *phdrs_len, size_t *notes_len,
			     size_t *data_offset)
{
	size_t try, size;
	struct kcore_list *m;

	*nphdr = 1; /* PT_NOTE */
	size = 0;

	list_for_each_entry(m, &kclist_head, list) {
		try = kc_vaddr_to_offset((size_t)m->addr + m->size);
		if (try > size)
			size = try;
		*nphdr = *nphdr + 1;
	}

	*phdrs_len = *nphdr * sizeof(struct elf_phdr);
	*notes_len = (4 * sizeof(struct elf_note) +
		      3 * ALIGN(sizeof(CORE_STR), 4) +
		      VMCOREINFO_NOTE_NAME_BYTES +
		      ALIGN(sizeof(struct elf_prstatus), 4) +
		      ALIGN(sizeof(struct elf_prpsinfo), 4) +
		      ALIGN(arch_task_struct_size, 4) +
		      ALIGN(vmcoreinfo_size, 4));
	*data_offset = PAGE_ALIGN(sizeof(struct elfhdr) + *phdrs_len +
				  *notes_len);
	return *data_offset + size;
}

#ifdef CONFIG_HIGHMEM
/*
 * If no highmem, we can assume [0...max_low_pfn) continuous range of memory
 * because memory hole is not as big as !HIGHMEM case.
 * (HIGHMEM is special because part of memory is _invisible_ from the kernel.)
 */
static int kcore_ram_list(struct list_head *head)
{
	struct kcore_list *ent;

	ent = kmalloc(sizeof(*ent), GFP_KERNEL);
	if (!ent)
		return -ENOMEM;
	ent->addr = (unsigned long)__va(0);
	ent->size = max_low_pfn << PAGE_SHIFT;
	ent->type = KCORE_RAM;
	list_add(&ent->list, head);
	return 0;
}

#else /* !CONFIG_HIGHMEM */

#ifdef CONFIG_SPARSEMEM_VMEMMAP
/* calculate vmemmap's address from given system ram pfn and register it */
static int
get_sparsemem_vmemmap_info(struct kcore_list *ent, struct list_head *head)
{
	unsigned long pfn = __pa(ent->addr) >> PAGE_SHIFT;
	unsigned long nr_pages = ent->size >> PAGE_SHIFT;
	unsigned long start, end;
	struct kcore_list *vmm, *tmp;


	start = ((unsigned long)pfn_to_page(pfn)) & PAGE_MASK;
	end = ((unsigned long)pfn_to_page(pfn + nr_pages)) - 1;
	end = PAGE_ALIGN(end);
	/* overlap check (because we have to align page */
	list_for_each_entry(tmp, head, list) {
		if (tmp->type != KCORE_VMEMMAP)
			continue;
		if (start < tmp->addr + tmp->size)
			if (end > tmp->addr)
				end = tmp->addr;
	}
	if (start < end) {
		vmm = kmalloc(sizeof(*vmm), GFP_KERNEL);
		if (!vmm)
			return 0;
		vmm->addr = start;
		vmm->size = end - start;
		vmm->type = KCORE_VMEMMAP;
		list_add_tail(&vmm->list, head);
	}
	return 1;

}
#else
static int
get_sparsemem_vmemmap_info(struct kcore_list *ent, struct list_head *head)
{
	return 1;
}

#endif

static int
kclist_add_private(unsigned long pfn, unsigned long nr_pages, void *arg)
{
	struct list_head *head = (struct list_head *)arg;
	struct kcore_list *ent;
	struct page *p;

	if (!pfn_valid(pfn))
		return 1;

	p = pfn_to_page(pfn);
	if (!memmap_valid_within(pfn, p, page_zone(p)))
		return 1;

	ent = kmalloc(sizeof(*ent), GFP_KERNEL);
	if (!ent)
		return -ENOMEM;
	ent->addr = (unsigned long)page_to_virt(p);
	ent->size = nr_pages << PAGE_SHIFT;

	if (!virt_addr_valid(ent->addr))
		goto free_out;

	/* cut not-mapped area. ....from ppc-32 code. */
	if (ULONG_MAX - ent->addr < ent->size)
		ent->size = ULONG_MAX - ent->addr;

	/*
	 * We've already checked virt_addr_valid so we know this address
	 * is a valid pointer, therefore we can check against it to determine
	 * if we need to trim
	 */
	if (VMALLOC_START > ent->addr) {
		if (VMALLOC_START - ent->addr < ent->size)
			ent->size = VMALLOC_START - ent->addr;
	}

	ent->type = KCORE_RAM;
	list_add_tail(&ent->list, head);

	if (!get_sparsemem_vmemmap_info(ent, head)) {
		list_del(&ent->list);
		goto free_out;
	}

	return 0;
free_out:
	kfree(ent);
	return 1;
}

static int kcore_ram_list(struct list_head *list)
{
	int nid, ret;
	unsigned long end_pfn;

	/* Not inialized....update now */
	/* find out "max pfn" */
	end_pfn = 0;
	for_each_node_state(nid, N_MEMORY) {
		unsigned long node_end;
		node_end = node_end_pfn(nid);
		if (end_pfn < node_end)
			end_pfn = node_end;
	}
	/* scan 0 to max_pfn */
	ret = walk_system_ram_range(0, end_pfn, list, kclist_add_private);
	if (ret)
		return -ENOMEM;
	return 0;
}
#endif /* CONFIG_HIGHMEM */

static int kcore_update_ram(void)
{
	LIST_HEAD(list);
	LIST_HEAD(garbage);
	int nphdr;
	size_t phdrs_len, notes_len, data_offset;
	struct kcore_list *tmp, *pos;
	int ret = 0;

	down_write(&kclist_lock);
	if (!xchg(&kcore_need_update, 0))
		goto out;

	ret = kcore_ram_list(&list);
	if (ret) {
		/* Couldn't get the RAM list, try again next time. */
		WRITE_ONCE(kcore_need_update, 1);
		list_splice_tail(&list, &garbage);
		goto out;
	}

	list_for_each_entry_safe(pos, tmp, &kclist_head, list) {
		if (pos->type == KCORE_RAM || pos->type == KCORE_VMEMMAP)
			list_move(&pos->list, &garbage);
	}
	list_splice_tail(&list, &kclist_head);

	proc_root_kcore->size = get_kcore_size(&nphdr, &phdrs_len, &notes_len,
					       &data_offset);

out:
	up_write(&kclist_lock);
	list_for_each_entry_safe(pos, tmp, &garbage, list) {
		list_del(&pos->list);
		kfree(pos);
	}
	return ret;
}

static void append_kcore_note(char *notes, size_t *i, const char *name,
			      unsigned int type, const void *desc,
			      size_t descsz)
{
	struct elf_note *note = (struct elf_note *)&notes[*i];

	note->n_namesz = strlen(name) + 1;
	note->n_descsz = descsz;
	note->n_type = type;
	*i += sizeof(*note);
	memcpy(&notes[*i], name, note->n_namesz);
	*i = ALIGN(*i + note->n_namesz, 4);
	memcpy(&notes[*i], desc, descsz);
	*i = ALIGN(*i + descsz, 4);
}

static ssize_t
read_kcore(struct file *file, char __user *buffer, size_t buflen, loff_t *fpos)
{
	char *buf = file->private_data;
	size_t phdrs_offset, notes_offset, data_offset;
	size_t phdrs_len, notes_len;
	struct kcore_list *m;
	size_t tsz;
	int nphdr;
	unsigned long start;
	size_t orig_buflen = buflen;
	int ret = 0;

	down_read(&kclist_lock);

	get_kcore_size(&nphdr, &phdrs_len, &notes_len, &data_offset);
	phdrs_offset = sizeof(struct elfhdr);
	notes_offset = phdrs_offset + phdrs_len;

	/* ELF file header. */
	if (buflen && *fpos < sizeof(struct elfhdr)) {
		struct elfhdr ehdr = {
			.e_ident = {
				[EI_MAG0] = ELFMAG0,
				[EI_MAG1] = ELFMAG1,
				[EI_MAG2] = ELFMAG2,
				[EI_MAG3] = ELFMAG3,
				[EI_CLASS] = ELF_CLASS,
				[EI_DATA] = ELF_DATA,
				[EI_VERSION] = EV_CURRENT,
				[EI_OSABI] = ELF_OSABI,
			},
			.e_type = ET_CORE,
			.e_machine = ELF_ARCH,
			.e_version = EV_CURRENT,
			.e_phoff = sizeof(struct elfhdr),
			.e_flags = ELF_CORE_EFLAGS,
			.e_ehsize = sizeof(struct elfhdr),
			.e_phentsize = sizeof(struct elf_phdr),
			.e_phnum = nphdr,
		};

		tsz = min_t(size_t, buflen, sizeof(struct elfhdr) - *fpos);
		if (copy_to_user(buffer, (char *)&ehdr + *fpos, tsz)) {
			ret = -EFAULT;
			goto out;
		}

		buffer += tsz;
		buflen -= tsz;
		*fpos += tsz;
	}

	/* ELF program headers. */
	if (buflen && *fpos < phdrs_offset + phdrs_len) {
		struct elf_phdr *phdrs, *phdr;

		phdrs = kzalloc(phdrs_len, GFP_KERNEL);
		if (!phdrs) {
			ret = -ENOMEM;
			goto out;
		}

		phdrs[0].p_type = PT_NOTE;
		phdrs[0].p_offset = notes_offset;
		phdrs[0].p_filesz = notes_len;

		phdr = &phdrs[1];
		list_for_each_entry(m, &kclist_head, list) {
			phdr->p_type = PT_LOAD;
			phdr->p_flags = PF_R | PF_W | PF_X;
			phdr->p_offset = kc_vaddr_to_offset(m->addr) + data_offset;
			if (m->type == KCORE_REMAP)
				phdr->p_vaddr = (size_t)m->vaddr;
			else
				phdr->p_vaddr = (size_t)m->addr;
			if (m->type == KCORE_RAM || m->type == KCORE_REMAP)
				phdr->p_paddr = __pa(m->addr);
			else if (m->type == KCORE_TEXT)
				phdr->p_paddr = __pa_symbol(m->addr);
			else
				phdr->p_paddr = (elf_addr_t)-1;
			phdr->p_filesz = phdr->p_memsz = m->size;
			phdr->p_align = PAGE_SIZE;
			phdr++;
		}

		tsz = min_t(size_t, buflen, phdrs_offset + phdrs_len - *fpos);
		if (copy_to_user(buffer, (char *)phdrs + *fpos - phdrs_offset,
				 tsz)) {
			kfree(phdrs);
			ret = -EFAULT;
			goto out;
		}
		kfree(phdrs);

		buffer += tsz;
		buflen -= tsz;
		*fpos += tsz;
	}

	/* ELF note segment. */
	if (buflen && *fpos < notes_offset + notes_len) {
		struct elf_prstatus prstatus = {};
		struct elf_prpsinfo prpsinfo = {
			.pr_sname = 'R',
			.pr_fname = "vmlinux",
		};
		char *notes;
		size_t i = 0;

		strlcpy(prpsinfo.pr_psargs, saved_command_line,
			sizeof(prpsinfo.pr_psargs));

		notes = kzalloc(notes_len, GFP_KERNEL);
		if (!notes) {
			ret = -ENOMEM;
			goto out;
		}

		append_kcore_note(notes, &i, CORE_STR, NT_PRSTATUS, &prstatus,
				  sizeof(prstatus));
		append_kcore_note(notes, &i, CORE_STR, NT_PRPSINFO, &prpsinfo,
				  sizeof(prpsinfo));
		append_kcore_note(notes, &i, CORE_STR, NT_TASKSTRUCT, current,
				  arch_task_struct_size);
		/*
		 * vmcoreinfo_size is mostly constant after init time, but it
		 * can be changed by crash_save_vmcoreinfo(). Racing here with a
		 * panic on another CPU before the machine goes down is insanely
		 * unlikely, but it's better to not leave potential buffer
		 * overflows lying around, regardless.
		 */
		append_kcore_note(notes, &i, VMCOREINFO_NOTE_NAME, 0,
				  vmcoreinfo_data,
				  min(vmcoreinfo_size, notes_len - i));

		tsz = min_t(size_t, buflen, notes_offset + notes_len - *fpos);
		if (copy_to_user(buffer, notes + *fpos - notes_offset, tsz)) {
			kfree(notes);
			ret = -EFAULT;
			goto out;
		}
		kfree(notes);

		buffer += tsz;
		buflen -= tsz;
		*fpos += tsz;
	}

	/*
	 * Check to see if our file offset matches with any of
	 * the addresses in the elf_phdr on our list.
	 */
	start = kc_offset_to_vaddr(*fpos - data_offset);
	if ((tsz = (PAGE_SIZE - (start & ~PAGE_MASK))) > buflen)
		tsz = buflen;

	m = NULL;
	while (buflen) {
		/*
		 * If this is the first iteration or the address is not within
		 * the previous entry, search for a matching entry.
		 */
		if (!m || start < m->addr || start >= m->addr + m->size) {
			list_for_each_entry(m, &kclist_head, list) {
				if (start >= m->addr &&
				    start < m->addr + m->size)
					break;
			}
		}

		if (&m->list == &kclist_head) {
			if (clear_user(buffer, tsz)) {
				ret = -EFAULT;
				goto out;
			}
			m = NULL;	/* skip the list anchor */
		} else if (!pfn_is_ram(__pa(start) >> PAGE_SHIFT)) {
			if (clear_user(buffer, tsz)) {
				ret = -EFAULT;
				goto out;
			}
		} else if (m->type == KCORE_VMALLOC) {
			vread(buf, (char *)start, tsz);
			/* we have to zero-fill user buffer even if no read */
			if (copy_to_user(buffer, buf, tsz)) {
				ret = -EFAULT;
				goto out;
			}
		} else if (m->type == KCORE_USER) {
			/* User page is handled prior to normal kernel page: */
			if (copy_to_user(buffer, (char *)start, tsz)) {
				ret = -EFAULT;
				goto out;
			}
		} else {
			if (kern_addr_valid(start)) {
				/*
				 * Using bounce buffer to bypass the
				 * hardened user copy kernel text checks.
				 */
				if (probe_kernel_read(buf, (void *) start, tsz)) {
					if (clear_user(buffer, tsz)) {
						ret = -EFAULT;
						goto out;
					}
				} else {
					if (copy_to_user(buffer, buf, tsz)) {
						ret = -EFAULT;
						goto out;
					}
				}
			} else {
				if (clear_user(buffer, tsz)) {
					ret = -EFAULT;
					goto out;
				}
			}
		}
		buflen -= tsz;
		*fpos += tsz;
		buffer += tsz;
		start += tsz;
		tsz = (buflen > PAGE_SIZE ? PAGE_SIZE : buflen);
	}

out:
	up_read(&kclist_lock);
	if (ret)
		return ret;
	return orig_buflen - buflen;
}

static int open_kcore(struct inode *inode, struct file *filp)
{
	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	filp->private_data = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!filp->private_data)
		return -ENOMEM;

	if (kcore_need_update)
		kcore_update_ram();
	if (i_size_read(inode) != proc_root_kcore->size) {
		inode_lock(inode);
		i_size_write(inode, proc_root_kcore->size);
		inode_unlock(inode);
	}
	return 0;
}

static int release_kcore(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

static const struct file_operations proc_kcore_operations = {
	.read		= read_kcore,
	.open		= open_kcore,
	.release	= release_kcore,
	.llseek		= default_llseek,
};

/* just remember that we have to update kcore */
static int __meminit kcore_callback(struct notifier_block *self,
				    unsigned long action, void *arg)
{
	switch (action) {
	case MEM_ONLINE:
	case MEM_OFFLINE:
		kcore_need_update = 1;
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block kcore_callback_nb __meminitdata = {
	.notifier_call = kcore_callback,
	.priority = 0,
};

static struct kcore_list kcore_vmalloc;

#ifdef CONFIG_ARCH_PROC_KCORE_TEXT
static struct kcore_list kcore_text;
/*
 * If defined, special segment is used for mapping kernel text instead of
 * direct-map area. We need to create special TEXT section.
 */
static void __init proc_kcore_text_init(void)
{
	kclist_add(&kcore_text, _text, _end - _text, KCORE_TEXT);
}
#else
static void __init proc_kcore_text_init(void)
{
}
#endif

#if defined(CONFIG_MODULES) && defined(MODULES_VADDR)
/*
 * MODULES_VADDR has no intersection with VMALLOC_ADDR.
 */
static struct kcore_list kcore_modules;
static void __init add_modules_range(void)
{
	if (MODULES_VADDR != VMALLOC_START && MODULES_END != VMALLOC_END) {
		kclist_add(&kcore_modules, (void *)MODULES_VADDR,
			MODULES_END - MODULES_VADDR, KCORE_VMALLOC);
	}
}
#else
static void __init add_modules_range(void)
{
}
#endif

static int __init proc_kcore_init(void)
{
	proc_root_kcore = proc_create("kcore", S_IRUSR, NULL,
				      &proc_kcore_operations);
	if (!proc_root_kcore) {
		pr_err("couldn't create /proc/kcore\n");
		return 0; /* Always returns 0. */
	}
	/* Store text area if it's special */
	proc_kcore_text_init();
	/* Store vmalloc area */
	kclist_add(&kcore_vmalloc, (void *)VMALLOC_START,
		VMALLOC_END - VMALLOC_START, KCORE_VMALLOC);
	add_modules_range();
	/* Store direct-map area from physical memory map */
	kcore_update_ram();
	register_hotmemory_notifier(&kcore_callback_nb);

	return 0;
}
fs_initcall(proc_kcore_init);
