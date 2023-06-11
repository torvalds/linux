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

#define _GNU_SOURCE
#include <elf.h>
#include <fnmatch.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>
#include <errno.h>
#include "modpost.h"
#include "../../include/linux/license.h"
#include "../../include/linux/module_symbol.h"

/* Are we using CONFIG_MODVERSIONS? */
static bool modversions;
/* Is CONFIG_MODULE_SRCVERSION_ALL set? */
static bool all_versions;
/* If we are modposting external module set to 1 */
static bool external_module;
/* Only warn about unresolved symbols */
static bool warn_unresolved;

static int sec_mismatch_count;
static bool sec_mismatch_warn_only = true;
/* Trim EXPORT_SYMBOLs that are unused by in-tree modules */
static bool trim_unused_exports;

/* ignore missing files */
static bool ignore_missing_files;
/* If set to 1, only warn (instead of error) about missing ns imports */
static bool allow_missing_ns_imports;

static bool error_occurred;

static bool extra_warn;

/*
 * Cut off the warnings when there are too many. This typically occurs when
 * vmlinux is missing. ('make modules' without building vmlinux.)
 */
#define MAX_UNRESOLVED_REPORTS	10
static unsigned int nr_unresolved;

/* In kernel, this size is defined in linux/module.h;
 * here we use Elf_Addr instead of long for covering cross-compile
 */

#define MODULE_NAME_LEN (64 - sizeof(Elf_Addr))

void __attribute__((format(printf, 2, 3)))
modpost_log(enum loglevel loglevel, const char *fmt, ...)
{
	va_list arglist;

	switch (loglevel) {
	case LOG_WARN:
		fprintf(stderr, "WARNING: ");
		break;
	case LOG_ERROR:
		fprintf(stderr, "ERROR: ");
		break;
	case LOG_FATAL:
		fprintf(stderr, "FATAL: ");
		break;
	default: /* invalid loglevel, ignore */
		break;
	}

	fprintf(stderr, "modpost: ");

	va_start(arglist, fmt);
	vfprintf(stderr, fmt, arglist);
	va_end(arglist);

	if (loglevel == LOG_FATAL)
		exit(1);
	if (loglevel == LOG_ERROR)
		error_occurred = true;
}

static inline bool strends(const char *str, const char *postfix)
{
	if (strlen(str) < strlen(postfix))
		return false;

	return strcmp(str + strlen(str) - strlen(postfix), postfix) == 0;
}

void *do_nofail(void *ptr, const char *expr)
{
	if (!ptr)
		fatal("Memory allocation failure: %s.\n", expr);

	return ptr;
}

char *read_text_file(const char *filename)
{
	struct stat st;
	size_t nbytes;
	int fd;
	char *buf;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		perror(filename);
		exit(1);
	}

	if (fstat(fd, &st) < 0) {
		perror(filename);
		exit(1);
	}

	buf = NOFAIL(malloc(st.st_size + 1));

	nbytes = st.st_size;

	while (nbytes) {
		ssize_t bytes_read;

		bytes_read = read(fd, buf, nbytes);
		if (bytes_read < 0) {
			perror(filename);
			exit(1);
		}

		nbytes -= bytes_read;
	}
	buf[st.st_size] = '\0';

	close(fd);

	return buf;
}

char *get_line(char **stringp)
{
	char *orig = *stringp, *next;

	/* do not return the unwanted extra line at EOF */
	if (!orig || *orig == '\0')
		return NULL;

	/* don't use strsep here, it is not available everywhere */
	next = strchr(orig, '\n');
	if (next)
		*next++ = '\0';

	*stringp = next;

	return orig;
}

/* A list of all modules we processed */
LIST_HEAD(modules);

static struct module *find_module(const char *modname)
{
	struct module *mod;

	list_for_each_entry(mod, &modules, list) {
		if (strcmp(mod->name, modname) == 0)
			return mod;
	}
	return NULL;
}

static struct module *new_module(const char *name, size_t namelen)
{
	struct module *mod;

	mod = NOFAIL(malloc(sizeof(*mod) + namelen + 1));
	memset(mod, 0, sizeof(*mod));

	INIT_LIST_HEAD(&mod->exported_symbols);
	INIT_LIST_HEAD(&mod->unresolved_symbols);
	INIT_LIST_HEAD(&mod->missing_namespaces);
	INIT_LIST_HEAD(&mod->imported_namespaces);

	memcpy(mod->name, name, namelen);
	mod->name[namelen] = '\0';
	mod->is_vmlinux = (strcmp(mod->name, "vmlinux") == 0);

	/*
	 * Set mod->is_gpl_compatible to true by default. If MODULE_LICENSE()
	 * is missing, do not check the use for EXPORT_SYMBOL_GPL() becasue
	 * modpost will exit wiht error anyway.
	 */
	mod->is_gpl_compatible = true;

	list_add_tail(&mod->list, &modules);

	return mod;
}

/* A hash of all exported symbols,
 * struct symbol is also used for lists of unresolved symbols */

#define SYMBOL_HASH_SIZE 1024

struct symbol {
	struct symbol *next;
	struct list_head list;	/* link to module::exported_symbols or module::unresolved_symbols */
	struct module *module;
	char *namespace;
	unsigned int crc;
	bool crc_valid;
	bool weak;
	bool is_func;
	bool is_gpl_only;	/* exported by EXPORT_SYMBOL_GPL */
	bool used;		/* there exists a user of this symbol */
	char name[];
};

static struct symbol *symbolhash[SYMBOL_HASH_SIZE];

/* This is based on the hash algorithm from gdbm, via tdb */
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
static struct symbol *alloc_symbol(const char *name)
{
	struct symbol *s = NOFAIL(malloc(sizeof(*s) + strlen(name) + 1));

	memset(s, 0, sizeof(*s));
	strcpy(s->name, name);

	return s;
}

/* For the hash of exported symbols */
static void hash_add_symbol(struct symbol *sym)
{
	unsigned int hash;

	hash = tdb_hash(sym->name) % SYMBOL_HASH_SIZE;
	sym->next = symbolhash[hash];
	symbolhash[hash] = sym;
}

static void sym_add_unresolved(const char *name, struct module *mod, bool weak)
{
	struct symbol *sym;

	sym = alloc_symbol(name);
	sym->weak = weak;

	list_add_tail(&sym->list, &mod->unresolved_symbols);
}

static struct symbol *sym_find_with_module(const char *name, struct module *mod)
{
	struct symbol *s;

	/* For our purposes, .foo matches foo.  PPC64 needs this. */
	if (name[0] == '.')
		name++;

	for (s = symbolhash[tdb_hash(name) % SYMBOL_HASH_SIZE]; s; s = s->next) {
		if (strcmp(s->name, name) == 0 && (!mod || s->module == mod))
			return s;
	}
	return NULL;
}

static struct symbol *find_symbol(const char *name)
{
	return sym_find_with_module(name, NULL);
}

struct namespace_list {
	struct list_head list;
	char namespace[];
};

static bool contains_namespace(struct list_head *head, const char *namespace)
{
	struct namespace_list *list;

	/*
	 * The default namespace is null string "", which is always implicitly
	 * contained.
	 */
	if (!namespace[0])
		return true;

	list_for_each_entry(list, head, list) {
		if (!strcmp(list->namespace, namespace))
			return true;
	}

	return false;
}

static void add_namespace(struct list_head *head, const char *namespace)
{
	struct namespace_list *ns_entry;

	if (!contains_namespace(head, namespace)) {
		ns_entry = NOFAIL(malloc(sizeof(*ns_entry) +
					 strlen(namespace) + 1));
		strcpy(ns_entry->namespace, namespace);
		list_add_tail(&ns_entry->list, head);
	}
}

static void *sym_get_data_by_offset(const struct elf_info *info,
				    unsigned int secindex, unsigned long offset)
{
	Elf_Shdr *sechdr = &info->sechdrs[secindex];

	return (void *)info->hdr + sechdr->sh_offset + offset;
}

void *sym_get_data(const struct elf_info *info, const Elf_Sym *sym)
{
	return sym_get_data_by_offset(info, get_secindex(info, sym),
				      sym->st_value);
}

static const char *sech_name(const struct elf_info *info, Elf_Shdr *sechdr)
{
	return sym_get_data_by_offset(info, info->secindex_strings,
				      sechdr->sh_name);
}

