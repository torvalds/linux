// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/stddef.h>
#include <linux/perf_event.h>
#include <linux/zalloc.h>
#include <api/fs/fs.h>
#include <api/io_dir.h>
#include <internal/cpumap.h>
#include <errno.h>

#include "../../../util/intel-pt.h"
#include "../../../util/intel-bts.h"
#include "../../../util/pmu.h"
#include "../../../util/fncache.h"
#include "../../../util/pmus.h"
#include "mem-events.h"
#include "util/debug.h"
#include "util/env.h"
#include "util/header.h"

static bool x86__is_intel_graniterapids(void)
{
	static bool checked_if_graniterapids;
	static bool is_graniterapids;

	if (!checked_if_graniterapids) {
		const char *graniterapids_cpuid = "GenuineIntel-6-A[DE]";
		char *cpuid = get_cpuid_str((struct perf_cpu){0});

		is_graniterapids = cpuid && strcmp_cpuid_str(graniterapids_cpuid, cpuid) == 0;
		free(cpuid);
		checked_if_graniterapids = true;
	}
	return is_graniterapids;
}

static struct perf_cpu_map *read_sysfs_cpu_map(const char *sysfs_path)
{
	struct perf_cpu_map *cpus;
	char *buf = NULL;
	size_t buf_len;

	if (sysfs__read_str(sysfs_path, &buf, &buf_len) < 0)
		return NULL;

	cpus = perf_cpu_map__new(buf);
	free(buf);
	return cpus;
}

static int snc_nodes_per_l3_cache(void)
{
	static bool checked_snc;
	static int snc_nodes;

	if (!checked_snc) {
		struct perf_cpu_map *node_cpus =
			read_sysfs_cpu_map("devices/system/node/node0/cpulist");
		struct perf_cpu_map *cache_cpus =
			read_sysfs_cpu_map("devices/system/cpu/cpu0/cache/index3/shared_cpu_list");

		snc_nodes = perf_cpu_map__nr(cache_cpus) / perf_cpu_map__nr(node_cpus);
		perf_cpu_map__put(cache_cpus);
		perf_cpu_map__put(node_cpus);
		checked_snc = true;
	}
	return snc_nodes;
}

static bool starts_with(const char *str, const char *prefix)
{
	return !strncmp(prefix, str, strlen(prefix));
}

static int num_chas(void)
{
	static bool checked_chas;
	static int num_chas;

	if (!checked_chas) {
		int fd = perf_pmu__event_source_devices_fd();
		struct io_dir dir;
		struct io_dirent64 *dent;

		if (fd < 0)
			return -1;

		io_dir__init(&dir, fd);

		while ((dent = io_dir__readdir(&dir)) != NULL) {
			/* Note, dent->d_type will be DT_LNK and so isn't a useful filter. */
			if (starts_with(dent->d_name, "uncore_cha_"))
				num_chas++;
		}
		close(fd);
		checked_chas = true;
	}
	return num_chas;
}

#define MAX_SNCS 6

static int uncore_cha_snc(struct perf_pmu *pmu)
{
	// CHA SNC numbers are ordered correspond to the CHAs number.
	unsigned int cha_num;
	int num_cha, chas_per_node, cha_snc;
	int snc_nodes = snc_nodes_per_l3_cache();

	if (snc_nodes <= 1)
		return 0;

	num_cha = num_chas();
	if (num_cha <= 0) {
		pr_warning("Unexpected: no CHAs found\n");
		return 0;
	}

	/* Compute SNC for PMU. */
	if (sscanf(pmu->name, "uncore_cha_%u", &cha_num) != 1) {
		pr_warning("Unexpected: unable to compute CHA number '%s'\n", pmu->name);
		return 0;
	}
	chas_per_node = num_cha / snc_nodes;
	cha_snc = cha_num / chas_per_node;

	/* Range check cha_snc. for unexpected out of bounds. */
	return cha_snc >= MAX_SNCS ? 0 : cha_snc;
}

