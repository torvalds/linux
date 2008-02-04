/* Postprocess module symbol versions
 *
 * Copyright 2003       Kai Germaschewski
 * Copyright 2002-2004  Rusty Russell, IBM Corporation
 * Copyright 2006-2008  Sam Ravnborg
 * Based in part on module-init-tools/depmod.c,file2alias
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Usage: modpost vmlinux module1.o module2.o ...
 */

#include <ctype.h>
#include "modpost.h"
#include "../../include/linux/license.h"

/* Are we using CONFIG_MODVERSIONS? */
int modversions = 0;
/* Warn about undefined symbols? (do so if we have vmlinux) */
int have_vmlinux = 0;
/* Is CONFIG_MODULE_SRCVERSION_ALL set? */
static int all_versions = 0;
/* If we are modposting external module set to 1 */
static int external_module = 0;
/* Warn about section mismatch in vmlinux if set to 1 */
static int vmlinux_section_warnings = 1;
/* Only warn about unresolved symbols */
static int warn_unresolved = 0;
/* How a symbol is exported */
static int sec_mismatch_count = 0;
static int sec_mismatch_verbose = 1;

enum export {
	export_plain,      export_unused,     export_gpl,
	export_unused_gpl, export_gpl_future, export_unknown
};

#define PRINTF __attribute__ ((format (printf, 1, 2)))

PRINTF void fatal(const char *fmt, ...)
{
	va_list arglist;

	fprintf(stderr, "FATAL: ");

	va_start(arglist, fmt);
	vfprintf(stderr, fmt, arglist);
	va_end(arglist);

	exit(1);
}

PRINTF void warn(const char *fmt, ...)
{
	va_list arglist;

	fprintf(stderr, "WARNING: ");

	va_start(arglist, fmt);
	vfprintf(stderr, fmt, arglist);
	va_end(arglist);
}

PRINTF void merror(const char *fmt, ...)
{
	va_list arglist;

	fprintf(stderr, "ERROR: ");

	va_start(arglist, fmt);
	vfprintf(stderr, fmt, arglist);
	va_end(arglist);
}

static int is_vmlinux(const char *modname)
{
	const char *myname;

	myname = strrchr(modname, '/');
	if (myname)
		myname++;
	else
		myname = modname;

	return (strcmp(myname, "vmlinux") == 0) ||
	       (strcmp(myname, "vmlinux.o") == 0);
}

void *do_nofail(void *ptr, const char *expr)
{
	if (!ptr)
		fatal("modpost: Memory allocation failure: %s.\n", expr);

	return ptr;
}

/* A list of all modules we processed */
static struct module *modules;

static struct module *find_module(char *modname)
{
	struct module *mod;

	for (mod = modules; mod; mod = mod->next)
		if (strcmp(mod->name, modname) == 0)
			break;
	return mod;
}

static struct module *new_module(char *modname)
{
	struct module *mod;
	char *p, *s;

	mod = NOFAIL(malloc(sizeof(*mod)));
	memset(mod, 0, sizeof(*mod));
	p = NOFAIL(strdup(modname));

	/* strip trailing .o */
	s = strrchr(p, '.');
	if (s != NULL)
		if (strcmp(s, ".o") == 0)
			*s = '\0';

	/* add to list */
	mod->name = p;
	mod->gpl_compatible = -1;
	mod->next = modules;
	modules = mod;

	return mod;
}

/* A hash of all exported symbols,
 * struct symbol is also used for lists of unresolved symbols */

#define SYMBOL_HASH_SIZE 1024

struct symbol {
	struct symbol *next;
	struct module *module;
	unsigned int crc;
	int crc_valid;
	unsigned int weak:1;
	unsigned int vmlinux:1;    /* 1 if symbol is defined in vmlinux */
	unsigned int kernel:1;     /* 1 if symbol is from kernel
				    *  (only for external modules) **/
	unsigned int preloaded:1;  /* 1 if symbol from Module.symvers */
	enum export  export;       /* Type of export */
	char name[0];
};

static struct symbol *symbolhash[SYMBOL_HASH_SIZE];

/* This is based on the hash agorithm from gdbm, via tdb */
static inline unsigned int tdb_hash(const char *name)
{
	unsigned value;	/* Used to compute the hash value.  */
	unsigned   i;	/* Used to cycle through random values. */

	/* Set the initial value from the key size. */
	for (value = 0x238F13AF * strlen(name), i = 0; name[i]; i++)
		value = (value + (((unsigned char *)name)[i] << (i*5 % 24)));

	return (1103515243 * value + 12345);
}

/**
 * Allocate a new symbols for use in the hash of exported symbols or
 * the list of unresolved symbols per module
 **/
static struct symbol *alloc_symbol(const char *name, unsigned int weak,
				   struct symbol *next)
{
	struct symbol *s = NOFAIL(malloc(sizeof(*s) + strlen(name) + 1));

	memset(s, 0, sizeof(*s));
	strcpy(s->name, name);
	s->weak = weak;
	s->next = next;
	return s;
}

/* For the hash of exported symbols */
static struct symbol *new_symbol(const char *name, struct module *module,
				 enum export export)
{
	unsigned int hash;
	struct symbol *new;

	hash = tdb_hash(name) % SYMBOL_HASH_SIZE;
	new = symbolhash[hash] = alloc_symbol(name, 0, symbolhash[hash]);
	new->module = module;
	new->export = export;
	return new;
}

static struct symbol *find_symbol(const char *name)
{
	struct symbol *s;

	/* For our purposes, .foo matches foo.  PPC64 needs this. */
	if (name[0] == '.')
		name++;

	for (s = symbolhash[tdb_hash(name) % SYMBOL_HASH_SIZE]; s; s = s->next) {
		if (strcmp(s->name, name) == 0)
			return s;
	}
	return NULL;
}

static struct {
	const char *str;
	enum export export;
} export_list[] = {
	{ .str = "EXPORT_SYMBOL",            .export = export_plain },
	{ .str = "EXPORT_UNUSED_SYMBOL",     .export = export_unused },
	{ .str = "EXPORT_SYMBOL_GPL",        .export = export_gpl },
	{ .str = "EXPORT_UNUSED_SYMBOL_GPL", .export = export_unused_gpl },
	{ .str = "EXPORT_SYMBOL_GPL_FUTURE", .export = export_gpl_future },
	{ .str = "(unknown)",                .export = export_unknown },
};


static const char *export_str(enum export ex)
{
	return export_list[ex].str;
}

static enum export export_no(const char *s)
{
	int i;

	if (!s)
		return export_unknown;
	for (i = 0; export_list[i].export != export_unknown; i++) {
		if (strcmp(export_list[i].str, s) == 0)
			return export_list[i].export;
	}
	return export_unknown;
}

static enum export export_from_sec(struct elf_info *elf, Elf_Section sec)
{
	if (sec == elf->export_sec)
		return export_plain;
	else if (sec == elf->export_unused_sec)
		return export_unused;
	else if (sec == elf->export_gpl_sec)
		return export_gpl;
	else if (sec == elf->export_unused_gpl_sec)
		return export_unused_gpl;
	else if (sec == elf->export_gpl_future_sec)
		return export_gpl_future;
	else
		return export_unknown;
}

/**
 * Add an exported symbol - it may have already been added without a
 * CRC, in this case just update the CRC
 **/
static struct symbol *sym_add_exported(const char *name, struct module *mod,
				       enum export export)
{
	struct symbol *s = find_symbol(name);