static const char *sec_name(const struct elf_info *info, unsigned int secindex)
{
	/*
	 * If sym->st_shndx is a special section index, there is no
	 * corresponding section header.
	 * Return "" if the index is out of range of info->sechdrs[] array.
	 */
	if (secindex >= info->num_sections)
		return "";

	return sech_name(info, &info->sechdrs[secindex]);
}

#define strstarts(str, prefix) (strncmp(str, prefix, strlen(prefix)) == 0)

static struct symbol *sym_add_exported(const char *name, struct module *mod,
				       bool gpl_only, const char *namespace)
{
	struct symbol *s = find_symbol(name);

	if (s && (!external_module || s->module->is_vmlinux || s->module == mod)) {
		error("%s: '%s' exported twice. Previous export was in %s%s\n",
		      mod->name, name, s->module->name,
		      s->module->is_vmlinux ? "" : ".ko");
	}

	s = alloc_symbol(name);
	s->module = mod;
	s->is_gpl_only = gpl_only;
	s->namespace = NOFAIL(strdup(namespace));
	list_add_tail(&s->list, &mod->exported_symbols);
	hash_add_symbol(s);

	return s;
}

static void sym_set_crc(struct symbol *sym, unsigned int crc)
{
	sym->crc = crc;
	sym->crc_valid = true;
}

static void *grab_file(const char *filename, size_t *size)
{
	struct stat st;
	void *map = MAP_FAILED;
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return NULL;
	if (fstat(fd, &st))
		goto failed;

	*size = st.st_size;
	map = mmap(NULL, *size, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);

failed:
	close(fd);
	if (map == MAP_FAILED)
		return NULL;
	return map;
}

static void release_file(void *file, size_t size)
{
	munmap(file, size);
}

