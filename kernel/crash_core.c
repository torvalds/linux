// SPDX-License-Identifier: GPL-2.0-only
/*
 * crash.c - kernel crash support code.
 * Copyright (C) 2002-2004 Eric Biederman  <ebiederm@xmission.com>
 */

#include <linux/buildid.h>
#include <linux/init.h>
#include <linux/utsname.h>
#include <linux/vmalloc.h>
#include <linux/sizes.h>
#include <linux/kexec.h>
#include <linux/memory.h>
#include <linux/cpuhotplug.h>
#include <linux/memblock.h>
#include <linux/kmemleak.h>

#include <asm/page.h>
#include <asm/sections.h>

#include <crypto/sha1.h>

#include "kallsyms_internal.h"
#include "kexec_internal.h"

/* Per cpu memory for storing cpu states in case of system crash. */
note_buf_t __percpu *crash_notes;

/* vmcoreinfo stuff */
unsigned char *vmcoreinfo_data;
size_t vmcoreinfo_size;
u32 *vmcoreinfo_note;

/* trusted vmcoreinfo, e.g. we can make a copy in the crash memory */
static unsigned char *vmcoreinfo_data_safecopy;

/* Location of the reserved area for the crash kernel */
struct resource crashk_res = {
	.name  = "Crash kernel",
	.start = 0,
	.end   = 0,
	.flags = IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM,
	.desc  = IORES_DESC_CRASH_KERNEL
};
struct resource crashk_low_res = {
	.name  = "Crash kernel",
	.start = 0,
	.end   = 0,
	.flags = IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM,
	.desc  = IORES_DESC_CRASH_KERNEL
};

/*
 * parsing the "crashkernel" commandline
 *
 * this code is intended to be called from architecture specific code
 */


/*
 * This function parses command lines in the format
 *
 *   crashkernel=ramsize-range:size[,...][@offset]
 *
 * The function returns 0 on success and -EINVAL on failure.
 */
static int __init parse_crashkernel_mem(char *cmdline,
					unsigned long long system_ram,
					unsigned long long *crash_size,
					unsigned long long *crash_base)
{
	char *cur = cmdline, *tmp;
	unsigned long long total_mem = system_ram;

	/*
	 * Firmware sometimes reserves some memory regions for its own use,
	 * so the system memory size is less than the actual physical memory
	 * size. Work around this by rounding up the total size to 128M,
	 * which is enough for most test cases.
	 */
	total_mem = roundup(total_mem, SZ_128M);

	/* for each entry of the comma-separated list */
	do {
		unsigned long long start, end = ULLONG_MAX, size;

		/* get the start of the range */
		start = memparse(cur, &tmp);
		if (cur == tmp) {
			pr_warn("crashkernel: Memory value expected\n");
			return -EINVAL;
		}
		cur = tmp;
		if (*cur != '-') {
			pr_warn("crashkernel: '-' expected\n");
			return -EINVAL;
		}
		cur++;

		/* if no ':' is here, than we read the end */
		if (*cur != ':') {
			end = memparse(cur, &tmp);
			if (cur == tmp) {
				pr_warn("crashkernel: Memory value expected\n");
				return -EINVAL;
			}
			cur = tmp;
			if (end <= start) {
				pr_warn("crashkernel: end <= start\n");
				return -EINVAL;
			}
		}

		if (*cur != ':') {
			pr_warn("crashkernel: ':' expected\n");
			return -EINVAL;
		}
		cur++;

		size = memparse(cur, &tmp);
		if (cur == tmp) {
			pr_warn("Memory value expected\n");
			return -EINVAL;
		}
		cur = tmp;
		if (size >= total_mem) {
			pr_warn("crashkernel: invalid size\n");
			return -EINVAL;
		}

		/* match ? */
		if (total_mem >= start && total_mem < end) {
			*crash_size = size;
			break;
		}
	} while (*cur++ == ',');

	if (*crash_size > 0) {
		while (*cur && *cur != ' ' && *cur != '@')
			cur++;
		if (*cur == '@') {
			cur++;
			*crash_base = memparse(cur, &tmp);
			if (cur == tmp) {
				pr_warn("Memory value expected after '@'\n");
				return -EINVAL;
			}
		}
	} else
		pr_info("crashkernel size resulted in zero bytes\n");

	return 0;
}

/*
 * That function parses "simple" (old) crashkernel command lines like
 *
 *	crashkernel=size[@offset]
 *
 * It returns 0 on success and -EINVAL on failure.
 */