	if (!s) {
		s = new_symbol(name, mod, export);
	} else {
		if (!s->preloaded) {
			warn("%s: '%s' exported twice. Previous export "
			     "was in %s%s\n", mod->name, name,
			     s->module->name,
			     is_vmlinux(s->module->name) ?"":".ko");
		} else {
			/* In case Modules.symvers was out of date */
			s->module = mod;
		}
	}
	s->preloaded = 0;
	s->vmlinux   = is_vmlinux(mod->name);
	s->kernel    = 0;
	s->export    = export;
	return s;
}

static void sym_update_crc(const char *name, struct module *mod,
			   unsigned int crc, enum export export)
{
	struct symbol *s = find_symbol(name);

	if (!s)
		s = new_symbol(name, mod, export);
	s->crc = crc;
	s->crc_valid = 1;
}

void *grab_file(const char *filename, unsigned long *size)
{
	struct stat st;
	void *map;
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd < 0 || fstat(fd, &st) != 0)
		return NULL;

	*size = st.st_size;
	map = mmap(NULL, *size, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
	close(fd);

	if (map == MAP_FAILED)
		return NULL;
	return map;
}

/**
  * Return a copy of the next line in a mmap'ed file.
  * spaces in the beginning of the line is trimmed away.
  * Return a pointer to a static buffer.
  **/
char *get_next_line(unsigned long *pos, void *file, unsigned long size)
{
	static char line[4096];
	int skip = 1;
	size_t len = 0;
	signed char *p = (signed char *)file + *pos;
	char *s = line;

	for (; *pos < size ; (*pos)++) {
		if (skip && isspace(*p)) {
			p++;
			continue;
		}
		skip = 0;
		if (*p != '\n' && (*pos < size)) {
			len++;
			*s++ = *p++;
			if (len > 4095)
				break; /* Too long, stop */
		} else {
			/* End of string */
			*s = '\0';
			return line;
		}
	}
	/* End of buffer */
	return NULL;
}

void release_file(void *file, unsigned long size)
{
	munmap(file, size);
}

static int parse_elf(struct elf_info *info, const char *filename)
{
	unsigned int i;
	Elf_Ehdr *hdr;
	Elf_Shdr *sechdrs;
	Elf_Sym  *sym;

	hdr = grab_file(filename, &info->size);
	if (!hdr) {
		perror(filename);
		exit(1);
	}
	info->hdr = hdr;
	if (info->size < sizeof(*hdr)) {
		/* file too small, assume this is an empty .o file */
		return 0;
	}
	/* Is this a valid ELF file? */
	if ((hdr->e_ident[EI_MAG0] != ELFMAG0) ||
	    (hdr->e_ident[EI_MAG1] != ELFMAG1) ||
	    (hdr->e_ident[EI_MAG2] != ELFMAG2) ||
	    (hdr->e_ident[EI_MAG3] != ELFMAG3)) {
		/* Not an ELF file - silently ignore it */
		return 0;
	}
	/* Fix endianness in ELF header */
	hdr->e_shoff    = TO_NATIVE(hdr->e_shoff);
	hdr->e_shstrndx = TO_NATIVE(hdr->e_shstrndx);
	hdr->e_shnum    = TO_NATIVE(hdr->e_shnum);
	hdr->e_machine  = TO_NATIVE(hdr->e_machine);
	hdr->e_type     = TO_NATIVE(hdr->e_type);
	sechdrs = (void *)hdr + hdr->e_shoff;
	info->sechdrs = sechdrs;

	/* Check if file offset is correct */
	if (hdr->e_shoff > info->size) {
		fatal("section header offset=%lu in file '%s' is bigger than "
		      "filesize=%lu\n", (unsigned long)hdr->e_shoff,
		      filename, info->size);
		return 0;
	}

	/* Fix endianness in section headers */
	for (i = 0; i < hdr->e_shnum; i++) {
		sechdrs[i].sh_type   = TO_NATIVE(sechdrs[i].sh_type);
		sechdrs[i].sh_offset = TO_NATIVE(sechdrs[i].sh_offset);
		sechdrs[i].sh_size   = TO_NATIVE(sechdrs[i].sh_size);
		sechdrs[i].sh_link   = TO_NATIVE(sechdrs[i].sh_link);
		sechdrs[i].sh_name   = TO_NATIVE(sechdrs[i].sh_name);
		sechdrs[i].sh_info   = TO_NATIVE(sechdrs[i].sh_info);
		sechdrs[i].sh_addr   = TO_NATIVE(sechdrs[i].sh_addr);
	}
	/* Find symbol table. */
	for (i = 1; i < hdr->e_shnum; i++) {
		const char *secstrings
			= (void *)hdr + sechdrs[hdr->e_shstrndx].sh_offset;
		const char *secname;

		if (sechdrs[i].sh_offset > info->size) {
			fatal("%s is truncated. sechdrs[i].sh_offset=%lu > "
			      "sizeof(*hrd)=%zu\n", filename,
			      (unsigned long)sechdrs[i].sh_offset,
			      sizeof(*hdr));
			return 0;
		}
		secname = secstrings + sechdrs[i].sh_name;
		if (strcmp(secname, ".modinfo") == 0) {
			info->modinfo = (void *)hdr + sechdrs[i].sh_offset;
			info->modinfo_len = sechdrs[i].sh_size;
		} else if (strcmp(secname, "__ksymtab") == 0)
			info->export_sec = i;
		else if (strcmp(secname, "__ksymtab_unused") == 0)
			info->export_unused_sec = i;
		else if (strcmp(secname, "__ksymtab_gpl") == 0)
			info->export_gpl_sec = i;
		else if (strcmp(secname, "__ksymtab_unused_gpl") == 0)
			info->export_unused_gpl_sec = i;
		else if (strcmp(secname, "__ksymtab_gpl_future") == 0)
			info->export_gpl_future_sec = i;

		if (sechdrs[i].sh_type != SHT_SYMTAB)
			continue;

		info->symtab_start = (void *)hdr + sechdrs[i].sh_offset;
		info->symtab_stop  = (void *)hdr + sechdrs[i].sh_offset
			                         + sechdrs[i].sh_size;
		info->strtab       = (void *)hdr +
			             sechdrs[sechdrs[i].sh_link].sh_offset;
	}
	if (!info->symtab_start)
		fatal("%s has no symtab?\n", filename);

	/* Fix endianness in symbols */
	for (sym = info->symtab_start; sym < info->symtab_stop; sym++) {
		sym->st_shndx = TO_NATIVE(sym->st_shndx);
		sym->st_name  = TO_NATIVE(sym->st_name);
		sym->st_value = TO_NATIVE(sym->st_value);
		sym->st_size  = TO_NATIVE(sym->st_size);
	}
	return 1;
}

static void parse_elf_finish(struct elf_info *info)
{
	release_file(info->hdr, info->size);
}

#define CRC_PFX     MODULE_SYMBOL_PREFIX "__crc_"
#define KSYMTAB_PFX MODULE_SYMBOL_PREFIX "__ksymtab_"

