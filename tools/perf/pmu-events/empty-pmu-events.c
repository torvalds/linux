// SPDX-License-Identifier: GPL-2.0
/*
 * An empty pmu-events.c file used when there is no architecture json files in
 * arch or when the jevents.py script cannot be run.
 *
 * The test cpu/soc is provided for testing.
 */
#include "pmu-events/pmu-events.h"
#include "util/header.h"
#include "util/pmu.h"
#include <string.h>
#include <stddef.h>

static const struct pmu_event pmu_events__test_soc_cpu[] = {
	{
		.name = "l3_cache_rd",
		.event = "event=0x40",
		.desc = "L3 cache access, read",
		.topic = "cache",
		.long_desc = "Attributable Level 3 cache access, read",
	},
	{
		.name = "segment_reg_loads.any",
		.event = "event=0x6,period=200000,umask=0x80",
		.desc = "Number of segment register loads",
		.topic = "other",
	},
	{
		.name = "dispatch_blocked.any",
		.event = "event=0x9,period=200000,umask=0x20",
		.desc = "Memory cluster signals to block micro-op dispatch for any reason",
		.topic = "other",
	},
	{
		.name = "eist_trans",
		.event = "event=0x3a,period=200000,umask=0x0",
		.desc = "Number of Enhanced Intel SpeedStep(R) Technology (EIST) transitions",
		.topic = "other",
	},
	{
		.name = "uncore_hisi_ddrc.flux_wcmd",
		.event = "event=0x2",
		.desc = "DDRC write commands. Unit: hisi_sccl,ddrc ",
		.topic = "uncore",
		.long_desc = "DDRC write commands",
		.pmu = "hisi_sccl,ddrc",
	},
	{
		.name = "unc_cbo_xsnp_response.miss_eviction",
		.event = "event=0x22,umask=0x81",
		.desc = "A cross-core snoop resulted from L3 Eviction which misses in some processor core. Unit: uncore_cbox ",
		.topic = "uncore",
		.long_desc = "A cross-core snoop resulted from L3 Eviction which misses in some processor core",
		.pmu = "uncore_cbox",
	},
	{
		.name = "event-hyphen",
		.event = "event=0xe0,umask=0x00",
		.desc = "UNC_CBO_HYPHEN. Unit: uncore_cbox ",
		.topic = "uncore",
		.long_desc = "UNC_CBO_HYPHEN",
		.pmu = "uncore_cbox",
	},
	{
		.name = "event-two-hyph",
		.event = "event=0xc0,umask=0x00",
		.desc = "UNC_CBO_TWO_HYPH. Unit: uncore_cbox ",
		.topic = "uncore",
		.long_desc = "UNC_CBO_TWO_HYPH",
		.pmu = "uncore_cbox",
	},
	{
		.name = "uncore_hisi_l3c.rd_hit_cpipe",
		.event = "event=0x7",
		.desc = "Total read hits. Unit: hisi_sccl,l3c ",
		.topic = "uncore",
		.long_desc = "Total read hits",
		.pmu = "hisi_sccl,l3c",
	},
	{
		.name = "uncore_imc_free_running.cache_miss",
		.event = "event=0x12",
		.desc = "Total cache misses. Unit: uncore_imc_free_running ",
		.topic = "uncore",
		.long_desc = "Total cache misses",
		.pmu = "uncore_imc_free_running",
	},
	{
		.name = "uncore_imc.cache_hits",
		.event = "event=0x34",
		.desc = "Total cache hits. Unit: uncore_imc ",
		.topic = "uncore",
		.long_desc = "Total cache hits",
		.pmu = "uncore_imc",
	},
	{
		.name = "bp_l1_btb_correct",
		.event = "event=0x8a",
		.desc = "L1 BTB Correction",
		.topic = "branch",
	},
	{
		.name = "bp_l2_btb_correct",
		.event = "event=0x8b",
		.desc = "L2 BTB Correction",
		.topic = "branch",
	},
	{
		.name = 0,
		.event = 0,
		.desc = 0,
	},
};