static int __init parse_crashkernel_simple(char *cmdline,
					   unsigned long long *crash_size,
					   unsigned long long *crash_base)
{
	char *cur = cmdline;

	*crash_size = memparse(cmdline, &cur);
	if (cmdline == cur) {
		pr_warn("crashkernel: memory value expected\n");
		return -EINVAL;
	}

	if (*cur == '@')
		*crash_base = memparse(cur+1, &cur);
	else if (*cur != ' ' && *cur != '\0') {
		pr_warn("crashkernel: unrecognized char: %c\n", *cur);
		return -EINVAL;
	}

	return 0;
}

#define SUFFIX_HIGH 0
#define SUFFIX_LOW  1
#define SUFFIX_NULL 2
static __initdata char *suffix_tbl[] = {
	[SUFFIX_HIGH] = ",high",
	[SUFFIX_LOW]  = ",low",
	[SUFFIX_NULL] = NULL,
};

/*
 * That function parses "suffix"  crashkernel command lines like
 *
 *	crashkernel=size,[high|low]
 *
 * It returns 0 on success and -EINVAL on failure.
 */
static int __init parse_crashkernel_suffix(char *cmdline,
					   unsigned long long *crash_size,
					   const char *suffix)
{
	char *cur = cmdline;

	*crash_size = memparse(cmdline, &cur);
	if (cmdline == cur) {
		pr_warn("crashkernel: memory value expected\n");
		return -EINVAL;
	}

	/* check with suffix */
	if (strncmp(cur, suffix, strlen(suffix))) {
		pr_warn("crashkernel: unrecognized char: %c\n", *cur);
		return -EINVAL;
	}
	cur += strlen(suffix);
	if (*cur != ' ' && *cur != '\0') {
		pr_warn("crashkernel: unrecognized char: %c\n", *cur);
		return -EINVAL;
	}

	return 0;
}

static __init char *get_last_crashkernel(char *cmdline,
			     const char *name,
			     const char *suffix)
{
	char *p = cmdline, *ck_cmdline = NULL;

	/* find crashkernel and use the last one if there are more */
	p = strstr(p, name);
	while (p) {
		char *end_p = strchr(p, ' ');
		char *q;

		if (!end_p)
			end_p = p + strlen(p);

		if (!suffix) {
			int i;

			/* skip the one with any known suffix */
			for (i = 0; suffix_tbl[i]; i++) {
				q = end_p - strlen(suffix_tbl[i]);
				if (!strncmp(q, suffix_tbl[i],
					     strlen(suffix_tbl[i])))
					goto next;
			}
			ck_cmdline = p;
		} else {
			q = end_p - strlen(suffix);
			if (!strncmp(q, suffix, strlen(suffix)))
				ck_cmdline = p;
		}
next:
		p = strstr(p+1, name);
	}

	return ck_cmdline;
}

static int __init __parse_crashkernel(char *cmdline,
			     unsigned long long system_ram,
			     unsigned long long *crash_size,
			     unsigned long long *crash_base,
			     const char *suffix)
{
	char *first_colon, *first_space;
	char *ck_cmdline;
	char *name = "crashkernel=";

	BUG_ON(!crash_size || !crash_base);
	*crash_size = 0;
	*crash_base = 0;

	ck_cmdline = get_last_crashkernel(cmdline, name, suffix);
	if (!ck_cmdline)
		return -ENOENT;

	ck_cmdline += strlen(name);

	if (suffix)
		return parse_crashkernel_suffix(ck_cmdline, crash_size,
				suffix);
	/*
	 * if the commandline contains a ':', then that's the extended
	 * syntax -- if not, it must be the classic syntax
	 */
	first_colon = strchr(ck_cmdline, ':');
	first_space = strchr(ck_cmdline, ' ');
	if (first_colon && (!first_space || first_colon < first_space))
		return parse_crashkernel_mem(ck_cmdline, system_ram,
				crash_size, crash_base);

	return parse_crashkernel_simple(ck_cmdline, crash_size, crash_base);
}

/*
 * That function is the entry point for command line parsing and should be
 * called from the arch-specific code.
 *
 * If crashkernel=,high|low is supported on architecture, non-NULL values
 * should be passed to parameters 'low_size' and 'high'.
 */