static int uncore_imc_snc(struct perf_pmu *pmu)
{
	// Compute the IMC SNC using lookup tables.
	unsigned int imc_num;
	int snc_nodes = snc_nodes_per_l3_cache();
	const u8 snc2_map[] = {1, 1, 0, 0, 1, 1, 0, 0};
	const u8 snc3_map[] = {1, 1, 0, 0, 2, 2, 1, 1, 0, 0, 2, 2};
	const u8 *snc_map;
	size_t snc_map_len;

	switch (snc_nodes) {
	case 2:
		snc_map = snc2_map;
		snc_map_len = ARRAY_SIZE(snc2_map);
		break;
	case 3:
		snc_map = snc3_map;
		snc_map_len = ARRAY_SIZE(snc3_map);
		break;
	default:
		/* Error or no lookup support for SNC with >3 nodes. */
		return 0;
	}

	/* Compute SNC for PMU. */
	if (sscanf(pmu->name, "uncore_imc_%u", &imc_num) != 1) {
		pr_warning("Unexpected: unable to compute IMC number '%s'\n", pmu->name);
		return 0;
	}
	if (imc_num >= snc_map_len) {
		pr_warning("Unexpected IMC %d for SNC%d mapping\n", imc_num, snc_nodes);
		return 0;
	}
	return snc_map[imc_num];
}

static int uncore_cha_imc_compute_cpu_adjust(int pmu_snc)
{
	static bool checked_cpu_adjust[MAX_SNCS];
	static int cpu_adjust[MAX_SNCS];
	struct perf_cpu_map *node_cpus;
	char node_path[] = "devices/system/node/node0/cpulist";

	/* Was adjust already computed? */
	if (checked_cpu_adjust[pmu_snc])
		return cpu_adjust[pmu_snc];

	/* SNC0 doesn't need an adjust. */
	if (pmu_snc == 0) {
		cpu_adjust[0] = 0;
		checked_cpu_adjust[0] = true;
		return 0;
	}

	/*
	 * Use NUMA topology to compute first CPU of the NUMA node, we want to
	 * adjust CPU 0 to be this and similarly for other CPUs if there is >1
	 * socket.
	 */
	assert(pmu_snc >= 0 && pmu_snc <= 9);
	node_path[24] += pmu_snc; // Shift node0 to be node<pmu_snc>.
	node_cpus = read_sysfs_cpu_map(node_path);
	cpu_adjust[pmu_snc] = perf_cpu_map__cpu(node_cpus, 0).cpu;
	if (cpu_adjust[pmu_snc] < 0) {
		pr_debug("Failed to read valid CPU list from <sysfs>/%s\n", node_path);
		cpu_adjust[pmu_snc] = 0;
	} else {
		checked_cpu_adjust[pmu_snc] = true;
	}
	perf_cpu_map__put(node_cpus);
	return cpu_adjust[pmu_snc];
}