static const struct pmu_metric pmu_metrics__test_soc_cpu[] = {
	{
		.metric_expr	= "1 / IPC",
		.metric_name	= "CPI",
	},
	{
		.metric_expr	= "inst_retired.any / cpu_clk_unhalted.thread",
		.metric_name	= "IPC",
		.metric_group	= "group1",
	},
	{
		.metric_expr	= "idq_uops_not_delivered.core / (4 * (( ( cpu_clk_unhalted.thread / 2 ) * "
		"( 1 + cpu_clk_unhalted.one_thread_active / cpu_clk_unhalted.ref_xclk ) )))",
		.metric_name	= "Frontend_Bound_SMT",
	},
	{
		.metric_expr	= "l1d\\-loads\\-misses / inst_retired.any",
		.metric_name	= "dcache_miss_cpi",
	},
	{
		.metric_expr	= "l1i\\-loads\\-misses / inst_retired.any",
		.metric_name	= "icache_miss_cycles",
	},
	{
		.metric_expr	= "(dcache_miss_cpi + icache_miss_cycles)",
		.metric_name	= "cache_miss_cycles",
		.metric_group	= "group1",
	},
	{
		.metric_expr	= "l2_rqsts.demand_data_rd_hit + l2_rqsts.pf_hit + l2_rqsts.rfo_hit",
		.metric_name	= "DCache_L2_All_Hits",
	},
	{
		.metric_expr	= "max(l2_rqsts.all_demand_data_rd - l2_rqsts.demand_data_rd_hit, 0) + "
		"l2_rqsts.pf_miss + l2_rqsts.rfo_miss",
		.metric_name	= "DCache_L2_All_Miss",
	},
	{
		.metric_expr	= "DCache_L2_All_Hits + DCache_L2_All_Miss",
		.metric_name	= "DCache_L2_All",
	},
	{
		.metric_expr	= "d_ratio(DCache_L2_All_Hits, DCache_L2_All)",
		.metric_name	= "DCache_L2_Hits",
	},
	{
		.metric_expr	= "d_ratio(DCache_L2_All_Miss, DCache_L2_All)",
		.metric_name	= "DCache_L2_Misses",
	},
	{
		.metric_expr	= "ipc + M2",
		.metric_name	= "M1",
	},
	{
		.metric_expr	= "ipc + M1",
		.metric_name	= "M2",
	},
	{
		.metric_expr	= "1/M3",
		.metric_name	= "M3",
	},
	{
		.metric_expr	= "64 * l1d.replacement / 1000000000 / duration_time",
		.metric_name	= "L1D_Cache_Fill_BW",
	},
	{
		.metric_expr = 0,
		.metric_name = 0,
	},
};

/* Struct used to make the PMU event table implementation opaque to callers. */
struct pmu_events_table {
	const struct pmu_event *entries;
};

/* Struct used to make the PMU metric table implementation opaque to callers. */
struct pmu_metrics_table {
	const struct pmu_metric *entries;
};

/*
 * Map a CPU to its table of PMU events. The CPU is identified by the
 * cpuid field, which is an arch-specific identifier for the CPU.
 * The identifier specified in tools/perf/pmu-events/arch/xxx/mapfile
 * must match the get_cpuid_str() in tools/perf/arch/xxx/util/header.c)
 *
 * The  cpuid can contain any character other than the comma.
 */
struct pmu_events_map {
	const char *arch;
	const char *cpuid;
	const struct pmu_events_table event_table;
	const struct pmu_metrics_table metric_table;
};

/*
 * Global table mapping each known CPU for the architecture to its
 * table of PMU events.
 */
static const struct pmu_events_map pmu_events_map[] = {
	{
		.arch = "testarch",
		.cpuid = "testcpu",
		.event_table = { pmu_events__test_soc_cpu },
		.metric_table = { pmu_metrics__test_soc_cpu },
	},
	{
		.arch = 0,
		.cpuid = 0,
		.event_table = { 0 },
		.metric_table = { 0 },
	},
};

static const struct pmu_event pmu_events__test_soc_sys[] = {
	{
		.name = "sys_ddr_pmu.write_cycles",
		.event = "event=0x2b",
		.desc = "ddr write-cycles event. Unit: uncore_sys_ddr_pmu ",
		.compat = "v8",
		.topic = "uncore",
		.pmu = "uncore_sys_ddr_pmu",
	},
	{
		.name = "sys_ccn_pmu.read_cycles",
		.event = "config=0x2c",
		.desc = "ccn read-cycles event. Unit: uncore_sys_ccn_pmu ",
		.compat = "0x01",
		.topic = "uncore",
		.pmu = "uncore_sys_ccn_pmu",
	},
	{
		.name = 0,
		.event = 0,
		.desc = 0,
	},
};

struct pmu_sys_events {
	const char *name;
	const struct pmu_events_table table;
};