int __init parse_crashkernel(char *cmdline,
			     unsigned long long system_ram,
			     unsigned long long *crash_size,
			     unsigned long long *crash_base,
			     unsigned long long *low_size,
			     bool *high)
{
	int ret;

	/* crashkernel=X[@offset] */
	ret = __parse_crashkernel(cmdline, system_ram, crash_size,
				crash_base, NULL);
#ifdef CONFIG_ARCH_HAS_GENERIC_CRASHKERNEL_RESERVATION
	/*
	 * If non-NULL 'high' passed in and no normal crashkernel
	 * setting detected, try parsing crashkernel=,high|low.
	 */
	if (high && ret == -ENOENT) {
		ret = __parse_crashkernel(cmdline, 0, crash_size,
				crash_base, suffix_tbl[SUFFIX_HIGH]);
		if (ret || !*crash_size)
			return -EINVAL;

		/*
		 * crashkernel=Y,low can be specified or not, but invalid value
		 * is not allowed.
		 */
		ret = __parse_crashkernel(cmdline, 0, low_size,
				crash_base, suffix_tbl[SUFFIX_LOW]);
		if (ret == -ENOENT) {
			*low_size = DEFAULT_CRASH_KERNEL_LOW_SIZE;
			ret = 0;
		} else if (ret) {
			return ret;
		}

		*high = true;
	}
#endif
	if (!*crash_size)
		ret = -EINVAL;

	return ret;
}

/*
 * Add a dummy early_param handler to mark crashkernel= as a known command line
 * parameter and suppress incorrect warnings in init/main.c.
 */
static int __init parse_crashkernel_dummy(char *arg)
{
	return 0;
}
early_param("crashkernel", parse_crashkernel_dummy);

#ifdef CONFIG_ARCH_HAS_GENERIC_CRASHKERNEL_RESERVATION
static int __init reserve_crashkernel_low(unsigned long long low_size)
{
#ifdef CONFIG_64BIT
	unsigned long long low_base;

	low_base = memblock_phys_alloc_range(low_size, CRASH_ALIGN, 0, CRASH_ADDR_LOW_MAX);
	if (!low_base) {
		pr_err("cannot allocate crashkernel low memory (size:0x%llx).\n", low_size);
		return -ENOMEM;
	}

	pr_info("crashkernel low memory reserved: 0x%08llx - 0x%08llx (%lld MB)\n",
		low_base, low_base + low_size, low_size >> 20);

	crashk_low_res.start = low_base;
	crashk_low_res.end   = low_base + low_size - 1;
	insert_resource(&iomem_resource, &crashk_low_res);
#endif
	return 0;
}

void __init reserve_crashkernel_generic(char *cmdline,
			     unsigned long long crash_size,
			     unsigned long long crash_base,
			     unsigned long long crash_low_size,
			     bool high)
{
	unsigned long long search_end = CRASH_ADDR_LOW_MAX, search_base = 0;
	bool fixed_base = false;

	/* User specifies base address explicitly. */
	if (crash_base) {
		fixed_base = true;
		search_base = crash_base;
		search_end = crash_base + crash_size;
	} else if (high) {
		search_base = CRASH_ADDR_LOW_MAX;
		search_end = CRASH_ADDR_HIGH_MAX;
	}

retry:
	crash_base = memblock_phys_alloc_range(crash_size, CRASH_ALIGN,
					       search_base, search_end);
	if (!crash_base) {
		/*
		 * For crashkernel=size[KMG]@offset[KMG], print out failure
		 * message if can't reserve the specified region.
		 */
		if (fixed_base) {
			pr_warn("crashkernel reservation failed - memory is in use.\n");
			return;
		}

		/*
		 * For crashkernel=size[KMG], if the first attempt was for
		 * low memory, fall back to high memory, the minimum required
		 * low memory will be reserved later.
		 */
		if (!high && search_end == CRASH_ADDR_LOW_MAX) {
			search_end = CRASH_ADDR_HIGH_MAX;
			search_base = CRASH_ADDR_LOW_MAX;
			crash_low_size = DEFAULT_CRASH_KERNEL_LOW_SIZE;
			goto retry;
		}

		/*
		 * For crashkernel=size[KMG],high, if the first attempt was
		 * for high memory, fall back to low memory.
		 */
		if (high && search_end == CRASH_ADDR_HIGH_MAX) {
			search_end = CRASH_ADDR_LOW_MAX;
			search_base = 0;
			goto retry;
		}
		pr_warn("cannot allocate crashkernel (size:0x%llx)\n",
			crash_size);
		return;
	}

	if ((crash_base >= CRASH_ADDR_LOW_MAX) &&
	     crash_low_size && reserve_crashkernel_low(crash_low_size)) {
		memblock_phys_free(crash_base, crash_size);
		return;
	}

	pr_info("crashkernel reserved: 0x%016llx - 0x%016llx (%lld MB)\n",
		crash_base, crash_base + crash_size, crash_size >> 20);

	/*
	 * The crashkernel memory will be removed from the kernel linear
	 * map. Inform kmemleak so that it won't try to access it.
	 */
	kmemleak_ignore_phys(crash_base);
	if (crashk_low_res.end)
		kmemleak_ignore_phys(crashk_low_res.start);

	crashk_res.start = crash_base;
	crashk_res.end = crash_base + crash_size - 1;
	insert_resource(&iomem_resource, &crashk_res);
}
#endif

