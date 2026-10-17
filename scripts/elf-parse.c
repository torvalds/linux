#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "elf-parse.h"

struct elf_funcs elf_parser;

/*
 * Get the whole file as a programming convenience in order to avoid
 * malloc+lseek+read+free of many pieces.  If successful, then mmap
 * avoids copying unused pieces; else just read the whole file.
 * Open for both read and write if writable is true, otherwise open
 * read-only.
 */
static void *map_file(char const *fname, size_t *size, bool writable)
{
	int fd, prot = PROT_READ, flags = MAP_PRIVATE;
	struct stat sb;
	void *addr = NULL;

	if (writable) {
		prot |= PROT_WRITE;
		flags = MAP_SHARED;
	}

	fd = open(fname, writable ? O_RDWR : O_RDONLY);
	if (fd < 0) {
		perror(fname);
		return NULL;
	}
	if (fstat(fd, &sb) < 0) {
		perror(fname);
		goto out;
	}
	if (!S_ISREG(sb.st_mode)) {
		fprintf(stderr, "not a regular file: %s\n", fname);
		goto out;
	}

	addr = mmap(0, sb.st_size, prot, flags, fd, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "Could not mmap file: %s\n", fname);
		goto out;
	}

	*size = sb.st_size;

out:
	close(fd);
	return addr;
}

static int elf_parse(const char *fname, void *addr, uint32_t types)
{
	Elf_Ehdr *ehdr = addr;
	uint16_t type;

	if (memcmp(ELFMAG, ehdr->e32.e_ident, SELFMAG) != 0 ||
	    ehdr->e32.e_ident[EI_VERSION] != EV_CURRENT) {
		fprintf(stderr, "unrecognized ELF file %s\n", fname);
		return -1;
	}

	switch (ehdr->e32.e_ident[EI_DATA]) {
	case ELFDATA2LSB:
		elf_parser.r	= rle;
		elf_parser.r2	= r2le;
		elf_parser.r8	= r8le;
		elf_parser.w	= wle;
		elf_parser.w8	= w8le;
		break;
	case ELFDATA2MSB:
		elf_parser.r	= rbe;
		elf_parser.r2	= r2be;
		elf_parser.r8	= r8be;
		elf_parser.w	= wbe;
		elf_parser.w8	= w8be;
		break;
	default:
		fprintf(stderr, "unrecognized ELF data encoding %d: %s\n",
			ehdr->e32.e_ident[EI_DATA], fname);
		return -1;
	}

	type = elf_parser.r2(&ehdr->e32.e_type);
	if (!((1 << type) & types)) {
		fprintf(stderr, "Invalid ELF type file %s\n", fname);
		return -1;
	}

	switch (ehdr->e32.e_ident[EI_CLASS]) {
	case ELFCLASS32: {
		elf_parser.ehdr_shoff		= ehdr32_shoff;
		elf_parser.ehdr_shentsize	= ehdr32_shentsize;
		elf_parser.ehdr_shstrndx	= ehdr32_shstrndx;
		elf_parser.ehdr_shnum		= ehdr32_shnum;
		elf_parser.shdr_addr		= shdr32_addr;
		elf_parser.shdr_offset		= shdr32_offset;
		elf_parser.shdr_link		= shdr32_link;
		elf_parser.shdr_size		= shdr32_size;
		elf_parser.shdr_name		= shdr32_name;
		elf_parser.shdr_type		= shdr32_type;
		elf_parser.shdr_entsize		= shdr32_entsize;
		elf_parser.shdr_flags		= shdr32_flags;
		elf_parser.sym_type		= sym32_type;
		elf_parser.sym_bind		= sym32_bind;
		elf_parser.sym_name		= sym32_name;
		elf_parser.sym_value		= sym32_value;
		elf_parser.sym_shndx		= sym32_shndx;
		elf_parser.rela_offset		= rela32_offset;
		elf_parser.rela_info		= rela32_info;
		elf_parser.rela_addend		= rela32_addend;
		elf_parser.rela_write_addend	= rela32_write_addend;

		if (elf_parser.r2(&ehdr->e32.e_ehsize) != sizeof(Elf32_Ehdr) ||
		    elf_parser.r2(&ehdr->e32.e_shentsize) != sizeof(Elf32_Shdr)) {
			fprintf(stderr,
				"unrecognized ET_EXEC/ET_DYN file: %s\n", fname);
			return -1;
		}

		}
		break;
	case ELFCLASS64: {
		elf_parser.ehdr_shoff		= ehdr64_shoff;
		elf_parser.ehdr_shentsize		= ehdr64_shentsize;
		elf_parser.ehdr_shstrndx		= ehdr64_shstrndx;
		elf_parser.ehdr_shnum		= ehdr64_shnum;
		elf_parser.shdr_addr		= shdr64_addr;
		elf_parser.shdr_offset		= shdr64_offset;
		elf_parser.shdr_link		= shdr64_link;
		elf_parser.shdr_size		= shdr64_size;
		elf_parser.shdr_name		= shdr64_name;
		elf_parser.shdr_type		= shdr64_type;
		elf_parser.shdr_entsize		= shdr64_entsize;
		elf_parser.shdr_flags		= shdr64_flags;
		elf_parser.sym_type		= sym64_type;
		elf_parser.sym_bind		= sym64_bind;
		elf_parser.sym_name		= sym64_name;
		elf_parser.sym_value		= sym64_value;
		elf_parser.sym_shndx		= sym64_shndx;
		elf_parser.rela_offset		= rela64_offset;
		elf_parser.rela_info		= rela64_info;
		elf_parser.rela_addend		= rela64_addend;
		elf_parser.rela_write_addend	= rela64_write_addend;

		if (elf_parser.r2(&ehdr->e64.e_ehsize) != sizeof(Elf64_Ehdr) ||
		    elf_parser.r2(&ehdr->e64.e_shentsize) != sizeof(Elf64_Shdr)) {
			fprintf(stderr,
				"unrecognized ET_EXEC/ET_DYN file: %s\n",
				fname);
			return -1;
		}

		}
		break;
	default:
		fprintf(stderr, "unrecognized ELF class %d %s\n",
			ehdr->e32.e_ident[EI_CLASS], fname);
		return -1;
	}
	return 0;
}

