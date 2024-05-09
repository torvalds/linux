// SPDX-License-Identifier: GPL-2.0
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <api/fs/fs.h>
#include <linux/kernel.h>
#include "map_symbol.h"
#include "mem-events.h"
#include "debug.h"
#include "symbol.h"
#include "pmu.h"
#include "pmus.h"

unsigned int perf_mem_events__loads_ldlat = 30;

#define E(t, n, s) { .tag = t, .name = n, .sysfs_name = s }

static struct perf_mem_event perf_mem_events[PERF_MEM_EVENTS__MAX] = {
	E("ldlat-loads",	"cpu/mem-loads,ldlat=%u/P",	"cpu/events/mem-loads"),
	E("ldlat-stores",	"cpu/mem-stores/P",		"cpu/events/mem-stores"),
	E(NULL,			NULL,				NULL),
};
#undef E

static char mem_loads_name[100];
static bool mem_loads_name__init;

struct perf_mem_event * __weak perf_mem_events__ptr(int i)
{
	if (i >= PERF_MEM_EVENTS__MAX)
		return NULL;

	return &perf_mem_events[i];
}

const char * __weak perf_mem_events__name(int i, const char *pmu_name  __maybe_unused)
{
	struct perf_mem_event *e = perf_mem_events__ptr(i);

	if (!e)
		return NULL;

	if (i == PERF_MEM_EVENTS__LOAD) {
		if (!mem_loads_name__init) {
			mem_loads_name__init = true;
			scnprintf(mem_loads_name, sizeof(mem_loads_name),
				  e->name, perf_mem_events__loads_ldlat);
		}
		return mem_loads_name;
	}

	return e->name;
}

__weak bool is_mem_loads_aux_event(struct evsel *leader __maybe_unused)
{
	return false;
}

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
			struct perf_mem_event *e = perf_mem_events__ptr(j);

			if (!e->tag)
				continue;

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

static bool perf_mem_event__supported(const char *mnt, char *sysfs_name)
{
	char path[PATH_MAX];
	struct stat st;

	scnprintf(path, PATH_MAX, "%s/devices/%s", mnt, sysfs_name);
	return !stat(path, &st);
}

int perf_mem_events__init(void)
{
	const char *mnt = sysfs__mount();
	bool found = false;
	int j;

	if (!mnt)
		return -ENOENT;

	for (j = 0; j < PERF_MEM_EVENTS__MAX; j++) {
		struct perf_mem_event *e = perf_mem_events__ptr(j);
		char sysfs_name[100];
		struct perf_pmu *pmu = NULL;

		/*
		 * If the event entry isn't valid, skip initialization
		 * and "e->supported" will keep false.
		 */
		if (!e->tag)
			continue;

		/*
		 * Scan all PMUs not just core ones, since perf mem/c2c on
		 * platforms like AMD uses IBS OP PMU which is independent
		 * of core PMU.
		 */
		while ((pmu = perf_pmus__scan(pmu)) != NULL) {
			scnprintf(sysfs_name, sizeof(sysfs_name), e->sysfs_name, pmu->name);
			e->supported |= perf_mem_event__supported(mnt, sysfs_name);
		}

		if (e->supported)
			found = true;
	}

	return found ? 0 : -ENOENT;
}

void perf_mem_events__list(void)
{
	int j;

	for (j = 0; j < PERF_MEM_EVENTS__MAX; j++) {
		struct perf_mem_event *e = perf_mem_events__ptr(j);

		fprintf(stderr, "%-*s%-*s%s",
			e->tag ? 13 : 0,
			e->tag ? : "",
			e->tag && verbose > 0 ? 25 : 0,
			e->tag && verbose > 0 ? perf_mem_events__name(j, NULL) : "",
			e->supported ? ": available\n" : "");
	}
}

static void perf_mem_events__print_unsupport_hybrid(struct perf_mem_event *e,
						    int idx)
{
	const char *mnt = sysfs__mount();
	char sysfs_name[100];
	struct perf_pmu *pmu = NULL;

	while ((pmu = perf_pmus__scan(pmu)) != NULL) {
		scnprintf(sysfs_name, sizeof(sysfs_name), e->sysfs_name,
			  pmu->name);
		if (!perf_mem_event__supported(mnt, sysfs_name)) {
			pr_err("failed: event '%s' not supported\n",
			       perf_mem_events__name(idx, pmu->name));
		}
	}
}

