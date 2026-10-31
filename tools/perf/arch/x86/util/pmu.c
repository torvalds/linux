// SPDX-License-Identifier: GPL-2.0
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/perf_event.h>
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

#define GENUINE_INTEL_SPR "GenuineIntel-6-8F"
#define GENUINE_INTEL_EMR "GenuineIntel-6-CF"
#define GENUINE_INTEL_GNR "GenuineIntel-6-A[DE]"

static bool cached_snc_supported;
static pthread_once_t snc_support_once = PTHREAD_ONCE_INIT;

static void init_snc_support(void)
{
	/* Sapphirerapids Emeraldrapids Graniterapids support SNC configuration. */
	static const char *const supported_cpuids[] = {
		GENUINE_INTEL_SPR, /* Sapphirerapids */
		GENUINE_INTEL_EMR, /* Emeraldrapids */
		GENUINE_INTEL_GNR, /* Graniterapids */
	};
	char *cpuid = get_cpuid_str((struct perf_cpu){0});

	for (size_t i = 0; i < ARRAY_SIZE(supported_cpuids); i++) {
		cached_snc_supported = cpuid && strcmp_cpuid_str(supported_cpuids[i], cpuid) == 0;
		if (cached_snc_supported)
			break;
	}
	free(cpuid);
}

