#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/kernel.h>

#include "util/dso.h"
#include "util/util.h"
#include "util/debug.h"

#ifdef HAVE_LIBBFD_SUPPORT

/*
 * Implement addr2line using libbfd.
 */
#define PACKAGE "perf"
#include <bfd.h>

struct a2l_data {
	const char 	*input;
	unsigned long 	addr;

	bool 		found;
	const char 	*filename;
	const char 	*funcname;
	unsigned 	line;

	bfd 		*abfd;
	asymbol 	**syms;
};

static int bfd_error(const char *string)
{
	const char *errmsg;

	errmsg = bfd_errmsg(bfd_get_error());
	fflush(stdout);

	if (string)
		pr_debug("%s: %s\n", string, errmsg);
	else
		pr_debug("%s\n", errmsg);

	return -1;
}

static int slurp_symtab(bfd *abfd, struct a2l_data *a2l)
{
	long storage;
	long symcount;
	asymbol **syms;
	bfd_boolean dynamic = FALSE;

	if ((bfd_get_file_flags(abfd) & HAS_SYMS) == 0)
		return bfd_error(bfd_get_filename(abfd));

	storage = bfd_get_symtab_upper_bound(abfd);
	if (storage == 0L) {
		storage = bfd_get_dynamic_symtab_upper_bound(abfd);
		dynamic = TRUE;
	}
	if (storage < 0L)
		return bfd_error(bfd_get_filename(abfd));

	syms = malloc(storage);
	if (dynamic)
		symcount = bfd_canonicalize_dynamic_symtab(abfd, syms);
	else
		symcount = bfd_canonicalize_symtab(abfd, syms);

	if (symcount < 0) {
		free(syms);
		return bfd_error(bfd_get_filename(abfd));
	}

	a2l->syms = syms;
	return 0;
}

static void find_address_in_section(bfd *abfd, asection *section, void *data)
{
	bfd_vma pc, vma;
	bfd_size_type size;
	struct a2l_data *a2l = data;

	if (a2l->found)
		return;

	if ((bfd_get_section_flags(abfd, section) & SEC_ALLOC) == 0)
		return;

	pc = a2l->addr;
	vma = bfd_get_section_vma(abfd, section);
	size = bfd_get_section_size(section);

	if (pc < vma || pc >= vma + size)
		return;

	a2l->found = bfd_find_nearest_line(abfd, section, a2l->syms, pc - vma,
					   &a2l->filename, &a2l->funcname,
					   &a2l->line);
}

static struct a2l_data *addr2line_init(const char *path)
{
	bfd *abfd;
	struct a2l_data *a2l = NULL;

	abfd = bfd_openr(path, NULL);
	if (abfd == NULL)
		return NULL;

	if (!bfd_check_format(abfd, bfd_object))
		goto out;

	a2l = zalloc(sizeof(*a2l));
	if (a2l == NULL)
		goto out;

	a2l->abfd = abfd;
	a2l->input = strdup(path);
	if (a2l->input == NULL)
		goto out;

	if (slurp_symtab(abfd, a2l))
		goto out;

	return a2l;

out:
	if (a2l) {
		free((void *)a2l->input);
		free(a2l);
	}
	bfd_close(abfd);
	return NULL;
}

static void addr2line_cleanup(struct a2l_data *a2l)
{
	if (a2l->abfd)
		bfd_close(a2l->abfd);
	free((void *)a2l->input);
	free(a2l->syms);
	free(a2l);
}

static int addr2line(const char *dso_name, unsigned long addr,
		     char **file, unsigned int *line)
{
	int ret = 0;
	struct a2l_data *a2l;

	a2l = addr2line_init(dso_name);
	if (a2l == NULL) {
		pr_warning("addr2line_init failed for %s\n", dso_name);
		return 0;
	}

	a2l->addr = addr;
	bfd_map_over_sections(a2l->abfd, find_address_in_section, a2l);

	if (a2l->found && a2l->filename) {
		*file = strdup(a2l->filename);
		*line = a2l->line;

		if (*file)
			ret = 1;
	}

	addr2line_cleanup(a2l);
	return ret;
}

#else /* HAVE_LIBBFD_SUPPORT */

static int addr2line(const char *dso_name, unsigned long addr,
		     char **file, unsigned int *line_nr)
{
	FILE *fp;
	char cmd[PATH_MAX];
	char *filename = NULL;
	size_t len;
	char *sep;
	int ret = 0;

	scnprintf(cmd, sizeof(cmd), "addr2line -e %s %016"PRIx64,
		  dso_name, addr);

	fp = popen(cmd, "r");
	if (fp == NULL) {
		pr_warning("popen failed for %s\n", dso_name);
		return 0;
	}

	if (getline(&filename, &len, fp) < 0 || !len) {
		pr_warning("addr2line has no output for %s\n", dso_name);
		goto out;
	}

	sep = strchr(filename, '\n');
	if (sep)
		*sep = '\0';

	if (!strcmp(filename, "??:0")) {
		pr_debug("no debugging info in %s\n", dso_name);
		free(filename);
		goto out;
	}

	sep = strchr(filename, ':');
	if (sep) {
		*sep++ = '\0';
		*file = filename;
		*line_nr = strtoul(sep, NULL, 0);
		ret = 1;
	}
out:
	pclose(fp);
	return ret;
}
#endif /* HAVE_LIBBFD_SUPPORT */

char *get_srcline(struct dso *dso, unsigned long addr)
{
	char *file = NULL;
	unsigned line = 0;
	char *srcline;
	char *dso_name = dso->long_name;
	size_t size;

	if (!dso->has_srcline)
		return SRCLINE_UNKNOWN;

	if (dso_name[0] == '[')
		goto out;

	if (!strncmp(dso_name, "/tmp/perf-", 10))
		goto out;

	if (!addr2line(dso_name, addr, &file, &line))
		goto out;

	/* just calculate actual length */
	size = snprintf(NULL, 0, "%s:%u", file, line) + 1;

	srcline = malloc(size);
	if (srcline)
		snprintf(srcline, size, "%s:%u", file, line);
	else
		srcline = SRCLINE_UNKNOWN;

	free(file);
	return srcline;

out:
	dso->has_srcline = 0;
	return SRCLINE_UNKNOWN;
}

void free_srcline(char *srcline)
{
	if (srcline && strcmp(srcline, SRCLINE_UNKNOWN) != 0)
		free(srcline);
}
