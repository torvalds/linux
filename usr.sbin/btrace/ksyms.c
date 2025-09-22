/*	$OpenBSD: ksyms.c,v 1.11 2025/09/22 07:49:43 sashan Exp $ */

/*
 * Copyright (c) 2016 Martin Pieuchot <mpi@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
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

#define _DYN_LOADER	/* needed for AuxInfo */

#include <sys/types.h>

#include <err.h>
#include <fcntl.h>
#include <gelf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <dev/dt/dtvar.h>
#include <sys/queue.h>

#include "btrace.h"

struct sym {
	char *sym_name;
	unsigned long sym_value;	/* from st_value */
	unsigned long sym_size;		/* from st_size */
};

struct syms {
	struct sym *table;
	size_t nsymb;
};

struct shlib_syms {
	struct syms		*sls_syms;
	caddr_t			 sls_start;
	caddr_t			 sls_end;
	LIST_ENTRY(shlib_syms)	 sls_le;
};

static LIST_HEAD(table, shlib_syms) shlib_lh = LIST_HEAD_INITIALIZER(table);

static int sym_compare_search(const void *, const void *);
static int sym_compare_sort(const void *, const void *);

static struct shlib_syms *
read_syms(Elf *elf, const void *offset, const void *dtrv_va)
{
	char *name;
	Elf_Data *data = NULL;
	Elf_Scn	*scn = NULL, *symtab = NULL;
	GElf_Sym sym;
	GElf_Shdr shdr;
	GElf_Phdr phdr;
	size_t phnum, i;
	size_t shstrndx, strtabndx = SIZE_MAX, symtab_size;
	unsigned long diff;
	struct sym *tmp;
	struct syms *syms = NULL;
	uintptr_t base_addr = 0;
	struct shlib_syms *sls;
	void *v_start, *v_end;

	if (elf_kind(elf) != ELF_K_ELF) {
		warnx("elf_keind() != ELF_K_ELF");
		return NULL;
	}


	if (elf_getshdrstrndx(elf, &shstrndx) != 0) {
		warnx("elf_getshdrstrndx: %s", elf_errmsg(-1));
		return NULL;
	}

	if (elf_getphdrnum(elf, &phnum) != 0) {
		warnx("elf_getphdrnum: %s", elf_errmsg(-1));
		return NULL;
	}

	/*
	 * calculate base_addr where DSO got loaded to at traced process.
	 * We need to combine
	 *	- address of .text segment (.dtrv_start)
	 *	- offset of .text segment (.dtrv_offset)
	 *	- program address we are resolving (.dtrv_va)
	 *	- .p_offset and .p_vaddr from elf header
	 *
	 * the offset comes from caller, caller did a math for us already:
	 *	offset =  .dtrv_offset + (.dtrv_va - .dtrv_start)
	 * Here we also use offset to find program header such offset
	 * fits into range <.p_offset, .p_offset + .p_filesz). The matching
	 * program header provides the last two pieces: .p_vaddr and .p_offset
	 * to calculate base_addr:
	 *	(dtrv_va - .p_vaddr) - (offset - .p_offset)
	 */
	v_start = NULL;
	v_end = NULL;
	for (i = 0; i < phnum && base_addr == 0 && v_start == NULL; i++) {
		if (gelf_getphdr(elf, i, &phdr) == NULL) {
			warnx("gelf_getphdr(%zu): %s", i, elf_errmsg(-1));
			continue;
		}

		if (base_addr == 0 && (void *)phdr.p_offset <= offset &&
		    offset < (void *)(phdr.p_offset + phdr.p_filesz))
			base_addr =
			    (uintptr_t)((dtrv_va - (void *)phdr.p_vaddr) -
			    (offset - (void *)phdr.p_offset));

		/*
		 * while walking the headers, find virtual address of .text
		 */
		if ((phdr.p_flags & PF_X) != 0) {
			v_start = (void *)phdr.p_vaddr;
			v_end = (void *)(phdr.p_vaddr + phdr.p_memsz);
		}
	}
	sls = malloc(sizeof (struct shlib_syms));
	if (sls == NULL) {
		err(1, "malloc");
	} else {
		sls->sls_start = v_start + base_addr;
		sls->sls_end = v_end + base_addr;
		sls->sls_syms = NULL;
	}


	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		if (gelf_getshdr(scn, &shdr) != &shdr) {
			warnx("elf_getshdr: %s", elf_errmsg(-1));
			return sls;
		}
		if ((name = elf_strptr(elf, shstrndx, shdr.sh_name)) == NULL) {
			warnx("elf_strptr: %s", elf_errmsg(-1));
			return sls;
		}
		if (strcmp(name, ELF_SYMTAB) == 0 &&
		    shdr.sh_type == SHT_SYMTAB && shdr.sh_entsize != 0) {
			symtab = scn;
			symtab_size = shdr.sh_size / shdr.sh_entsize;
		}
		if (strcmp(name, ELF_STRTAB) == 0 &&
		    shdr.sh_type == SHT_STRTAB) {
			strtabndx = elf_ndxscn(scn);
		}
	}
	if (symtab == NULL) {
		warnx("%s: %s: section not found", __func__, ELF_SYMTAB);
		return sls;
	}
	if (strtabndx == SIZE_MAX) {
		warnx("%s: %s: section not found", __func__, ELF_STRTAB);
		return sls;
	}

	data = elf_rawdata(symtab, data);
	if (data == NULL) {
		warnx("%s elf_rwadata() unable to read syms from\n", __func__);
		return sls;
	}

	if ((syms = calloc(1, sizeof(*syms))) == NULL)
		err(1, NULL);
	syms->table = calloc(symtab_size, sizeof *syms->table);
	if (syms->table == NULL)
		err(1, "calloc");
	for (i = 0; i < symtab_size; i++) {
		if (gelf_getsym(data, i, &sym) == NULL)
			continue;
		if (GELF_ST_TYPE(sym.st_info) != STT_FUNC)
			continue;
		name = elf_strptr(elf, strtabndx, sym.st_name);
		if (name == NULL)
			continue;
		if (sym.st_value == 0)
			continue;
		syms->table[syms->nsymb].sym_name = strdup(name);
		if (syms->table[syms->nsymb].sym_name == NULL)
			err(1, NULL);
		syms->table[syms->nsymb].sym_value = sym.st_value + base_addr;
		syms->table[syms->nsymb].sym_size = sym.st_size;
		syms->nsymb++;
	}
	tmp = reallocarray(syms->table, syms->nsymb, sizeof *syms->table);
	if (tmp == NULL)
		err(1, "reallocarray");
	syms->table = tmp;

	/* Sort symbols in ascending order by address. */
	qsort(syms->table, syms->nsymb, sizeof *syms->table, sym_compare_sort);

	/*
	 * Some functions, particularly those written in assembly, have an
	 * st_size of zero.  We can approximate a size for these by assuming
	 * that they extend from their st_value to that of the next function.
	 */
	for (i = 0; i < syms->nsymb; i++) {
		if (syms->table[i].sym_size != 0)
			continue;
		/* Can't do anything for the last symbol. */
		if (i + 1 == syms->nsymb)
			continue;
		diff = syms->table[i + 1].sym_value - syms->table[i].sym_value;
		syms->table[i].sym_size = diff;
	}

	sls->sls_syms = syms;

	return sls;
}