int crash_prepare_elf64_headers(struct crash_mem *mem, int need_kernel_map,
			  void **addr, unsigned long *sz)
{
	Elf64_Ehdr *ehdr;
	Elf64_Phdr *phdr;
	unsigned long nr_cpus = num_possible_cpus(), nr_phdr, elf_sz;
	unsigned char *buf;
	unsigned int cpu, i;
	unsigned long long notes_addr;
	unsigned long mstart, mend;

	/* extra phdr for vmcoreinfo ELF note */
	nr_phdr = nr_cpus + 1;
	nr_phdr += mem->nr_ranges;

	/*
	 * kexec-tools creates an extra PT_LOAD phdr for kernel text mapping
	 * area (for example, ffffffff80000000 - ffffffffa0000000 on x86_64).
	 * I think this is required by tools like gdb. So same physical
	 * memory will be mapped in two ELF headers. One will contain kernel
	 * text virtual addresses and other will have __va(physical) addresses.
	 */

	nr_phdr++;
	elf_sz = sizeof(Elf64_Ehdr) + nr_phdr * sizeof(Elf64_Phdr);
	elf_sz = ALIGN(elf_sz, ELF_CORE_HEADER_ALIGN);

	buf = vzalloc(elf_sz);
	if (!buf)
		return -ENOMEM;

	ehdr = (Elf64_Ehdr *)buf;
	phdr = (Elf64_Phdr *)(ehdr + 1);
	memcpy(ehdr->e_ident, ELFMAG, SELFMAG);
	ehdr->e_ident[EI_CLASS] = ELFCLASS64;
	ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
	ehdr->e_ident[EI_VERSION] = EV_CURRENT;
	ehdr->e_ident[EI_OSABI] = ELF_OSABI;
	memset(ehdr->e_ident + EI_PAD, 0, EI_NIDENT - EI_PAD);
	ehdr->e_type = ET_CORE;
	ehdr->e_machine = ELF_ARCH;
	ehdr->e_version = EV_CURRENT;
	ehdr->e_phoff = sizeof(Elf64_Ehdr);
	ehdr->e_ehsize = sizeof(Elf64_Ehdr);
	ehdr->e_phentsize = sizeof(Elf64_Phdr);

	/* Prepare one phdr of type PT_NOTE for each possible CPU */
	for_each_possible_cpu(cpu) {
		phdr->p_type = PT_NOTE;
		notes_addr = per_cpu_ptr_to_phys(per_cpu_ptr(crash_notes, cpu));
		phdr->p_offset = phdr->p_paddr = notes_addr;
		phdr->p_filesz = phdr->p_memsz = sizeof(note_buf_t);
		(ehdr->e_phnum)++;
		phdr++;
	}

	/* Prepare one PT_NOTE header for vmcoreinfo */
	phdr->p_type = PT_NOTE;
	phdr->p_offset = phdr->p_paddr = paddr_vmcoreinfo_note();
	phdr->p_filesz = phdr->p_memsz = VMCOREINFO_NOTE_SIZE;
	(ehdr->e_phnum)++;
	phdr++;

	/* Prepare PT_LOAD type program header for kernel text region */
	if (need_kernel_map) {
		phdr->p_type = PT_LOAD;
		phdr->p_flags = PF_R|PF_W|PF_X;
		phdr->p_vaddr = (unsigned long) _text;
		phdr->p_filesz = phdr->p_memsz = _end - _text;
		phdr->p_offset = phdr->p_paddr = __pa_symbol(_text);
		ehdr->e_phnum++;
		phdr++;
	}

	/* Go through all the ranges in mem->ranges[] and prepare phdr */
	for (i = 0; i < mem->nr_ranges; i++) {
		mstart = mem->ranges[i].start;
		mend = mem->ranges[i].end;

		phdr->p_type = PT_LOAD;
		phdr->p_flags = PF_R|PF_W|PF_X;
		phdr->p_offset  = mstart;

		phdr->p_paddr = mstart;
		phdr->p_vaddr = (unsigned long) __va(mstart);
		phdr->p_filesz = phdr->p_memsz = mend - mstart + 1;
		phdr->p_align = 0;
		ehdr->e_phnum++;
#ifdef CONFIG_KEXEC_FILE
		kexec_dprintk("Crash PT_LOAD ELF header. phdr=%p vaddr=0x%llx, paddr=0x%llx, sz=0x%llx e_phnum=%d p_offset=0x%llx\n",
			      phdr, phdr->p_vaddr, phdr->p_paddr, phdr->p_filesz,
			      ehdr->e_phnum, phdr->p_offset);
#endif
		phdr++;
	}

	*addr = buf;
	*sz = elf_sz;
	return 0;
}