static void handle_modversions(struct module *mod, struct elf_info *info,
			       Elf_Sym *sym, const char *symname)
{
	unsigned int crc;
	enum export export = export_from_sec(info, sym->st_shndx);

	switch (sym->st_shndx) {
	case SHN_COMMON:
		warn("\"%s\" [%s] is COMMON symbol\n", symname, mod->name);
		break;
	case SHN_ABS:
		/* CRC'd symbol */
		if (memcmp(symname, CRC_PFX, strlen(CRC_PFX)) == 0) {
			crc = (unsigned int) sym->st_value;
			sym_update_crc(symname + strlen(CRC_PFX), mod, crc,
					export);
		}
		break;
	case SHN_UNDEF:
		/* undefined symbol */
		if (ELF_ST_BIND(sym->st_info) != STB_GLOBAL &&
		    ELF_ST_BIND(sym->st_info) != STB_WEAK)
			break;
		/* ignore global offset table */
		if (strcmp(symname, "_GLOBAL_OFFSET_TABLE_") == 0)
			break;
		/* ignore __this_module, it will be resolved shortly */
		if (strcmp(symname, MODULE_SYMBOL_PREFIX "__this_module") == 0)
			break;
/* cope with newer glibc (2.3.4 or higher) STT_ definition in elf.h */
#if defined(STT_REGISTER) || defined(STT_SPARC_REGISTER)
/* add compatibility with older glibc */
#ifndef STT_SPARC_REGISTER
#define STT_SPARC_REGISTER STT_REGISTER
#endif
		if (info->hdr->e_machine == EM_SPARC ||
		    info->hdr->e_machine == EM_SPARCV9) {
			/* Ignore register directives. */
			if (ELF_ST_TYPE(sym->st_info) == STT_SPARC_REGISTER)
				break;
			if (symname[0] == '.') {
				char *munged = strdup(symname);
				munged[0] = '_';
				munged[1] = toupper(munged[1]);
				symname = munged;
			}
		}
#endif

		if (memcmp(symname, MODULE_SYMBOL_PREFIX,
			   strlen(MODULE_SYMBOL_PREFIX)) == 0) {
			mod->unres =
			  alloc_symbol(symname +
			               strlen(MODULE_SYMBOL_PREFIX),
			               ELF_ST_BIND(sym->st_info) == STB_WEAK,
			               mod->unres);
		}
		break;
	default:
		/* All exported symbols */
		if (memcmp(symname, KSYMTAB_PFX, strlen(KSYMTAB_PFX)) == 0) {
			sym_add_exported(symname + strlen(KSYMTAB_PFX), mod,
					export);
		}
		if (strcmp(symname, MODULE_SYMBOL_PREFIX "init_module") == 0)
			mod->has_init = 1;
		if (strcmp(symname, MODULE_SYMBOL_PREFIX "cleanup_module") == 0)
			mod->has_cleanup = 1;
		break;
	}
}

/**
 * Parse tag=value strings from .modinfo section
 **/
static char *next_string(char *string, unsigned long *secsize)
{
	/* Skip non-zero chars */
	while (string[0]) {
		string++;
		if ((*secsize)-- <= 1)
			return NULL;
	}

	/* Skip any zero padding. */
	while (!string[0]) {
		string++;
		if ((*secsize)-- <= 1)
			return NULL;
	}
	return string;
}

static char *get_next_modinfo(void *modinfo, unsigned long modinfo_len,
			      const char *tag, char *info)
{
	char *p;
	unsigned int taglen = strlen(tag);
	unsigned long size = modinfo_len;

	if (info) {
		size -= info - (char *)modinfo;
		modinfo = next_string(info, &size);
	}

	for (p = modinfo; p; p = next_string(p, &size)) {
		if (strncmp(p, tag, taglen) == 0 && p[taglen] == '=')
			return p + taglen + 1;
	}
	return NULL;
}

static char *get_modinfo(void *modinfo, unsigned long modinfo_len,
			 const char *tag)

{
	return get_next_modinfo(modinfo, modinfo_len, tag, NULL);
}

/**
 * Test if string s ends in string sub
 * return 0 if match
 **/
static int strrcmp(const char *s, const char *sub)
{
	int slen, sublen;

	if (!s || !sub)
		return 1;

	slen = strlen(s);
	sublen = strlen(sub);

	if ((slen == 0) || (sublen == 0))
		return 1;

	if (sublen > slen)
		return 1;

	return memcmp(s + slen - sublen, sub, sublen);
}

static const char *sym_name(struct elf_info *elf, Elf_Sym *sym)
{
	if (sym)
		return elf->strtab + sym->st_name;
	else
		return "";
}

static const char *sec_name(struct elf_info *elf, int shndx)
{
	Elf_Shdr *sechdrs = elf->sechdrs;
	return (void *)elf->hdr +
	        elf->sechdrs[elf->hdr->e_shstrndx].sh_offset +
	        sechdrs[shndx].sh_name;
}

static const char *sech_name(struct elf_info *elf, Elf_Shdr *sechdr)
{
	return (void *)elf->hdr +
	        elf->sechdrs[elf->hdr->e_shstrndx].sh_offset +
	        sechdr->sh_name;
}

/* if sym is empty or point to a string
 * like ".[0-9]+" then return 1.
 * This is the optional prefix added by ld to some sections
 */
static int number_prefix(const char *sym)
{
	if (*sym++ == '\0')
		return 1;
	if (*sym != '.')
		return 0;
	do {
		char c = *sym++;
		if (c < '0' || c > '9')
			return 0;
	} while (*sym);
	return 1;
}

/* The pattern is an array of simple patterns.
 * "foo" will match an exact string equal to "foo"
 * "*foo" will match a string that ends with "foo"
 * "foo*" will match a string that begins with "foo"
 * "foo$" will match a string equal to "foo" or "foo.1"
 *   where the '1' can be any number including several digits.
 *   The $ syntax is for sections where ld append a dot number
 *   to make section name unique.
 */
int match(const char *sym, const char * const pat[])
{
	const char *p;
	while (*pat) {
		p = *pat++;
		const char *endp = p + strlen(p) - 1;

		/* "*foo" */
		if (*p == '*') {
			if (strrcmp(sym, p + 1) == 0)
				return 1;
		}
		/* "foo*" */
		else if (*endp == '*') {
			if (strncmp(sym, p, strlen(p) - 1) == 0)
				return 1;
		}
		/* "foo$" */
		else if (*endp == '$') {
			if (strncmp(sym, p, strlen(p) - 1) == 0) {
				if (number_prefix(sym + strlen(p) - 1))
					return 1;
			}
		}
		/* no wildcards */
		else {
			if (strcmp(p, sym) == 0)
				return 1;
		}
	}
	/* no match */
	return 0;
}

/* sections that we do not want to do full section mismatch check on */
static const char *section_white_list[] =
	{ ".debug*", ".stab*", ".note*", ".got*", ".toc*", NULL };

/*
 * Is this section one we do not want to check?
 * This is often debug sections.
 * If we are going to check this section then
 * test if section name ends with a dot and a number.
 * This is used to find sections where the linker have
 * appended a dot-number to make the name unique.
 * The cause of this is often a section specified in assembler
 * without "ax" / "aw" and the same section used in .c
 * code where gcc add these.
 */
static int check_section(const char *modname, const char *sec)
{
	const char *e = sec + strlen(sec) - 1;
	if (match(sec, section_white_list))
		return 1;

	if (*e && isdigit(*e)) {
		/* consume all digits */
		while (*e && e != sec && isdigit(*e))
			e--;
		if (*e == '.') {
			warn("%s (%s): unexpected section name.\n"
			     "The (.[number]+) following section name are "
			     "ld generated and not expected.\n"
			     "Did you forget to use \"ax\"/\"aw\" "
			     "in a .S file?\n"
			     "Note that for example <linux/init.h> contains\n"
			     "section definitions for use in .S files.\n\n",
			     modname, sec);
		}
	}
	return 0;
}



#define ALL_INIT_DATA_SECTIONS \
	".init.data$", ".devinit.data$", ".cpuinit.data$", ".meminit.data$"
#define ALL_EXIT_DATA_SECTIONS \
	".exit.data$", ".devexit.data$", ".cpuexit.data$", ".memexit.data$"

#define ALL_INIT_TEXT_SECTIONS \
	".init.text$", ".devinit.text$", ".cpuinit.text$", ".meminit.text$"
#define ALL_EXIT_TEXT_SECTIONS \
	".exit.text$", ".devexit.text$", ".cpuexit.text$", ".memexit.text$"