int perf_mem_events__record_args(const char **rec_argv, int *argv_nr,
				 char **rec_tmp, int *tmp_nr)
{
	int i = *argv_nr, k = 0;
	struct perf_mem_event *e;
	struct perf_pmu *pmu;

	for (int j = 0; j < PERF_MEM_EVENTS__MAX; j++) {
		e = perf_mem_events__ptr(j);
		if (!e->record)
			continue;

		if (perf_pmus__num_mem_pmus() == 1) {
			if (!e->supported) {
				pr_err("failed: event '%s' not supported\n",
				       perf_mem_events__name(j, NULL));
				return -1;
			}

			rec_argv[i++] = "-e";
			rec_argv[i++] = perf_mem_events__name(j, NULL);
		} else {
			if (!e->supported) {
				perf_mem_events__print_unsupport_hybrid(e, j);
				return -1;
			}

			while ((pmu = perf_pmus__scan(pmu)) != NULL) {
				const char *s = perf_mem_events__name(j, pmu->name);

				rec_argv[i++] = "-e";
				if (s) {
					char *copy = strdup(s);
					if (!copy)
						return -1;

					rec_argv[i++] = copy;
					rec_tmp[k++] = copy;
				}
			}
		}
	}

	*argv_nr = i;
	*tmp_nr = k;
	return 0;
}

static const char * const tlb_access[] = {
	"N/A",
	"HIT",
	"MISS",
	"L1",
	"L2",
	"Walker",
	"Fault",
};

int perf_mem__tlb_scnprintf(char *out, size_t sz, struct mem_info *mem_info)
{
	size_t l = 0, i;
	u64 m = PERF_MEM_TLB_NA;
	u64 hit, miss;

	sz -= 1; /* -1 for null termination */
	out[0] = '\0';

	if (mem_info)
		m = mem_info->data_src.mem_dtlb;

	hit = m & PERF_MEM_TLB_HIT;
	miss = m & PERF_MEM_TLB_MISS;

	/* already taken care of */
	m &= ~(PERF_MEM_TLB_HIT|PERF_MEM_TLB_MISS);

	for (i = 0; m && i < ARRAY_SIZE(tlb_access); i++, m >>= 1) {
		if (!(m & 0x1))
			continue;
		if (l) {
			strcat(out, " or ");
			l += 4;
		}
		l += scnprintf(out + l, sz - l, tlb_access[i]);
	}
	if (*out == '\0')
		l += scnprintf(out, sz - l, "N/A");
	if (hit)
		l += scnprintf(out + l, sz - l, " hit");
	if (miss)
		l += scnprintf(out + l, sz - l, " miss");

	return l;
}

static const char * const mem_lvl[] = {
	"N/A",
	"HIT",
	"MISS",
	"L1",
	"LFB/MAB",
	"L2",
	"L3",
	"Local RAM",
	"Remote RAM (1 hop)",
	"Remote RAM (2 hops)",
	"Remote Cache (1 hop)",
	"Remote Cache (2 hops)",
	"I/O",
	"Uncached",
};

static const char * const mem_lvlnum[] = {
	[PERF_MEM_LVLNUM_UNC] = "Uncached",
	[PERF_MEM_LVLNUM_CXL] = "CXL",
	[PERF_MEM_LVLNUM_IO] = "I/O",
	[PERF_MEM_LVLNUM_ANY_CACHE] = "Any cache",
	[PERF_MEM_LVLNUM_LFB] = "LFB/MAB",
	[PERF_MEM_LVLNUM_RAM] = "RAM",
	[PERF_MEM_LVLNUM_PMEM] = "PMEM",
	[PERF_MEM_LVLNUM_NA] = "N/A",
};

static const char * const mem_hops[] = {
	"N/A",
	/*
	 * While printing, 'Remote' will be added to represent
	 * 'Remote core, same node' accesses as remote field need
	 * to be set with mem_hops field.
	 */
	"core, same node",
	"node, same socket",
	"socket, same board",
	"board",
};