int crash_exclude_mem_range(struct crash_mem *mem,
			    unsigned long long mstart, unsigned long long mend)
{
	int i, j;
	unsigned long long start, end, p_start, p_end;
	struct range temp_range = {0, 0};

	for (i = 0; i < mem->nr_ranges; i++) {
		start = mem->ranges[i].start;
		end = mem->ranges[i].end;
		p_start = mstart;
		p_end = mend;

		if (mstart > end || mend < start)
			continue;

		/* Truncate any area outside of range */
		if (mstart < start)
			p_start = start;
		if (mend > end)
			p_end = end;

		/* Found completely overlapping range */
		if (p_start == start && p_end == end) {
			mem->ranges[i].start = 0;
			mem->ranges[i].end = 0;
			if (i < mem->nr_ranges - 1) {
				/* Shift rest of the ranges to left */
				for (j = i; j < mem->nr_ranges - 1; j++) {
					mem->ranges[j].start =
						mem->ranges[j+1].start;
					mem->ranges[j].end =
							mem->ranges[j+1].end;
				}

				/*
				 * Continue to check if there are another overlapping ranges
				 * from the current position because of shifting the above
				 * mem ranges.
				 */
				i--;
				mem->nr_ranges--;
				continue;
			}
			mem->nr_ranges--;
			return 0;
		}

		if (p_start > start && p_end < end) {
			/* Split original range */
			mem->ranges[i].end = p_start - 1;
			temp_range.start = p_end + 1;
			temp_range.end = end;
		} else if (p_start != start)
			mem->ranges[i].end = p_start - 1;
		else
			mem->ranges[i].start = p_end + 1;
		break;
	}

	/* If a split happened, add the split to array */
	if (!temp_range.end)
		return 0;

	/* Split happened */
	if (i == mem->max_nr_ranges - 1)
		return -ENOMEM;

	/* Location where new range should go */
	j = i + 1;
	if (j < mem->nr_ranges) {
		/* Move over all ranges one slot towards the end */
		for (i = mem->nr_ranges - 1; i >= j; i--)
			mem->ranges[i + 1] = mem->ranges[i];
	}

	mem->ranges[j].start = temp_range.start;
	mem->ranges[j].end = temp_range.end;
	mem->nr_ranges++;
	return 0;
}

Elf_Word *append_elf_note(Elf_Word *buf, char *name, unsigned int type,
			  void *data, size_t data_len)
{
	struct elf_note *note = (struct elf_note *)buf;

	note->n_namesz = strlen(name) + 1;
	note->n_descsz = data_len;
	note->n_type   = type;
	buf += DIV_ROUND_UP(sizeof(*note), sizeof(Elf_Word));
	memcpy(buf, name, note->n_namesz);
	buf += DIV_ROUND_UP(note->n_namesz, sizeof(Elf_Word));
	memcpy(buf, data, data_len);
	buf += DIV_ROUND_UP(data_len, sizeof(Elf_Word));

	return buf;
}

void final_note(Elf_Word *buf)
{
	memset(buf, 0, sizeof(struct elf_note));
}

static void update_vmcoreinfo_note(void)
{
	u32 *buf = vmcoreinfo_note;

	if (!vmcoreinfo_size)
		return;
	buf = append_elf_note(buf, VMCOREINFO_NOTE_NAME, 0, vmcoreinfo_data,
			      vmcoreinfo_size);
	final_note(buf);
}

void crash_update_vmcoreinfo_safecopy(void *ptr)
{
	if (ptr)
		memcpy(ptr, vmcoreinfo_data, vmcoreinfo_size);

	vmcoreinfo_data_safecopy = ptr;
}

void crash_save_vmcoreinfo(void)
{
	if (!vmcoreinfo_note)
		return;

	/* Use the safe copy to generate vmcoreinfo note if have */
	if (vmcoreinfo_data_safecopy)
		vmcoreinfo_data = vmcoreinfo_data_safecopy;

	vmcoreinfo_append_str("CRASHTIME=%lld\n", ktime_get_real_seconds());
	update_vmcoreinfo_note();
}

void vmcoreinfo_append_str(const char *fmt, ...)
{
	va_list args;
	char buf[0x50];
	size_t r;

	va_start(args, fmt);
	r = vscnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	r = min(r, (size_t)VMCOREINFO_BYTES - vmcoreinfo_size);

	memcpy(&vmcoreinfo_data[vmcoreinfo_size], buf, r);

	vmcoreinfo_size += r;

	WARN_ONCE(vmcoreinfo_size == VMCOREINFO_BYTES,
		  "vmcoreinfo data exceeds allocated size, truncating");
}

