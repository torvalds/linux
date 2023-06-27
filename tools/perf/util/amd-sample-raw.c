// SPDX-License-Identifier: GPL-2.0
/*
 * AMD specific. Provide textual annotation for IBS raw sample data.
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include <linux/string.h>
#include "../../arch/x86/include/asm/amd-ibs.h"

#include "debug.h"
#include "session.h"
#include "evlist.h"
#include "sample-raw.h"
#include "pmu-events/pmu-events.h"
#include "util/sample.h"

static u32 cpu_family, cpu_model, ibs_fetch_type, ibs_op_type;
static bool zen4_ibs_extensions;

static void pr_ibs_fetch_ctl(union ibs_fetch_ctl reg)
{
	const char * const ic_miss_strs[] = {
		" IcMiss 0",
		" IcMiss 1",
	};
	const char * const l1tlb_pgsz_strs[] = {
		" L1TlbPgSz 4KB",
		" L1TlbPgSz 2MB",
		" L1TlbPgSz 1GB",
		" L1TlbPgSz RESERVED"
	};
	const char * const l1tlb_pgsz_strs_erratum1347[] = {
		" L1TlbPgSz 4KB",
		" L1TlbPgSz 16KB",
		" L1TlbPgSz 2MB",
		" L1TlbPgSz 1GB"
	};
	const char *ic_miss_str = NULL;
	const char *l1tlb_pgsz_str = NULL;
	char l3_miss_str[sizeof(" L3MissOnly _ FetchOcMiss _ FetchL3Miss _")] = "";

	if (cpu_family == 0x19 && cpu_model < 0x10) {
		/*
		 * Erratum #1238 workaround is to ignore MSRC001_1030[IbsIcMiss]
		 * Erratum #1347 workaround is to use table provided in erratum
		 */
		if (reg.phy_addr_valid)
			l1tlb_pgsz_str = l1tlb_pgsz_strs_erratum1347[reg.l1tlb_pgsz];
	} else {
		if (reg.phy_addr_valid)
			l1tlb_pgsz_str = l1tlb_pgsz_strs[reg.l1tlb_pgsz];
		ic_miss_str = ic_miss_strs[reg.ic_miss];
	}

	if (zen4_ibs_extensions) {
		snprintf(l3_miss_str, sizeof(l3_miss_str),
			 " L3MissOnly %d FetchOcMiss %d FetchL3Miss %d",
			 reg.l3_miss_only, reg.fetch_oc_miss, reg.fetch_l3_miss);
	}

	printf("ibs_fetch_ctl:\t%016llx MaxCnt %7d Cnt %7d Lat %5d En %d Val %d Comp %d%s "
		"PhyAddrValid %d%s L1TlbMiss %d L2TlbMiss %d RandEn %d%s%s\n",
		reg.val, reg.fetch_maxcnt << 4, reg.fetch_cnt << 4, reg.fetch_lat,
		reg.fetch_en, reg.fetch_val, reg.fetch_comp, ic_miss_str ? : "",
		reg.phy_addr_valid, l1tlb_pgsz_str ? : "", reg.l1tlb_miss, reg.l2tlb_miss,
		reg.rand_en, reg.fetch_comp ? (reg.fetch_l2_miss ? " L2Miss 1" : " L2Miss 0") : "",
		l3_miss_str);
}

static void pr_ic_ibs_extd_ctl(union ic_ibs_extd_ctl reg)
{
	printf("ic_ibs_ext_ctl:\t%016llx IbsItlbRefillLat %3d\n", reg.val, reg.itlb_refill_lat);
}

static void pr_ibs_op_ctl(union ibs_op_ctl reg)
{
	char l3_miss_only[sizeof(" L3MissOnly _")] = "";

	if (zen4_ibs_extensions)
		snprintf(l3_miss_only, sizeof(l3_miss_only), " L3MissOnly %d", reg.l3_miss_only);

	printf("ibs_op_ctl:\t%016llx MaxCnt %9d%s En %d Val %d CntCtl %d=%s CurCnt %9d\n",
		reg.val, ((reg.opmaxcnt_ext << 16) | reg.opmaxcnt) << 4, l3_miss_only,
		reg.op_en, reg.op_val, reg.cnt_ctl,
		reg.cnt_ctl ? "uOps" : "cycles", reg.opcurcnt);
}

