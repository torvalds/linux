
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
#include "machine.h"
#include "linux/string.h"
#include "debug.h"

#define VDSO__TEMP_FILE_NAME "/tmp/perf-vdso.so-XXXXXX"

struct vdso_file {
	bool found;
	bool error;
	char temp_file_name[sizeof(VDSO__TEMP_FILE_NAME)];
	const char *dso_name;
};

struct vdso_info {
	struct vdso_file vdso;
};

static struct vdso_info vdso_info_ = {
	.vdso = {
		.temp_file_name = VDSO__TEMP_FILE_NAME,
		.dso_name = VDSO__MAP_NAME,
	},
};

static struct vdso_info *vdso_info = &vdso_info_;

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

static char *get_file(struct vdso_file *vdso_file)
{
	char *vdso = NULL;
	char *buf = NULL;
	void *start, *end;
	size_t size;
	int fd;

	if (vdso_file->found)
		return vdso_file->temp_file_name;

	if (vdso_file->error || find_vdso_map(&start, &end))
		return NULL;

	size = end - start;

	buf = memdup(start, size);
	if (!buf)
		return NULL;

	fd = mkstemp(vdso_file->temp_file_name);
	if (fd < 0)
		goto out;

	if (size == (size_t) write(fd, buf, size))
		vdso = vdso_file->temp_file_name;

	close(fd);

 out:
	free(buf);

	vdso_file->found = (vdso != NULL);
	vdso_file->error = !vdso_file->found;
	return vdso;
}

void vdso__exit(void)
{
	if (vdso_info->vdso.found)
		unlink(vdso_info->vdso.temp_file_name);
}

struct dso *vdso__dso_findnew(struct machine *machine)
{
	struct dso *dso = dsos__find(&machine->user_dsos, VDSO__MAP_NAME, true);

	if (!dso) {
		char *file;

		file = get_file(&vdso_info->vdso);
		if (!file)
			return NULL;

		dso = dso__new(VDSO__MAP_NAME);
		if (dso != NULL) {
			dsos__add(&machine->user_dsos, dso);
			dso__set_long_name(dso, file, false);
		}
	}

	return dso;
}