#define ALL_INIT_SECTIONS ALL_INIT_DATA_SECTIONS, ALL_INIT_TEXT_SECTIONS
#define ALL_EXIT_SECTIONS ALL_EXIT_DATA_SECTIONS, ALL_EXIT_TEXT_SECTIONS

#define DATA_SECTIONS ".data$", ".data.rel$"
#define TEXT_SECTIONS ".text$"

#define INIT_SECTIONS      ".init.data$", ".init.text$"
#define DEV_INIT_SECTIONS  ".devinit.data$", ".devinit.text$"
#define CPU_INIT_SECTIONS  ".cpuinit.data$", ".cpuinit.text$"
#define MEM_INIT_SECTIONS  ".meminit.data$", ".meminit.text$"

#define EXIT_SECTIONS      ".exit.data$", ".exit.text$"
#define DEV_EXIT_SECTIONS  ".devexit.data$", ".devexit.text$"
#define CPU_EXIT_SECTIONS  ".cpuexit.data$", ".cpuexit.text$"
#define MEM_EXIT_SECTIONS  ".memexit.data$", ".memexit.text$"

/* init data sections */
static const char *init_data_sections[] = { ALL_INIT_DATA_SECTIONS, NULL };

/* all init sections */
static const char *init_sections[] = { ALL_INIT_SECTIONS, NULL };

/* All init and exit sections (code + data) */
static const char *init_exit_sections[] =
	{ALL_INIT_SECTIONS, ALL_EXIT_SECTIONS, NULL };

/* data section */
static const char *data_sections[] = { DATA_SECTIONS, NULL };

/* sections that may refer to an init/exit section with no warning */
static const char *initref_sections[] =
{
	".text.init.refok*",
	".exit.text.refok*",
	".data.init.refok*",
	NULL
};


/* symbols in .data that may refer to init/exit sections */
static const char *symbol_white_list[] =
{
	"*driver",
	"*_template", /* scsi uses *_template a lot */
	"*_timer",    /* arm uses ops structures named _timer a lot */
	"*_sht",      /* scsi also used *_sht to some extent */
	"*_ops",
	"*_probe",
	"*_probe_one",
	"*_console",
	NULL
};

static const char *head_sections[] = { ".head.text*", NULL };
static const char *linker_symbols[] =
	{ "__init_begin", "_sinittext", "_einittext", NULL };

enum mismatch {
	NO_MISMATCH,
	TEXT_TO_INIT,
	DATA_TO_INIT,
	TEXT_TO_EXIT,
	DATA_TO_EXIT,
	XXXINIT_TO_INIT,
	XXXEXIT_TO_EXIT,
	INIT_TO_EXIT,
	EXIT_TO_INIT,
	EXPORT_TO_INIT_EXIT,
};

struct sectioncheck {
	const char *fromsec[20];
	const char *tosec[20];
	enum mismatch mismatch;
};

const struct sectioncheck sectioncheck[] = {
/* Do not reference init/exit code/data from
 * normal code and data
 */
{
	.fromsec = { TEXT_SECTIONS, NULL },
	.tosec   = { ALL_INIT_SECTIONS, NULL },
	.mismatch = TEXT_TO_INIT,
},
{
	.fromsec = { DATA_SECTIONS, NULL },
	.tosec   = { ALL_INIT_SECTIONS, NULL },
	.mismatch = DATA_TO_INIT,
},
{
	.fromsec = { TEXT_SECTIONS, NULL },
	.tosec   = { ALL_EXIT_SECTIONS, NULL },
	.mismatch = TEXT_TO_EXIT,
},
{
	.fromsec = { DATA_SECTIONS, NULL },
	.tosec   = { ALL_EXIT_SECTIONS, NULL },
	.mismatch = DATA_TO_EXIT,
},
/* Do not reference init code/data from devinit/cpuinit/meminit code/data */
{
	.fromsec = { DEV_INIT_SECTIONS, CPU_INIT_SECTIONS, MEM_INIT_SECTIONS, NULL },
	.tosec   = { INIT_SECTIONS, NULL },
	.mismatch = XXXINIT_TO_INIT,
},
/* Do not reference exit code/data from devexit/cpuexit/memexit code/data */
{
	.fromsec = { DEV_EXIT_SECTIONS, CPU_EXIT_SECTIONS, MEM_EXIT_SECTIONS, NULL },
	.tosec   = { EXIT_SECTIONS, NULL },
	.mismatch = XXXEXIT_TO_EXIT,
},
/* Do not use exit code/data from init code */
{
	.fromsec = { ALL_INIT_SECTIONS, NULL },
	.tosec   = { ALL_EXIT_SECTIONS, NULL },
	.mismatch = INIT_TO_EXIT,
},
/* Do not use init code/data from exit code */
{
	.fromsec = { ALL_EXIT_SECTIONS, NULL },
	.tosec   = { ALL_INIT_SECTIONS, NULL },
	.mismatch = EXIT_TO_INIT,
},
/* Do not export init/exit functions or data */
{
	.fromsec = { "__ksymtab*", NULL },
	.tosec   = { INIT_SECTIONS, EXIT_SECTIONS, NULL },
	.mismatch = EXPORT_TO_INIT_EXIT
}
};

static int section_mismatch(const char *fromsec, const char *tosec)
{
	int i;
	int elems = sizeof(sectioncheck) / sizeof(struct sectioncheck);
	const struct sectioncheck *check = &sectioncheck[0];

	for (i = 0; i < elems; i++) {
		if (match(fromsec, check->fromsec) &&
		    match(tosec, check->tosec))
			return check->mismatch;
		check++;
	}
	return NO_MISMATCH;
}

/**
 * Whitelist to allow certain references to pass with no warning.
 *
 * Pattern 0:
 *   Do not warn if funtion/data are marked with __init_refok/__initdata_refok.
 *   The pattern is identified by:
 *   fromsec = .text.init.refok* | .data.init.refok*
 *
 * Pattern 1:
 *   If a module parameter is declared __initdata and permissions=0
 *   then this is legal despite the warning generated.
 *   We cannot see value of permissions here, so just ignore
 *   this pattern.
 *   The pattern is identified by:
 *   tosec   = .init.data
 *   fromsec = .data*
 *   atsym   =__param*
 *
 * Pattern 2:
 *   Many drivers utilise a *driver container with references to
 *   add, remove, probe functions etc.
 *   These functions may often be marked __init and we do not want to
 *   warn here.
 *   the pattern is identified by:
 *   tosec   = init or exit section
 *   fromsec = data section
 *   atsym = *driver, *_template, *_sht, *_ops, *_probe,
 *           *probe_one, *_console, *_timer
 *
 * Pattern 3:
 *   Whitelist all refereces from .text.head to .init.data
 *   Whitelist all refereces from .text.head to .init.text
 *
 * Pattern 4:
 *   Some symbols belong to init section but still it is ok to reference
 *   these from non-init sections as these symbols don't have any memory
 *   allocated for them and symbol address and value are same. So even
 *   if init section is freed, its ok to reference those symbols.
 *   For ex. symbols marking the init section boundaries.
 *   This pattern is identified by
 *   refsymname = __init_begin, _sinittext, _einittext
 *
 **/
static int secref_whitelist(const char *fromsec, const char *fromsym,
			    const char *tosec, const char *tosym)
{
	/* Check for pattern 0 */
	if (match(fromsec, initref_sections))
		return 0;

	/* Check for pattern 1 */
	if (match(tosec, init_data_sections) &&
	    match(fromsec, data_sections) &&
	    (strncmp(fromsym, "__param", strlen("__param")) == 0))
		return 0;

	/* Check for pattern 2 */
	if (match(tosec, init_exit_sections) &&
	    match(fromsec, data_sections) &&
	    match(fromsym, symbol_white_list))
		return 0;

	/* Check for pattern 3 */
	if (match(fromsec, head_sections) &&
	    match(tosec, init_sections))
		return 0;

	/* Check for pattern 4 */
	if (match(tosym, linker_symbols))
		return 0;

	return 1;
}

