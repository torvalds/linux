#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/kernel.h>

#include "util/dso.h"
#include "util/util.h"
#include "util/debug.h"

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

char *get_srcline(struct dso *dso, unsigned long addr)
{
	char *file;
	unsigned line;
	char *srcline = SRCLINE_UNKNOWN;
	char *dso_name = dso->long_name;
	size_t size;

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
out:
	return srcline;
}

void free_srcline(char *srcline)
{
	if (srcline && strcmp(srcline, SRCLINE_UNKNOWN) != 0)
		free(srcline);
}
