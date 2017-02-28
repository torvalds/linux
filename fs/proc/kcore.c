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
#include <linux/bootmem.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/io.h>
#include <linux/list.h>
#include <linux/ioport.h>
#include <linux/memory.h>
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

/* An ELF note in memory */
struct memelfnote
{
	const char *name;
	int type;
	unsigned int datasz;
	void *data;
};

static LIST_HEAD(kclist_head);
static DEFINE_RWLOCK(kclist_lock);
static int kcore_need_update = 1;

void
kclist_add(struct kcore_list *new, void *addr, size_t size, int type)
{
	new->addr = (unsigned long)addr;
	new->size = size;
	new->type = type;

	write_lock(&kclist_lock);
	list_add_tail(&new->list, &kclist_head);
	write_unlock(&kclist_lock);
}

static size_t get_kcore_size(int *nphdr, size_t *elf_buflen)
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
	*elf_buflen =	sizeof(struct elfhdr) + 
			(*nphdr + 2)*sizeof(struct elf_phdr) + 
			3 * ((sizeof(struct elf_note)) +
			     roundup(sizeof(CORE_STR), 4)) +
			roundup(sizeof(struct elf_prstatus), 4) +
			roundup(sizeof(struct elf_prpsinfo), 4) +
			roundup(arch_task_struct_size, 4);
	*elf_buflen = PAGE_ALIGN(*elf_buflen);
	return size + *elf_buflen;
}

static void free_kclist_ents(struct list_head *head)
{
	struct kcore_list *tmp, *pos;

	list_for_each_entry_safe(pos, tmp, head, list) {
		list_del(&pos->list);
		kfree(pos);
	}
}
/*
 * Replace all KCORE_RAM/KCORE_VMEMMAP information with passed list.
 */
static void __kcore_update_ram(struct list_head *list)
{
	int nphdr;
	size_t size;
	struct kcore_list *tmp, *pos;
	LIST_HEAD(garbage);

	write_lock(&kclist_lock);
	if (kcore_need_update) {
		list_for_each_entry_safe(pos, tmp, &kclist_head, list) {
			if (pos->type == KCORE_RAM
				|| pos->type == KCORE_VMEMMAP)
				list_move(&pos->list, &garbage);
		}
		list_splice_tail(list, &kclist_head);
	} else
		list_splice(list, &garbage);
	kcore_need_update = 0;
	proc_root_kcore->size = get_kcore_size(&nphdr, &size);
	write_unlock(&kclist_lock);

	free_kclist_ents(&garbage);
}


#ifdef CONFIG_HIGHMEM
/*
 * If no highmem, we can assume [0...max_low_pfn) continuous range of memory
 * because memory hole is not as big as !HIGHMEM case.
 * (HIGHMEM is special because part of memory is _invisible_ from the kernel.)
 */