static void pr_ibs_op_data(union ibs_op_data reg)
{
	printf("ibs_op_data:\t%016llx CompToRetCtr %5d TagToRetCtr %5d%s%s%s BrnRet %d "
		" RipInvalid %d BrnFuse %d Microcode %d\n",
		reg.val, reg.comp_to_ret_ctr, reg.tag_to_ret_ctr,
		reg.op_brn_ret ? (reg.op_return ? " OpReturn 1" : " OpReturn 0") : "",
		reg.op_brn_ret ? (reg.op_brn_taken ? " OpBrnTaken 1" : " OpBrnTaken 0") : "",
		reg.op_brn_ret ? (reg.op_brn_misp ? " OpBrnMisp 1" : " OpBrnMisp 0") : "",
		reg.op_brn_ret, reg.op_rip_invalid, reg.op_brn_fuse, reg.op_microcode);
}

static void pr_ibs_op_data2_extended(union ibs_op_data2 reg)
{
	static const char * const data_src_str[] = {
		"",
		" DataSrc 1=Local L3 or other L1/L2 in CCX",
		" DataSrc 2=Another CCX cache in the same NUMA node",
		" DataSrc 3=DRAM",
		" DataSrc 4=(reserved)",
		" DataSrc 5=Another CCX cache in a different NUMA node",
		" DataSrc 6=Long-latency DIMM",
		" DataSrc 7=MMIO/Config/PCI/APIC",
		" DataSrc 8=Extension Memory",
		" DataSrc 9=(reserved)",
		" DataSrc 10=(reserved)",
		" DataSrc 11=(reserved)",
		" DataSrc 12=Coherent Memory of a different processor type",
		/* 13 to 31 are reserved. Avoid printing them. */
	};
	int data_src = (reg.data_src_hi << 3) | reg.data_src_lo;

	printf("ibs_op_data2:\t%016llx %sRmtNode %d%s\n", reg.val,
		(data_src == 1 || data_src == 2 || data_src == 5) ?
			(reg.cache_hit_st ? "CacheHitSt 1=O-State " : "CacheHitSt 0=M-state ") : "",
		reg.rmt_node,
		data_src < (int)ARRAY_SIZE(data_src_str) ? data_src_str[data_src] : "");
}

static void pr_ibs_op_data2_default(union ibs_op_data2 reg)
{
	static const char * const data_src_str[] = {
		"",
		" DataSrc 1=(reserved)",
		" DataSrc 2=Local node cache",
		" DataSrc 3=DRAM",
		" DataSrc 4=Remote node cache",
		" DataSrc 5=(reserved)",
		" DataSrc 6=(reserved)",
		" DataSrc 7=Other"
	};

	printf("ibs_op_data2:\t%016llx %sRmtNode %d%s\n", reg.val,
	       reg.data_src_lo == 2 ? (reg.cache_hit_st ? "CacheHitSt 1=O-State "
						     : "CacheHitSt 0=M-state ") : "",
	       reg.rmt_node, data_src_str[reg.data_src_lo]);
}

static void pr_ibs_op_data2(union ibs_op_data2 reg)
{
	if (zen4_ibs_extensions)
		return pr_ibs_op_data2_extended(reg);
	pr_ibs_op_data2_default(reg);
}