int elf_map_machine(void *addr)
{
	Elf_Ehdr *ehdr = addr;

	return elf_parser.r2(&ehdr->e32.e_machine);
}

int elf_map_long_size(void *addr)
{
	Elf_Ehdr *ehdr = addr;

	return ehdr->e32.e_ident[EI_CLASS] == ELFCLASS32 ? 4 : 8;
}

static void *__elf_map(char const *fname, size_t *size, uint32_t types,
		       bool writable)
{
	void *addr;
	int ret;

	addr = map_file(fname, size, writable);
	if (!addr)
		return NULL;

	ret = elf_parse(fname, addr, types);
	if (ret < 0) {
		elf_unmap(addr, *size);
		return NULL;
	}

	return addr;
}

void *elf_map(char const *fname, size_t *size, uint32_t types)
{
	return __elf_map(fname, size, types, true);
}

/* For tools that only read the file. */
void *elf_map_ro(char const *fname, size_t *size, uint32_t types)
{
	return __elf_map(fname, size, types, false);
}

void elf_unmap(void *addr, size_t size)
{
	munmap(addr, size);
}

/*
 * Open an ELF file and map it read-only, locating its section headers, section
 * names and symbol table which are populated in the elf out parameter.
 *
 * Returns 0 on success, otherwise -1.
 */
int elf_open_ro(char const *fname, uint32_t types, struct elf_file *elf)
{
	Elf_Ehdr *ehdr;
	Elf_Shdr *first;
	unsigned int shstrndx, i;

	memset(elf, 0, sizeof(*elf));
	elf->base = elf_map_ro(fname, &elf->size, types);
	if (!elf->base)
		return -1;

	ehdr = elf->base;
	elf->shdrs = (const char *)elf->base + ehdr_shoff(ehdr);
	elf->shentsize = ehdr_shentsize(ehdr);
	first = elf_section(elf, 0);

	/* A count or index too large for the header lives in section 0. */
	elf->shnum = ehdr_shnum(ehdr);
	if (elf->shnum == SHN_UNDEF)
		elf->shnum = shdr_size(first);
	shstrndx = ehdr_shstrndx(ehdr);
	if (shstrndx == SHN_XINDEX)
		shstrndx = shdr_link(first);
	elf->shstrtab = (const char *)elf->base +
			shdr_offset(elf_section(elf, shstrndx));

	for (i = 0; i < elf->shnum && !elf->symtab; i++)
		if (shdr_type(elf_section(elf, i)) == SHT_SYMTAB)
			elf->symtab = elf_section(elf, i);

	if (!elf->symtab) {
		fprintf(stderr, "%s: no symbol table\n", fname);
		elf_close(elf);
		return -1;
	}

	elf->strtab = (const char *)elf->base +
		      shdr_offset(elf_section(elf, shdr_link(elf->symtab)));
	elf->nr_syms = shdr_size(elf->symtab) / shdr_entsize(elf->symtab);

	return 0;
}

void elf_close(struct elf_file *elf)
{
	elf_unmap(elf->base, elf->size);
	elf->base = NULL;
}
