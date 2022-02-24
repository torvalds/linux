/* SPDX-License-Identifier: GPL-2.0 */
/*
 * From PPR Vol 1 for AMD Family 19h Model 01h B1
 * 55898 Rev 0.35 - Feb 5, 2021
 */

#include "msr-index.h"

/*
 * IBS Hardware MSRs
 */

/* MSR 0xc0011030: IBS Fetch Control */
union ibs_fetch_ctl {
	__u64 val;
	struct {
		__u64	fetch_maxcnt:16,/* 0-15: instruction fetch max. count */
			fetch_cnt:16,	/* 16-31: instruction fetch count */
			fetch_lat:16,	/* 32-47: instruction fetch latency */
			fetch_en:1,	/* 48: instruction fetch enable */
			fetch_val:1,	/* 49: instruction fetch valid */
			fetch_comp:1,	/* 50: instruction fetch complete */
			ic_miss:1,	/* 51: i-cache miss */
			phy_addr_valid:1,/* 52: physical address valid */
			l1tlb_pgsz:2,	/* 53-54: i-cache L1TLB page size
					 *	  (needs IbsPhyAddrValid) */
			l1tlb_miss:1,	/* 55: i-cache fetch missed in L1TLB */
			l2tlb_miss:1,	/* 56: i-cache fetch missed in L2TLB */
			rand_en:1,	/* 57: random tagging enable */
			fetch_l2_miss:1,/* 58: L2 miss for sampled fetch
					 *      (needs IbsFetchComp) */
			reserved:5;	/* 59-63: reserved */
	};
};

/* MSR 0xc0011033: IBS Execution Control */
union ibs_op_ctl {
	__u64 val;
	struct {
		__u64	opmaxcnt:16,	/* 0-15: periodic op max. count */
			reserved0:1,	/* 16: reserved */
			op_en:1,	/* 17: op sampling enable */
			op_val:1,	/* 18: op sample valid */
			cnt_ctl:1,	/* 19: periodic op counter control */
			opmaxcnt_ext:7,	/* 20-26: upper 7 bits of periodic op maximum count */
			reserved1:5,	/* 27-31: reserved */
			opcurcnt:27,	/* 32-58: periodic op counter current count */
			reserved2:5;	/* 59-63: reserved */
	};
};

/* MSR 0xc0011035: IBS Op Data 2 */
union ibs_op_data {
	__u64 val;
	struct {
		__u64	comp_to_ret_ctr:16,	/* 0-15: op completion to retire count */
			tag_to_ret_ctr:16,	/* 15-31: op tag to retire count */
			reserved1:2,		/* 32-33: reserved */
			op_return:1,		/* 34: return op */
			op_brn_taken:1,		/* 35: taken branch op */
			op_brn_misp:1,		/* 36: mispredicted branch op */
			op_brn_ret:1,		/* 37: branch op retired */
			op_rip_invalid:1,	/* 38: RIP is invalid */
			op_brn_fuse:1,		/* 39: fused branch op */
			op_microcode:1,		/* 40: microcode op */
			reserved2:23;		/* 41-63: reserved */
	};
};

/* MSR 0xc0011036: IBS Op Data 2 */
union ibs_op_data2 {
	__u64 val;
	struct {
		__u64	data_src:3,	/* 0-2: data source */
			reserved0:1,	/* 3: reserved */
			rmt_node:1,	/* 4: destination node */
			cache_hit_st:1,	/* 5: cache hit state */
			reserved1:57;	/* 5-63: reserved */
	};
};

/* MSR 0xc0011037: IBS Op Data 3 */
union ibs_op_data3 {
	__u64 val;
	struct {
		__u64	ld_op:1,			/* 0: load op */
			st_op:1,			/* 1: store op */
			dc_l1tlb_miss:1,		/* 2: data cache L1TLB miss */
			dc_l2tlb_miss:1,		/* 3: data cache L2TLB hit in 2M page */
			dc_l1tlb_hit_2m:1,		/* 4: data cache L1TLB hit in 2M page */
			dc_l1tlb_hit_1g:1,		/* 5: data cache L1TLB hit in 1G page */
			dc_l2tlb_hit_2m:1,		/* 6: data cache L2TLB hit in 2M page */
			dc_miss:1,			/* 7: data cache miss */
			dc_mis_acc:1,			/* 8: misaligned access */
			reserved:4,			/* 9-12: reserved */
			dc_wc_mem_acc:1,		/* 13: write combining memory access */
			dc_uc_mem_acc:1,		/* 14: uncacheable memory access */
			dc_locked_op:1,			/* 15: locked operation */
			dc_miss_no_mab_alloc:1,		/* 16: DC miss with no MAB allocated */
			dc_lin_addr_valid:1,		/* 17: data cache linear address valid */
			dc_phy_addr_valid:1,		/* 18: data cache physical address valid */
			dc_l2_tlb_hit_1g:1,		/* 19: data cache L2 hit in 1GB page */
			l2_miss:1,			/* 20: L2 cache miss */
			sw_pf:1,			/* 21: software prefetch */
			op_mem_width:4,			/* 22-25: load/store size in bytes */
			op_dc_miss_open_mem_reqs:6,	/* 26-31: outstanding mem reqs on DC fill */
			dc_miss_lat:16,			/* 32-47: data cache miss latency */
			tlb_refill_lat:16;		/* 48-63: L1 TLB refill latency */
	};
};

/* MSR 0xc001103c: IBS Fetch Control Extended */
union ic_ibs_extd_ctl {
	__u64 val;
	struct {
		__u64	itlb_refill_lat:16,	/* 0-15: ITLB Refill latency for sampled fetch */
			reserved:48;		/* 16-63: reserved */
	};
};

/*
 * IBS driver related
 */

struct perf_ibs_data {
	u32		size;
	union {
		u32	data[0];	/* data buffer starts here */
		u32	caps;
	};
	u64		regs[MSR_AMD64_IBS_REG_COUNT_MAX];
};