/**
 * Find symbol based on relocation record info.
 * In some cases the symbol supplied is a valid symbol so
 * return refsym. If st_name != 0 we assume this is a valid symbol.
 * In other cases the symbol needs to be looked up in the symbol table
 * based on section and address.
 *  **/
static Elf_Sym *find_elf_symbol(struct elf_info *elf, Elf64_Sword addr,
				Elf_Sym *relsym)
{
	Elf_Sym *sym;
	Elf_Sym *near = NULL;
	Elf64_Sword distance = 20;
	Elf64_Sword d;

	if (relsym->st_name != 0)
		return relsym;
	for (sym = elf->symtab_start; sym < elf->symtab_stop; sym++) {
		if (sym->st_shndx != relsym->st_shndx)
			continue;
		if (ELF_ST_TYPE(sym->st_info) == STT_SECTION)
			continue;
		if (sym->st_value == addr)
			return sym;
		/* Find a symbol nearby - addr are maybe negative */
		d = sym->st_value - addr;
		if (d < 0)
			d = addr - sym->st_value;
		if (d < distance) {
			distance = d;
			near = sym;
		}
	}
	/* We need a close match */
	if (distance < 20)
		return near;
	else
		return NULL;
}

static inline int is_arm_mapping_symbol(const char *str)
{
	return str[0] == '$' && strchr("atd", str[1])
	       && (str[2] == '\0' || str[2] == '.');
}

/*
 * If there's no name there, ignore it; likewise, ignore it if it's
 * one of the magic symbols emitted used by current ARM tools.
 *
 * Otherwise if find_symbols_between() returns those symbols, they'll
 * fail the whitelist tests and cause lots of false alarms ... fixable
 * only by merging __exit and __init sections into __text, bloating
 * the kernel (which is especially evil on embedded platforms).
 */
static inline int is_valid_name(struct elf_info *elf, Elf_Sym *sym)
{
	const char *name = elf->strtab + sym->st_name;

	if (!name || !strlen(name))
		return 0;
	return !is_arm_mapping_symbol(name);
}

/*
 * Find symbols before or equal addr and after addr - in the section sec.
 * If we find two symbols with equal offset prefer one with a valid name.
 * The ELF format may have a better way to detect what type of symbol
 * it is, but this works for now.
 **/
static Elf_Sym *find_elf_symbol2(struct elf_info *elf, Elf_Addr addr,
				 const char *sec)
{
	Elf_Sym *sym;
	Elf_Sym *near = NULL;
	Elf_Addr distance = ~0;

	for (sym = elf->symtab_start; sym < elf->symtab_stop; sym++) {
		const char *symsec;

		if (sym->st_shndx >= SHN_LORESERVE)
			continue;
		symsec = sec_name(elf, sym->st_shndx);
		if (strcmp(symsec, sec) != 0)
			continue;
		if (!is_valid_name(elf, sym))
			continue;
		if (sym->st_value <= addr) {
			if ((addr - sym->st_value) < distance) {
				distance = addr - sym->st_value;
				near = sym;
			} else if ((addr - sym->st_value) == distance) {
				near = sym;
			}
		}
	}
	return near;
}

/*
 * Convert a section name to the function/data attribute
 * .init.text => __init
 * .cpuinit.data => __cpudata
 * .memexitconst => __memconst
 * etc.
*/
static char *sec2annotation(const char *s)
{
	if (match(s, init_exit_sections)) {
		char *p = malloc(20);
		char *r = p;

		*p++ = '_';
		*p++ = '_';
		if (*s == '.')
			s++;
		while (*s && *s != '.')
			*p++ = *s++;
		*p = '\0';
		if (*s == '.')
			s++;
		if (strstr(s, "rodata") != NULL)
			strcat(p, "const ");
		else if (strstr(s, "data") != NULL)
			strcat(p, "data ");
		else
			strcat(p, " ");
		return r; /* we leak her but we do not care */
	} else {
		return "";
	}
}

static int is_function(Elf_Sym *sym)
{
	if (sym)
		return ELF_ST_TYPE(sym->st_info) == STT_FUNC;
	else
		return 0;
}

/*
 * Print a warning about a section mismatch.
 * Try to find symbols near it so user can find it.
 * Check whitelist before warning - it may be a false positive.
 */
static void report_sec_mismatch(const char *modname, enum mismatch mismatch,
                                const char *fromsec,
                                unsigned long long fromaddr,
                                const char *fromsym,
                                int from_is_func,
                                const char *tosec, const char *tosym,
                                int to_is_func)
{
	const char *from, *from_p;
	const char *to, *to_p;
	from = from_is_func ? "function" : "variable";
	from_p = from_is_func ? "()" : "";
	to = to_is_func ? "function" : "variable";
	to_p = to_is_func ? "()" : "";

	sec_mismatch_count++;
	if (!sec_mismatch_verbose)
		return;

	fprintf(stderr, "WARNING: %s(%s+0x%llx): Section mismatch in"
	                " reference from the %s %s%s to the %s %s:%s%s\n",
                        modname, fromsec, fromaddr, from, fromsym, from_p,
	                to, tosec, tosym, to_p);

	switch (mismatch) {
	case TEXT_TO_INIT:
		fprintf(stderr,
		"The function %s %s() references\n"
		"the %s %s%s%s.\n"
		"This is often because %s lacks a %s\n"
		"annotation or the annotation of %s is wrong.\n",
		sec2annotation(fromsec), fromsym,
		to, sec2annotation(tosec), tosym, to_p,
		fromsym, sec2annotation(tosec), tosym);
		break;
	case DATA_TO_INIT: {
		const char **s = symbol_white_list;
		fprintf(stderr,
		"The variable %s references\n"
		"the %s %s%s%s\n"
		"If the reference is valid then annotate the\n"
		"variable with __init* (see linux/init.h) "
		"or name the variable:\n",
		fromsym, to, sec2annotation(tosec), tosym, to_p);
		while (*s)
			fprintf(stderr, "%s, ", *s++);
		fprintf(stderr, "\n");
		break;
	}
	case TEXT_TO_EXIT:
		fprintf(stderr,
		"The function %s() references a %s in an exit section.\n"
		"Often the %s %s%s has valid usage outside the exit section\n"
		"and the fix is to remove the %sannotation of %s.\n",
		fromsym, to, to, tosym, to_p, sec2annotation(tosec), tosym);
		break;
	case DATA_TO_EXIT: {
		const char **s = symbol_white_list;
		fprintf(stderr,
		"The variable %s references\n"
		"the %s %s%s%s\n"
		"If the reference is valid then annotate the\n"
		"variable with __exit* (see linux/init.h) or "
		"name the variable:\n",
		fromsym, to, sec2annotation(tosec), tosym, to_p);
		while (*s)
			fprintf(stderr, "%s, ", *s++);
		fprintf(stderr, "\n");
		break;
	}
	case XXXINIT_TO_INIT:
	case XXXEXIT_TO_EXIT:
		fprintf(stderr,
		"The %s %s%s%s references\n"
		"a %s %s%s%s.\n"
		"If %s is only used by %s then\n"
		"annotate %s with a matching annotation.\n",
		from, sec2annotation(fromsec), fromsym, from_p,
		to, sec2annotation(tosec), tosym, to_p,
		fromsym, tosym, fromsym);
		break;
	case INIT_TO_EXIT:
		fprintf(stderr,
		"The %s %s%s%s references\n"
		"a %s %s%s%s.\n"
		"This is often seen when error handling "
		"in the init function\n"
		"uses functionality in the exit path.\n"
		"The fix is often to remove the %sannotation of\n"
		"%s%s so it may be used outside an exit section.\n",
		from, sec2annotation(fromsec), fromsym, from_p,
		to, sec2annotation(tosec), tosym, to_p,
		sec2annotation(tosec), tosym, to_p);
		break;
	case EXIT_TO_INIT:
		fprintf(stderr,
		"The %s %s%s%s references\n"
		"a %s %s%s%s.\n"
		"This is often seen when error handling "
		"in the exit function\n"
		"uses functionality in the init path.\n"
		"The fix is often to remove the %sannotation of\n"
		"%s%s so it may be used outside an init section.\n",
		from, sec2annotation(fromsec), fromsym, from_p,
		to, sec2annotation(tosec), tosym, to_p,
		sec2annotation(tosec), tosym, to_p);
		break;
	case EXPORT_TO_INIT_EXIT:
		fprintf(stderr,
		"The symbol %s is exported and annotated %s\n"
		"Fix this by removing the %sannotation of %s "
		"or drop the export.\n",
		tosym, sec2annotation(tosec), sec2annotation(tosec), tosym);
	case NO_MISMATCH:
		/* To get warnings on missing members */
		break;
	}
	fprintf(stderr, "\n");
}

