/* Postprocess module symbol versions
 *
 * Copyright 2003       Kai Germaschewski
 * Copyright 2002-2004  Rusty Russell, IBM Corporation
 * Copyright 2006       Sam Ravnborg
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
/* Only warn about unresolved symbols */
static int warn_unresolved = 0;
/* How a symbol is exported */
enum export {
	export_plain,      export_unused,     export_gpl,
	export_unused_gpl, export_gpl_future, export_unknown
};

void fatal(const char *fmt, ...)
{
	va_list arglist;

	fprintf(stderr, "FATAL: ");

	va_start(arglist, fmt);
	vfprintf(stderr, fmt, arglist);
	va_end(arglist);

	exit(1);
}

void warn(const char *fmt, ...)
{
	va_list arglist;

	fprintf(stderr, "WARNING: ");

	va_start(arglist, fmt);
	vfprintf(stderr, fmt, arglist);
	va_end(arglist);
}

void merror(const char *fmt, ...)
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

	if ((myname = strrchr(modname, '/')))
		myname++;
	else
		myname = modname;

	return strcmp(myname, "vmlinux") == 0;
}

void *do_nofail(void *ptr, const char *expr)
{
	if (!ptr) {
		fatal("modpost: Memory allocation failure: %s.\n", expr);
	}
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
	if ((s = strrchr(p, '.')) != NULL)
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
	for (value = 0x238F13AF * strlen(name), i=0; name[i]; i++)
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

	for (s = symbolhash[tdb_hash(name) % SYMBOL_HASH_SIZE]; s; s=s->next) {
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

static enum export export_no(const char * s)
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
char* get_next_line(unsigned long *pos, void *file, unsigned long size)
{
	static char line[4096];
	int skip = 1;
	size_t len = 0;
	signed char *p = (signed char *)file + *pos;
	char *s = line;

	for (; *pos < size ; (*pos)++)
	{
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
	sechdrs = (void *)hdr + hdr->e_shoff;
	info->sechdrs = sechdrs;

	/* Fix endianness in section headers */
	for (i = 0; i < hdr->e_shnum; i++) {
		sechdrs[i].sh_type   = TO_NATIVE(sechdrs[i].sh_type);
		sechdrs[i].sh_offset = TO_NATIVE(sechdrs[i].sh_offset);
		sechdrs[i].sh_size   = TO_NATIVE(sechdrs[i].sh_size);
		sechdrs[i].sh_link   = TO_NATIVE(sechdrs[i].sh_link);
		sechdrs[i].sh_name   = TO_NATIVE(sechdrs[i].sh_name);
	}
	/* Find symbol table. */
	for (i = 1; i < hdr->e_shnum; i++) {
		const char *secstrings
			= (void *)hdr + sechdrs[hdr->e_shstrndx].sh_offset;
		const char *secname;

		if (sechdrs[i].sh_offset > info->size) {
			fatal("%s is truncated. sechdrs[i].sh_offset=%u > sizeof(*hrd)=%ul\n", filename, (unsigned int)sechdrs[i].sh_offset, sizeof(*hdr));
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
	if (!info->symtab_start) {
		fatal("%s has no symtab?\n", filename);
	}
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
			   strlen(MODULE_SYMBOL_PREFIX)) == 0)
			mod->unres = alloc_symbol(symname +
						  strlen(MODULE_SYMBOL_PREFIX),
						  ELF_ST_BIND(sym->st_info) == STB_WEAK,
						  mod->unres);
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

/**
 * Whitelist to allow certain references to pass with no warning.
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
 *   tosec   = .init.text | .exit.text | .init.data
 *   fromsec = .data
 *   atsym = *driver, *_template, *_sht, *_ops, *_probe, *probe_one, *_console
 *
 * Pattern 3:
 *   Whitelist all references from .pci_fixup* section to .init.text
 *   This is part of the PCI init when built-in
 *
 * Pattern 4:
 *   Whitelist all refereces from .text.head to .init.data
 *   Whitelist all refereces from .text.head to .init.text
 *
 * Pattern 5:
 *   Some symbols belong to init section but still it is ok to reference
 *   these from non-init sections as these symbols don't have any memory
 *   allocated for them and symbol address and value are same. So even
 *   if init section is freed, its ok to reference those symbols.
 *   For ex. symbols marking the init section boundaries.
 *   This pattern is identified by
 *   refsymname = __init_begin, _sinittext, _einittext
 *
 * Pattern 6:
 *   During the early init phase we have references from .init.text to
 *   .text we have an intended section mismatch - do not warn about it.
 *   See kernel_init() in init/main.c
 *   tosec   = .init.text
 *   fromsec = .text
 *   atsym = kernel_init
 *
 * Pattern 7:
 *  Logos used in drivers/video/logo reside in __initdata but the
 *  funtion that references them are EXPORT_SYMBOL() so cannot be
 *  marker __init. So we whitelist them here.
 *  The pattern is:
 *  tosec      = .init.data
 *  fromsec    = .text*
 *  refsymname = logo_
 *
 * Pattern 8:
 *  Symbols contained in .paravirtprobe may safely reference .init.text.
 *  The pattern is:
 *  tosec   = .init.text
 *  fromsec  = .paravirtprobe
 *
 * Pattern 9:
 *  Some of functions are common code between boot time and hotplug
 *  time. The bootmem allocater is called only boot time in its
 *  functions. So it's ok to reference.
 *  tosec    = .init.text
 *
 * Pattern 10:
 *  ia64 has machvec table for each platform. It is mixture of function
 *  pointer of .init.text and .text.
 *  fromsec  = .machvec
 **/
static int secref_whitelist(const char *modname, const char *tosec,
			    const char *fromsec, const char *atsym,
			    const char *refsymname)
{
	int f1 = 1, f2 = 1;
	const char **s;
	const char *pat2sym[] = {
		"driver",
		"_template", /* scsi uses *_template a lot */
		"_sht",      /* scsi also used *_sht to some extent */
		"_ops",
		"_probe",
		"_probe_one",
		"_console",
		"apic_es7000",
		NULL
	};

	const char *pat3refsym[] = {
		"__init_begin",
		"_sinittext",
		"_einittext",
		NULL
	};

	const char *pat4sym[] = {
		"sparse_index_alloc",
		"zone_wait_table_init",
		NULL
	};

	/* Check for pattern 1 */
	if (strcmp(tosec, ".init.data") != 0)
		f1 = 0;
	if (strncmp(fromsec, ".data", strlen(".data")) != 0)
		f1 = 0;
	if (strncmp(atsym, "__param", strlen("__param")) != 0)
		f1 = 0;

	if (f1)
		return f1;

	/* Check for pattern 2 */
	if ((strcmp(tosec, ".init.text") != 0) &&
	    (strcmp(tosec, ".exit.text") != 0) &&
	    (strcmp(tosec, ".init.data") != 0))
		f2 = 0;
	if (strcmp(fromsec, ".data") != 0)
		f2 = 0;

	for (s = pat2sym; *s; s++)
		if (strrcmp(atsym, *s) == 0)
			f1 = 1;
	if (f1 && f2)
		return 1;

	/* Check for pattern 3 */
	if ((strncmp(fromsec, ".pci_fixup", strlen(".pci_fixup")) == 0) &&
	    (strcmp(tosec, ".init.text") == 0))
	return 1;

	/* Check for pattern 4 */
	if ((strcmp(fromsec, ".text.head") == 0) &&
		((strcmp(tosec, ".init.data") == 0) ||
		(strcmp(tosec, ".init.text") == 0)))
	return 1;

	/* Check for pattern 5 */
	for (s = pat3refsym; *s; s++)
		if (strcmp(refsymname, *s) == 0)
			return 1;

	/* Check for pattern 6 */
	if ((strcmp(tosec, ".init.text") == 0) &&
	    (strcmp(fromsec, ".text") == 0) &&
	    (strcmp(refsymname, "kernel_init") == 0))
		return 1;

	/* Check for pattern 7 */
	if ((strcmp(tosec, ".init.data") == 0) &&
	    (strncmp(fromsec, ".text", strlen(".text")) == 0) &&
	    (strncmp(refsymname, "logo_", strlen("logo_")) == 0))
		return 1;

	/* Check for pattern 8 */
	if ((strcmp(tosec, ".init.text") == 0) &&
	    (strcmp(fromsec, ".paravirtprobe") == 0))
		return 1;

	/* Check for pattern 9 */
	if ((strcmp(tosec, ".init.text") == 0) &&
	    (strcmp(fromsec, ".text") == 0))
		for (s = pat4sym; *s; s++)
			if (strcmp(atsym, *s) == 0)
				return 1;

	/* Check for pattern 10 */
	if (strcmp(fromsec, ".machvec") == 0)
		return 1;

	return 0;
}

/**
 * Find symbol based on relocation record info.
 * In some cases the symbol supplied is a valid symbol so
 * return refsym. If st_name != 0 we assume this is a valid symbol.
 * In other cases the symbol needs to be looked up in the symbol table
 * based on section and address.
 *  **/
static Elf_Sym *find_elf_symbol(struct elf_info *elf, Elf_Addr addr,
				Elf_Sym *relsym)
{
	Elf_Sym *sym;

	if (relsym->st_name != 0)
		return relsym;
	for (sym = elf->symtab_start; sym < elf->symtab_stop; sym++) {
		if (sym->st_shndx != relsym->st_shndx)
			continue;
		if (sym->st_value == addr)
			return sym;
	}
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
static void find_symbols_between(struct elf_info *elf, Elf_Addr addr,
				 const char *sec,
			         Elf_Sym **before, Elf_Sym **after)
{
	Elf_Sym *sym;
	Elf_Ehdr *hdr = elf->hdr;
	Elf_Addr beforediff = ~0;
	Elf_Addr afterdiff = ~0;
	const char *secstrings = (void *)hdr +
				 elf->sechdrs[hdr->e_shstrndx].sh_offset;

	*before = NULL;
	*after = NULL;

	for (sym = elf->symtab_start; sym < elf->symtab_stop; sym++) {
		const char *symsec;

		if (sym->st_shndx >= SHN_LORESERVE)
			continue;
		symsec = secstrings + elf->sechdrs[sym->st_shndx].sh_name;
		if (strcmp(symsec, sec) != 0)
			continue;
		if (!is_valid_name(elf, sym))
			continue;
		if (sym->st_value <= addr) {
			if ((addr - sym->st_value) < beforediff) {
				beforediff = addr - sym->st_value;
				*before = sym;
			}
			else if ((addr - sym->st_value) == beforediff) {
				*before = sym;
			}
		}
		else
		{
			if ((sym->st_value - addr) < afterdiff) {
				afterdiff = sym->st_value - addr;
				*after = sym;
			}
			else if ((sym->st_value - addr) == afterdiff) {
				*after = sym;
			}
		}
	}
}

/**
 * Print a warning about a section mismatch.
 * Try to find symbols near it so user can find it.
 * Check whitelist before warning - it may be a false positive.
 **/
static void warn_sec_mismatch(const char *modname, const char *fromsec,
			      struct elf_info *elf, Elf_Sym *sym, Elf_Rela r)
{
	const char *refsymname = "";
	Elf_Sym *before, *after;
	Elf_Sym *refsym;
	Elf_Ehdr *hdr = elf->hdr;
	Elf_Shdr *sechdrs = elf->sechdrs;
	const char *secstrings = (void *)hdr +
				 sechdrs[hdr->e_shstrndx].sh_offset;
	const char *secname = secstrings + sechdrs[sym->st_shndx].sh_name;

	find_symbols_between(elf, r.r_offset, fromsec, &before, &after);

	refsym = find_elf_symbol(elf, r.r_addend, sym);
	if (refsym && strlen(elf->strtab + refsym->st_name))
		refsymname = elf->strtab + refsym->st_name;

	/* check whitelist - we may ignore it */
	if (before &&
	    secref_whitelist(modname, secname, fromsec,
			     elf->strtab + before->st_name, refsymname))
		return;

	if (before && after) {
		warn("%s - Section mismatch: reference to %s:%s from %s "
		     "between '%s' (at offset 0x%llx) and '%s'\n",
		     modname, secname, refsymname, fromsec,
		     elf->strtab + before->st_name,
		     (long long)r.r_offset,
		     elf->strtab + after->st_name);
	} else if (before) {
		warn("%s - Section mismatch: reference to %s:%s from %s "
		     "after '%s' (at offset 0x%llx)\n",
		     modname, secname, refsymname, fromsec,
		     elf->strtab + before->st_name,
		     (long long)r.r_offset);
	} else if (after) {
		warn("%s - Section mismatch: reference to %s:%s from %s "
		     "before '%s' (at offset -0x%llx)\n",
		     modname, secname, refsymname, fromsec,
		     elf->strtab + after->st_name,
		     (long long)r.r_offset);
	} else {
		warn("%s - Section mismatch: reference to %s:%s from %s "
		     "(offset 0x%llx)\n",
		     modname, secname, fromsec, refsymname,
		     (long long)r.r_offset);
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
			  struct elf_info *elf,
			  int section(const char*),
			  int section_ref_ok(const char *))
{
	int i;
	Elf_Sym  *sym;
	Elf_Ehdr *hdr = elf->hdr;
	Elf_Shdr *sechdrs = elf->sechdrs;
	const char *secstrings = (void *)hdr +
				 sechdrs[hdr->e_shstrndx].sh_offset;

	/* Walk through all sections */
	for (i = 0; i < hdr->e_shnum; i++) {
		const char *name = secstrings + sechdrs[i].sh_name;
		const char *secname;
		Elf_Rela r;
		unsigned int r_sym;
		/* We want to process only relocation sections and not .init */
		if (sechdrs[i].sh_type == SHT_RELA) {
			Elf_Rela *rela;
			Elf_Rela *start = (void *)hdr + sechdrs[i].sh_offset;
			Elf_Rela *stop  = (void*)start + sechdrs[i].sh_size;
			name += strlen(".rela");
			if (section_ref_ok(name))
				continue;

			for (rela = start; rela < stop; rela++) {
				r.r_offset = TO_NATIVE(rela->r_offset);
#if KERNEL_ELFCLASS == ELFCLASS64
				if (hdr->e_machine == EM_MIPS) {
					r_sym = ELF64_MIPS_R_SYM(rela->r_info);
					r_sym = TO_NATIVE(r_sym);
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

				secname = secstrings +
					sechdrs[sym->st_shndx].sh_name;
				if (section(secname))
					warn_sec_mismatch(modname, name,
							  elf, sym, r);
			}
		} else if (sechdrs[i].sh_type == SHT_REL) {
			Elf_Rel *rel;
			Elf_Rel *start = (void *)hdr + sechdrs[i].sh_offset;
			Elf_Rel *stop  = (void*)start + sechdrs[i].sh_size;
			name += strlen(".rel");
			if (section_ref_ok(name))
				continue;

			for (rel = start; rel < stop; rel++) {
				r.r_offset = TO_NATIVE(rel->r_offset);
#if KERNEL_ELFCLASS == ELFCLASS64
				if (hdr->e_machine == EM_MIPS) {
					r_sym = ELF64_MIPS_R_SYM(rel->r_info);
					r_sym = TO_NATIVE(r_sym);
				} else {
					r.r_info = TO_NATIVE(rel->r_info);
					r_sym = ELF_R_SYM(r.r_info);
				}
#else
				r.r_info = TO_NATIVE(rel->r_info);
				r_sym = ELF_R_SYM(r.r_info);
#endif
				r.r_addend = 0;
				sym = elf->symtab_start + r_sym;
				/* Skip special sections */
				if (sym->st_shndx >= SHN_LORESERVE)
					continue;

				secname = secstrings +
					sechdrs[sym->st_shndx].sh_name;
				if (section(secname))
					warn_sec_mismatch(modname, name,
							  elf, sym, r);
			}
		}
	}
}

/**
 * Functions used only during module init is marked __init and is stored in
 * a .init.text section. Likewise data is marked __initdata and stored in
 * a .init.data section.
 * If this section is one of these sections return 1
 * See include/linux/init.h for the details
 **/
static int init_section(const char *name)
{
	if (strcmp(name, ".init") == 0)
		return 1;
	if (strncmp(name, ".init.", strlen(".init.")) == 0)
		return 1;
	return 0;
}

/**
 * Identify sections from which references to a .init section is OK.
 *
 * Unfortunately references to read only data that referenced .init
 * sections had to be excluded. Almost all of these are false
 * positives, they are created by gcc. The downside of excluding rodata
 * is that there really are some user references from rodata to
 * init code, e.g. drivers/video/vgacon.c:
 *
 * const struct consw vga_con = {
 *        con_startup:            vgacon_startup,
 *
 * where vgacon_startup is __init.  If you want to wade through the false
 * positives, take out the check for rodata.
 **/
static int init_section_ref_ok(const char *name)
{
	const char **s;
	/* Absolute section names */
	const char *namelist1[] = {
		".init",
		".opd",   /* see comment [OPD] at exit_section_ref_ok() */
		".toc1",  /* used by ppc64 */
		".stab",
		".data.rel.ro", /* used by parisc64 */
		".parainstructions",
		".text.lock",
		"__bug_table", /* used by powerpc for BUG() */
		".pci_fixup_header",
		".pci_fixup_final",
		".pdr",
		"__param",
		"__ex_table",
		".fixup",
		".smp_locks",
		".plt",  /* seen on ARCH=um build on x86_64. Harmless */
		"__ftr_fixup",		/* powerpc cpu feature fixup */
		"__fw_ftr_fixup",	/* powerpc firmware feature fixup */
		NULL
	};
	/* Start of section names */
	const char *namelist2[] = {
		".init.",
		".altinstructions",
		".eh_frame",
		".debug",
		".parainstructions",
		".rodata",
		NULL
	};
	/* part of section name */
	const char *namelist3 [] = {
		".unwind",  /* sample: IA_64.unwind.init.text */
		NULL
	};

	for (s = namelist1; *s; s++)
		if (strcmp(*s, name) == 0)
			return 1;
	for (s = namelist2; *s; s++)
		if (strncmp(*s, name, strlen(*s)) == 0)
			return 1;
	for (s = namelist3; *s; s++)
		if (strstr(name, *s) != NULL)
			return 1;
	if (strrcmp(name, ".init") == 0)
		return 1;
	return 0;
}

/*
 * Functions used only during module exit is marked __exit and is stored in
 * a .exit.text section. Likewise data is marked __exitdata and stored in
 * a .exit.data section.
 * If this section is one of these sections return 1
 * See include/linux/init.h for the details
 **/
static int exit_section(const char *name)
{
	if (strcmp(name, ".exit.text") == 0)
		return 1;
	if (strcmp(name, ".exit.data") == 0)
		return 1;
	return 0;

}

/*
 * Identify sections from which references to a .exit section is OK.
 *
 * [OPD] Keith Ownes <kaos@sgi.com> commented:
 * For our future {in}sanity, add a comment that this is the ppc .opd
 * section, not the ia64 .opd section.
 * ia64 .opd should not point to discarded sections.
 * [.rodata] like for .init.text we ignore .rodata references -same reason
 **/
static int exit_section_ref_ok(const char *name)
{
	const char **s;
	/* Absolute section names */
	const char *namelist1[] = {
		".exit.text",
		".exit.data",
		".init.text",
		".rodata",
		".opd", /* See comment [OPD] */
		".toc1",  /* used by ppc64 */
		".altinstructions",
		".pdr",
		"__bug_table", /* used by powerpc for BUG() */
		".exitcall.exit",
		".eh_frame",
		".parainstructions",
		".stab",
		"__ex_table",
		".fixup",
		".smp_locks",
		".plt",  /* seen on ARCH=um build on x86_64. Harmless */
		NULL
	};
	/* Start of section names */
	const char *namelist2[] = {
		".debug",
		NULL
	};
	/* part of section name */
	const char *namelist3 [] = {
		".unwind",  /* Sample: IA_64.unwind.exit.text */
		NULL
	};

	for (s = namelist1; *s; s++)
		if (strcmp(*s, name) == 0)
			return 1;
	for (s = namelist2; *s; s++)
		if (strncmp(*s, name, strlen(*s)) == 0)
			return 1;
	for (s = namelist3; *s; s++)
		if (strstr(name, *s) != NULL)
			return 1;
	return 0;
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
	check_sec_ref(mod, modname, &info, init_section, init_section_ref_ok);
	check_sec_ref(mod, modname, &info, exit_section, exit_section_ref_ok);

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

static void check_for_unused(enum export exp, const char* m, const char* s)
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
	buf_printf(b, "__attribute_used__\n");
	buf_printf(b, "__attribute__((section(\"__versions\"))) = {\n");

	for (s = mod->unres; s; s = s->next) {
		if (!s->module) {
			continue;
		}
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

	for (m = modules; m; m = m->next) {
		m->seen = is_vmlinux(m->name);
	}

	buf_printf(b, "\n");
	buf_printf(b, "static const char __module_depends[]\n");
	buf_printf(b, "__attribute_used__\n");
	buf_printf(b, "__attribute__((section(\".modinfo\"))) =\n");
	buf_printf(b, "\"depends=");
	for (s = mod->unres; s; s = s->next) {
		const char *p;
		if (!s->module)
			continue;

		if (s->module->seen)
			continue;

		s->module->seen = 1;
		if ((p = strrchr(s->module->name, '/')) != NULL)
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

		if (!(mod = find_module(modname))) {
			if (is_vmlinux(modname)) {
				have_vmlinux = 1;
			}
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
	char fname[SZ];
	char *kernel_read = NULL, *module_read = NULL;
	char *dump_write = NULL;
	int opt;
	int err;

	while ((opt = getopt(argc, argv, "i:I:mo:aw")) != -1) {
		switch(opt) {
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

	while (optind < argc) {
		read_symbols(argv[optind++]);
	}

	for (mod = modules; mod; mod = mod->next) {
		if (mod->skip)
			continue;
		check_exports(mod);
	}

	err = 0;

	for (mod = modules; mod; mod = mod->next) {
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

	return err;
}
