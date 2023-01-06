#include <stdio.h>
#include <stdlib.h>
#include <api/fs/fs.h>
#include <errno.h>
#include "../../util/debug.h"
#include "../../util/header.h"

#define STR_LEN 1024
#define ID_SIZE 64

static int _get_cpuid(char *buf, size_t sz)
{
	const char *sysfs = sysfs__mountpoint();
	u64 id = 0;
	char path[PATH_MAX];
	FILE *file;

	if (!sysfs || sz < ID_SIZE)
		return -EINVAL;

	scnprintf(path, PATH_MAX, "%s/devices/platform/riscv-pmu/id",
			sysfs);

	file = fopen(path, "r");
	if (!file) {
		pr_debug("fopen failed for file %s\n", path);
		return -EINVAL;
	}
	if (!fgets(buf, ID_SIZE, file)) {
		fclose(file);
		return -EINVAL;
	}

	fclose(file);

	/*Check if value is numeric and remove special characters*/
	id = strtoul(buf, NULL, 16);
	if (!id)
		return -EINVAL;
	scnprintf(buf, ID_SIZE, "0x%lx", id);

	return 0;
}

char *get_cpuid_str(struct perf_pmu *pmu __maybe_unused)
{
	char *buf = NULL;
	int res;

	if (!pmu)
		return NULL;

	buf = malloc(ID_SIZE);
	if (!buf)
		return NULL;

	/* read id */
	res = _get_cpuid(buf, ID_SIZE);
	if (res) {
		pr_err("failed to get cpuid string for PMU %s\n", pmu->name);
		free(buf);
		buf = NULL;
	}

	return buf;
}