static void check_section_mismatch(const char *modname, struct elf_info *elf,
                                   Elf_Rela *r, Elf_Sym *sym, const char *fromsec)
{
	const char *tosec;
	enum mismatch mismatch;

	tosec = sec_name(elf, sym->st_shndx);
	mismatch = section_mismatch(fromsec, tosec);
	if (mismatch != NO_MISMATCH) {
		Elf_Sym *to;
		Elf_Sym *from;
		const char *tosym;
		const char *fromsym;

		from = find_elf_symbol2(elf, r->r_offset, fromsec);
		fromsym = sym_name(elf, from);
		to = find_elf_symbol(elf, r->r_addend, sym);
		tosym = sym_name(elf, to);

		/* check whitelist - we may ignore it */
		if (secref_whitelist(fromsec, fromsym, tosec, tosym)) {
			report_sec_mismatch(modname, mismatch,
			   fromsec, r->r_offset, fromsym,
			   is_function(from), tosec, tosym,
			   is_function(to));
		}
	}
}

static unsigned int *reloc_location(struct elf_info *elf,
				    Elf_Shdr *sechdr, Elf_Rela *r)
{
	Elf_Shdr *sechdrs = elf->sechdrs;
	int section = sechdr->sh_info;

	return (void *)elf->hdr + sechdrs[section].sh_offset +
		(r->r_offset - sechdrs[section].sh_addr);
}

static int addend_386_rel(struct elf_info *elf, Elf_Shdr *sechdr, Elf_Rela *r)
{
	unsigned int r_typ = ELF_R_TYPE(r->r_info);
	unsigned int *location = reloc_location(elf, sechdr, r);

	switch (r_typ) {
	case R_386_32:
		r->r_addend = TO_NATIVE(*location);
		break;
	case R_386_PC32:
		r->r_addend = TO_NATIVE(*location) + 4;
		/* For CONFIG_RELOCATABLE=y */
		if (elf->hdr->e_type == ET_EXEC)
			r->r_addend += r->r_offset;
		break;
	}
	return 0;
}

static int addend_arm_rel(struct elf_info *elf, Elf_Shdr *sechdr, Elf_Rela *r)
{
	unsigned int r_typ = ELF_R_TYPE(r->r_info);

	switch (r_typ) {
	case R_ARM_ABS32:
		/* From ARM ABI: (S + A) | T */
		r->r_addend = (int)(long)
		              (elf->symtab_start + ELF_R_SYM(r->r_info));
		break;
	case R_ARM_PC24:
		/* From ARM ABI: ((S + A) | T) - P */
		r->r_addend = (int)(long)(elf->hdr +
		              sechdr->sh_offset +
		              (r->r_offset - sechdr->sh_addr));
		break;
	default:
		return 1;
	}
	return 0;
}

static int addend_mips_rel(struct elf_info *elf, Elf_Shdr *sechdr, Elf_Rela *r)
{
	unsigned int r_typ = ELF_R_TYPE(r->r_info);
	unsigned int *location = reloc_location(elf, sechdr, r);
	unsigned int inst;

	if (r_typ == R_MIPS_HI16)
		return 1;	/* skip this */
	inst = TO_NATIVE(*location);
	switch (r_typ) {
	case R_MIPS_LO16:
		r->r_addend = inst & 0xffff;
		break;
	case R_MIPS_26:
		r->r_addend = (inst & 0x03ffffff) << 2;
		break;
	case R_MIPS_32:
		r->r_addend = inst;
		break;
	}
	return 0;
}

static void section_rela(const char *modname, struct elf_info *elf,
                         Elf_Shdr *sechdr)
{
	Elf_Sym  *sym;
	Elf_Rela *rela;
	Elf_Rela r;
	unsigned int r_sym;
	const char *fromsec;

	Elf_Rela *start = (void *)elf->hdr + sechdr->sh_offset;
	Elf_Rela *stop  = (void *)start + sechdr->sh_size;

	fromsec = sech_name(elf, sechdr);
	fromsec += strlen(".rela");
	/* if from section (name) is know good then skip it */
	if (check_section(modname, fromsec))
		return;

	for (rela = start; rela < stop; rela++) {
		r.r_offset = TO_NATIVE(rela->r_offset);
#if KERNEL_ELFCLASS == ELFCLASS64
		if (elf->hdr->e_machine == EM_MIPS) {
			unsigned int r_typ;
			r_sym = ELF64_MIPS_R_SYM(rela->r_info);
			r_sym = TO_NATIVE(r_sym);
			r_typ = ELF64_MIPS_R_TYPE(rela->r_info);
			r.r_info = ELF64_R_INFO(r_sym, r_typ);
		} else {
			r.r_info = TO_NATIVE(rela->r_info);
			r_sym = ELF_R_SYM(r.r_info);
		}
#else
		r.r_info = TO_NATIVE(rela->r_info);
		r_sym = ELF_R_SYM(r.r_info);
#endif
		r.r_addend = TO_NATIVE(rela->r_addend);
		sym = elf->symtab_start + r_sym;
		/* Skip special sections */
		if (sym->st_shndx >= SHN_LORESERVE)
			continue;
		check_section_mismatch(modname, elf, &r, sym, fromsec);
	}
}

static void section_rel(const char *modname, struct elf_info *elf,
                        Elf_Shdr *sechdr)
{
	Elf_Sym *sym;
	Elf_Rel *rel;
	Elf_Rela r;
	unsigned int r_sym;
	const char *fromsec;

	Elf_Rel *start = (void *)elf->hdr + sechdr->sh_offset;
	Elf_Rel *stop  = (void *)start + sechdr->sh_size;

	fromsec = sech_name(elf, sechdr);
	fromsec += strlen(".rel");
	/* if from section (name) is know good then skip it */
	if (check_section(modname, fromsec))
		return;

	for (rel = start; rel < stop; rel++) {
		r.r_offset = TO_NATIVE(rel->r_offset);
#if KERNEL_ELFCLASS == ELFCLASS64
		if (elf->hdr->e_machine == EM_MIPS) {
			unsigned int r_typ;
			r_sym = ELF64_MIPS_R_SYM(rel->r_info);
			r_sym = TO_NATIVE(r_sym);
			r_typ = ELF64_MIPS_R_TYPE(rel->r_info);
			r.r_info = ELF64_R_INFO(r_sym, r_typ);
		} else {
			r.r_info = TO_NATIVE(rel->r_info);
			r_sym = ELF_R_SYM(r.r_info);
		}
#else
		r.r_info = TO_NATIVE(rel->r_info);
		r_sym = ELF_R_SYM(r.r_info);
#endif
		r.r_addend = 0;
		switch (elf->hdr->e_machine) {
		case EM_386:
			if (addend_386_rel(elf, sechdr, &r))
				continue;
			break;
		case EM_ARM:
			if (addend_arm_rel(elf, sechdr, &r))
				continue;
			break;
		case EM_MIPS:
			if (addend_mips_rel(elf, sechdr, &r))
				continue;
			break;
		}
		sym = elf->symtab_start + r_sym;
		/* Skip special sections */
		if (sym->st_shndx >= SHN_LORESERVE)
			continue;
		check_section_mismatch(modname, elf, &r, sym, fromsec);
	}
}