static void pr_ibs_op_data3(union ibs_op_data3 reg)
{
	char l2_miss_str[sizeof(" L2Miss _")] = "";
	char op_mem_width_str[sizeof(" OpMemWidth _____ bytes")] = "";
	char op_dc_miss_open_mem_reqs_str[sizeof(" OpDcMissOpenMemReqs __")] = "";

	/*
	 * Erratum #1293
	 * Ignore L2Miss and OpDcMissOpenMemReqs (and opdata2) if DcMissNoMabAlloc or SwPf set
	 */
	if (!(cpu_family == 0x19 && cpu_model < 0x10 && (reg.dc_miss_no_mab_alloc || reg.sw_pf))) {
		snprintf(l2_miss_str, sizeof(l2_miss_str), " L2Miss %d", reg.l2_miss);
		snprintf(op_dc_miss_open_mem_reqs_str, sizeof(op_dc_miss_open_mem_reqs_str),
			 " OpDcMissOpenMemReqs %2d", reg.op_dc_miss_open_mem_reqs);
	}

	if (reg.op_mem_width)
		snprintf(op_mem_width_str, sizeof(op_mem_width_str),
			 " OpMemWidth %2d bytes", 1 << (reg.op_mem_width - 1));

	printf("ibs_op_data3:\t%016llx LdOp %d StOp %d DcL1TlbMiss %d DcL2TlbMiss %d "
		"DcL1TlbHit2M %d DcL1TlbHit1G %d DcL2TlbHit2M %d DcMiss %d DcMisAcc %d "
		"DcWcMemAcc %d DcUcMemAcc %d DcLockedOp %d DcMissNoMabAlloc %d DcLinAddrValid %d "
		"DcPhyAddrValid %d DcL2TlbHit1G %d%s SwPf %d%s%s DcMissLat %5d TlbRefillLat %5d\n",
		reg.val, reg.ld_op, reg.st_op, reg.dc_l1tlb_miss, reg.dc_l2tlb_miss,
		reg.dc_l1tlb_hit_2m, reg.dc_l1tlb_hit_1g, reg.dc_l2tlb_hit_2m, reg.dc_miss,
		reg.dc_mis_acc, reg.dc_wc_mem_acc, reg.dc_uc_mem_acc, reg.dc_locked_op,
		reg.dc_miss_no_mab_alloc, reg.dc_lin_addr_valid, reg.dc_phy_addr_valid,
		reg.dc_l2_tlb_hit_1g, l2_miss_str, reg.sw_pf, op_mem_width_str,
		op_dc_miss_open_mem_reqs_str, reg.dc_miss_lat, reg.tlb_refill_lat);
}

/*
 * IBS Op/Execution MSRs always saved, in order, are:
 * IBS_OP_CTL, IBS_OP_RIP, IBS_OP_DATA, IBS_OP_DATA2,
 * IBS_OP_DATA3, IBS_DC_LINADDR, IBS_DC_PHYSADDR, BP_IBSTGT_RIP
 */
static void amd_dump_ibs_op(struct perf_sample *sample)
{
	struct perf_ibs_data *data = sample->raw_data;
	union ibs_op_ctl *op_ctl = (union ibs_op_ctl *)data->data;
	__u64 *rip = (__u64 *)op_ctl + 1;
	union ibs_op_data *op_data = (union ibs_op_data *)(rip + 1);
	union ibs_op_data3 *op_data3 = (union ibs_op_data3 *)(rip + 3);

	pr_ibs_op_ctl(*op_ctl);
	if (!op_data->op_rip_invalid)
		printf("IbsOpRip:\t%016llx\n", *rip);
	pr_ibs_op_data(*op_data);
	/*
	 * Erratum #1293: ignore op_data2 if DcMissNoMabAlloc or SwPf are set
	 */
	if (!(cpu_family == 0x19 && cpu_model < 0x10 &&
	      (op_data3->dc_miss_no_mab_alloc || op_data3->sw_pf)))
		pr_ibs_op_data2(*(union ibs_op_data2 *)(rip + 2));
	pr_ibs_op_data3(*op_data3);
	if (op_data3->dc_lin_addr_valid)
		printf("IbsDCLinAd:\t%016llx\n", *(rip + 4));
	if (op_data3->dc_phy_addr_valid)
		printf("IbsDCPhysAd:\t%016llx\n", *(rip + 5));
	if (op_data->op_brn_ret && *(rip + 6))
		printf("IbsBrTarget:\t%016llx\n", *(rip + 6));
}

/*
 * IBS Fetch MSRs always saved, in order, are:
 * IBS_FETCH_CTL, IBS_FETCH_LINADDR, IBS_FETCH_PHYSADDR, IC_IBS_EXTD_CTL
 */
