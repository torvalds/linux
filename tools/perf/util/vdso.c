
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <linux/kernel.h>

#include "vdso.h"
#include "util.h"
#include "symbol.h"
#include "linux/string.h"

static bool vdso_found;
static char vdso_file[] = "/tmp/perf-vdso.so-XXXXXX";

static int find_vdso_map(void **start, void **end)
{
	FILE *maps;
	char line[128];
	int found = 0;

	maps = fopen("/proc/self/maps", "r");
	if (!maps) {
		pr_err("vdso: cannot open maps\n");
		return -1;
	}

	while (!found && fgets(line, sizeof(line), maps)) {
		int m = -1;

		/* We care only about private r-x mappings. */
		if (2 != sscanf(line, "%p-%p r-xp %*x %*x:%*x %*u %n",
				start, end, &m))
			continue;
		if (m < 0)
			continue;

		if (!strncmp(&line[m], VDSO__MAP_NAME,
			     sizeof(VDSO__MAP_NAME) - 1))
			found = 1;
	}

	fclose(maps);
	return !found;
}

static char *get_file(void)
{
	char *vdso = NULL;
	char *buf = NULL;
	void *start, *end;
	size_t size;
	int fd;

	if (vdso_found)
		return vdso_file;

	if (find_vdso_map(&start, &end))
		return NULL;

	size = end - start;

	buf = memdup(start, size);
	if (!buf)
		return NULL;

	fd = mkstemp(vdso_file);
	if (fd < 0)
		goto out;

	if (size == (size_t) write(fd, buf, size))
		vdso = vdso_file;

	close(fd);

 out:
	free(buf);

	vdso_found = (vdso != NULL);
	return vdso;
}

void vdso__exit(void)
{
	if (vdso_found)
		unlink(vdso_file);
}

struct dso *vdso__dso_findnew(struct list_head *head)
{
	struct dso *dso = dsos__find(head, VDSO__MAP_NAME, true);

	if (!dso) {
		char *file;

		file = get_file();
		if (!file)
			return NULL;

		dso = dso__new(VDSO__MAP_NAME);
		if (dso != NULL) {
			dsos__add(head, dso);
			dso__set_long_name(dso, file, false);
		}
	}

	return dso;
}