static const struct pmu_sys_events pmu_sys_event_tables[] = {
	{
		.table = { pmu_events__test_soc_sys },
		.name = "pmu_events__test_soc_sys",
	},
	{
		.table = { 0 }
	},
};

int pmu_events_table_for_each_event(const struct pmu_events_table *table, pmu_event_iter_fn fn,
				    void *data)
{
	for (const struct pmu_event *pe = &table->entries[0]; pe->name; pe++) {
		int ret = fn(pe, table, data);

		if (ret)
			return ret;
	}
	return 0;
}

int pmu_metrics_table_for_each_metric(const struct pmu_metrics_table *table, pmu_metric_iter_fn fn,
				      void *data)
{
	for (const struct pmu_metric *pm = &table->entries[0]; pm->metric_expr; pm++) {
		int ret = fn(pm, table, data);

		if (ret)
			return ret;
	}
	return 0;
}

const struct pmu_events_table *perf_pmu__find_events_table(struct perf_pmu *pmu)
{
	const struct pmu_events_table *table = NULL;
	char *cpuid = perf_pmu__getcpuid(pmu);
	int i;

	/* on some platforms which uses cpus map, cpuid can be NULL for
	 * PMUs other than CORE PMUs.
	 */
	if (!cpuid)
		return NULL;

	i = 0;
	for (;;) {
		const struct pmu_events_map *map = &pmu_events_map[i++];

		if (!map->cpuid)
			break;

		if (!strcmp_cpuid_str(map->cpuid, cpuid)) {
			table = &map->event_table;
			break;
		}
	}
	free(cpuid);
	return table;
}

const struct pmu_metrics_table *perf_pmu__find_metrics_table(struct perf_pmu *pmu)
{
	const struct pmu_metrics_table *table = NULL;
	char *cpuid = perf_pmu__getcpuid(pmu);
	int i;

	/* on some platforms which uses cpus map, cpuid can be NULL for
	 * PMUs other than CORE PMUs.
	 */
	if (!cpuid)
		return NULL;

	i = 0;
	for (;;) {
		const struct pmu_events_map *map = &pmu_events_map[i++];

		if (!map->cpuid)
			break;

		if (!strcmp_cpuid_str(map->cpuid, cpuid)) {
			table = &map->metric_table;
			break;
		}
	}
	free(cpuid);
	return table;
}

const struct pmu_events_table *find_core_events_table(const char *arch, const char *cpuid)
{
	for (const struct pmu_events_map *tables = &pmu_events_map[0];
	     tables->arch;
	     tables++) {
		if (!strcmp(tables->arch, arch) && !strcmp_cpuid_str(tables->cpuid, cpuid))
			return &tables->event_table;
	}
	return NULL;
}

const struct pmu_metrics_table *find_core_metrics_table(const char *arch, const char *cpuid)
{
	for (const struct pmu_events_map *tables = &pmu_events_map[0];
	     tables->arch;
	     tables++) {
		if (!strcmp(tables->arch, arch) && !strcmp_cpuid_str(tables->cpuid, cpuid))
			return &tables->metric_table;
	}
	return NULL;
}

int pmu_for_each_core_event(pmu_event_iter_fn fn, void *data)
{
	for (const struct pmu_events_map *tables = &pmu_events_map[0]; tables->arch; tables++) {
		int ret = pmu_events_table_for_each_event(&tables->event_table, fn, data);

		if (ret)
			return ret;
	}
	return 0;
}

int pmu_for_each_core_metric(pmu_metric_iter_fn fn, void *data)
{
	for (const struct pmu_events_map *tables = &pmu_events_map[0];
	     tables->arch;
	     tables++) {
		int ret = pmu_metrics_table_for_each_metric(&tables->metric_table, fn, data);

		if (ret)
			return ret;
	}
	return 0;
}

const struct pmu_events_table *find_sys_events_table(const char *name)
{
	for (const struct pmu_sys_events *tables = &pmu_sys_event_tables[0];
	     tables->name;
	     tables++) {
		if (!strcmp(tables->name, name))
			return &tables->table;
	}
	return NULL;
}

int pmu_for_each_sys_event(pmu_event_iter_fn fn, void *data)
{
	for (const struct pmu_sys_events *tables = &pmu_sys_event_tables[0];
	     tables->name;
	     tables++) {
		int ret = pmu_events_table_for_each_event(&tables->table, fn, data);

		if (ret)
			return ret;
	}
	return 0;
}

int pmu_for_each_sys_metric(pmu_metric_iter_fn fn __maybe_unused, void *data __maybe_unused)
{
	return 0;
}