/*
 * provide an empty default implementation here -- architecture
 * code may override this
 */
void __weak arch_crash_save_vmcoreinfo(void)
{}

phys_addr_t __weak paddr_vmcoreinfo_note(void)
{
	return __pa(vmcoreinfo_note);
}
EXPORT_SYMBOL(paddr_vmcoreinfo_note);

static int __init crash_save_vmcoreinfo_init(void)
{
	vmcoreinfo_data = (unsigned char *)get_zeroed_page(GFP_KERNEL);
	if (!vmcoreinfo_data) {
		pr_warn("Memory allocation for vmcoreinfo_data failed\n");
		return -ENOMEM;
	}

	vmcoreinfo_note = alloc_pages_exact(VMCOREINFO_NOTE_SIZE,
						GFP_KERNEL | __GFP_ZERO);
	if (!vmcoreinfo_note) {
		free_page((unsigned long)vmcoreinfo_data);
		vmcoreinfo_data = NULL;
		pr_warn("Memory allocation for vmcoreinfo_note failed\n");
		return -ENOMEM;
	}

	VMCOREINFO_OSRELEASE(init_uts_ns.name.release);
	VMCOREINFO_BUILD_ID();
	VMCOREINFO_PAGESIZE(PAGE_SIZE);

	VMCOREINFO_SYMBOL(init_uts_ns);
	VMCOREINFO_OFFSET(uts_namespace, name);
	VMCOREINFO_SYMBOL(node_online_map);
#ifdef CONFIG_MMU
	VMCOREINFO_SYMBOL_ARRAY(swapper_pg_dir);
#endif
	VMCOREINFO_SYMBOL(_stext);
	VMCOREINFO_SYMBOL(vmap_area_list);

#ifndef CONFIG_NUMA
	VMCOREINFO_SYMBOL(mem_map);
	VMCOREINFO_SYMBOL(contig_page_data);
#endif
#ifdef CONFIG_SPARSEMEM
	VMCOREINFO_SYMBOL_ARRAY(mem_section);
	VMCOREINFO_LENGTH(mem_section, NR_SECTION_ROOTS);
	VMCOREINFO_STRUCT_SIZE(mem_section);
	VMCOREINFO_OFFSET(mem_section, section_mem_map);
	VMCOREINFO_NUMBER(SECTION_SIZE_BITS);
	VMCOREINFO_NUMBER(MAX_PHYSMEM_BITS);
#endif
	VMCOREINFO_STRUCT_SIZE(page);
	VMCOREINFO_STRUCT_SIZE(pglist_data);
	VMCOREINFO_STRUCT_SIZE(zone);
	VMCOREINFO_STRUCT_SIZE(free_area);
	VMCOREINFO_STRUCT_SIZE(list_head);
	VMCOREINFO_SIZE(nodemask_t);
	VMCOREINFO_OFFSET(page, flags);
	VMCOREINFO_OFFSET(page, _refcount);
	VMCOREINFO_OFFSET(page, mapping);
	VMCOREINFO_OFFSET(page, lru);
	VMCOREINFO_OFFSET(page, _mapcount);
	VMCOREINFO_OFFSET(page, private);
	VMCOREINFO_OFFSET(page, compound_head);
	VMCOREINFO_OFFSET(pglist_data, node_zones);
	VMCOREINFO_OFFSET(pglist_data, nr_zones);
#ifdef CONFIG_FLATMEM
	VMCOREINFO_OFFSET(pglist_data, node_mem_map);
#endif
	VMCOREINFO_OFFSET(pglist_data, node_start_pfn);
	VMCOREINFO_OFFSET(pglist_data, node_spanned_pages);
	VMCOREINFO_OFFSET(pglist_data, node_id);
	VMCOREINFO_OFFSET(zone, free_area);
	VMCOREINFO_OFFSET(zone, vm_stat);
	VMCOREINFO_OFFSET(zone, spanned_pages);
	VMCOREINFO_OFFSET(free_area, free_list);
	VMCOREINFO_OFFSET(list_head, next);
	VMCOREINFO_OFFSET(list_head, prev);
	VMCOREINFO_OFFSET(vmap_area, va_start);
	VMCOREINFO_OFFSET(vmap_area, list);
	VMCOREINFO_LENGTH(zone.free_area, MAX_ORDER + 1);
	log_buf_vmcoreinfo_setup();
	VMCOREINFO_LENGTH(free_area.free_list, MIGRATE_TYPES);
	VMCOREINFO_NUMBER(NR_FREE_PAGES);
	VMCOREINFO_NUMBER(PG_lru);
	VMCOREINFO_NUMBER(PG_private);
	VMCOREINFO_NUMBER(PG_swapcache);
	VMCOREINFO_NUMBER(PG_swapbacked);
	VMCOREINFO_NUMBER(PG_slab);
#ifdef CONFIG_MEMORY_FAILURE
	VMCOREINFO_NUMBER(PG_hwpoison);
#endif
	VMCOREINFO_NUMBER(PG_head_mask);
#define PAGE_BUDDY_MAPCOUNT_VALUE	(~PG_buddy)
	VMCOREINFO_NUMBER(PAGE_BUDDY_MAPCOUNT_VALUE);
#ifdef CONFIG_HUGETLB_PAGE
	VMCOREINFO_NUMBER(PG_hugetlb);
#define PAGE_OFFLINE_MAPCOUNT_VALUE	(~PG_offline)
	VMCOREINFO_NUMBER(PAGE_OFFLINE_MAPCOUNT_VALUE);
#endif

#ifdef CONFIG_KALLSYMS
	VMCOREINFO_SYMBOL(kallsyms_names);
	VMCOREINFO_SYMBOL(kallsyms_num_syms);
	VMCOREINFO_SYMBOL(kallsyms_token_table);
	VMCOREINFO_SYMBOL(kallsyms_token_index);
#ifdef CONFIG_KALLSYMS_BASE_RELATIVE
	VMCOREINFO_SYMBOL(kallsyms_offsets);
	VMCOREINFO_SYMBOL(kallsyms_relative_base);
#else
	VMCOREINFO_SYMBOL(kallsyms_addresses);
#endif /* CONFIG_KALLSYMS_BASE_RELATIVE */
#endif /* CONFIG_KALLSYMS */

	arch_crash_save_vmcoreinfo();
	update_vmcoreinfo_note();

	return 0;
}

