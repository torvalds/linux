/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_MEM_EVENTS_H
#define __PERF_MEM_EVENTS_H

#include <stdbool.h>
#include <linux/types.h>

struct perf_mem_event {
	bool		supported;
	bool		ldlat;
	u32		aux_event;
	const char	*tag;
	const char	*name;
	const char	*event_name;
};

enum {
	PERF_MEM_EVENTS__LOAD,
	PERF_MEM_EVENTS__STORE,
	PERF_MEM_EVENTS__LOAD_STORE,
	PERF_MEM_EVENTS__MAX,
};

struct evsel;
struct mem_info;
struct perf_pmu;

extern unsigned int perf_mem_events__loads_ldlat;
extern struct perf_mem_event perf_mem_events[PERF_MEM_EVENTS__MAX];
extern bool perf_mem_record[PERF_MEM_EVENTS__MAX];

int perf_pmu__mem_events_parse(struct perf_pmu *pmu, const char *str);
int perf_pmu__mem_events_init(void);

struct perf_mem_event *perf_pmu__mem_events_ptr(struct perf_pmu *pmu, int i);
struct perf_pmu *perf_mem_events_find_pmu(void);
int perf_pmu__mem_events_num_mem_pmus(struct perf_pmu *pmu);
bool is_mem_loads_aux_event(struct evsel *leader);

void perf_pmu__mem_events_list(struct perf_pmu *pmu);
int perf_mem_events__record_args(const char **rec_argv, int *argv_nr,
				 char **event_name_storage_out);

int perf_mem__tlb_scnprintf(char *out, size_t sz, const struct mem_info *mem_info);
int perf_mem__lvl_scnprintf(char *out, size_t sz, const struct mem_info *mem_info);
int perf_mem__snp_scnprintf(char *out, size_t sz, const struct mem_info *mem_info);
int perf_mem__lck_scnprintf(char *out, size_t sz, const struct mem_info *mem_info);
int perf_mem__blk_scnprintf(char *out, size_t sz, const struct mem_info *mem_info);

int perf_script__meminfo_scnprintf(char *bf, size_t size, const struct mem_info *mem_info);

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