static void amd_dump_ibs_fetch(struct perf_sample *sample)
{
	struct perf_ibs_data *data = sample->raw_data;
	union ibs_fetch_ctl *fetch_ctl = (union ibs_fetch_ctl *)data->data;
	__u64 *addr = (__u64 *)fetch_ctl + 1;
	union ic_ibs_extd_ctl *extd_ctl = (union ic_ibs_extd_ctl *)addr + 2;

	pr_ibs_fetch_ctl(*fetch_ctl);
	printf("IbsFetchLinAd:\t%016llx\n", *addr++);
	if (fetch_ctl->phy_addr_valid)
		printf("IbsFetchPhysAd:\t%016llx\n", *addr);
	pr_ic_ibs_extd_ctl(*extd_ctl);
}

/*
 * Test for enable and valid bits in captured control MSRs.
 */
static bool is_valid_ibs_fetch_sample(struct perf_sample *sample)
{
	struct perf_ibs_data *data = sample->raw_data;
	union ibs_fetch_ctl *fetch_ctl = (union ibs_fetch_ctl *)data->data;

	if (fetch_ctl->fetch_en && fetch_ctl->fetch_val)
		return true;

	return false;
}

static bool is_valid_ibs_op_sample(struct perf_sample *sample)
{
	struct perf_ibs_data *data = sample->raw_data;
	union ibs_op_ctl *op_ctl = (union ibs_op_ctl *)data->data;

	if (op_ctl->op_en && op_ctl->op_val)
		return true;

	return false;
}

/* AMD vendor specific raw sample function. Check for PERF_RECORD_SAMPLE events
 * and if the event was triggered by IBS, display its raw data with decoded text.
 * The function is only invoked when the dump flag -D is set.
 */
void evlist__amd_sample_raw(struct evlist *evlist, union perf_event *event,
			    struct perf_sample *sample)
{
	struct evsel *evsel;

	if (event->header.type != PERF_RECORD_SAMPLE || !sample->raw_size)
		return;

	evsel = evlist__event2evsel(evlist, event);
	if (!evsel)
		return;

	if (evsel->core.attr.type == ibs_fetch_type) {
		if (!is_valid_ibs_fetch_sample(sample)) {
			pr_debug("Invalid raw IBS Fetch MSR data encountered\n");
			return;
		}
		amd_dump_ibs_fetch(sample);
	} else if (evsel->core.attr.type == ibs_op_type) {
		if (!is_valid_ibs_op_sample(sample)) {
			pr_debug("Invalid raw IBS Op MSR data encountered\n");
			return;
		}
		amd_dump_ibs_op(sample);
	}
}

static void parse_cpuid(struct perf_env *env)
{
	const char *cpuid;
	int ret;

	cpuid = perf_env__cpuid(env);
	/*
	 * cpuid = "AuthenticAMD,family,model,stepping"
	 */
	ret = sscanf(cpuid, "%*[^,],%u,%u", &cpu_family, &cpu_model);
	if (ret != 2)
		pr_debug("problem parsing cpuid\n");
}

/*
 * Find and assign the type number used for ibs_op or ibs_fetch samples.
 * Device names can be large - we are only interested in the first 9 characters,
 * to match "ibs_fetch".
 */
bool evlist__has_amd_ibs(struct evlist *evlist)
{
	struct perf_env *env = evlist->env;
	int ret, nr_pmu_mappings = perf_env__nr_pmu_mappings(env);
	const char *pmu_mapping = perf_env__pmu_mappings(env);
	char name[sizeof("ibs_fetch")];
	u32 type;

	while (nr_pmu_mappings--) {
		ret = sscanf(pmu_mapping, "%u:%9s", &type, name);
		if (ret == 2) {
			if (strstarts(name, "ibs_op"))
				ibs_op_type = type;
			else if (strstarts(name, "ibs_fetch"))
				ibs_fetch_type = type;
		}
		pmu_mapping += strlen(pmu_mapping) + 1 /* '\0' */;
	}

	if (perf_env__find_pmu_cap(env, "ibs_op", "zen4_ibs_extensions"))
		zen4_ibs_extensions = 1;

	if (ibs_fetch_type || ibs_op_type) {
		if (!cpu_family)
			parse_cpuid(env);
		return true;
	}

	return false;
}