static int kcore_update_ram(void)
{
	LIST_HEAD(head);
	struct kcore_list *ent;
	int ret = 0;

	ent = kmalloc(sizeof(*ent), GFP_KERNEL);
	if (!ent)
		return -ENOMEM;
	ent->addr = (unsigned long)__va(0);
	ent->size = max_low_pfn << PAGE_SHIFT;
	ent->type = KCORE_RAM;
	list_add(&ent->list, &head);
	__kcore_update_ram(&head);
	return ret;
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

	ent = kmalloc(sizeof(*ent), GFP_KERNEL);
	if (!ent)
		return -ENOMEM;
	ent->addr = (unsigned long)__va((pfn << PAGE_SHIFT));
	ent->size = nr_pages << PAGE_SHIFT;

	/* Sanity check: Can happen in 32bit arch...maybe */
	if (ent->addr < (unsigned long) __va(0))
		goto free_out;

	/* cut not-mapped area. ....from ppc-32 code. */
	if (ULONG_MAX - ent->addr < ent->size)
		ent->size = ULONG_MAX - ent->addr;

	/* cut when vmalloc() area is higher than direct-map area */
	if (VMALLOC_START > (unsigned long)__va(0)) {
		if (ent->addr > VMALLOC_START)
			goto free_out;
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

static int kcore_update_ram(void)
{
	int nid, ret;
	unsigned long end_pfn;
	LIST_HEAD(head);

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
	ret = walk_system_ram_range(0, end_pfn, &head, kclist_add_private);
	if (ret) {
		free_kclist_ents(&head);
		return -ENOMEM;
	}
	__kcore_update_ram(&head);
	return ret;
}
#endif /* CONFIG_HIGHMEM */

/*****************************************************************************/
/*
 * determine size of ELF note
 */
static int notesize(struct memelfnote *en)
{
	int sz;

	sz = sizeof(struct elf_note);
	sz += roundup((strlen(en->name) + 1), 4);
	sz += roundup(en->datasz, 4);

	return sz;
} /* end notesize() */

/*****************************************************************************/
/*
 * store a note in the header buffer
 */
static char *storenote(struct memelfnote *men, char *bufp)
{
	struct elf_note en;

#define DUMP_WRITE(addr,nr) do { memcpy(bufp,addr,nr); bufp += nr; } while(0)

	en.n_namesz = strlen(men->name) + 1;
	en.n_descsz = men->datasz;
	en.n_type = men->type;

	DUMP_WRITE(&en, sizeof(en));
	DUMP_WRITE(men->name, en.n_namesz);

	/* XXX - cast from long long to long to avoid need for libgcc.a */
	bufp = (char*) roundup((unsigned long)bufp,4);
	DUMP_WRITE(men->data, men->datasz);
	bufp = (char*) roundup((unsigned long)bufp,4);

#undef DUMP_WRITE

	return bufp;
} /* end storenote() */

/*
 * store an ELF coredump header in the supplied buffer
 * nphdr is the number of elf_phdr to insert
 */
static void elf_kcore_store_hdr(char *bufp, int nphdr, int dataoff)
{
	struct elf_prstatus prstatus;	/* NT_PRSTATUS */
	struct elf_prpsinfo prpsinfo;	/* NT_PRPSINFO */
	struct elf_phdr *nhdr, *phdr;
	struct elfhdr *elf;
	struct memelfnote notes[3];
	off_t offset = 0;
	struct kcore_list *m;

	/* setup ELF header */
	elf = (struct elfhdr *) bufp;
	bufp += sizeof(struct elfhdr);
	offset += sizeof(struct elfhdr);
	memcpy(elf->e_ident, ELFMAG, SELFMAG);
	elf->e_ident[EI_CLASS]	= ELF_CLASS;
	elf->e_ident[EI_DATA]	= ELF_DATA;
	elf->e_ident[EI_VERSION]= EV_CURRENT;
	elf->e_ident[EI_OSABI] = ELF_OSABI;
	memset(elf->e_ident+EI_PAD, 0, EI_NIDENT-EI_PAD);
	elf->e_type	= ET_CORE;
	elf->e_machine	= ELF_ARCH;
	elf->e_version	= EV_CURRENT;
	elf->e_entry	= 0;
	elf->e_phoff	= sizeof(struct elfhdr);
	elf->e_shoff	= 0;
	elf->e_flags	= ELF_CORE_EFLAGS;
	elf->e_ehsize	= sizeof(struct elfhdr);
	elf->e_phentsize= sizeof(struct elf_phdr);
	elf->e_phnum	= nphdr;
	elf->e_shentsize= 0;
	elf->e_shnum	= 0;
	elf->e_shstrndx	= 0;

	/* setup ELF PT_NOTE program header */
	nhdr = (struct elf_phdr *) bufp;
	bufp += sizeof(struct elf_phdr);
	offset += sizeof(struct elf_phdr);
	nhdr->p_type	= PT_NOTE;
	nhdr->p_offset	= 0;
	nhdr->p_vaddr	= 0;
	nhdr->p_paddr	= 0;
	nhdr->p_filesz	= 0;
	nhdr->p_memsz	= 0;
	nhdr->p_flags	= 0;
	nhdr->p_align	= 0;

	/* setup ELF PT_LOAD program header for every area */
	list_for_each_entry(m, &kclist_head, list) {
		phdr = (struct elf_phdr *) bufp;
		bufp += sizeof(struct elf_phdr);
		offset += sizeof(struct elf_phdr);

		phdr->p_type	= PT_LOAD;
		phdr->p_flags	= PF_R|PF_W|PF_X;
		phdr->p_offset	= kc_vaddr_to_offset(m->addr) + dataoff;
		phdr->p_vaddr	= (size_t)m->addr;
		if (m->type == KCORE_RAM || m->type == KCORE_TEXT)
			phdr->p_paddr	= __pa(m->addr);
		else
			phdr->p_paddr	= (elf_addr_t)-1;
		phdr->p_filesz	= phdr->p_memsz	= m->size;
		phdr->p_align	= PAGE_SIZE;
	}

	/*
	 * Set up the notes in similar form to SVR4 core dumps made
	 * with info from their /proc.
	 */
	nhdr->p_offset	= offset;

	/* set up the process status */
	notes[0].name = CORE_STR;
	notes[0].type = NT_PRSTATUS;
	notes[0].datasz = sizeof(struct elf_prstatus);
	notes[0].data = &prstatus;

	memset(&prstatus, 0, sizeof(struct elf_prstatus));

	nhdr->p_filesz	= notesize(&notes[0]);
	bufp = storenote(&notes[0], bufp);

	/* set up the process info */
	notes[1].name	= CORE_STR;
	notes[1].type	= NT_PRPSINFO;
	notes[1].datasz	= sizeof(struct elf_prpsinfo);
	notes[1].data	= &prpsinfo;

	memset(&prpsinfo, 0, sizeof(struct elf_prpsinfo));
	prpsinfo.pr_state	= 0;
	prpsinfo.pr_sname	= 'R';
	prpsinfo.pr_zomb	= 0;

	strcpy(prpsinfo.pr_fname, "vmlinux");
	strlcpy(prpsinfo.pr_psargs, saved_command_line, sizeof(prpsinfo.pr_psargs));

	nhdr->p_filesz	+= notesize(&notes[1]);
	bufp = storenote(&notes[1], bufp);

	/* set up the task structure */
	notes[2].name	= CORE_STR;
	notes[2].type	= NT_TASKSTRUCT;
	notes[2].datasz	= arch_task_struct_size;
	notes[2].data	= current;

	nhdr->p_filesz	+= notesize(&notes[2]);
	bufp = storenote(&notes[2], bufp);

} /* end elf_kcore_store_hdr() */

/*****************************************************************************/
/*
 * read from the ELF header and then kernel memory
 */
static ssize_t
read_kcore(struct file *file, char __user *buffer, size_t buflen, loff_t *fpos)
{
	char *buf = file->private_data;
	ssize_t acc = 0;
	size_t size, tsz;
	size_t elf_buflen;
	int nphdr;
	unsigned long start;

	read_lock(&kclist_lock);
	size = get_kcore_size(&nphdr, &elf_buflen);

	if (buflen == 0 || *fpos >= size) {
		read_unlock(&kclist_lock);
		return 0;
	}

	/* trim buflen to not go beyond EOF */
	if (buflen > size - *fpos)
		buflen = size - *fpos;

	/* construct an ELF core header if we'll need some of it */
	if (*fpos < elf_buflen) {
		char * elf_buf;

		tsz = elf_buflen - *fpos;
		if (buflen < tsz)
			tsz = buflen;
		elf_buf = kzalloc(elf_buflen, GFP_ATOMIC);
		if (!elf_buf) {
			read_unlock(&kclist_lock);
			return -ENOMEM;
		}
		elf_kcore_store_hdr(elf_buf, nphdr, elf_buflen);
		read_unlock(&kclist_lock);
		if (copy_to_user(buffer, elf_buf + *fpos, tsz)) {
			kfree(elf_buf);
			return -EFAULT;
		}
		kfree(elf_buf);
		buflen -= tsz;
		*fpos += tsz;
		buffer += tsz;
		acc += tsz;

		/* leave now if filled buffer already */
		if (buflen == 0)
			return acc;
	} else
		read_unlock(&kclist_lock);

	/*
	 * Check to see if our file offset matches with any of
	 * the addresses in the elf_phdr on our list.
	 */
	start = kc_offset_to_vaddr(*fpos - elf_buflen);
	if ((tsz = (PAGE_SIZE - (start & ~PAGE_MASK))) > buflen)
		tsz = buflen;
		
	while (buflen) {
		struct kcore_list *m;

		read_lock(&kclist_lock);
		list_for_each_entry(m, &kclist_head, list) {
			if (start >= m->addr && start < (m->addr+m->size))
				break;
		}
		read_unlock(&kclist_lock);

		if (&m->list == &kclist_head) {
			if (clear_user(buffer, tsz))
				return -EFAULT;
		} else if (is_vmalloc_or_module_addr((void *)start)) {
			vread(buf, (char *)start, tsz);
			/* we have to zero-fill user buffer even if no read */
			if (copy_to_user(buffer, buf, tsz))
				return -EFAULT;
		} else {
			if (kern_addr_valid(start)) {
				unsigned long n;

				/*
				 * Using bounce buffer to bypass the
				 * hardened user copy kernel text checks.
				 */
				memcpy(buf, (char *) start, tsz);
				n = copy_to_user(buffer, buf, tsz);
				/*
				 * We cannot distinguish between fault on source
				 * and fault on destination. When this happens
				 * we clear too and hope it will trigger the
				 * EFAULT again.
				 */
				if (n) { 
					if (clear_user(buffer + tsz - n,
								n))
						return -EFAULT;
				}
			} else {
				if (clear_user(buffer, tsz))
					return -EFAULT;
			}
		}
		buflen -= tsz;
		*fpos += tsz;
		buffer += tsz;
		acc += tsz;
		start += tsz;
		tsz = (buflen > PAGE_SIZE ? PAGE_SIZE : buflen);
	}

	return acc;
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
		write_lock(&kclist_lock);
		kcore_need_update = 1;
		write_unlock(&kclist_lock);
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
struct kcore_list kcore_modules;
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