static int perf_mem__op_scnprintf(char *out, size_t sz, struct mem_info *mem_info)
{
	u64 op = PERF_MEM_LOCK_NA;
	int l;

	if (mem_info)
		op = mem_info->data_src.mem_op;

	if (op & PERF_MEM_OP_NA)
		l = scnprintf(out, sz, "N/A");
	else if (op & PERF_MEM_OP_LOAD)
		l = scnprintf(out, sz, "LOAD");
	else if (op & PERF_MEM_OP_STORE)
		l = scnprintf(out, sz, "STORE");
	else if (op & PERF_MEM_OP_PFETCH)
		l = scnprintf(out, sz, "PFETCH");
	else if (op & PERF_MEM_OP_EXEC)
		l = scnprintf(out, sz, "EXEC");
	else
		l = scnprintf(out, sz, "No");

	return l;
}

int perf_mem__lvl_scnprintf(char *out, size_t sz, struct mem_info *mem_info)
{
	union perf_mem_data_src data_src;
	int printed = 0;
	size_t l = 0;
	size_t i;
	int lvl;
	char hit_miss[5] = {0};

	sz -= 1; /* -1 for null termination */
	out[0] = '\0';

	if (!mem_info)
		goto na;

	data_src = mem_info->data_src;

	if (data_src.mem_lvl & PERF_MEM_LVL_HIT)
		memcpy(hit_miss, "hit", 3);
	else if (data_src.mem_lvl & PERF_MEM_LVL_MISS)
		memcpy(hit_miss, "miss", 4);

	lvl = data_src.mem_lvl_num;
	if (lvl && lvl != PERF_MEM_LVLNUM_NA) {
		if (data_src.mem_remote) {
			strcat(out, "Remote ");
			l += 7;
		}

		if (data_src.mem_hops)
			l += scnprintf(out + l, sz - l, "%s ", mem_hops[data_src.mem_hops]);

		if (mem_lvlnum[lvl])
			l += scnprintf(out + l, sz - l, mem_lvlnum[lvl]);
		else
			l += scnprintf(out + l, sz - l, "L%d", lvl);

		l += scnprintf(out + l, sz - l, " %s", hit_miss);
		return l;
	}

	lvl = data_src.mem_lvl;
	if (!lvl)
		goto na;

	lvl &= ~(PERF_MEM_LVL_NA | PERF_MEM_LVL_HIT | PERF_MEM_LVL_MISS);
	if (!lvl)
		goto na;

	for (i = 0; lvl && i < ARRAY_SIZE(mem_lvl); i++, lvl >>= 1) {
		if (!(lvl & 0x1))
			continue;
		if (printed++) {
			strcat(out, " or ");
			l += 4;
		}
		l += scnprintf(out + l, sz - l, mem_lvl[i]);
	}

	if (printed) {
		l += scnprintf(out + l, sz - l, " %s", hit_miss);
		return l;
	}

na:
	strcat(out, "N/A");
	return 3;
}

static const char * const snoop_access[] = {
	"N/A",
	"None",
	"Hit",
	"Miss",
	"HitM",
};

static const char * const snoopx_access[] = {
	"Fwd",
	"Peer",
};

int perf_mem__snp_scnprintf(char *out, size_t sz, struct mem_info *mem_info)
{
	size_t i, l = 0;
	u64 m = PERF_MEM_SNOOP_NA;

	sz -= 1; /* -1 for null termination */
	out[0] = '\0';

	if (mem_info)
		m = mem_info->data_src.mem_snoop;

	for (i = 0; m && i < ARRAY_SIZE(snoop_access); i++, m >>= 1) {
		if (!(m & 0x1))
			continue;
		if (l) {
			strcat(out, " or ");
			l += 4;
		}
		l += scnprintf(out + l, sz - l, snoop_access[i]);
	}

	m = 0;
	if (mem_info)
		m = mem_info->data_src.mem_snoopx;

	for (i = 0; m && i < ARRAY_SIZE(snoopx_access); i++, m >>= 1) {
		if (!(m & 0x1))
			continue;

		if (l) {
			strcat(out, " or ");
			l += 4;
		}
		l += scnprintf(out + l, sz - l, snoopx_access[i]);
	}

	if (*out == '\0')
		l += scnprintf(out, sz - l, "N/A");

	return l;
}