/**
 * A module includes a number of sections that are discarded
 * either when loaded or when used as built-in.
 * For loaded modules all functions marked __init and all data
 * marked __initdata will be discarded when the module has been intialized.
 * Likewise for modules used built-in the sections marked __exit
 * are discarded because __exit marked function are supposed to be called
 * only when a moduel is unloaded which never happes for built-in modules.
 * The check_sec_ref() function traverses all relocation records
 * to find all references to a section that reference a section that will
 * be discarded and warns about it.
 **/
static void check_sec_ref(struct module *mod, const char *modname,
                          struct elf_info *elf)
{
	int i;
	Elf_Shdr *sechdrs = elf->sechdrs;

	/* Walk through all sections */
	for (i = 0; i < elf->hdr->e_shnum; i++) {
		/* We want to process only relocation sections and not .init */
		if (sechdrs[i].sh_type == SHT_RELA)
			section_rela(modname, elf, &elf->sechdrs[i]);
		else if (sechdrs[i].sh_type == SHT_REL)
			section_rel(modname, elf, &elf->sechdrs[i]);
	}
}

static void read_symbols(char *modname)
{
	const char *symname;
	char *version;
	char *license;
	struct module *mod;
	struct elf_info info = { };
	Elf_Sym *sym;

	if (!parse_elf(&info, modname))
		return;

	mod = new_module(modname);

	/* When there's no vmlinux, don't print warnings about
	 * unresolved symbols (since there'll be too many ;) */
	if (is_vmlinux(modname)) {
		have_vmlinux = 1;
		mod->skip = 1;
	}

	license = get_modinfo(info.modinfo, info.modinfo_len, "license");
	while (license) {
		if (license_is_gpl_compatible(license))
			mod->gpl_compatible = 1;
		else {
			mod->gpl_compatible = 0;
			break;
		}
		license = get_next_modinfo(info.modinfo, info.modinfo_len,
					   "license", license);
	}

	for (sym = info.symtab_start; sym < info.symtab_stop; sym++) {
		symname = info.strtab + sym->st_name;

		handle_modversions(mod, &info, sym, symname);
		handle_moddevtable(mod, &info, sym, symname);
	}
	if (!is_vmlinux(modname) ||
	     (is_vmlinux(modname) && vmlinux_section_warnings))
		check_sec_ref(mod, modname, &info);

	version = get_modinfo(info.modinfo, info.modinfo_len, "version");
	if (version)
		maybe_frob_rcs_version(modname, version, info.modinfo,
				       version - (char *)info.hdr);
	if (version || (all_versions && !is_vmlinux(modname)))
		get_src_version(modname, mod->srcversion,
				sizeof(mod->srcversion)-1);

	parse_elf_finish(&info);

	/* Our trick to get versioning for struct_module - it's
	 * never passed as an argument to an exported function, so
	 * the automatic versioning doesn't pick it up, but it's really
	 * important anyhow */
	if (modversions)
		mod->unres = alloc_symbol("struct_module", 0, mod->unres);
}

#define SZ 500

/* We first write the generated file into memory using the
 * following helper, then compare to the file on disk and
 * only update the later if anything changed */

void __attribute__((format(printf, 2, 3))) buf_printf(struct buffer *buf,
						      const char *fmt, ...)
{
	char tmp[SZ];
	int len;
	va_list ap;

	va_start(ap, fmt);
	len = vsnprintf(tmp, SZ, fmt, ap);
	buf_write(buf, tmp, len);
	va_end(ap);
}

void buf_write(struct buffer *buf, const char *s, int len)
{
	if (buf->size - buf->pos < len) {
		buf->size += len + SZ;
		buf->p = realloc(buf->p, buf->size);
	}
	strncpy(buf->p + buf->pos, s, len);
	buf->pos += len;
}

static void check_for_gpl_usage(enum export exp, const char *m, const char *s)
{
	const char *e = is_vmlinux(m) ?"":".ko";

	switch (exp) {
	case export_gpl:
		fatal("modpost: GPL-incompatible module %s%s "
		      "uses GPL-only symbol '%s'\n", m, e, s);
		break;
	case export_unused_gpl:
		fatal("modpost: GPL-incompatible module %s%s "
		      "uses GPL-only symbol marked UNUSED '%s'\n", m, e, s);
		break;
	case export_gpl_future:
		warn("modpost: GPL-incompatible module %s%s "
		      "uses future GPL-only symbol '%s'\n", m, e, s);
		break;
	case export_plain:
	case export_unused:
	case export_unknown:
		/* ignore */
		break;
	}
}

static void check_for_unused(enum export exp, const char *m, const char *s)
{
	const char *e = is_vmlinux(m) ?"":".ko";

	switch (exp) {
	case export_unused:
	case export_unused_gpl:
		warn("modpost: module %s%s "
		      "uses symbol '%s' marked UNUSED\n", m, e, s);
		break;
	default:
		/* ignore */
		break;
	}
}

static void check_exports(struct module *mod)
{
	struct symbol *s, *exp;

	for (s = mod->unres; s; s = s->next) {
		const char *basename;
		exp = find_symbol(s->name);
		if (!exp || exp->module == mod)
			continue;
		basename = strrchr(mod->name, '/');
		if (basename)
			basename++;
		else
			basename = mod->name;
		if (!mod->gpl_compatible)
			check_for_gpl_usage(exp->export, basename, exp->name);
		check_for_unused(exp->export, basename, exp->name);
	}
}

/**
 * Header for the generated file
 **/
static void add_header(struct buffer *b, struct module *mod)
{
	buf_printf(b, "#include <linux/module.h>\n");
	buf_printf(b, "#include <linux/vermagic.h>\n");
	buf_printf(b, "#include <linux/compiler.h>\n");
	buf_printf(b, "\n");
	buf_printf(b, "MODULE_INFO(vermagic, VERMAGIC_STRING);\n");
	buf_printf(b, "\n");
	buf_printf(b, "struct module __this_module\n");
	buf_printf(b, "__attribute__((section(\".gnu.linkonce.this_module\"))) = {\n");
	buf_printf(b, " .name = KBUILD_MODNAME,\n");
	if (mod->has_init)
		buf_printf(b, " .init = init_module,\n");
	if (mod->has_cleanup)
		buf_printf(b, "#ifdef CONFIG_MODULE_UNLOAD\n"
			      " .exit = cleanup_module,\n"
			      "#endif\n");
	buf_printf(b, " .arch = MODULE_ARCH_INIT,\n");
	buf_printf(b, "};\n");
}

/**
 * Record CRCs for unresolved symbols
 **/
