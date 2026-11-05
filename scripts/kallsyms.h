/* SPDX-License-Identifier: GPL-2.0 */
#ifndef KALLSYMS_H
#define KALLSYMS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <array_size.h>
#include "elf-parse.h"

static inline bool string_starts_with(const char *s, const char *prefix)
{
	return strncmp(s, prefix, strlen(prefix)) == 0;
}

static inline bool string_ends_with(const char *s, const char *suffix)
{
	size_t len = strlen(s), suffix_len = strlen(suffix);

	return len >= suffix_len && strcmp(s + len - suffix_len, suffix) == 0;
}

/* A symbol as nm lists it. */
struct sysmap_symbol {
	unsigned long long addr;
	const char *name;
	char type;
};

/* The symbols of an ELF file that System.map lists, in its order. */
struct sysmap {
	struct sysmap_symbol *syms;
	size_t nr_syms;
	int addr_width;		/* hex digits of an address */
	struct elf_file elf;	/* the mapping the names point into */
};

struct sysmap *sysmap_read(const char *path);
void sysmap_write(const struct sysmap *map, FILE *out);
void sysmap_free(struct sysmap *map);

#endif /* KALLSYMS_H */