int perf_mem__lck_scnprintf(char *out, size_t sz, struct mem_info *mem_info)
{
	u64 mask = PERF_MEM_LOCK_NA;
	int l;

	if (mem_info)
		mask = mem_info->data_src.mem_lock;

	if (mask & PERF_MEM_LOCK_NA)
		l = scnprintf(out, sz, "N/A");
	else if (mask & PERF_MEM_LOCK_LOCKED)
		l = scnprintf(out, sz, "Yes");
	else
		l = scnprintf(out, sz, "No");

	return l;
}

int perf_mem__blk_scnprintf(char *out, size_t sz, struct mem_info *mem_info)
{
	size_t l = 0;
	u64 mask = PERF_MEM_BLK_NA;

	sz -= 1; /* -1 for null termination */
	out[0] = '\0';

	if (mem_info)
		mask = mem_info->data_src.mem_blk;

	if (!mask || (mask & PERF_MEM_BLK_NA)) {
		l += scnprintf(out + l, sz - l, " N/A");
		return l;
	}
	if (mask & PERF_MEM_BLK_DATA)
		l += scnprintf(out + l, sz - l, " Data");
	if (mask & PERF_MEM_BLK_ADDR)
		l += scnprintf(out + l, sz - l, " Addr");

	return l;
}

int perf_script__meminfo_scnprintf(char *out, size_t sz, struct mem_info *mem_info)
{
	int i = 0;

	i += scnprintf(out, sz, "|OP ");
	i += perf_mem__op_scnprintf(out + i, sz - i, mem_info);
	i += scnprintf(out + i, sz - i, "|LVL ");
	i += perf_mem__lvl_scnprintf(out + i, sz, mem_info);
	i += scnprintf(out + i, sz - i, "|SNP ");
	i += perf_mem__snp_scnprintf(out + i, sz - i, mem_info);
	i += scnprintf(out + i, sz - i, "|TLB ");
	i += perf_mem__tlb_scnprintf(out + i, sz - i, mem_info);
	i += scnprintf(out + i, sz - i, "|LCK ");
	i += perf_mem__lck_scnprintf(out + i, sz - i, mem_info);
	i += scnprintf(out + i, sz - i, "|BLK ");
	i += perf_mem__blk_scnprintf(out + i, sz - i, mem_info);

	return i;
}

