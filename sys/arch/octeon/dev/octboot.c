/*	$OpenBSD: octboot.c,v 1.5 2022/01/10 16:21:19 visa Exp $	*/

/*
 * Copyright (c) 2019-2020 Visa Hankala
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/exec_elf.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/mount.h>

#include <uvm/uvm_extern.h>

#include <mips64/memconf.h>

#include <machine/autoconf.h>
#include <machine/octboot.h>
#include <machine/octeonvar.h>

typedef void (*kentry)(register_t, register_t, register_t, register_t);
#define PRIMARY 1

int	octboot_kexec(struct octboot_kexec_args *, struct proc *);
int	octboot_read(struct octboot_kexec_args *, void *, size_t, off_t);

uint64_t	octeon_boot_entry;
uint32_t	octeon_boot_ready;

void
octbootattach(int num)
{
}

int
octbootopen(dev_t dev, int flags, int mode, struct proc *p)
{
	return (0);
}

int
octbootclose(dev_t dev, int flags, int mode, struct proc *p)
{
	return (0);
}

int
octbootioctl(dev_t dev, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	int error = 0;

	switch (cmd) {
	case OBIOC_GETROOTDEV:
		if (strlen(uboot_rootdev) == 0) {
			error = ENOENT;
			break;
		}
		strlcpy((char *)data, uboot_rootdev, PATH_MAX);
		break;

	case OBIOC_KEXEC:
		error = suser(p);
		if (error != 0)
			break;
		error = octboot_kexec((struct octboot_kexec_args *)data, p);
		break;

	default:
		error = ENOTTY;
		break;
	}

	return error;
}

int
octboot_kexec(struct octboot_kexec_args *kargs, struct proc *p)
{
	extern char start[], end[];
	Elf_Ehdr eh;
	Elf_Phdr *ph = NULL;
	Elf_Shdr *sh = NULL;
	paddr_t ekern = 0, elfp, maxp = 0, off, pa, shp;
	size_t phsize = 0, shsize = 0, shstrsize = 0;
	size_t len, size;
	char *argbuf = NULL, *argptr;
	char *shstr = NULL;
	int argc = 0, error, havesyms = 0, i, nalloc = 0;

	memset(&eh, 0, sizeof(eh));

	/*
	 * Load kernel arguments into a temporary buffer.
	 * This also translates the userspace argv pointers to kernel pointers.
	 */
	argbuf = malloc(PAGE_SIZE, M_TEMP, M_NOWAIT);
	if (argbuf == NULL) {
		error = ENOMEM;
		goto fail;
	}
	argptr = argbuf;
	for (i = 0; i < OCTBOOT_MAX_ARGS && kargs->argv[i] != NULL; i++) {
		len = argbuf + PAGE_SIZE - argptr;
		error = copyinstr(kargs->argv[i], argptr, len, &len);
		if (error != 0)
			goto fail;
		kargs->argv[i] = argptr;
		argptr += len;
		argc++;
	}

	/*
	 * Read the headers and validate them.
	 */
	error = octboot_read(kargs, &eh, sizeof(eh), 0);
	if (error != 0)
		goto fail;

	/* Load program headers. */
	ph = mallocarray(eh.e_phnum, sizeof(Elf_Phdr), M_TEMP, M_NOWAIT);
	if (ph == NULL) {
		error = ENOMEM;
		goto fail;
	}
	phsize = eh.e_phnum * sizeof(Elf_Phdr);
	error = octboot_read(kargs, ph, phsize, eh.e_phoff);
	if (error != 0)
		goto fail;

	/* Load section headers. */
	sh = mallocarray(eh.e_shnum, sizeof(Elf_Shdr), M_TEMP, M_NOWAIT);
	if (sh == NULL) {
		error = ENOMEM;
		goto fail;
	}
	shsize = eh.e_shnum * sizeof(Elf_Shdr);
	error = octboot_read(kargs, sh, shsize, eh.e_shoff);
	if (error != 0)
		goto fail;

	/* Sanity-check addresses. */
	for (i = 0; i < eh.e_phnum; i++) {
		if (ph[i].p_type != PT_LOAD &&
		    ph[i].p_type != PT_OPENBSD_RANDOMIZE)
			continue;
		if (ph[i].p_paddr < CKSEG0_BASE ||
		    ph[i].p_paddr + ph[i].p_memsz >= CKSEG0_BASE + CKSEG_SIZE) {
			error = ENOEXEC;
			goto fail;
		}
	}

	/*
	 * Allocate physical memory and load the segments.
	 */

	for (i = 0; i < eh.e_phnum; i++) {
		if (ph[i].p_type != PT_LOAD)
			continue;
		pa = CKSEG0_TO_PHYS(ph[i].p_paddr);
		size = roundup(ph[i].p_memsz, BOOTMEM_BLOCK_ALIGN);
		if (bootmem_alloc_region(pa, size) != 0) {
			printf("kexec: failed to allocate segment "
			    "0x%lx @ 0x%lx\n", size, pa);
			error = ENOMEM;
			goto fail;
		}
		if (maxp < pa + size)
			maxp = pa + size;
		nalloc++;
	}

	for (i = 0; i < eh.e_phnum; i++) {
		if (ph[i].p_type == PT_OPENBSD_RANDOMIZE) {
			/* Assume that the segment is inside a LOAD segment. */
			arc4random_buf((caddr_t)ph[i].p_paddr, ph[i].p_filesz);
			continue;
		}

		if (ph[i].p_type != PT_LOAD)
			continue;

		error = octboot_read(kargs, (caddr_t)ph[i].p_paddr,
		    ph[i].p_filesz, ph[i].p_offset);
		if (error != 0)
			goto fail;

		/* Clear any BSS. */
		if (ph[i].p_memsz > ph[i].p_filesz) {
			memset((caddr_t)ph[i].p_paddr + ph[i].p_filesz,
			    0, ph[i].p_memsz - ph[i].p_filesz);
		}
	}
	ekern = maxp;

	for (i = 0; i < eh.e_shnum; i++) {
		if (sh[i].sh_type == SHT_SYMTAB) {
			havesyms = 1;
			break;
		}
	}

	if (havesyms) {
		/* Reserve space for ssym and esym pointers. */
		maxp += sizeof(int32_t) * 2;

		elfp = roundup(maxp, sizeof(Elf_Addr));
		maxp = elfp + sizeof(Elf_Ehdr);
		shp = maxp;
		maxp = shp + roundup(shsize, sizeof(Elf_Addr));
		maxp = roundup(maxp, BOOTMEM_BLOCK_ALIGN);
		if (bootmem_alloc_region(ekern, maxp - ekern) != 0) {
			printf("kexec: failed to allocate %zu bytes for ELF "
			    "and section headers\n", maxp - ekern);
			error = ENOMEM;
			goto fail;
		}

		shstrsize = sh[eh.e_shstrndx].sh_size;
		shstr = malloc(shstrsize, M_TEMP, M_NOWAIT);
		if (shstr == NULL) {
			error = ENOMEM;
			goto fail;
		}
		error = octboot_read(kargs, shstr, shstrsize,
		    sh[eh.e_shstrndx].sh_offset);
		if (error != 0)
			goto fail;

		off = maxp - elfp;
		for (i = 0; i < eh.e_shnum; i++) {
			if (sh[i].sh_type == SHT_STRTAB ||
			    sh[i].sh_type == SHT_SYMTAB ||
			    strcmp(shstr + sh[i].sh_name, ELF_CTF) == 0 ||
			    strcmp(shstr + sh[i].sh_name, ".debug_line") == 0) {
				size_t bsize = roundup(sh[i].sh_size,
				    BOOTMEM_BLOCK_ALIGN);

				if (bootmem_alloc_region(maxp, bsize) != 0) {
					error = ENOMEM;
					goto fail;
				}
				error = octboot_read(kargs,
				    (caddr_t)PHYS_TO_CKSEG0(maxp),
				    sh[i].sh_size, sh[i].sh_offset);
				maxp += bsize;
				if (error != 0)
					goto fail;
				sh[i].sh_offset = off;
				sh[i].sh_flags |= SHF_ALLOC;
				off += bsize;
			}
		}

		eh.e_phoff = 0;
		eh.e_shoff = sizeof(eh);
		eh.e_phentsize = 0;
		eh.e_phnum = 0;
		memcpy((caddr_t)PHYS_TO_CKSEG0(elfp), &eh, sizeof(eh));
		memcpy((caddr_t)PHYS_TO_CKSEG0(shp), sh, shsize);

		*(int32_t *)PHYS_TO_CKSEG0(ekern) = PHYS_TO_CKSEG0(elfp);
		*((int32_t *)PHYS_TO_CKSEG0(ekern) + 1) = PHYS_TO_CKSEG0(maxp);
	}

	/*
	 * Put kernel arguments in place.
	 */
	octeon_boot_desc->argc = 0;
	for (i = 0; i < OCTEON_ARGV_MAX; i++)
		octeon_boot_desc->argv[i] = 0;
	if (argptr > argbuf) {
		size = roundup(argptr - argbuf, BOOTMEM_BLOCK_ALIGN);
		if (bootmem_alloc_region(maxp, size) != 0) {
			error = ENOMEM;
			goto fail;
		}
		memcpy((caddr_t)PHYS_TO_CKSEG0(maxp), argbuf, argptr - argbuf);
		for (i = 0; i < argc; i++) {
			KASSERT(kargs->argv[i] >= argbuf);
			KASSERT(kargs->argv[i] < argbuf + PAGE_SIZE);
			octeon_boot_desc->argv[i] = kargs->argv[i] - argbuf +
			    maxp;
		}
		octeon_boot_desc->argc = argc;
		maxp += size;
	}

	vfs_shutdown(p);

	printf("launching kernel\n");

	config_suspend_all(DVACT_POWERDOWN);

	intr_disable();

	/* Put UVM memory back to the free list. */
	for (i = 0; mem_layout[i].mem_last_page != 0; i++) {
		uint64_t fp = mem_layout[i].mem_first_page;
		uint64_t lp = mem_layout[i].mem_last_page;

		bootmem_free(ptoa(fp), ptoa(lp) - ptoa(fp));
	}

	/*
	 * Release the memory of the bootloader kernel.
	 * This may overwrite a tiny region at the start of the running image.
	 */
	bootmem_free(CKSEG0_TO_PHYS((vaddr_t)start), end - start);

	/* Let secondary cores proceed to the new kernel. */
	octeon_boot_entry = eh.e_entry;
	octeon_syncw();		/* Order writes. */
	octeon_boot_ready = 1;	/* Open the gate. */
	octeon_syncw();		/* Flush writes. */
	delay(1000);		/* Give secondary cores a lead. */

	__asm__ volatile (
	"	cache 1, 0($0)\n"	/* Flush and invalidate dcache. */
	"	cache 0, 0($0)\n"	/* Invalidate icache. */
	::: "memory");

	(*(kentry)eh.e_entry)(0, 0, PRIMARY, (register_t)octeon_boot_desc);

	for (;;)
		continue;

fail:
	if (ekern != 0)
		bootmem_free(ekern, maxp - ekern);
	for (i = 0; i < eh.e_phnum && nalloc > 0; i++) {
		if (ph[i].p_type == PT_LOAD) {
			pa = CKSEG0_TO_PHYS(ph[i].p_paddr);
			bootmem_free(pa, ph[i].p_memsz);
			nalloc--;
		}
	}
	free(shstr, M_TEMP, shstrsize);
	free(sh, M_TEMP, shsize);
	free(ph, M_TEMP, phsize);
	free(argbuf, M_TEMP, PAGE_SIZE);
	return error;
}

int
octboot_read(struct octboot_kexec_args *kargs, void *buf, size_t size,
    off_t off)
{
	if (off + size < off || off + size > kargs->klen)
		return ENOEXEC;
	return copyin(kargs->kimg + off, buf, size);
}
