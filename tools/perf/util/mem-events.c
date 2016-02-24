#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <api/fs/fs.h>
#include "mem-events.h"
#include "debug.h"

#define E(t, n, s) { .tag = t, .name = n, .sysfs_name = s }

struct perf_mem_event perf_mem_events[PERF_MEM_EVENTS__MAX] = {
	E("ldlat-loads",	"cpu/mem-loads,ldlat=30/P",	"mem-loads"),
	E("ldlat-stores",	"cpu/mem-stores/P",		"mem-stores"),
};
#undef E

#undef E

int perf_mem_events__parse(const char *str)
{
	char *tok, *saveptr = NULL;
	bool found = false;
	char *buf;
	int j;

	/* We need buffer that we know we can write to. */
	buf = malloc(strlen(str) + 1);
	if (!buf)
		return -ENOMEM;

	strcpy(buf, str);

	tok = strtok_r((char *)buf, ",", &saveptr);

	while (tok) {
		for (j = 0; j < PERF_MEM_EVENTS__MAX; j++) {
			struct perf_mem_event *e = &perf_mem_events[j];

			if (strstr(e->tag, tok))
				e->record = found = true;
		}

		tok = strtok_r(NULL, ",", &saveptr);
	}

	free(buf);

	if (found)
		return 0;

	pr_err("failed: event '%s' not found, use '-e list' to get list of available events\n", str);
	return -1;
}

int perf_mem_events__init(void)
{
	const char *mnt = sysfs__mount();
	bool found = false;
	int j;

	if (!mnt)
		return -ENOENT;

	for (j = 0; j < PERF_MEM_EVENTS__MAX; j++) {
		char path[PATH_MAX];
		struct perf_mem_event *e = &perf_mem_events[j];
		struct stat st;

		scnprintf(path, PATH_MAX, "%s/devices/cpu/events/%s",
			  mnt, e->sysfs_name);

		if (!stat(path, &st))
			e->supported = found = true;
	}

	return found ? 0 : -ENOENT;
}
