#include <stdio.h>
#include <stdlib.h>
#include <perf/cpumap.h>
#include <util/cpumap.h>
#include <internal/cpumap.h>
#include <api/fs/fs.h>
#include <errno.h>
#include "debug.h"
#include "header.h"

#define MIDR "/regs/identification/midr_el1"
#define MIDR_SIZE 19
#define MIDR_REVISION_MASK      0xf
#define MIDR_VARIANT_SHIFT      20
#define MIDR_VARIANT_MASK       (0xf << MIDR_VARIANT_SHIFT)

static int _get_cpuid(char *buf, size_t sz, struct perf_cpu_map *cpus)
{
	const char *sysfs = sysfs__mountpoint();
	u64 midr = 0;
	int cpu;

	if (!sysfs || sz < MIDR_SIZE)
		return EINVAL;

	cpus = perf_cpu_map__get(cpus);

	for (cpu = 0; cpu < perf_cpu_map__nr(cpus); cpu++) {
		char path[PATH_MAX];
		FILE *file;

		scnprintf(path, PATH_MAX, "%s/devices/system/cpu/cpu%d" MIDR,
			  sysfs, RC_CHK_ACCESS(cpus)->map[cpu].cpu);

		file = fopen(path, "r");
		if (!file) {
			pr_debug("fopen failed for file %s\n", path);
			continue;
		}

		if (!fgets(buf, MIDR_SIZE, file)) {
			fclose(file);
			continue;
		}
		fclose(file);

		/* Ignore/clear Variant[23:20] and
		 * Revision[3:0] of MIDR
		 */
		midr = strtoul(buf, NULL, 16);
		midr &= (~(MIDR_VARIANT_MASK | MIDR_REVISION_MASK));
		scnprintf(buf, MIDR_SIZE, "0x%016lx", midr);
		/* got midr break loop */
		break;
	}

	perf_cpu_map__put(cpus);

	if (!midr)
		return EINVAL;

	return 0;
}

int get_cpuid(char *buf, size_t sz)
{
	struct perf_cpu_map *cpus = perf_cpu_map__new(NULL);
	int ret;

	if (!cpus)
		return EINVAL;

	ret = _get_cpuid(buf, sz, cpus);

	perf_cpu_map__put(cpus);

	return ret;
}

char *get_cpuid_str(struct perf_pmu *pmu)
{
	char *buf = NULL;
	int res;

	if (!pmu || !pmu->cpus)
		return NULL;

	buf = malloc(MIDR_SIZE);
	if (!buf)
		return NULL;

	/* read midr from list of cpus mapped to this pmu */
	res = _get_cpuid(buf, MIDR_SIZE, pmu->cpus);
	if (res) {
		pr_err("failed to get cpuid string for PMU %s\n", pmu->name);
		free(buf);
		buf = NULL;
	}

	return buf;
}