static struct shlib_syms *
read_syms_buf(char *elfbuf, size_t elfbuf_sz, caddr_t offset, caddr_t dtrv_va)
{
	Elf *elf;
	struct shlib_syms *sls;

	if (elf_version(EV_CURRENT) == EV_NONE)
		errx(1, "elf_version: %s", elf_errmsg(-1));

	if ((elf = elf_memory(elfbuf, elfbuf_sz)) == NULL) {
		warnx("elf_memory: %s", elf_errmsg(-1));
		return NULL;
	}

	sls = read_syms(elf, offset, dtrv_va);
	elf_end(elf);

	return sls;
}

static void
free_syms(struct syms *syms)
{
	size_t i;

	if (syms != NULL) {
		for (i = 0; i < syms->nsymb; i++)
			free(syms->table[i].sym_name);

		free(syms->table);
		free(syms);
	}
}

static struct shlib_syms *
load_syms(int dtdev, pid_t pid, caddr_t pc)
{
	struct shlib_syms *new_sls, *mark_sls, *sls;
	struct dtioc_rdvn dtrv;
        char *elfbuf;

	memset(&dtrv, 0, sizeof (dtrv));
	dtrv.dtrv_pid = pid;
	dtrv.dtrv_va = pc;
	dtrv.dtrv_fd = 0;
	if (ioctl(dtdev, DTIOCRDVNODE, &dtrv)) {
		warn("DTIOCRDVNODE fails for %p\n", pc);
		return NULL;
	}

	elfbuf = mmap(NULL, dtrv.dtrv_len, PROT_READ, MAP_PRIVATE,
	    dtrv.dtrv_fd, 0);
	if (elfbuf == MAP_FAILED) {
		warn("mmap");
		close(dtrv.dtrv_fd);
		return NULL;
	}
	/*
	 * calculate offset we use to determine the base_addr where DSO got
	 * loaded at in traced process.
	 */
	dtrv.dtrv_offset += (dtrv.dtrv_va - dtrv.dtrv_start);
	new_sls = read_syms_buf(elfbuf, dtrv.dtrv_len, dtrv.dtrv_offset,
	    dtrv.dtrv_va);
	munmap(elfbuf, dtrv.dtrv_len);
	close(dtrv.dtrv_fd);
	if (new_sls == NULL) {
		warn("%s malloc(shlib_syms))", __func__);
		return NULL;
	}

	/*
	 * Keep list of symbol tables sorted ascending from address.
	 */
	mark_sls = NULL;
	LIST_FOREACH(sls, &shlib_lh, sls_le) {
		mark_sls = sls;
		if (new_sls->sls_start < sls->sls_start)
			break;
	}
	if (mark_sls == NULL)
		LIST_INSERT_HEAD(&shlib_lh, new_sls, sls_le);
	else if (new_sls->sls_start > mark_sls->sls_start)
		LIST_INSERT_AFTER(mark_sls, new_sls, sls_le);
	else
		LIST_INSERT_BEFORE(mark_sls, new_sls, sls_le);

	return new_sls;
}