subsys_initcall(crash_save_vmcoreinfo_init);

static int __init crash_notes_memory_init(void)
{
	/* Allocate memory for saving cpu registers. */
	size_t size, align;

	/*
	 * crash_notes could be allocated across 2 vmalloc pages when percpu
	 * is vmalloc based . vmalloc doesn't guarantee 2 continuous vmalloc
	 * pages are also on 2 continuous physical pages. In this case the
	 * 2nd part of crash_notes in 2nd page could be lost since only the
	 * starting address and size of crash_notes are exported through sysfs.
	 * Here round up the size of crash_notes to the nearest power of two
	 * and pass it to __alloc_percpu as align value. This can make sure
	 * crash_notes is allocated inside one physical page.
	 */
	size = sizeof(note_buf_t);
	align = min(roundup_pow_of_two(sizeof(note_buf_t)), PAGE_SIZE);

	/*
	 * Break compile if size is bigger than PAGE_SIZE since crash_notes
	 * definitely will be in 2 pages with that.
	 */
	BUILD_BUG_ON(size > PAGE_SIZE);

	crash_notes = __alloc_percpu(size, align);
	if (!crash_notes) {
		pr_warn("Memory allocation for saving cpu register states failed\n");
		return -ENOMEM;
	}
	return 0;
}
subsys_initcall(crash_notes_memory_init);

#ifdef CONFIG_CRASH_HOTPLUG
#undef pr_fmt
#define pr_fmt(fmt) "crash hp: " fmt

/*
 * Different than kexec/kdump loading/unloading/jumping/shrinking which
 * usually rarely happen, there will be many crash hotplug events notified
 * during one short period, e.g one memory board is hot added and memory
 * regions are online. So mutex lock  __crash_hotplug_lock is used to
 * serialize the crash hotplug handling specifically.
 */
DEFINE_MUTEX(__crash_hotplug_lock);
#define crash_hotplug_lock() mutex_lock(&__crash_hotplug_lock)
#define crash_hotplug_unlock() mutex_unlock(&__crash_hotplug_lock)

/*
 * This routine utilized when the crash_hotplug sysfs node is read.
 * It reflects the kernel's ability/permission to update the crash
 * elfcorehdr directly.
 */
int crash_check_update_elfcorehdr(void)
{
	int rc = 0;

	crash_hotplug_lock();
	/* Obtain lock while reading crash information */
	if (!kexec_trylock()) {
		pr_info("kexec_trylock() failed, elfcorehdr may be inaccurate\n");
		crash_hotplug_unlock();
		return 0;
	}
	if (kexec_crash_image) {
		if (kexec_crash_image->file_mode)
			rc = 1;
		else
			rc = kexec_crash_image->update_elfcorehdr;
	}
	/* Release lock now that update complete */
	kexec_unlock();
	crash_hotplug_unlock();

	return rc;
}