static int add_versions(struct buffer *b, struct module *mod)
{
	struct symbol *s, *exp;
	int err = 0;

	for (s = mod->unres; s; s = s->next) {
		exp = find_symbol(s->name);
		if (!exp || exp->module == mod) {
			if (have_vmlinux && !s->weak) {
				if (warn_unresolved) {
					warn("\"%s\" [%s.ko] undefined!\n",
					     s->name, mod->name);
				} else {
					merror("\"%s\" [%s.ko] undefined!\n",
					          s->name, mod->name);
					err = 1;
				}
			}
			continue;
		}
		s->module = exp->module;
		s->crc_valid = exp->crc_valid;
		s->crc = exp->crc;
	}

	if (!modversions)
		return err;

	buf_printf(b, "\n");
	buf_printf(b, "static const struct modversion_info ____versions[]\n");
	buf_printf(b, "__used\n");
	buf_printf(b, "__attribute__((section(\"__versions\"))) = {\n");

	for (s = mod->unres; s; s = s->next) {
		if (!s->module)
			continue;
		if (!s->crc_valid) {
			warn("\"%s\" [%s.ko] has no CRC!\n",
				s->name, mod->name);
			continue;
		}
		buf_printf(b, "\t{ %#8x, \"%s\" },\n", s->crc, s->name);
	}

	buf_printf(b, "};\n");

	return err;
}

static void add_depends(struct buffer *b, struct module *mod,
			struct module *modules)
{
	struct symbol *s;
	struct module *m;
	int first = 1;

	for (m = modules; m; m = m->next)
		m->seen = is_vmlinux(m->name);

	buf_printf(b, "\n");
	buf_printf(b, "static const char __module_depends[]\n");
	buf_printf(b, "__used\n");
	buf_printf(b, "__attribute__((section(\".modinfo\"))) =\n");
	buf_printf(b, "\"depends=");
	for (s = mod->unres; s; s = s->next) {
		const char *p;
		if (!s->module)
			continue;

		if (s->module->seen)
			continue;

		s->module->seen = 1;
		p = strrchr(s->module->name, '/');
		if (p)
			p++;
		else
			p = s->module->name;
		buf_printf(b, "%s%s", first ? "" : ",", p);
		first = 0;
	}
	buf_printf(b, "\";\n");
}

static void add_srcversion(struct buffer *b, struct module *mod)
{
	if (mod->srcversion[0]) {
		buf_printf(b, "\n");
		buf_printf(b, "MODULE_INFO(srcversion, \"%s\");\n",
			   mod->srcversion);
	}
}

static void write_if_changed(struct buffer *b, const char *fname)
{
	char *tmp;
	FILE *file;
	struct stat st;

	file = fopen(fname, "r");
	if (!file)
		goto write;

	if (fstat(fileno(file), &st) < 0)
		goto close_write;

	if (st.st_size != b->pos)
		goto close_write;

	tmp = NOFAIL(malloc(b->pos));
	if (fread(tmp, 1, b->pos, file) != b->pos)
		goto free_write;

	if (memcmp(tmp, b->p, b->pos) != 0)
		goto free_write;

	free(tmp);
	fclose(file);
	return;

 free_write:
	free(tmp);
 close_write:
	fclose(file);
 write:
	file = fopen(fname, "w");
	if (!file) {
		perror(fname);
		exit(1);
	}
	if (fwrite(b->p, 1, b->pos, file) != b->pos) {
		perror(fname);
		exit(1);
	}
	fclose(file);
}

/* parse Module.symvers file. line format:
 * 0x12345678<tab>symbol<tab>module[[<tab>export]<tab>something]
 **/
static void read_dump(const char *fname, unsigned int kernel)
{
	unsigned long size, pos = 0;
	void *file = grab_file(fname, &size);
	char *line;

	if (!file)
		/* No symbol versions, silently ignore */
		return;

	while ((line = get_next_line(&pos, file, size))) {
		char *symname, *modname, *d, *export, *end;
		unsigned int crc;
		struct module *mod;
		struct symbol *s;

		if (!(symname = strchr(line, '\t')))
			goto fail;
		*symname++ = '\0';
		if (!(modname = strchr(symname, '\t')))
			goto fail;
		*modname++ = '\0';
		if ((export = strchr(modname, '\t')) != NULL)
			*export++ = '\0';
		if (export && ((end = strchr(export, '\t')) != NULL))
			*end = '\0';
		crc = strtoul(line, &d, 16);
		if (*symname == '\0' || *modname == '\0' || *d != '\0')
			goto fail;
		mod = find_module(modname);
		if (!mod) {
			if (is_vmlinux(modname))
				have_vmlinux = 1;
			mod = new_module(NOFAIL(strdup(modname)));
			mod->skip = 1;
		}
		s = sym_add_exported(symname, mod, export_no(export));
		s->kernel    = kernel;
		s->preloaded = 1;
		sym_update_crc(symname, mod, crc, export_no(export));
	}
	return;
fail:
	fatal("parse error in symbol dump file\n");
}

/* For normal builds always dump all symbols.
 * For external modules only dump symbols
 * that are not read from kernel Module.symvers.
 **/
static int dump_sym(struct symbol *sym)
{
	if (!external_module)
		return 1;
	if (sym->vmlinux || sym->kernel)
		return 0;
	return 1;
}

static void write_dump(const char *fname)
{
	struct buffer buf = { };
	struct symbol *symbol;
	int n;

	for (n = 0; n < SYMBOL_HASH_SIZE ; n++) {
		symbol = symbolhash[n];
		while (symbol) {
			if (dump_sym(symbol))
				buf_printf(&buf, "0x%08x\t%s\t%s\t%s\n",
					symbol->crc, symbol->name,
					symbol->module->name,
					export_str(symbol->export));
			symbol = symbol->next;
		}
	}
	write_if_changed(&buf, fname);
}

int main(int argc, char **argv)
{
	struct module *mod;
	struct buffer buf = { };
	char *kernel_read = NULL, *module_read = NULL;
	char *dump_write = NULL;
	int opt;
	int err;

	while ((opt = getopt(argc, argv, "i:I:msSo:aw")) != -1) {
		switch (opt) {
		case 'i':
			kernel_read = optarg;
			break;
		case 'I':
			module_read = optarg;
			external_module = 1;
			break;
		case 'm':
			modversions = 1;
			break;
		case 'o':
			dump_write = optarg;
			break;
		case 'a':
			all_versions = 1;
			break;
		case 's':
			vmlinux_section_warnings = 0;
			break;
		case 'S':
			sec_mismatch_verbose = 0;
			break;
		case 'w':
			warn_unresolved = 1;
			break;
		default:
			exit(1);
		}
	}

	if (kernel_read)
		read_dump(kernel_read, 1);
	if (module_read)
		read_dump(module_read, 0);

	while (optind < argc)
		read_symbols(argv[optind++]);

	for (mod = modules; mod; mod = mod->next) {
		if (mod->skip)
			continue;
		check_exports(mod);
	}

	err = 0;

	for (mod = modules; mod; mod = mod->next) {
		char fname[strlen(mod->name) + 10];

		if (mod->skip)
			continue;

		buf.pos = 0;

		add_header(&buf, mod);
		err |= add_versions(&buf, mod);
		add_depends(&buf, mod, modules);
		add_moddevtable(&buf, mod);
		add_srcversion(&buf, mod);

		sprintf(fname, "%s.mod.c", mod->name);
		write_if_changed(&buf, fname);
	}

	if (dump_write)
		write_dump(dump_write);
	if (sec_mismatch_count && !sec_mismatch_verbose)
		fprintf(stderr, "modpost: Found %d section mismatch(es).\n"
		        "To see full details build your kernel with:\n"
		        "'make CONFIG_DEBUG_SECTION_MISMATCH=y'\n",
		        sec_mismatch_count);

	return err;
}