static struct shlib_syms *
find_shlib(caddr_t pc)
{
	struct shlib_syms *sls, *match_sls;

	match_sls = NULL;
	LIST_FOREACH(sls, &shlib_lh, sls_le) {
		if (sls->sls_start > pc)
			break;
		match_sls = sls;
	}

	/*
	 * program counter must fit <sls_start, sls_end> range,
	 * if it does not, then the address has not been resolved
	 * yet.
	 */
	if (match_sls == NULL || match_sls->sls_end <= pc)
		match_sls = NULL;

	return match_sls;
}

static int
sym_compare_sort(const void *ap, const void *bp)
{
	const struct sym *a = ap, *b = bp;

	if (a->sym_value < b->sym_value)
		return -1;
	return a->sym_value > b->sym_value;
}

static int
sym_compare_search(const void *keyp, const void *entryp)
{
	const struct sym *entry = entryp, *key = keyp;

	if (key->sym_value < entry->sym_value)
		return -1;
	return key->sym_value >= entry->sym_value + entry->sym_size;
}

struct syms *
kelf_open_kernel(const char *path)
{
	struct shlib_syms *sls;
	struct syms *syms;
	Elf *elf;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return NULL;

	if (elf_version(EV_CURRENT) == EV_NONE)
		errx(1, "elf_version: %s", elf_errmsg(-1));

	elf = elf_begin(fd, ELF_C_READ, NULL);
	if (elf == NULL) {
		close(fd);
		return NULL;
	}

	sls = read_syms(elf, 0, 0);
	elf_end(elf);
	close(fd);

	if (sls != NULL) {
		syms = sls->sls_syms;
		free(sls);
	} else {
		syms = NULL;
	}

	return syms;
}

void
kelf_open(void)
{
	LIST_INIT(&shlib_lh);
}

void
kelf_close(struct syms *ksyms)
{
	struct shlib_syms *sls;

	while (!LIST_EMPTY(&shlib_lh)) {
		sls = LIST_FIRST(&shlib_lh);
		LIST_REMOVE(sls, sls_le);
		free_syms(sls->sls_syms);
		free(sls);
	}

	free_syms(ksyms);
}

int
kelf_snprintsym_proc(int dtfd, pid_t pid, char *str, size_t size,
    unsigned long pc)
{
	struct shlib_syms *sls;
	struct sym key = { .sym_value = pc };
	struct sym *entry;
	Elf_Addr offset;

	*str = '\0';
	if (pc == 0)
		return 0;

	if ((sls = find_shlib((caddr_t)key.sym_value)) == NULL)
		sls = load_syms(dtfd, pid, (caddr_t)key.sym_value);

	if (sls == NULL || sls->sls_syms == NULL)
		return snprintf(str, size, "\n0x%lx", pc);

	entry = bsearch(&key, sls->sls_syms->table, sls->sls_syms->nsymb,
	    sizeof *sls->sls_syms->table, sym_compare_search);
	if (entry == NULL)
		return snprintf(str, size, "\n0x%lx", pc);

	offset = pc - entry->sym_value;
	if (offset != 0) {
		return snprintf(str, size, "\n%s+0x%llx",
		    entry->sym_name, (unsigned long long)offset);
	}

	return snprintf(str, size, "\n%s", entry->sym_name);
}

int
kelf_snprintsym_kernel(struct syms *syms, char *str, size_t size,
    unsigned long pc)
{
	struct sym key = { .sym_value = pc };
	struct sym *entry;
	Elf_Addr offset;

	entry = bsearch(&key, syms->table, syms->nsymb, sizeof *syms->table,
	    sym_compare_search);
	if (entry == NULL)
		return snprintf(str, size, "\n0x%lx", pc);

	offset = pc - entry->sym_value;
	if (offset != 0) {
		return snprintf(str, size, "\n%s+0x%llx",
		    entry->sym_name, (unsigned long long)offset);
	}

	return snprintf(str, size, "\n%s", entry->sym_name);
}