/*
 * To accurately reflect hot un/plug changes of cpu and memory resources
 * (including onling and offlining of those resources), the elfcorehdr
 * (which is passed to the crash kernel via the elfcorehdr= parameter)
 * must be updated with the new list of CPUs and memories.
 *
 * In order to make changes to elfcorehdr, two conditions are needed:
 * First, the segment containing the elfcorehdr must be large enough
 * to permit a growing number of resources; the elfcorehdr memory size
 * is based on NR_CPUS_DEFAULT and CRASH_MAX_MEMORY_RANGES.
 * Second, purgatory must explicitly exclude the elfcorehdr from the
 * list of segments it checks (since the elfcorehdr changes and thus
 * would require an update to purgatory itself to update the digest).
 */
static void crash_handle_hotplug_event(unsigned int hp_action, unsigned int cpu)
{
	struct kimage *image;

	crash_hotplug_lock();
	/* Obtain lock while changing crash information */
	if (!kexec_trylock()) {
		pr_info("kexec_trylock() failed, elfcorehdr may be inaccurate\n");
		crash_hotplug_unlock();
		return;
	}

	/* Check kdump is not loaded */
	if (!kexec_crash_image)
		goto out;

	image = kexec_crash_image;

	/* Check that updating elfcorehdr is permitted */
	if (!(image->file_mode || image->update_elfcorehdr))
		goto out;

	if (hp_action == KEXEC_CRASH_HP_ADD_CPU ||
		hp_action == KEXEC_CRASH_HP_REMOVE_CPU)
		pr_debug("hp_action %u, cpu %u\n", hp_action, cpu);
	else
		pr_debug("hp_action %u\n", hp_action);

	/*
	 * The elfcorehdr_index is set to -1 when the struct kimage
	 * is allocated. Find the segment containing the elfcorehdr,
	 * if not already found.
	 */
	if (image->elfcorehdr_index < 0) {
		unsigned long mem;
		unsigned char *ptr;
		unsigned int n;

		for (n = 0; n < image->nr_segments; n++) {
			mem = image->segment[n].mem;
			ptr = kmap_local_page(pfn_to_page(mem >> PAGE_SHIFT));
			if (ptr) {
				/* The segment containing elfcorehdr */
				if (memcmp(ptr, ELFMAG, SELFMAG) == 0)
					image->elfcorehdr_index = (int)n;
				kunmap_local(ptr);
			}
		}
	}

	if (image->elfcorehdr_index < 0) {
		pr_err("unable to locate elfcorehdr segment");
		goto out;
	}

	/* Needed in order for the segments to be updated */
	arch_kexec_unprotect_crashkres();

	/* Differentiate between normal load and hotplug update */
	image->hp_action = hp_action;

	/* Now invoke arch-specific update handler */
	arch_crash_handle_hotplug_event(image);

	/* No longer handling a hotplug event */
	image->hp_action = KEXEC_CRASH_HP_NONE;
	image->elfcorehdr_updated = true;

	/* Change back to read-only */
	arch_kexec_protect_crashkres();

	/* Errors in the callback is not a reason to rollback state */
out:
	/* Release lock now that update complete */
	kexec_unlock();
	crash_hotplug_unlock();
}

static int crash_memhp_notifier(struct notifier_block *nb, unsigned long val, void *v)
{
	switch (val) {
	case MEM_ONLINE:
		crash_handle_hotplug_event(KEXEC_CRASH_HP_ADD_MEMORY,
			KEXEC_CRASH_HP_INVALID_CPU);
		break;

	case MEM_OFFLINE:
		crash_handle_hotplug_event(KEXEC_CRASH_HP_REMOVE_MEMORY,
			KEXEC_CRASH_HP_INVALID_CPU);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block crash_memhp_nb = {
	.notifier_call = crash_memhp_notifier,
	.priority = 0
};

static int crash_cpuhp_online(unsigned int cpu)
{
	crash_handle_hotplug_event(KEXEC_CRASH_HP_ADD_CPU, cpu);
	return 0;
}

static int crash_cpuhp_offline(unsigned int cpu)
{
	crash_handle_hotplug_event(KEXEC_CRASH_HP_REMOVE_CPU, cpu);
	return 0;
}

static int __init crash_hotplug_init(void)
{
	int result = 0;

	if (IS_ENABLED(CONFIG_MEMORY_HOTPLUG))
		register_memory_notifier(&crash_memhp_nb);

	if (IS_ENABLED(CONFIG_HOTPLUG_CPU)) {
		result = cpuhp_setup_state_nocalls(CPUHP_BP_PREPARE_DYN,
			"crash/cpuhp", crash_cpuhp_online, crash_cpuhp_offline);
	}

	return result;
}

subsys_initcall(crash_hotplug_init);
#endif