static void gnr_uncore_cha_imc_adjust_cpumask_for_snc(struct perf_pmu *pmu, bool cha)
{
	// With sub-NUMA clustering (SNC) there is a NUMA node per SNC in the
	// topology. For example, a two socket graniterapids machine may be set
	// up with 3-way SNC meaning there are 6 NUMA nodes that should be
	// displayed with --per-node. The cpumask of the CHA and IMC PMUs
	// reflects per-socket information meaning, for example, uncore_cha_60
	// on a two socket graniterapids machine with 120 cores per socket will
	// have a cpumask of "0,120". This cpumask needs adjusting to "40,160"
	// to reflect that uncore_cha_60 is used for the 2nd SNC of each
	// socket. Without the adjustment events on uncore_cha_60 will appear in
	// node 0 and node 3 (in our example 2 socket 3-way set up), but with
	// the adjustment they will appear in node 1 and node 4. The number of
	// CHAs is typically larger than the number of cores. The CHA numbers
	// are assumed to split evenly and inorder wrt core numbers. There are
	// fewer memory IMC PMUs than cores and mapping is handled using lookup
	// tables.
	static struct perf_cpu_map *cha_adjusted[MAX_SNCS];
	static struct perf_cpu_map *imc_adjusted[MAX_SNCS];
	struct perf_cpu_map **adjusted = cha ? cha_adjusted : imc_adjusted;
	int idx, pmu_snc, cpu_adjust;
	struct perf_cpu cpu;
	bool alloc;

	// Cpus from the kernel holds first CPU of each socket. e.g. 0,120.
	if (perf_cpu_map__cpu(pmu->cpus, 0).cpu != 0) {
		pr_debug("Ignoring cpumask adjust for %s as unexpected first CPU\n", pmu->name);
		return;
	}

	pmu_snc = cha ? uncore_cha_snc(pmu) : uncore_imc_snc(pmu);
	if (pmu_snc == 0) {
		// No adjustment necessary for the first SNC.
		return;
	}

	alloc = adjusted[pmu_snc] == NULL;
	if (alloc) {
		// Hold onto the perf_cpu_map globally to avoid recomputation.
		cpu_adjust = uncore_cha_imc_compute_cpu_adjust(pmu_snc);
		adjusted[pmu_snc] = perf_cpu_map__empty_new(perf_cpu_map__nr(pmu->cpus));
		if (!adjusted[pmu_snc])
			return;
	}

	perf_cpu_map__for_each_cpu(cpu, idx, pmu->cpus) {
		// Compute the new cpu map values or if not allocating, assert
		// that they match expectations. asserts will be removed to
		// avoid overhead in NDEBUG builds.
		if (alloc) {
			RC_CHK_ACCESS(adjusted[pmu_snc])->map[idx].cpu = cpu.cpu + cpu_adjust;
		} else if (idx == 0) {
			cpu_adjust = perf_cpu_map__cpu(adjusted[pmu_snc], idx).cpu - cpu.cpu;
			assert(uncore_cha_imc_compute_cpu_adjust(pmu_snc) == cpu_adjust);
		} else {
			assert(perf_cpu_map__cpu(adjusted[pmu_snc], idx).cpu ==
			       cpu.cpu + cpu_adjust);
		}
	}

	perf_cpu_map__put(pmu->cpus);
	pmu->cpus = perf_cpu_map__get(adjusted[pmu_snc]);
}

void perf_pmu__arch_init(struct perf_pmu *pmu)
{
	struct perf_pmu_caps *ldlat_cap;

#ifdef HAVE_AUXTRACE_SUPPORT
	if (!strcmp(pmu->name, INTEL_PT_PMU_NAME)) {
		pmu->auxtrace = true;
		pmu->selectable = true;
		pmu->perf_event_attr_init_default = intel_pt_pmu_default_config;
	}
	if (!strcmp(pmu->name, INTEL_BTS_PMU_NAME)) {
		pmu->auxtrace = true;
		pmu->selectable = true;
	}
#endif

	if (x86__is_amd_cpu()) {
		if (strcmp(pmu->name, "ibs_op"))
			return;

		pmu->mem_events = perf_mem_events_amd;

		if (!perf_pmu__caps_parse(pmu))
			return;

		ldlat_cap = perf_pmu__get_cap(pmu, "ldlat");
		if (!ldlat_cap || strcmp(ldlat_cap->value, "1"))
			return;

		perf_mem_events__loads_ldlat = 0;
		pmu->mem_events = perf_mem_events_amd_ldlat;
	} else {
		if (pmu->is_core) {
			if (perf_pmu__have_event(pmu, "mem-loads-aux"))
				pmu->mem_events = perf_mem_events_intel_aux;
			else
				pmu->mem_events = perf_mem_events_intel;
		} else if (x86__is_intel_graniterapids()) {
			if (starts_with(pmu->name, "uncore_cha_"))
				gnr_uncore_cha_imc_adjust_cpumask_for_snc(pmu, /*cha=*/true);
			else if (starts_with(pmu->name, "uncore_imc_"))
				gnr_uncore_cha_imc_adjust_cpumask_for_snc(pmu, /*cha=*/false);
		}
	}
}