static int parse_elf(struct elf_info *info, const char *filename)
{
	unsigned int i;
	Elf_Ehdr *hdr;
	Elf_Shdr *sechdrs;
	Elf_Sym  *sym;
	const char *secstrings;
	unsigned int symtab_idx = ~0U, symtab_shndx_idx = ~0U;

	hdr = grab_file(filename, &info->size);
	if (!hdr) {
		if (ignore_missing_files) {
			fprintf(stderr, "%s: %s (ignored)\n", filename,
				strerror(errno));
			return 0;
		}
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
	hdr->e_type      = TO_NATIVE(hdr->e_type);
	hdr->e_machine   = TO_NATIVE(hdr->e_machine);
	hdr->e_version   = TO_NATIVE(hdr->e_version);
	hdr->e_entry     = TO_NATIVE(hdr->e_entry);
	hdr->e_phoff     = TO_NATIVE(hdr->e_phoff);
	hdr->e_shoff     = TO_NATIVE(hdr->e_shoff);
	hdr->e_flags     = TO_NATIVE(hdr->e_flags);
	hdr->e_ehsize    = TO_NATIVE(hdr->e_ehsize);
	hdr->e_phentsize = TO_NATIVE(hdr->e_phentsize);
	hdr->e_phnum     = TO_NATIVE(hdr->e_phnum);
	hdr->e_shentsize = TO_NATIVE(hdr->e_shentsize);
	hdr->e_shnum     = TO_NATIVE(hdr->e_shnum);
	hdr->e_shstrndx  = TO_NATIVE(hdr->e_shstrndx);
	sechdrs = (void *)hdr + hdr->e_shoff;
	info->sechdrs = sechdrs;

	/* modpost only works for relocatable objects */
	if (hdr->e_type != ET_REL)
		fatal("%s: not relocatable object.", filename);

	/* Check if file offset is correct */
	if (hdr->e_shoff > info->size) {
		fatal("section header offset=%lu in file '%s' is bigger than filesize=%zu\n",
		      (unsigned long)hdr->e_shoff, filename, info->size);
		return 0;
	}

	if (hdr->e_shnum == SHN_UNDEF) {
		/*
		 * There are more than 64k sections,
		 * read count from .sh_size.
		 */
		info->num_sections = TO_NATIVE(sechdrs[0].sh_size);
	}
	else {
		info->num_sections = hdr->e_shnum;
	}
	if (hdr->e_shstrndx == SHN_XINDEX) {
		info->secindex_strings = TO_NATIVE(sechdrs[0].sh_link);
	}
	else {
		info->secindex_strings = hdr->e_shstrndx;
	}

	/* Fix endianness in section headers */
	for (i = 0; i < info->num_sections; i++) {
		sechdrs[i].sh_name      = TO_NATIVE(sechdrs[i].sh_name);
		sechdrs[i].sh_type      = TO_NATIVE(sechdrs[i].sh_type);
		sechdrs[i].sh_flags     = TO_NATIVE(sechdrs[i].sh_flags);
		sechdrs[i].sh_addr      = TO_NATIVE(sechdrs[i].sh_addr);
		sechdrs[i].sh_offset    = TO_NATIVE(sechdrs[i].sh_offset);
		sechdrs[i].sh_size      = TO_NATIVE(sechdrs[i].sh_size);
		sechdrs[i].sh_link      = TO_NATIVE(sechdrs[i].sh_link);
		sechdrs[i].sh_info      = TO_NATIVE(sechdrs[i].sh_info);
		sechdrs[i].sh_addralign = TO_NATIVE(sechdrs[i].sh_addralign);
		sechdrs[i].sh_entsize   = TO_NATIVE(sechdrs[i].sh_entsize);
	}
	/* Find symbol table. */
	secstrings = (void *)hdr + sechdrs[info->secindex_strings].sh_offset;
	for (i = 1; i < info->num_sections; i++) {
		const char *secname;
		int nobits = sechdrs[i].sh_type == SHT_NOBITS;

		if (!nobits && sechdrs[i].sh_offset > info->size) {
			fatal("%s is truncated. sechdrs[i].sh_offset=%lu > sizeof(*hrd)=%zu\n",
			      filename, (unsigned long)sechdrs[i].sh_offset,
			      sizeof(*hdr));
			return 0;
		}
		secname = secstrings + sechdrs[i].sh_name;
		if (strcmp(secname, ".modinfo") == 0) {
			if (nobits)
				fatal("%s has NOBITS .modinfo\n", filename);
			info->modinfo = (void *)hdr + sechdrs[i].sh_offset;
			info->modinfo_len = sechdrs[i].sh_size;
		} else if (!strcmp(secname, ".export_symbol")) {
			info->export_symbol_secndx = i;
		}

		if (sechdrs[i].sh_type == SHT_SYMTAB) {
			unsigned int sh_link_idx;
			symtab_idx = i;
			info->symtab_start = (void *)hdr +
			    sechdrs[i].sh_offset;
			info->symtab_stop  = (void *)hdr +
			    sechdrs[i].sh_offset + sechdrs[i].sh_size;
			sh_link_idx = sechdrs[i].sh_link;
			info->strtab       = (void *)hdr +
			    sechdrs[sh_link_idx].sh_offset;
		}

		/* 32bit section no. table? ("more than 64k sections") */
		if (sechdrs[i].sh_type == SHT_SYMTAB_SHNDX) {
			symtab_shndx_idx = i;
			info->symtab_shndx_start = (void *)hdr +
			    sechdrs[i].sh_offset;
			info->symtab_shndx_stop  = (void *)hdr +
			    sechdrs[i].sh_offset + sechdrs[i].sh_size;
		}
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

	if (symtab_shndx_idx != ~0U) {
		Elf32_Word *p;
		if (symtab_idx != sechdrs[symtab_shndx_idx].sh_link)
			fatal("%s: SYMTAB_SHNDX has bad sh_link: %u!=%u\n",
			      filename, sechdrs[symtab_shndx_idx].sh_link,
			      symtab_idx);
		/* Fix endianness */
		for (p = info->symtab_shndx_start; p < info->symtab_shndx_stop;
		     p++)
			*p = TO_NATIVE(*p);
	}

	return 1;
}

static void parse_elf_finish(struct elf_info *info)
{
	release_file(info->hdr, info->size);
}

static int ignore_undef_symbol(struct elf_info *info, const char *symname)
{
	/* ignore __this_module, it will be resolved shortly */
	if (strcmp(symname, "__this_module") == 0)
		return 1;
	/* ignore global offset table */
	if (strcmp(symname, "_GLOBAL_OFFSET_TABLE_") == 0)
		return 1;
	if (info->hdr->e_machine == EM_PPC)
		/* Special register function linked on all modules during final link of .ko */
		if (strstarts(symname, "_restgpr_") ||
		    strstarts(symname, "_savegpr_") ||
		    strstarts(symname, "_rest32gpr_") ||
		    strstarts(symname, "_save32gpr_") ||
		    strstarts(symname, "_restvr_") ||
		    strstarts(symname, "_savevr_"))
			return 1;
	if (info->hdr->e_machine == EM_PPC64)
		/* Special register function linked on all modules during final link of .ko */
		if (strstarts(symname, "_restgpr0_") ||
		    strstarts(symname, "_savegpr0_") ||
		    strstarts(symname, "_restvr_") ||
		    strstarts(symname, "_savevr_") ||
		    strcmp(symname, ".TOC.") == 0)
			return 1;

	if (info->hdr->e_machine == EM_S390)
		/* Expoline thunks are linked on all kernel modules during final link of .ko */
		if (strstarts(symname, "__s390_indirect_jump_r"))
			return 1;
	/* Do not ignore this symbol */
	return 0;
}

static void handle_symbol(struct module *mod, struct elf_info *info,
			  const Elf_Sym *sym, const char *symname)
{
	switch (sym->st_shndx) {
	case SHN_COMMON:
		if (strstarts(symname, "__gnu_lto_")) {
			/* Should warn here, but modpost runs before the linker */
		} else
			warn("\"%s\" [%s] is COMMON symbol\n", symname, mod->name);
		break;
	case SHN_UNDEF:
		/* undefined symbol */
		if (ELF_ST_BIND(sym->st_info) != STB_GLOBAL &&
		    ELF_ST_BIND(sym->st_info) != STB_WEAK)
			break;
		if (ignore_undef_symbol(info, symname))
			break;
		if (info->hdr->e_machine == EM_SPARC ||
		    info->hdr->e_machine == EM_SPARCV9) {
			/* Ignore register directives. */
			if (ELF_ST_TYPE(sym->st_info) == STT_SPARC_REGISTER)
				break;
			if (symname[0] == '.') {
				char *munged = NOFAIL(strdup(symname));
				munged[0] = '_';
				munged[1] = toupper(munged[1]);
				symname = munged;
			}
		}

		sym_add_unresolved(symname, mod,
				   ELF_ST_BIND(sym->st_info) == STB_WEAK);
		break;
	default:
		if (strcmp(symname, "init_module") == 0)
			mod->has_init = true;
		if (strcmp(symname, "cleanup_module") == 0)
			mod->has_cleanup = true;
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

static char *get_next_modinfo(struct elf_info *info, const char *tag,
			      char *prev)
{
	char *p;
	unsigned int taglen = strlen(tag);
	char *modinfo = info->modinfo;
	unsigned long size = info->modinfo_len;

	if (prev) {
		size -= prev - modinfo;
		modinfo = next_string(prev, &size);
	}

	for (p = modinfo; p; p = next_string(p, &size)) {
		if (strncmp(p, tag, taglen) == 0 && p[taglen] == '=')
			return p + taglen + 1;
	}
	return NULL;
}

static char *get_modinfo(struct elf_info *info, const char *tag)

{
	return get_next_modinfo(info, tag, NULL);
}

static const char *sym_name(struct elf_info *elf, Elf_Sym *sym)
{
	if (sym)
		return elf->strtab + sym->st_name;
	else
		return "(unknown)";
}

/*
 * Check whether the 'string' argument matches one of the 'patterns',
 * an array of shell wildcard patterns (glob).
 *
 * Return true is there is a match.
 */
static bool match(const char *string, const char *const patterns[])
{
	const char *pattern;

	while ((pattern = *patterns++)) {
		if (!fnmatch(pattern, string, 0))
			return true;
	}

	return false;
}

/* useful to pass patterns to match() directly */
#define PATTERNS(...) \
	({ \
		static const char *const patterns[] = {__VA_ARGS__, NULL}; \
		patterns; \
	})

/* sections that we do not want to do full section mismatch check on */
static const char *const section_white_list[] =
{
	".comment*",
	".debug*",
	".zdebug*",		/* Compressed debug sections. */
	".GCC.command.line",	/* record-gcc-switches */
	".mdebug*",        /* alpha, score, mips etc. */
	".pdr",            /* alpha, score, mips etc. */
	".stab*",
	".note*",
	".got*",
	".toc*",
	".xt.prop",				 /* xtensa */
	".xt.lit",         /* xtensa */
	".arcextmap*",			/* arc */
	".gnu.linkonce.arcext*",	/* arc : modules */
	".cmem*",			/* EZchip */
	".fmt_slot*",			/* EZchip */
	".gnu.lto*",
	".discard.*",
	NULL
};

/*
 * This is used to find sections missing the SHF_ALLOC flag.
 * The cause of this is often a section specified in assembler
 * without "ax" / "aw".
 */
static void check_section(const char *modname, struct elf_info *elf,
			  Elf_Shdr *sechdr)
{
	const char *sec = sech_name(elf, sechdr);

	if (sechdr->sh_type == SHT_PROGBITS &&
	    !(sechdr->sh_flags & SHF_ALLOC) &&
	    !match(sec, section_white_list)) {
		warn("%s (%s): unexpected non-allocatable section.\n"
		     "Did you forget to use \"ax\"/\"aw\" in a .S file?\n"
		     "Note that for example <linux/init.h> contains\n"
		     "section definitions for use in .S files.\n\n",
		     modname, sec);
	}
}



#define ALL_INIT_DATA_SECTIONS \
	".init.setup", ".init.rodata", ".meminit.rodata", \
	".init.data", ".meminit.data"
#define ALL_EXIT_DATA_SECTIONS \
	".exit.data", ".memexit.data"

#define ALL_INIT_TEXT_SECTIONS \
	".init.text", ".meminit.text"
#define ALL_EXIT_TEXT_SECTIONS \
	".exit.text", ".memexit.text"

#define ALL_PCI_INIT_SECTIONS	\
	".pci_fixup_early", ".pci_fixup_header", ".pci_fixup_final", \
	".pci_fixup_enable", ".pci_fixup_resume", \
	".pci_fixup_resume_early", ".pci_fixup_suspend"

#define ALL_XXXINIT_SECTIONS MEM_INIT_SECTIONS
#define ALL_XXXEXIT_SECTIONS MEM_EXIT_SECTIONS

#define ALL_INIT_SECTIONS INIT_SECTIONS, ALL_XXXINIT_SECTIONS
#define ALL_EXIT_SECTIONS EXIT_SECTIONS, ALL_XXXEXIT_SECTIONS

#define DATA_SECTIONS ".data", ".data.rel"
#define TEXT_SECTIONS ".text", ".text.*", ".sched.text", \
		".kprobes.text", ".cpuidle.text", ".noinstr.text"
#define OTHER_TEXT_SECTIONS ".ref.text", ".head.text", ".spinlock.text", \
		".fixup", ".entry.text", ".exception.text", \
		".coldtext", ".softirqentry.text"

#define INIT_SECTIONS      ".init.*"
#define MEM_INIT_SECTIONS  ".meminit.*"

#define EXIT_SECTIONS      ".exit.*"
#define MEM_EXIT_SECTIONS  ".memexit.*"

#define ALL_TEXT_SECTIONS  ALL_INIT_TEXT_SECTIONS, ALL_EXIT_TEXT_SECTIONS, \
		TEXT_SECTIONS, OTHER_TEXT_SECTIONS

enum mismatch {
	TEXT_TO_ANY_INIT,
	DATA_TO_ANY_INIT,
	TEXTDATA_TO_ANY_EXIT,
	XXXINIT_TO_SOME_INIT,
	XXXEXIT_TO_SOME_EXIT,
	ANY_INIT_TO_ANY_EXIT,
	ANY_EXIT_TO_ANY_INIT,
	EXTABLE_TO_NON_TEXT,
};

/**
 * Describe how to match sections on different criteria:
 *
 * @fromsec: Array of sections to be matched.
 *
 * @bad_tosec: Relocations applied to a section in @fromsec to a section in
 * this array is forbidden (black-list).  Can be empty.
 *
 * @good_tosec: Relocations applied to a section in @fromsec must be
 * targeting sections in this array (white-list).  Can be empty.
 *
 * @mismatch: Type of mismatch.
 */
struct sectioncheck {
	const char *fromsec[20];
	const char *bad_tosec[20];
	const char *good_tosec[20];
	enum mismatch mismatch;
};

static const struct sectioncheck sectioncheck[] = {
/* Do not reference init/exit code/data from
 * normal code and data
 */
{
	.fromsec = { TEXT_SECTIONS, NULL },
	.bad_tosec = { ALL_INIT_SECTIONS, NULL },
	.mismatch = TEXT_TO_ANY_INIT,
},
{
	.fromsec = { DATA_SECTIONS, NULL },
	.bad_tosec = { ALL_XXXINIT_SECTIONS, INIT_SECTIONS, NULL },
	.mismatch = DATA_TO_ANY_INIT,
},
{
	.fromsec = { TEXT_SECTIONS, DATA_SECTIONS, NULL },
	.bad_tosec = { ALL_EXIT_SECTIONS, NULL },
	.mismatch = TEXTDATA_TO_ANY_EXIT,
},
/* Do not reference init code/data from meminit code/data */
{
	.fromsec = { ALL_XXXINIT_SECTIONS, NULL },
	.bad_tosec = { INIT_SECTIONS, NULL },
	.mismatch = XXXINIT_TO_SOME_INIT,
},
/* Do not reference exit code/data from memexit code/data */
{
	.fromsec = { ALL_XXXEXIT_SECTIONS, NULL },
	.bad_tosec = { EXIT_SECTIONS, NULL },
	.mismatch = XXXEXIT_TO_SOME_EXIT,
},
/* Do not use exit code/data from init code */
{
	.fromsec = { ALL_INIT_SECTIONS, NULL },
	.bad_tosec = { ALL_EXIT_SECTIONS, NULL },
	.mismatch = ANY_INIT_TO_ANY_EXIT,
},
/* Do not use init code/data from exit code */
{
	.fromsec = { ALL_EXIT_SECTIONS, NULL },
	.bad_tosec = { ALL_INIT_SECTIONS, NULL },
	.mismatch = ANY_EXIT_TO_ANY_INIT,
},
{
	.fromsec = { ALL_PCI_INIT_SECTIONS, NULL },
	.bad_tosec = { INIT_SECTIONS, NULL },
	.mismatch = ANY_INIT_TO_ANY_EXIT,
},
{
	.fromsec = { "__ex_table", NULL },
	/* If you're adding any new black-listed sections in here, consider
	 * adding a special 'printer' for them in scripts/check_extable.
	 */
	.bad_tosec = { ".altinstr_replacement", NULL },
	.good_tosec = {ALL_TEXT_SECTIONS , NULL},
	.mismatch = EXTABLE_TO_NON_TEXT,
}
};

static const struct sectioncheck *section_mismatch(
		const char *fromsec, const char *tosec)
{
	int i;

	/*
	 * The target section could be the SHT_NUL section when we're
	 * handling relocations to un-resolved symbols, trying to match it
	 * doesn't make much sense and causes build failures on parisc
	 * architectures.
	 */
	if (*tosec == '\0')
		return NULL;

	for (i = 0; i < ARRAY_SIZE(sectioncheck); i++) {
		const struct sectioncheck *check = &sectioncheck[i];

		if (match(fromsec, check->fromsec)) {
			if (check->bad_tosec[0] && match(tosec, check->bad_tosec))
				return check;
			if (check->good_tosec[0] && !match(tosec, check->good_tosec))
				return check;
		}
	}
	return NULL;
}

/**
 * Whitelist to allow certain references to pass with no warning.
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
 * Pattern 1a:
 *   module_param_call() ops can refer to __init set function if permissions=0
 *   The pattern is identified by:
 *   tosec   = .init.text
 *   fromsec = .data*
 *   atsym   = __param_ops_*
 *
 * Pattern 3:
 *   Whitelist all references from .head.text to any init section
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
 * Pattern 5:
 *   GCC may optimize static inlines when fed constant arg(s) resulting
 *   in functions like cpumask_empty() -- generating an associated symbol
 *   cpumask_empty.constprop.3 that appears in the audit.  If the const that
 *   is passed in comes from __init, like say nmi_ipi_mask, we get a
 *   meaningless section warning.  May need to add isra symbols too...
 *   This pattern is identified by
 *   tosec   = init section
 *   fromsec = text section
 *   refsymname = *.constprop.*
 *
 **/
static int secref_whitelist(const char *fromsec, const char *fromsym,
			    const char *tosec, const char *tosym)
{
	/* Check for pattern 1 */
	if (match(tosec, PATTERNS(ALL_INIT_DATA_SECTIONS)) &&
	    match(fromsec, PATTERNS(DATA_SECTIONS)) &&
	    strstarts(fromsym, "__param"))
		return 0;

	/* Check for pattern 1a */
	if (strcmp(tosec, ".init.text") == 0 &&
	    match(fromsec, PATTERNS(DATA_SECTIONS)) &&
	    strstarts(fromsym, "__param_ops_"))
		return 0;

	/* symbols in data sections that may refer to any init/exit sections */
	if (match(fromsec, PATTERNS(DATA_SECTIONS)) &&
	    match(tosec, PATTERNS(ALL_INIT_SECTIONS, ALL_EXIT_SECTIONS)) &&
	    match(fromsym, PATTERNS("*_template", // scsi uses *_template a lot
				    "*_timer", // arm uses ops structures named _timer a lot
				    "*_sht", // scsi also used *_sht to some extent
				    "*_ops",
				    "*_probe",
				    "*_probe_one",
				    "*_console")))
		return 0;

	/* symbols in data sections that may refer to meminit/exit sections */
	if (match(fromsec, PATTERNS(DATA_SECTIONS)) &&
	    match(tosec, PATTERNS(ALL_XXXINIT_SECTIONS, ALL_EXIT_SECTIONS)) &&
	    match(fromsym, PATTERNS("*driver")))
		return 0;

	/* Check for pattern 3 */
	if (strstarts(fromsec, ".head.text") &&
	    match(tosec, PATTERNS(ALL_INIT_SECTIONS)))
		return 0;

	/* Check for pattern 4 */
	if (match(tosym, PATTERNS("__init_begin", "_sinittext", "_einittext")))
		return 0;

	/* Check for pattern 5 */
	if (match(fromsec, PATTERNS(ALL_TEXT_SECTIONS)) &&
	    match(tosec, PATTERNS(ALL_INIT_SECTIONS)) &&
	    match(fromsym, PATTERNS("*.constprop.*")))
		return 0;

	return 1;
}

/*
 * If there's no name there, ignore it; likewise, ignore it if it's
 * one of the magic symbols emitted used by current tools.
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
	return !is_mapping_symbol(name);
}

/* Look up the nearest symbol based on the section and the address */
static Elf_Sym *find_nearest_sym(struct elf_info *elf, Elf_Addr addr,
				 unsigned int secndx, bool allow_negative,
				 Elf_Addr min_distance)
{
	Elf_Sym *sym;
	Elf_Sym *near = NULL;
	Elf_Addr sym_addr, distance;
	bool is_arm = (elf->hdr->e_machine == EM_ARM);

	for (sym = elf->symtab_start; sym < elf->symtab_stop; sym++) {
		if (get_secindex(elf, sym) != secndx)
			continue;
		if (!is_valid_name(elf, sym))
			continue;

		sym_addr = sym->st_value;

		/*
		 * For ARM Thumb instruction, the bit 0 of st_value is set
		 * if the symbol is STT_FUNC type. Mask it to get the address.
		 */
		if (is_arm && ELF_ST_TYPE(sym->st_info) == STT_FUNC)
			 sym_addr &= ~1;

		if (addr >= sym_addr)
			distance = addr - sym_addr;
		else if (allow_negative)
			distance = sym_addr - addr;
		else
			continue;

		if (distance <= min_distance) {
			min_distance = distance;
			near = sym;
		}

		if (min_distance == 0)
			break;
	}
	return near;
}

static Elf_Sym *find_fromsym(struct elf_info *elf, Elf_Addr addr,
			     unsigned int secndx)
{
	return find_nearest_sym(elf, addr, secndx, false, ~0);
}

static Elf_Sym *find_tosym(struct elf_info *elf, Elf_Addr addr, Elf_Sym *sym)
{
	/* If the supplied symbol has a valid name, return it */
	if (is_valid_name(elf, sym))
		return sym;

	/*
	 * Strive to find a better symbol name, but the resulting name may not
	 * match the symbol referenced in the original code.
	 */
	return find_nearest_sym(elf, addr, get_secindex(elf, sym), true, 20);
}

static bool is_executable_section(struct elf_info *elf, unsigned int secndx)
{
	if (secndx >= elf->num_sections)
		return false;

	return (elf->sechdrs[secndx].sh_flags & SHF_EXECINSTR) != 0;
}

static void default_mismatch_handler(const char *modname, struct elf_info *elf,
				     const struct sectioncheck* const mismatch,
				     Elf_Sym *tsym,
				     unsigned int fsecndx, const char *fromsec, Elf_Addr faddr,
				     const char *tosec, Elf_Addr taddr)
{
	Elf_Sym *from;
	const char *tosym;
	const char *fromsym;

	from = find_fromsym(elf, faddr, fsecndx);
	fromsym = sym_name(elf, from);

	tsym = find_tosym(elf, taddr, tsym);
	tosym = sym_name(elf, tsym);

	/* check whitelist - we may ignore it */
	if (!secref_whitelist(fromsec, fromsym, tosec, tosym))
		return;

	sec_mismatch_count++;

	warn("%s: section mismatch in reference: %s+0x%x (section: %s) -> %s (section: %s)\n",
	     modname, fromsym, (unsigned int)(faddr - from->st_value), fromsec, tosym, tosec);

	if (mismatch->mismatch == EXTABLE_TO_NON_TEXT) {
		if (match(tosec, mismatch->bad_tosec))
			fatal("The relocation at %s+0x%lx references\n"
			      "section \"%s\" which is black-listed.\n"
			      "Something is seriously wrong and should be fixed.\n"
			      "You might get more information about where this is\n"
			      "coming from by using scripts/check_extable.sh %s\n",
			      fromsec, (long)faddr, tosec, modname);
		else if (is_executable_section(elf, get_secindex(elf, tsym)))
			warn("The relocation at %s+0x%lx references\n"
			     "section \"%s\" which is not in the list of\n"
			     "authorized sections.  If you're adding a new section\n"
			     "and/or if this reference is valid, add \"%s\" to the\n"
			     "list of authorized sections to jump to on fault.\n"
			     "This can be achieved by adding \"%s\" to\n"
			     "OTHER_TEXT_SECTIONS in scripts/mod/modpost.c.\n",
			     fromsec, (long)faddr, tosec, tosec, tosec);
		else
			error("%s+0x%lx references non-executable section '%s'\n",
			      fromsec, (long)faddr, tosec);
	}
}

static void check_export_symbol(struct module *mod, struct elf_info *elf,
				Elf_Addr faddr, const char *secname,
				Elf_Sym *sym)
{
	static const char *prefix = "__export_symbol_";
	const char *label_name, *name, *data;
	Elf_Sym *label;
	struct symbol *s;
	bool is_gpl;

	label = find_fromsym(elf, faddr, elf->export_symbol_secndx);
	label_name = sym_name(elf, label);

	if (!strstarts(label_name, prefix)) {
		error("%s: .export_symbol section contains strange symbol '%s'\n",
		      mod->name, label_name);
		return;
	}

	if (ELF_ST_BIND(sym->st_info) != STB_GLOBAL &&
	    ELF_ST_BIND(sym->st_info) != STB_WEAK) {
		error("%s: local symbol '%s' was exported\n", mod->name,
		      label_name + strlen(prefix));
		return;
	}

	name = sym_name(elf, sym);
	if (strcmp(label_name + strlen(prefix), name)) {
		error("%s: .export_symbol section references '%s', but it does not seem to be an export symbol\n",
		      mod->name, name);
		return;
	}

	data = sym_get_data(elf, label);	/* license */
	if (!strcmp(data, "GPL")) {
		is_gpl = true;
	} else if (!strcmp(data, "")) {
		is_gpl = false;
	} else {
		error("%s: unknown license '%s' was specified for '%s'\n",
		      mod->name, data, name);
		return;
	}

	data += strlen(data) + 1;	/* namespace */
	s = sym_add_exported(name, mod, is_gpl, data);

	/*
	 * We need to be aware whether we are exporting a function or
	 * a data on some architectures.
	 */
	s->is_func = (ELF_ST_TYPE(sym->st_info) == STT_FUNC);

	if (match(secname, PATTERNS(INIT_SECTIONS)))
		warn("%s: %s: EXPORT_SYMBOL used for init symbol. Remove __init or EXPORT_SYMBOL.\n",
		     mod->name, name);
	else if (match(secname, PATTERNS(EXIT_SECTIONS)))
		warn("%s: %s: EXPORT_SYMBOL used for exit symbol. Remove __exit or EXPORT_SYMBOL.\n",
		     mod->name, name);
}

static void check_section_mismatch(struct module *mod, struct elf_info *elf,
				   Elf_Sym *sym,
				   unsigned int fsecndx, const char *fromsec,
				   Elf_Addr faddr, Elf_Addr taddr)
{
	const char *tosec = sec_name(elf, get_secindex(elf, sym));
	const struct sectioncheck *mismatch;

	if (elf->export_symbol_secndx == fsecndx) {
		check_export_symbol(mod, elf, faddr, tosec, sym);
		return;
	}

	mismatch = section_mismatch(fromsec, tosec);
	if (!mismatch)
		return;

	default_mismatch_handler(mod->name, elf, mismatch, sym,
				 fsecndx, fromsec, faddr,
				 tosec, taddr);
}

static unsigned int *reloc_location(struct elf_info *elf,
				    Elf_Shdr *sechdr, Elf_Rela *r)
{
	return sym_get_data_by_offset(elf, sechdr->sh_info, r->r_offset);
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
		break;
	}
	return 0;
}

#ifndef R_ARM_CALL
#define R_ARM_CALL	28
#endif
#ifndef R_ARM_JUMP24
#define R_ARM_JUMP24	29
#endif

#ifndef	R_ARM_THM_CALL
#define	R_ARM_THM_CALL		10
#endif
#ifndef	R_ARM_THM_JUMP24
#define	R_ARM_THM_JUMP24	30
#endif
#ifndef	R_ARM_THM_JUMP19
#define	R_ARM_THM_JUMP19	51
#endif

static int32_t sign_extend32(int32_t value, int index)
{
	uint8_t shift = 31 - index;

	return (int32_t)(value << shift) >> shift;
}

static int addend_arm_rel(struct elf_info *elf, Elf_Shdr *sechdr, Elf_Rela *r)
{
	unsigned int r_typ = ELF_R_TYPE(r->r_info);
	Elf_Sym *sym = elf->symtab_start + ELF_R_SYM(r->r_info);
	void *loc = reloc_location(elf, sechdr, r);
	uint32_t inst, upper, lower, sign, j1, j2;
	int32_t offset;

	switch (r_typ) {
	case R_ARM_ABS32:
	case R_ARM_REL32:
		inst = TO_NATIVE(*(uint32_t *)loc);
		r->r_addend = inst + sym->st_value;
		break;
	case R_ARM_MOVW_ABS_NC:
	case R_ARM_MOVT_ABS:
		inst = TO_NATIVE(*(uint32_t *)loc);
		offset = sign_extend32(((inst & 0xf0000) >> 4) | (inst & 0xfff),
				       15);
		r->r_addend = offset + sym->st_value;
		break;
	case R_ARM_PC24:
	case R_ARM_CALL:
	case R_ARM_JUMP24:
		inst = TO_NATIVE(*(uint32_t *)loc);
		offset = sign_extend32((inst & 0x00ffffff) << 2, 25);
		r->r_addend = offset + sym->st_value + 8;
		break;
	case R_ARM_THM_MOVW_ABS_NC:
	case R_ARM_THM_MOVT_ABS:
		upper = TO_NATIVE(*(uint16_t *)loc);
		lower = TO_NATIVE(*((uint16_t *)loc + 1));
		offset = sign_extend32(((upper & 0x000f) << 12) |
				       ((upper & 0x0400) << 1) |
				       ((lower & 0x7000) >> 4) |
				       (lower & 0x00ff),
				       15);
		r->r_addend = offset + sym->st_value;
		break;
	case R_ARM_THM_JUMP19:
		/*
		 * Encoding T3:
		 * S     = upper[10]
		 * imm6  = upper[5:0]
		 * J1    = lower[13]
		 * J2    = lower[11]
		 * imm11 = lower[10:0]
		 * imm32 = SignExtend(S:J2:J1:imm6:imm11:'0')
		 */
		upper = TO_NATIVE(*(uint16_t *)loc);
		lower = TO_NATIVE(*((uint16_t *)loc + 1));

		sign = (upper >> 10) & 1;
		j1 = (lower >> 13) & 1;
		j2 = (lower >> 11) & 1;
		offset = sign_extend32((sign << 20) | (j2 << 19) | (j1 << 18) |
				       ((upper & 0x03f) << 12) |
				       ((lower & 0x07ff) << 1),
				       20);
		r->r_addend = offset + sym->st_value + 4;
		break;
	case R_ARM_THM_CALL:
	case R_ARM_THM_JUMP24:
		/*
		 * Encoding T4:
		 * S     = upper[10]
		 * imm10 = upper[9:0]
		 * J1    = lower[13]
		 * J2    = lower[11]
		 * imm11 = lower[10:0]
		 * I1    = NOT(J1 XOR S)
		 * I2    = NOT(J2 XOR S)
		 * imm32 = SignExtend(S:I1:I2:imm10:imm11:'0')
		 */
		upper = TO_NATIVE(*(uint16_t *)loc);
		lower = TO_NATIVE(*((uint16_t *)loc + 1));

		sign = (upper >> 10) & 1;
		j1 = (lower >> 13) & 1;
		j2 = (lower >> 11) & 1;
		offset = sign_extend32((sign << 24) |
				       ((~(j1 ^ sign) & 1) << 23) |
				       ((~(j2 ^ sign) & 1) << 22) |
				       ((upper & 0x03ff) << 12) |
				       ((lower & 0x07ff) << 1),
				       24);
		r->r_addend = offset + sym->st_value + 4;
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

#ifndef EM_RISCV
#define EM_RISCV		243
#endif

#ifndef R_RISCV_SUB32
#define R_RISCV_SUB32		39
#endif

#ifndef EM_LOONGARCH
#define EM_LOONGARCH		258
#endif

#ifndef R_LARCH_SUB32
#define R_LARCH_SUB32		55
#endif

static void section_rela(struct module *mod, struct elf_info *elf,
			 Elf_Shdr *sechdr)
{
	Elf_Rela *rela;
	Elf_Rela r;
	unsigned int r_sym;
	unsigned int fsecndx = sechdr->sh_info;
	const char *fromsec = sec_name(elf, fsecndx);
	Elf_Rela *start = (void *)elf->hdr + sechdr->sh_offset;
	Elf_Rela *stop  = (void *)start + sechdr->sh_size;

	/* if from section (name) is know good then skip it */
	if (match(fromsec, section_white_list))
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
		switch (elf->hdr->e_machine) {
		case EM_RISCV:
			if (!strcmp("__ex_table", fromsec) &&
			    ELF_R_TYPE(r.r_info) == R_RISCV_SUB32)
				continue;
			break;
		case EM_LOONGARCH:
			if (!strcmp("__ex_table", fromsec) &&
			    ELF_R_TYPE(r.r_info) == R_LARCH_SUB32)
				continue;
			break;
		}

		check_section_mismatch(mod, elf, elf->symtab_start + r_sym,
				       fsecndx, fromsec, r.r_offset, r.r_addend);
	}
}

static void section_rel(struct module *mod, struct elf_info *elf,
			Elf_Shdr *sechdr)
{
	Elf_Rel *rel;
	Elf_Rela r;
	unsigned int r_sym;
	unsigned int fsecndx = sechdr->sh_info;
	const char *fromsec = sec_name(elf, fsecndx);
	Elf_Rel *start = (void *)elf->hdr + sechdr->sh_offset;
	Elf_Rel *stop  = (void *)start + sechdr->sh_size;

	/* if from section (name) is know good then skip it */
	if (match(fromsec, section_white_list))
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
		default:
			fatal("Please add code to calculate addend for this architecture\n");
		}

		check_section_mismatch(mod, elf, elf->symtab_start + r_sym,
				       fsecndx, fromsec, r.r_offset, r.r_addend);
	}
}

/**
 * A module includes a number of sections that are discarded
 * either when loaded or when used as built-in.
 * For loaded modules all functions marked __init and all data
 * marked __initdata will be discarded when the module has been initialized.
 * Likewise for modules used built-in the sections marked __exit
 * are discarded because __exit marked function are supposed to be called
 * only when a module is unloaded which never happens for built-in modules.
 * The check_sec_ref() function traverses all relocation records
 * to find all references to a section that reference a section that will
 * be discarded and warns about it.
 **/
static void check_sec_ref(struct module *mod, struct elf_info *elf)
{
	int i;
	Elf_Shdr *sechdrs = elf->sechdrs;

	/* Walk through all sections */
	for (i = 0; i < elf->num_sections; i++) {
		check_section(mod->name, elf, &elf->sechdrs[i]);
		/* We want to process only relocation sections and not .init */
		if (sechdrs[i].sh_type == SHT_RELA)
			section_rela(mod, elf, &elf->sechdrs[i]);
		else if (sechdrs[i].sh_type == SHT_REL)
			section_rel(mod, elf, &elf->sechdrs[i]);
	}
}

static char *remove_dot(char *s)
{
	size_t n = strcspn(s, ".");

	if (n && s[n]) {
		size_t m = strspn(s + n + 1, "0123456789");
		if (m && (s[n + m + 1] == '.' || s[n + m + 1] == 0))
			s[n] = 0;
	}
	return s;
}

/*
 * The CRCs are recorded in .*.cmd files in the form of:
 * #SYMVER <name> <crc>
 */
static void extract_crcs_for_object(const char *object, struct module *mod)
{
	char cmd_file[PATH_MAX];
	char *buf, *p;
	const char *base;
	int dirlen, ret;

	base = strrchr(object, '/');
	if (base) {
		base++;
		dirlen = base - object;
	} else {
		dirlen = 0;
		base = object;
	}

	ret = snprintf(cmd_file, sizeof(cmd_file), "%.*s.%s.cmd",
		       dirlen, object, base);
	if (ret >= sizeof(cmd_file)) {
		error("%s: too long path was truncated\n", cmd_file);
		return;
	}

	buf = read_text_file(cmd_file);
	p = buf;

	while ((p = strstr(p, "\n#SYMVER "))) {
		char *name;
		size_t namelen;
		unsigned int crc;
		struct symbol *sym;

		name = p + strlen("\n#SYMVER ");

		p = strchr(name, ' ');
		if (!p)
			break;

		namelen = p - name;
		p++;

		if (!isdigit(*p))
			continue;	/* skip this line */

		crc = strtoul(p, &p, 0);
		if (*p != '\n')
			continue;	/* skip this line */

		name[namelen] = '\0';

		/*
		 * sym_find_with_module() may return NULL here.
		 * It typically occurs when CONFIG_TRIM_UNUSED_KSYMS=y.
		 * Since commit e1327a127703, genksyms calculates CRCs of all
		 * symbols, including trimmed ones. Ignore orphan CRCs.
		 */
		sym = sym_find_with_module(name, mod);
		if (sym)
			sym_set_crc(sym, crc);
	}

	free(buf);
}

/*
 * The symbol versions (CRC) are recorded in the .*.cmd files.
 * Parse them to retrieve CRCs for the current module.
 */
static void mod_set_crcs(struct module *mod)
{
	char objlist[PATH_MAX];
	char *buf, *p, *obj;
	int ret;

	if (mod->is_vmlinux) {
		strcpy(objlist, ".vmlinux.objs");
	} else {
		/* objects for a module are listed in the *.mod file. */
		ret = snprintf(objlist, sizeof(objlist), "%s.mod", mod->name);
		if (ret >= sizeof(objlist)) {
			error("%s: too long path was truncated\n", objlist);
			return;
		}
	}

	buf = read_text_file(objlist);
	p = buf;

	while ((obj = strsep(&p, "\n")) && obj[0])
		extract_crcs_for_object(obj, mod);

	free(buf);
}

static void read_symbols(const char *modname)
{
	const char *symname;
	char *version;
	char *license;
	char *namespace;
	struct module *mod;
	struct elf_info info = { };
	Elf_Sym *sym;

	if (!parse_elf(&info, modname))
		return;

	if (!strends(modname, ".o")) {
		error("%s: filename must be suffixed with .o\n", modname);
		return;
	}

	/* strip trailing .o */
	mod = new_module(modname, strlen(modname) - strlen(".o"));

	if (!mod->is_vmlinux) {
		license = get_modinfo(&info, "license");
		if (!license)
			error("missing MODULE_LICENSE() in %s\n", modname);
		while (license) {
			if (!license_is_gpl_compatible(license)) {
				mod->is_gpl_compatible = false;
				break;
			}
			license = get_next_modinfo(&info, "license", license);
		}

		namespace = get_modinfo(&info, "import_ns");
		while (namespace) {
			add_namespace(&mod->imported_namespaces, namespace);
			namespace = get_next_modinfo(&info, "import_ns",
						     namespace);
		}
	}

	for (sym = info.symtab_start; sym < info.symtab_stop; sym++) {
		symname = remove_dot(info.strtab + sym->st_name);

		handle_symbol(mod, &info, sym, symname);
		handle_moddevtable(mod, &info, sym, symname);
	}

	check_sec_ref(mod, &info);

	if (!mod->is_vmlinux) {
		version = get_modinfo(&info, "version");
		if (version || all_versions)
			get_src_version(mod->name, mod->srcversion,
					sizeof(mod->srcversion) - 1);
	}

	parse_elf_finish(&info);

	if (modversions) {
		/*
		 * Our trick to get versioning for module struct etc. - it's
		 * never passed as an argument to an exported function, so
		 * the automatic versioning doesn't pick it up, but it's really
		 * important anyhow.
		 */
		sym_add_unresolved("module_layout", mod, false);

		mod_set_crcs(mod);
	}
}

static void read_symbols_from_files(const char *filename)
{
	FILE *in = stdin;
	char fname[PATH_MAX];

	in = fopen(filename, "r");
	if (!in)
		fatal("Can't open filenames file %s: %m", filename);

	while (fgets(fname, PATH_MAX, in) != NULL) {
		if (strends(fname, "\n"))
			fname[strlen(fname)-1] = '\0';
		read_symbols(fname);
	}

	fclose(in);
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
		buf->p = NOFAIL(realloc(buf->p, buf->size));
	}
	strncpy(buf->p + buf->pos, s, len);
	buf->pos += len;
}

static void check_exports(struct module *mod)
{
	struct symbol *s, *exp;

	list_for_each_entry(s, &mod->unresolved_symbols, list) {
		const char *basename;
		exp = find_symbol(s->name);
		if (!exp) {
			if (!s->weak && nr_unresolved++ < MAX_UNRESOLVED_REPORTS)
				modpost_log(warn_unresolved ? LOG_WARN : LOG_ERROR,
					    "\"%s\" [%s.ko] undefined!\n",
					    s->name, mod->name);
			continue;
		}
		if (exp->module == mod) {
			error("\"%s\" [%s.ko] was exported without definition\n",
			      s->name, mod->name);
			continue;
		}

		exp->used = true;
		s->module = exp->module;
		s->crc_valid = exp->crc_valid;
		s->crc = exp->crc;

		basename = strrchr(mod->name, '/');
		if (basename)
			basename++;
		else
			basename = mod->name;

		if (!contains_namespace(&mod->imported_namespaces, exp->namespace)) {
			modpost_log(allow_missing_ns_imports ? LOG_WARN : LOG_ERROR,
				    "module %s uses symbol %s from namespace %s, but does not import it.\n",
				    basename, exp->name, exp->namespace);
			add_namespace(&mod->missing_namespaces, exp->namespace);
		}

		if (!mod->is_gpl_compatible && exp->is_gpl_only)
			error("GPL-incompatible module %s.ko uses GPL-only symbol '%s'\n",
			      basename, exp->name);
	}
}

static void handle_white_list_exports(const char *white_list)
{
	char *buf, *p, *name;

	buf = read_text_file(white_list);
	p = buf;

	while ((name = strsep(&p, "\n"))) {
		struct symbol *sym = find_symbol(name);

		if (sym)
			sym->used = true;
	}

	free(buf);
}

static void check_modname_len(struct module *mod)
{
	const char *mod_name;

	mod_name = strrchr(mod->name, '/');
	if (mod_name == NULL)
		mod_name = mod->name;
	else
		mod_name++;
	if (strlen(mod_name) >= MODULE_NAME_LEN)
		error("module name is too long [%s.ko]\n", mod->name);
}

/**
 * Header for the generated file
 **/
static void add_header(struct buffer *b, struct module *mod)
{
	buf_printf(b, "#include <linux/module.h>\n");
	/*
	 * Include build-salt.h after module.h in order to
	 * inherit the definitions.
	 */
	buf_printf(b, "#define INCLUDE_VERMAGIC\n");
	buf_printf(b, "#include <linux/build-salt.h>\n");
	buf_printf(b, "#include <linux/elfnote-lto.h>\n");
	buf_printf(b, "#include <linux/export-internal.h>\n");
	buf_printf(b, "#include <linux/vermagic.h>\n");
	buf_printf(b, "#include <linux/compiler.h>\n");
	buf_printf(b, "\n");
	buf_printf(b, "BUILD_SALT;\n");
	buf_printf(b, "BUILD_LTO_INFO;\n");
	buf_printf(b, "\n");
	buf_printf(b, "MODULE_INFO(vermagic, VERMAGIC_STRING);\n");
	buf_printf(b, "MODULE_INFO(name, KBUILD_MODNAME);\n");
	buf_printf(b, "\n");
	buf_printf(b, "__visible struct module __this_module\n");
	buf_printf(b, "__section(\".gnu.linkonce.this_module\") = {\n");
	buf_printf(b, "\t.name = KBUILD_MODNAME,\n");
	if (mod->has_init)
		buf_printf(b, "\t.init = init_module,\n");
	if (mod->has_cleanup)
		buf_printf(b, "#ifdef CONFIG_MODULE_UNLOAD\n"
			      "\t.exit = cleanup_module,\n"
			      "#endif\n");
	buf_printf(b, "\t.arch = MODULE_ARCH_INIT,\n");
	buf_printf(b, "};\n");

	if (!external_module)
		buf_printf(b, "\nMODULE_INFO(intree, \"Y\");\n");

	buf_printf(b,
		   "\n"
		   "#ifdef CONFIG_RETPOLINE\n"
		   "MODULE_INFO(retpoline, \"Y\");\n"
		   "#endif\n");

	if (strstarts(mod->name, "drivers/staging"))
		buf_printf(b, "\nMODULE_INFO(staging, \"Y\");\n");

	if (strstarts(mod->name, "tools/testing"))
		buf_printf(b, "\nMODULE_INFO(test, \"Y\");\n");
}

static void add_exported_symbols(struct buffer *buf, struct module *mod)
{
	struct symbol *sym;

	/* generate struct for exported symbols */
	buf_printf(buf, "\n");
	list_for_each_entry(sym, &mod->exported_symbols, list) {
		if (trim_unused_exports && !sym->used)
			continue;

		buf_printf(buf, "KSYMTAB_%s(%s, \"%s\", \"%s\");\n",
			   sym->is_func ? "FUNC" : "DATA", sym->name,
			   sym->is_gpl_only ? "_gpl" : "", sym->namespace);
	}

	if (!modversions)
		return;

	/* record CRCs for exported symbols */
	buf_printf(buf, "\n");
	list_for_each_entry(sym, &mod->exported_symbols, list) {
		if (trim_unused_exports && !sym->used)
			continue;

		if (!sym->crc_valid)
			warn("EXPORT symbol \"%s\" [%s%s] version generation failed, symbol will not be versioned.\n"
			     "Is \"%s\" prototyped in <asm/asm-prototypes.h>?\n",
			     sym->name, mod->name, mod->is_vmlinux ? "" : ".ko",
			     sym->name);

		buf_printf(buf, "SYMBOL_CRC(%s, 0x%08x, \"%s\");\n",
			   sym->name, sym->crc, sym->is_gpl_only ? "_gpl" : "");
	}
}

/**
 * Record CRCs for unresolved symbols
 **/
static void add_versions(struct buffer *b, struct module *mod)
{
	struct symbol *s;

	if (!modversions)
		return;

	buf_printf(b, "\n");
	buf_printf(b, "static const struct modversion_info ____versions[]\n");
	buf_printf(b, "__used __section(\"__versions\") = {\n");

	list_for_each_entry(s, &mod->unresolved_symbols, list) {
		if (!s->module)
			continue;
		if (!s->crc_valid) {
			warn("\"%s\" [%s.ko] has no CRC!\n",
				s->name, mod->name);
			continue;
		}
		if (strlen(s->name) >= MODULE_NAME_LEN) {
			error("too long symbol \"%s\" [%s.ko]\n",
			      s->name, mod->name);
			break;
		}
		buf_printf(b, "\t{ %#8x, \"%s\" },\n",
			   s->crc, s->name);
	}

	buf_printf(b, "};\n");
}

static void add_depends(struct buffer *b, struct module *mod)
{
	struct symbol *s;
	int first = 1;

	/* Clear ->seen flag of modules that own symbols needed by this. */
	list_for_each_entry(s, &mod->unresolved_symbols, list) {
		if (s->module)
			s->module->seen = s->module->is_vmlinux;
	}

	buf_printf(b, "\n");
	buf_printf(b, "MODULE_INFO(depends, \"");
	list_for_each_entry(s, &mod->unresolved_symbols, list) {
		const char *p;
		if (!s->module)
			continue;

		if (s->module->seen)
			continue;

		s->module->seen = true;
		p = strrchr(s->module->name, '/');
		if (p)
			p++;
		else
			p = s->module->name;
		buf_printf(b, "%s%s", first ? "" : ",", p);
		first = 0;
	}
	buf_printf(b, "\");\n");
}

static void add_srcversion(struct buffer *b, struct module *mod)
{
	if (mod->srcversion[0]) {
		buf_printf(b, "\n");
		buf_printf(b, "MODULE_INFO(srcversion, \"%s\");\n",
			   mod->srcversion);
	}
}

static void write_buf(struct buffer *b, const char *fname)
{
	FILE *file;

	if (error_occurred)
		return;

	file = fopen(fname, "w");
	if (!file) {
		perror(fname);
		exit(1);
	}
	if (fwrite(b->p, 1, b->pos, file) != b->pos) {
		perror(fname);
		exit(1);
	}
	if (fclose(file) != 0) {
		perror(fname);
		exit(1);
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
	write_buf(b, fname);
}

static void write_vmlinux_export_c_file(struct module *mod)
{
	struct buffer buf = { };

	buf_printf(&buf,
		   "#include <linux/export-internal.h>\n");

	add_exported_symbols(&buf, mod);
	write_if_changed(&buf, ".vmlinux.export.c");
	free(buf.p);
}

/* do sanity checks, and generate *.mod.c file */
static void write_mod_c_file(struct module *mod)
{
	struct buffer buf = { };
	char fname[PATH_MAX];
	int ret;

	add_header(&buf, mod);
	add_exported_symbols(&buf, mod);
	add_versions(&buf, mod);
	add_depends(&buf, mod);
	add_moddevtable(&buf, mod);
	add_srcversion(&buf, mod);

	ret = snprintf(fname, sizeof(fname), "%s.mod.c", mod->name);
	if (ret >= sizeof(fname)) {
		error("%s: too long path was truncated\n", fname);
		goto free;
	}

	write_if_changed(&buf, fname);

free:
	free(buf.p);
}

/* parse Module.symvers file. line format:
 * 0x12345678<tab>symbol<tab>module<tab>export<tab>namespace
 **/
static void read_dump(const char *fname)
{
	char *buf, *pos, *line;

	buf = read_text_file(fname);
	if (!buf)
		/* No symbol versions, silently ignore */
		return;

	pos = buf;

	while ((line = get_line(&pos))) {
		char *symname, *namespace, *modname, *d, *export;
		unsigned int crc;
		struct module *mod;
		struct symbol *s;
		bool gpl_only;

		if (!(symname = strchr(line, '\t')))
			goto fail;
		*symname++ = '\0';
		if (!(modname = strchr(symname, '\t')))
			goto fail;
		*modname++ = '\0';
		if (!(export = strchr(modname, '\t')))
			goto fail;
		*export++ = '\0';
		if (!(namespace = strchr(export, '\t')))
			goto fail;
		*namespace++ = '\0';

		crc = strtoul(line, &d, 16);
		if (*symname == '\0' || *modname == '\0' || *d != '\0')
			goto fail;

		if (!strcmp(export, "EXPORT_SYMBOL_GPL")) {
			gpl_only = true;
		} else if (!strcmp(export, "EXPORT_SYMBOL")) {
			gpl_only = false;
		} else {
			error("%s: unknown license %s. skip", symname, export);
			continue;
		}

		mod = find_module(modname);
		if (!mod) {
			mod = new_module(modname, strlen(modname));
			mod->from_dump = true;
		}
		s = sym_add_exported(symname, mod, gpl_only, namespace);
		sym_set_crc(s, crc);
	}
	free(buf);
	return;
fail:
	free(buf);
	fatal("parse error in symbol dump file\n");
}

static void write_dump(const char *fname)
{
	struct buffer buf = { };
	struct module *mod;
	struct symbol *sym;

	list_for_each_entry(mod, &modules, list) {
		if (mod->from_dump)
			continue;
		list_for_each_entry(sym, &mod->exported_symbols, list) {
			if (trim_unused_exports && !sym->used)
				continue;

			buf_printf(&buf, "0x%08x\t%s\t%s\tEXPORT_SYMBOL%s\t%s\n",
				   sym->crc, sym->name, mod->name,
				   sym->is_gpl_only ? "_GPL" : "",
				   sym->namespace);
		}
	}
	write_buf(&buf, fname);
	free(buf.p);
}

static void write_namespace_deps_files(const char *fname)
{
	struct module *mod;
	struct namespace_list *ns;
	struct buffer ns_deps_buf = {};

	list_for_each_entry(mod, &modules, list) {

		if (mod->from_dump || list_empty(&mod->missing_namespaces))
			continue;

		buf_printf(&ns_deps_buf, "%s.ko:", mod->name);

		list_for_each_entry(ns, &mod->missing_namespaces, list)
			buf_printf(&ns_deps_buf, " %s", ns->namespace);

		buf_printf(&ns_deps_buf, "\n");
	}

	write_if_changed(&ns_deps_buf, fname);
	free(ns_deps_buf.p);
}

struct dump_list {
	struct list_head list;
	const char *file;
};

int main(int argc, char **argv)
{
	struct module *mod;
	char *missing_namespace_deps = NULL;
	char *unused_exports_white_list = NULL;
	char *dump_write = NULL, *files_source = NULL;
	int opt;
	LIST_HEAD(dump_lists);
	struct dump_list *dl, *dl2;

	while ((opt = getopt(argc, argv, "ei:mnT:to:au:WwENd:")) != -1) {
		switch (opt) {
		case 'e':
			external_module = true;
			break;
		case 'i':
			dl = NOFAIL(malloc(sizeof(*dl)));
			dl->file = optarg;
			list_add_tail(&dl->list, &dump_lists);
			break;
		case 'm':
			modversions = true;
			break;
		case 'n':
			ignore_missing_files = true;
			break;
		case 'o':
			dump_write = optarg;
			break;
		case 'a':
			all_versions = true;
			break;
		case 'T':
			files_source = optarg;
			break;
		case 't':
			trim_unused_exports = true;
			break;
		case 'u':
			unused_exports_white_list = optarg;
			break;
		case 'W':
			extra_warn = true;
			break;
		case 'w':
			warn_unresolved = true;
			break;
		case 'E':
			sec_mismatch_warn_only = false;
			break;
		case 'N':
			allow_missing_ns_imports = true;
			break;
		case 'd':
			missing_namespace_deps = optarg;
			break;
		default:
			exit(1);
		}
	}

	list_for_each_entry_safe(dl, dl2, &dump_lists, list) {
		read_dump(dl->file);
		list_del(&dl->list);
		free(dl);
	}

	while (optind < argc)
		read_symbols(argv[optind++]);

	if (files_source)
		read_symbols_from_files(files_source);

	list_for_each_entry(mod, &modules, list) {
		if (mod->from_dump || mod->is_vmlinux)
			continue;

		check_modname_len(mod);
		check_exports(mod);
	}

	if (unused_exports_white_list)
		handle_white_list_exports(unused_exports_white_list);

	list_for_each_entry(mod, &modules, list) {
		if (mod->from_dump)
			continue;

		if (mod->is_vmlinux)
			write_vmlinux_export_c_file(mod);
		else
			write_mod_c_file(mod);
	}

	if (missing_namespace_deps)
		write_namespace_deps_files(missing_namespace_deps);

	if (dump_write)
		write_dump(dump_write);
	if (sec_mismatch_count && !sec_mismatch_warn_only)
		error("Section mismatches detected.\n"
		      "Set CONFIG_SECTION_MISMATCH_WARN_ONLY=y to allow them.\n");

	if (nr_unresolved > MAX_UNRESOLVED_REPORTS)
		warn("suppressed %u unresolved symbol warnings because there were too many)\n",
		     nr_unresolved - MAX_UNRESOLVED_REPORTS);

	return error_occurred ? 1 : 0;
}
