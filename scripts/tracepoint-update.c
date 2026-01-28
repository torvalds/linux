// SPDX-License-Identifier: GPL-2.0-only

#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include "elf-parse.h"

static Elf_Shdr *check_data_sec;
static Elf_Shdr *tracepoint_data_sec;

static inline void *get_index(void *start, int entsize, int index)
{
	return start + (entsize * index);
}

static int compare_strings(const void *a, const void *b)
{
	const char *av = *(const char **)a;
	const char *bv = *(const char **)b;

	return strcmp(av, bv);
}

struct elf_tracepoint {
	Elf_Ehdr *ehdr;
	const char **array;
	int count;
};

#define REALLOC_SIZE (1 << 10)
#define REALLOC_MASK (REALLOC_SIZE - 1)

static int add_string(const char *str, const char ***vals, int *count)
{
	const char **array = *vals;

	if (!(*count & REALLOC_MASK)) {
		int size = (*count) + REALLOC_SIZE;

		array = realloc(array, sizeof(char *) * size);
		if (!array) {
			fprintf(stderr, "Failed memory allocation\n");
			free(*vals);
			*vals = NULL;
			return -1;
		}
		*vals = array;
	}

	array[(*count)++] = str;
	return 0;
}

/**
 * for_each_shdr_str - iterator that reads strings that are in an ELF section.
 * @len: "int" to hold the length of the current string
 * @ehdr: A pointer to the ehdr of the ELF file
 * @sec: The section that has the strings to iterate on
 *
 * This is a for loop that iterates over all the nul terminated strings
 * that are in a given ELF section. The variable "str" will hold
 * the current string for each iteration and the passed in @len will
 * contain the strlen() of that string.
 */
#define for_each_shdr_str(len, ehdr, sec)				\
	for (const char *str = (void *)(ehdr) + shdr_offset(sec),	\
			*end = str + shdr_size(sec);			\
	     len = strlen(str), str < end;				\
	     str += (len) + 1)


static void make_trace_array(struct elf_tracepoint *etrace)
{
	Elf_Ehdr *ehdr = etrace->ehdr;
	const char **vals = NULL;
	int count = 0;
	int len;

	etrace->array = NULL;

	/*
	 * The __tracepoint_check section is filled with strings of the
	 * names of tracepoints (in tracepoint_strings). Create an array
	 * that points to each string and then sort the array.
	 */
	for_each_shdr_str(len, ehdr, check_data_sec) {
		if (!len)
			continue;
		if (add_string(str, &vals, &count) < 0)
			return;
	}

	/* If CONFIG_TRACEPOINT_VERIFY_USED is not set, there's nothing to do */
	if (!count)
		return;

	qsort(vals, count, sizeof(char *), compare_strings);

	etrace->array = vals;
	etrace->count = count;
}

static int find_event(const char *str, void *array, size_t size)
{
	return bsearch(&str, array, size, sizeof(char *), compare_strings) != NULL;
}

static void check_tracepoints(struct elf_tracepoint *etrace, const char *fname)
{
	Elf_Ehdr *ehdr = etrace->ehdr;
	int len;

	if (!etrace->array)
		return;

	/*
	 * The __tracepoints_strings section holds all the names of the
	 * defined tracepoints. If any of them are not in the
	 * __tracepoint_check_section it means they are not used.
	 */
	for_each_shdr_str(len, ehdr, tracepoint_data_sec) {
		if (!len)
			continue;
		if (!find_event(str, etrace->array, etrace->count)) {
			fprintf(stderr, "warning: tracepoint '%s' is unused", str);
			if (fname)
				fprintf(stderr, " in module %s\n", fname);
			else
				fprintf(stderr, "\n");
		}
	}

	free(etrace->array);
}

static void *tracepoint_check(struct elf_tracepoint *etrace, const char *fname)
{
	make_trace_array(etrace);
	check_tracepoints(etrace, fname);

	return NULL;
}

static int process_tracepoints(bool mod, void *addr, const char *fname)
{
	struct elf_tracepoint etrace = {0};
	Elf_Ehdr *ehdr = addr;
	Elf_Shdr *shdr_start;
	Elf_Shdr *string_sec;
	const char *secstrings;
	unsigned int shnum;
	unsigned int shstrndx;
	int shentsize;
	int idx;
	int done = 2;

	shdr_start = (Elf_Shdr *)((char *)ehdr + ehdr_shoff(ehdr));
	shentsize = ehdr_shentsize(ehdr);

	shstrndx = ehdr_shstrndx(ehdr);
	if (shstrndx == SHN_XINDEX)
		shstrndx = shdr_link(shdr_start);
	string_sec = get_index(shdr_start, shentsize, shstrndx);
	secstrings = (const char *)ehdr + shdr_offset(string_sec);

	shnum = ehdr_shnum(ehdr);
	if (shnum == SHN_UNDEF)
		shnum = shdr_size(shdr_start);

	for (int i = 0; done && i < shnum; i++) {
		Elf_Shdr *shdr = get_index(shdr_start, shentsize, i);

		idx = shdr_name(shdr);

		/* locate the __tracepoint_check in vmlinux */
		if (!strcmp(secstrings + idx, "__tracepoint_check")) {
			check_data_sec = shdr;
			done--;
		}

		/* locate the __tracepoints_ptrs section in vmlinux */
		if (!strcmp(secstrings + idx, "__tracepoints_strings")) {
			tracepoint_data_sec = shdr;
			done--;
		}
	}

	/*
	 * Modules may not have either section. But if it has one section,
	 * it should have both of them.
	 */
	if (mod && !check_data_sec && !tracepoint_data_sec)
		return 0;

	if (!check_data_sec) {
		if (mod) {
			fprintf(stderr, "warning: Module %s has only unused tracepoints\n", fname);
			/* Do not fail build */
			return 0;
		}
		fprintf(stderr,	"no __tracepoint_check in file: %s\n", fname);
		return -1;
	}

	if (!tracepoint_data_sec) {
		/* A module may reference only exported tracepoints */
		if (mod)
			return 0;
		fprintf(stderr,	"no __tracepoint_strings in file: %s\n", fname);
		return -1;
	}

	if (!mod)
		fname = NULL;

	etrace.ehdr = ehdr;
	tracepoint_check(&etrace, fname);
	return 0;
}

int main(int argc, char *argv[])
{
	int n_error = 0;
	size_t size = 0;
	void *addr = NULL;
	bool mod = false;

	if (argc > 1 && strcmp(argv[1], "--module") == 0) {
		mod = true;
		argc--;
		argv++;
	}

	if (argc < 2) {
		if (mod)
			fprintf(stderr, "usage: tracepoint-update --module module...\n");
		else
			fprintf(stderr, "usage: tracepoint-update vmlinux...\n");
		return 0;
	}

	/* Process each file in turn, allowing deep failure. */
	for (int i = 1; i < argc; i++) {
		addr = elf_map(argv[i], &size, 1 << ET_REL);
		if (!addr) {
			++n_error;
			continue;
		}

		if (process_tracepoints(mod, addr, argv[i]))
			++n_error;

		elf_unmap(addr, size);
	}

	return !!n_error;
}
