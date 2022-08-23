// SPDX-License-Identifier: GPL-2.0
/*
 * An empty pmu-events.c file used when there is no architecture json files in
 * arch or when the jevents.py script cannot be run.
 *
 * The test cpu/soc is provided for testing.
 */
#include "pmu-events/pmu-events.h"

static const struct pmu_event pme_test_soc_cpu[] = {
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

const struct pmu_events_map pmu_events_map[] = {
	{
		.cpuid = "testcpu",
		.version = "v1",
		.type = "core",
		.table = pme_test_soc_cpu,
	},
	{
		.cpuid = 0,
		.version = 0,
		.type = 0,
		.table = 0,
	},
};

static const struct pmu_event pme_test_soc_sys[] = {
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

const struct pmu_sys_events pmu_sys_event_tables[] = {
	{
		.table = pme_test_soc_sys,
		.name = "pme_test_soc_sys",
	},
	{
		.table = 0
	},
};
