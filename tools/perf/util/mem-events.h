/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_MEM_EVENTS_H
#define __PERF_MEM_EVENTS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <linux/types.h>
#include <linux/refcount.h>
#include <linux/perf_event.h>
#include "stat.h"
#include "evsel.h"

struct perf_mem_event {
	bool		record;
	bool		supported;
	const char	*tag;
	const char	*name;
	const char	*sysfs_name;
};

struct mem_info {
	struct addr_map_symbol	iaddr;
	struct addr_map_symbol	daddr;
	union perf_mem_data_src	data_src;
	refcount_t		refcnt;
};

enum {
	PERF_MEM_EVENTS__LOAD,
	PERF_MEM_EVENTS__STORE,
	PERF_MEM_EVENTS__LOAD_STORE,
	PERF_MEM_EVENTS__MAX,
};

extern unsigned int perf_mem_events__loads_ldlat;

int perf_mem_events__parse(const char *str);
int perf_mem_events__init(void);

const char *perf_mem_events__name(int i, const char *pmu_name);
struct perf_mem_event *perf_mem_events__ptr(int i);
bool is_mem_loads_aux_event(struct evsel *leader);

void perf_mem_events__list(void);
int perf_mem_events__record_args(const char **rec_argv, int *argv_nr,
				 char **rec_tmp, int *tmp_nr);

int perf_mem__tlb_scnprintf(char *out, size_t sz, struct mem_info *mem_info);
int perf_mem__lvl_scnprintf(char *out, size_t sz, struct mem_info *mem_info);
int perf_mem__snp_scnprintf(char *out, size_t sz, struct mem_info *mem_info);
int perf_mem__lck_scnprintf(char *out, size_t sz, struct mem_info *mem_info);
int perf_mem__blk_scnprintf(char *out, size_t sz, struct mem_info *mem_info);

int perf_script__meminfo_scnprintf(char *bf, size_t size, struct mem_info *mem_info);

struct c2c_stats {
	u32	nr_entries;

	u32	locks;               /* count of 'lock' transactions */
	u32	store;               /* count of all stores in trace */
	u32	st_uncache;          /* stores to uncacheable address */
	u32	st_noadrs;           /* cacheable store with no address */
	u32	st_l1hit;            /* count of stores that hit L1D */
	u32	st_l1miss;           /* count of stores that miss L1D */
	u32	st_na;               /* count of stores with memory level is not available */
	u32	load;                /* count of all loads in trace */
	u32	ld_excl;             /* exclusive loads, rmt/lcl DRAM - snp none/miss */
	u32	ld_shared;           /* shared loads, rmt/lcl DRAM - snp hit */
	u32	ld_uncache;          /* loads to uncacheable address */
	u32	ld_io;               /* loads to io address */
	u32	ld_miss;             /* loads miss */
	u32	ld_noadrs;           /* cacheable load with no address */
	u32	ld_fbhit;            /* count of loads hitting Fill Buffer */
	u32	ld_l1hit;            /* count of loads that hit L1D */
	u32	ld_l2hit;            /* count of loads that hit L2D */
	u32	ld_llchit;           /* count of loads that hit LLC */
	u32	lcl_hitm;            /* count of loads with local HITM  */
	u32	rmt_hitm;            /* count of loads with remote HITM */
	u32	tot_hitm;            /* count of loads with local and remote HITM */
	u32	lcl_peer;            /* count of loads with local peer cache */
	u32	rmt_peer;            /* count of loads with remote peer cache */
	u32	tot_peer;            /* count of loads with local and remote peer cache */
	u32	rmt_hit;             /* count of loads with remote hit clean; */
	u32	lcl_dram;            /* count of loads miss to local DRAM */
	u32	rmt_dram;            /* count of loads miss to remote DRAM */
	u32	blk_data;            /* count of loads blocked by data */
	u32	blk_addr;            /* count of loads blocked by address conflict */
	u32	nomap;               /* count of load/stores with no phys addrs */
	u32	noparse;             /* count of unparsable data sources */
};

struct hist_entry;
int c2c_decode_stats(struct c2c_stats *stats, struct mem_info *mi);
void c2c_add_stats(struct c2c_stats *stats, struct c2c_stats *add);

#endif /* __PERF_MEM_EVENTS_H */