int c2c_decode_stats(struct c2c_stats *stats, struct mem_info *mi)
{
	union perf_mem_data_src *data_src = &mi->data_src;
	u64 daddr  = mi->daddr.addr;
	u64 op     = data_src->mem_op;
	u64 lvl    = data_src->mem_lvl;
	u64 snoop  = data_src->mem_snoop;
	u64 snoopx = data_src->mem_snoopx;
	u64 lock   = data_src->mem_lock;
	u64 blk    = data_src->mem_blk;
	/*
	 * Skylake might report unknown remote level via this
	 * bit, consider it when evaluating remote HITMs.
	 *
	 * Incase of power, remote field can also be used to denote cache
	 * accesses from the another core of same node. Hence, setting
	 * mrem only when HOPS is zero along with set remote field.
	 */
	bool mrem  = (data_src->mem_remote && !data_src->mem_hops);
	int err = 0;

#define HITM_INC(__f)		\
do {				\
	stats->__f++;		\
	stats->tot_hitm++;	\
} while (0)

#define PEER_INC(__f)		\
do {				\
	stats->__f++;		\
	stats->tot_peer++;	\
} while (0)

#define P(a, b) PERF_MEM_##a##_##b

	stats->nr_entries++;

	if (lock & P(LOCK, LOCKED)) stats->locks++;

	if (blk & P(BLK, DATA)) stats->blk_data++;
	if (blk & P(BLK, ADDR)) stats->blk_addr++;

	if (op & P(OP, LOAD)) {
		/* load */
		stats->load++;

		if (!daddr) {
			stats->ld_noadrs++;
			return -1;
		}

		if (lvl & P(LVL, HIT)) {
			if (lvl & P(LVL, UNC)) stats->ld_uncache++;
			if (lvl & P(LVL, IO))  stats->ld_io++;
			if (lvl & P(LVL, LFB)) stats->ld_fbhit++;
			if (lvl & P(LVL, L1 )) stats->ld_l1hit++;
			if (lvl & P(LVL, L2)) {
				stats->ld_l2hit++;

				if (snoopx & P(SNOOPX, PEER))
					PEER_INC(lcl_peer);
			}
			if (lvl & P(LVL, L3 )) {
				if (snoop & P(SNOOP, HITM))
					HITM_INC(lcl_hitm);
				else
					stats->ld_llchit++;

				if (snoopx & P(SNOOPX, PEER))
					PEER_INC(lcl_peer);
			}

			if (lvl & P(LVL, LOC_RAM)) {
				stats->lcl_dram++;
				if (snoop & P(SNOOP, HIT))
					stats->ld_shared++;
				else
					stats->ld_excl++;
			}

			if ((lvl & P(LVL, REM_RAM1)) ||
			    (lvl & P(LVL, REM_RAM2)) ||
			     mrem) {
				stats->rmt_dram++;
				if (snoop & P(SNOOP, HIT))
					stats->ld_shared++;
				else
					stats->ld_excl++;
			}
		}

		if ((lvl & P(LVL, REM_CCE1)) ||
		    (lvl & P(LVL, REM_CCE2)) ||
		     mrem) {
			if (snoop & P(SNOOP, HIT)) {
				stats->rmt_hit++;
			} else if (snoop & P(SNOOP, HITM)) {
				HITM_INC(rmt_hitm);
			} else if (snoopx & P(SNOOPX, PEER)) {
				stats->rmt_hit++;
				PEER_INC(rmt_peer);
			}
		}

		if ((lvl & P(LVL, MISS)))
			stats->ld_miss++;

	} else if (op & P(OP, STORE)) {
		/* store */
		stats->store++;

		if (!daddr) {
			stats->st_noadrs++;
			return -1;
		}

		if (lvl & P(LVL, HIT)) {
			if (lvl & P(LVL, UNC)) stats->st_uncache++;
			if (lvl & P(LVL, L1 )) stats->st_l1hit++;
		}
		if (lvl & P(LVL, MISS))
			if (lvl & P(LVL, L1)) stats->st_l1miss++;
		if (lvl & P(LVL, NA))
			stats->st_na++;
	} else {
		/* unparsable data_src? */
		stats->noparse++;
		return -1;
	}

	if (!mi->daddr.ms.map || !mi->iaddr.ms.map) {
		stats->nomap++;
		return -1;
	}

#undef P
#undef HITM_INC
	return err;
}

void c2c_add_stats(struct c2c_stats *stats, struct c2c_stats *add)
{
	stats->nr_entries	+= add->nr_entries;

	stats->locks		+= add->locks;
	stats->store		+= add->store;
	stats->st_uncache	+= add->st_uncache;
	stats->st_noadrs	+= add->st_noadrs;
	stats->st_l1hit		+= add->st_l1hit;
	stats->st_l1miss	+= add->st_l1miss;
	stats->st_na		+= add->st_na;
	stats->load		+= add->load;
	stats->ld_excl		+= add->ld_excl;
	stats->ld_shared	+= add->ld_shared;
	stats->ld_uncache	+= add->ld_uncache;
	stats->ld_io		+= add->ld_io;
	stats->ld_miss		+= add->ld_miss;
	stats->ld_noadrs	+= add->ld_noadrs;
	stats->ld_fbhit		+= add->ld_fbhit;
	stats->ld_l1hit		+= add->ld_l1hit;
	stats->ld_l2hit		+= add->ld_l2hit;
	stats->ld_llchit	+= add->ld_llchit;
	stats->lcl_hitm		+= add->lcl_hitm;
	stats->rmt_hitm		+= add->rmt_hitm;
	stats->tot_hitm		+= add->tot_hitm;
	stats->lcl_peer		+= add->lcl_peer;
	stats->rmt_peer		+= add->rmt_peer;
	stats->tot_peer		+= add->tot_peer;
	stats->rmt_hit		+= add->rmt_hit;
	stats->lcl_dram		+= add->lcl_dram;
	stats->rmt_dram		+= add->rmt_dram;
	stats->blk_data		+= add->blk_data;
	stats->blk_addr		+= add->blk_addr;
	stats->nomap		+= add->nomap;
	stats->noparse		+= add->noparse;
}
