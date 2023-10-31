#include <linux/kernel.h>
#include <linux/bits.h>
#include <linux/bitfield.h>
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
#define MIDR_REVISION_MASK      GENMASK(3, 0)
#define MIDR_VARIANT_MASK	GENMASK(23, 20)

static int _get_cpuid(char *buf, size_t sz, struct perf_cpu_map *cpus)
{
	const char *sysfs = sysfs__mountpoint();
	int cpu;
	int ret = EINVAL;

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

		/* got midr break loop */
		ret = 0;
		break;
	}

	perf_cpu_map__put(cpus);
	return ret;
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

/*
 * Return 0 if idstr is a higher or equal to version of the same part as
 * mapcpuid. Therefore, if mapcpuid has 0 for revision and variant then any
 * version of idstr will match as long as it's the same CPU type.
 *
 * Return 1 if the CPU type is different or the version of idstr is lower.
 */
int strcmp_cpuid_str(const char *mapcpuid, const char *idstr)
{
	u64 map_id = strtoull(mapcpuid, NULL, 16);
	char map_id_variant = FIELD_GET(MIDR_VARIANT_MASK, map_id);
	char map_id_revision = FIELD_GET(MIDR_REVISION_MASK, map_id);
	u64 id = strtoull(idstr, NULL, 16);
	char id_variant = FIELD_GET(MIDR_VARIANT_MASK, id);
	char id_revision = FIELD_GET(MIDR_REVISION_MASK, id);
	u64 id_fields = ~(MIDR_VARIANT_MASK | MIDR_REVISION_MASK);

	/* Compare without version first */
	if ((map_id & id_fields) != (id & id_fields))
		return 1;

	/*
	 * ID matches, now compare version.
	 *
	 * Arm revisions (like r0p0) are compared here like two digit semver
	 * values eg. 1.3 < 2.0 < 2.1 < 2.2.
	 *
	 *  r = high value = 'Variant' field in MIDR
	 *  p = low value  = 'Revision' field in MIDR
	 *
	 */
	if (id_variant > map_id_variant)
		return 0;

	if (id_variant == map_id_variant && id_revision >= map_id_revision)
		return 0;

	/*
	 * variant is less than mapfile variant or variants are the same but
	 * the revision doesn't match. Return no match.
	 */
	return 1;
}