static bool x86__is_snc_supported(void)
{
	pthread_once(&snc_support_once, init_snc_support);
	return cached_snc_supported;
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

static int cached_snc_nodes;
static pthread_once_t snc_nodes_once = PTHREAD_ONCE_INIT;

static void init_snc_nodes(void)
{
	struct perf_cpu_map *node_cpus =
		read_sysfs_cpu_map("devices/system/node/node0/cpulist");
	struct perf_cpu_map *cache_cpus =
		read_sysfs_cpu_map("devices/system/cpu/cpu0/cache/index3/shared_cpu_list");

	if (node_cpus && cache_cpus)
		cached_snc_nodes = perf_cpu_map__nr(cache_cpus) / perf_cpu_map__nr(node_cpus);
	else
		cached_snc_nodes = 0;
	perf_cpu_map__put(cache_cpus);
	perf_cpu_map__put(node_cpus);
}

static int snc_nodes_per_l3_cache(void)
{
	pthread_once(&snc_nodes_once, init_snc_nodes);
	return cached_snc_nodes;
}

static int cached_num_chas;
static pthread_once_t num_chas_once = PTHREAD_ONCE_INIT;

static void init_num_chas(void)
{
	int fd = perf_pmu__event_source_devices_fd();
	struct io_dir dir;
	struct io_dirent64 *dent;

	if (fd < 0) {
		cached_num_chas = -1;
		return;
	}

	io_dir__init(&dir, fd);

	while ((dent = io_dir__readdir(&dir)) != NULL) {
		/* Note, dent->d_type will be DT_LNK and so isn't a useful filter. */
		if (strstarts(dent->d_name, "uncore_cha_"))
			cached_num_chas++;
	}
	close(fd);
}

static int num_chas(void)
{
	pthread_once(&num_chas_once, init_num_chas);
	return cached_num_chas;
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
	if (chas_per_node == 0) {
		pr_warning("Unexpected: chas_per_node is 0 (num_cha=%d, snc_nodes=%d)\n",
			   num_cha, snc_nodes);
		return 0;
	}
	cha_snc = cha_num / chas_per_node;

	/* Range check cha_snc. for unexpected out of bounds. */
	return cha_snc >= MAX_SNCS ? 0 : cha_snc;
}

static const u8 *cached_imc_snc_map;
static size_t cached_imc_snc_map_len;
static pthread_once_t imc_snc_map_once = PTHREAD_ONCE_INIT;

static void init_snc_map(void)
{
	int snc_nodes = snc_nodes_per_l3_cache();
	char *cpuid;
	static const u8 spr_emr_snc2_map[] = { 0, 0, 1, 1 };
	static const u8 gnr_snc2_map[] = { 1, 1, 0, 0 };
	static const u8 snc3_map[] = { 1, 1, 0, 0, 2, 2 };

	switch (snc_nodes) {
	case 2:
		cpuid = get_cpuid_str((struct perf_cpu){ 0 });
		if (cpuid) {
			if (strcmp_cpuid_str(GENUINE_INTEL_SPR, cpuid) == 0 ||
			    strcmp_cpuid_str(GENUINE_INTEL_EMR, cpuid) == 0) {
				cached_imc_snc_map = spr_emr_snc2_map;
				cached_imc_snc_map_len = ARRAY_SIZE(spr_emr_snc2_map);
			} else if (strcmp_cpuid_str(GENUINE_INTEL_GNR, cpuid) == 0) {
				cached_imc_snc_map = gnr_snc2_map;
				cached_imc_snc_map_len = ARRAY_SIZE(gnr_snc2_map);
			}
			free(cpuid);
		}
		break;
	case 3:
		cached_imc_snc_map = snc3_map;
		cached_imc_snc_map_len = ARRAY_SIZE(snc3_map);
		break;
	default:
		/* Error or no lookup support for SNC with >3 nodes. */
		break;
	}

	if (!cached_imc_snc_map)
		pr_warning("Unexpected: can not find snc map config\n");
}

static int uncore_imc_snc(struct perf_pmu *pmu)
{
	// Compute the IMC SNC using lookup tables.
	unsigned int imc_num;
	int snc_nodes = snc_nodes_per_l3_cache();

	if (snc_nodes <= 1)
		return 0;

	pthread_once(&imc_snc_map_once, init_snc_map);

	/* Compute SNC for PMU. */
	if (sscanf(pmu->name, "uncore_imc_%u", &imc_num) != 1) {
		pr_warning("Unexpected: unable to compute IMC number '%s'\n", pmu->name);
		return 0;
	}

	if (!cached_imc_snc_map)
		return 0;

	return cached_imc_snc_map[imc_num % cached_imc_snc_map_len];
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

static pthread_mutex_t pmu_adjust_mutex = PTHREAD_MUTEX_INITIALIZER;

static void uncore_cha_imc_adjust_cpumask_for_snc(struct perf_pmu *pmu, bool cha)
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
	unsigned int idx;
	int pmu_snc, cpu_adjust;
	struct perf_cpu cpu;
	bool alloc;

	// Cpus from the kernel holds first CPU of each socket. e.g. 0,120.
	if (perf_cpu_map__cpu(pmu->cpus, 0).cpu != 0) {
		pr_debug("Ignoring cpumask adjust for %s as unexpected first CPU\n", pmu->name);
		return;
	}

	pthread_mutex_lock(&pmu_adjust_mutex);

	pmu_snc = cha ? uncore_cha_snc(pmu) : uncore_imc_snc(pmu);
	if (pmu_snc == 0) {
		pthread_mutex_unlock(&pmu_adjust_mutex);
		return;
	}

	alloc = adjusted[pmu_snc] == NULL;
	if (alloc) {
		// Hold onto the perf_cpu_map globally to avoid recomputation.
		cpu_adjust = uncore_cha_imc_compute_cpu_adjust(pmu_snc);
		adjusted[pmu_snc] = perf_cpu_map__empty_new(perf_cpu_map__nr(pmu->cpus));
		if (!adjusted[pmu_snc]) {
			pthread_mutex_unlock(&pmu_adjust_mutex);
			return;
		}
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

	pthread_mutex_unlock(&pmu_adjust_mutex);
}

void perf_pmu__arch_init(struct perf_pmu *pmu)
{
	struct perf_pmu_caps *ldlat_cap;
	bool swfilt_format;

	if (!strcmp(pmu->name, INTEL_PT_PMU_NAME)) {
		pmu->auxtrace = true;
		pmu->selectable = true;
		pmu->perf_event_attr_init_default = intel_pt_pmu_default_config;
	}
	if (!strcmp(pmu->name, INTEL_BTS_PMU_NAME)) {
		pmu->auxtrace = true;
		pmu->selectable = true;
	}

	if (x86__is_amd_cpu()) {
		if (strcmp(pmu->name, "ibs_op"))
			return;

		swfilt_format = perf_pmu__has_format(pmu, "swfilt");
		pmu->mem_events = swfilt_format ?
				  perf_mem_events_amd_swfilt :
				  perf_mem_events_amd;

		if (!perf_pmu__caps_parse(pmu))
			return;

		ldlat_cap = perf_pmu__get_cap(pmu, "ldlat");
		if (!ldlat_cap || strcmp(ldlat_cap->value, "1"))
			return;

		perf_mem_events__loads_ldlat = 0;
		pmu->mem_events = swfilt_format ?
				  perf_mem_events_amd_ldlat_swfilt :
				  perf_mem_events_amd_ldlat;
	} else {
		if (pmu->is_core) {
			if (perf_pmu__have_event(pmu, "mem-loads-aux"))
				pmu->mem_events = perf_mem_events_intel_aux;
			else
				pmu->mem_events = perf_mem_events_intel;
		} else if (x86__is_snc_supported()) {
			int snc_nodes = snc_nodes_per_l3_cache();

			if (snc_nodes == 2 || snc_nodes == 3) {
				if (strstarts(pmu->name, "uncore_cha_"))
					uncore_cha_imc_adjust_cpumask_for_snc(pmu, /*cha=*/true);
				else if (strstarts(pmu->name, "uncore_imc_") &&
					 !strstarts(pmu->name, "uncore_imc_free_running"))
					uncore_cha_imc_adjust_cpumask_for_snc(pmu, /*cha=*/false);
			}
		}
	}
}
