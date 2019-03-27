/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Justin Hibbits
 * Copyright (c) 2005, Joseph Koshy
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/pmc.h>
#include <sys/pmckern.h>
#include <sys/systm.h>

#include <machine/pmc_mdep.h>
#include <machine/spr.h>
#include <machine/cpu.h>

#include "hwpmc_powerpc.h"

#define	POWERPC_PMC_CAPS	(PMC_CAP_INTERRUPT | PMC_CAP_USER |     \
				 PMC_CAP_SYSTEM | PMC_CAP_EDGE |	\
				 PMC_CAP_THRESHOLD | PMC_CAP_READ |	\
				 PMC_CAP_WRITE | PMC_CAP_INVERT |	\
				 PMC_CAP_QUALIFIER)

#define PPC_SET_PMC1SEL(r, x)	((r & ~(SPR_MMCR0_PMC1SEL(0x3f))) | SPR_MMCR0_PMC1SEL(x))
#define PPC_SET_PMC2SEL(r, x)	((r & ~(SPR_MMCR0_PMC2SEL(0x3f))) | SPR_MMCR0_PMC2SEL(x))
#define PPC_SET_PMC3SEL(r, x)	((r & ~(SPR_MMCR1_PMC3SEL(0x1f))) | SPR_MMCR1_PMC3SEL(x))
#define PPC_SET_PMC4SEL(r, x)	((r & ~(SPR_MMCR1_PMC4SEL(0x1f))) | SPR_MMCR1_PMC4SEL(x))
#define PPC_SET_PMC5SEL(r, x)	((r & ~(SPR_MMCR1_PMC5SEL(0x1f))) | SPR_MMCR1_PMC5SEL(x))
#define PPC_SET_PMC6SEL(r, x)	((r & ~(SPR_MMCR1_PMC6SEL(0x3f))) | SPR_MMCR1_PMC6SEL(x))

/* Change this when we support more than just the 7450. */
#define MPC7XXX_MAX_PMCS	6

#define MPC7XXX_PMC_HAS_OVERFLOWED(x) (mpc7xxx_pmcn_read(x) & (0x1 << 31))

/*
 * Things to improve on this:
 * - It stops (clears to 0) the PMC and resets it at every context switch
 *   currently.
 */

/*
 * This should work for every 32-bit PowerPC implementation I know of (G3 and G4
 * specifically).
 */

struct mpc7xxx_event_code_map {
	enum pmc_event	pe_ev;       /* enum value */
	uint8_t         pe_counter_mask;  /* Which counter this can be counted in. */
	uint8_t		pe_code;     /* numeric code */
};

#define PPC_PMC_MASK1	0
#define PPC_PMC_MASK2	1
#define PPC_PMC_MASK3	2
#define PPC_PMC_MASK4	3
#define PPC_PMC_MASK5	4
#define PPC_PMC_MASK6	5
#define PPC_PMC_MASK_ALL	0x3f
#define PMC_POWERPC_EVENT(id, mask, number) \
	{ .pe_ev = PMC_EV_PPC7450_##id, .pe_counter_mask = mask, .pe_code = number }

static struct mpc7xxx_event_code_map mpc7xxx_event_codes[] = {
	PMC_POWERPC_EVENT(CYCLE,PPC_PMC_MASK_ALL, 1),
	PMC_POWERPC_EVENT(INSTR_COMPLETED, 0x0f, 2),
	PMC_POWERPC_EVENT(TLB_BIT_TRANSITIONS, 0x0f, 3),
	PMC_POWERPC_EVENT(INSTR_DISPATCHED, 0x0f, 4),
	PMC_POWERPC_EVENT(PMON_EXCEPT, 0x0f, 5),
	PMC_POWERPC_EVENT(PMON_SIG, 0x0f, 7),
	PMC_POWERPC_EVENT(VPU_INSTR_COMPLETED, 0x03, 8),
	PMC_POWERPC_EVENT(VFPU_INSTR_COMPLETED, 0x03, 9),
	PMC_POWERPC_EVENT(VIU1_INSTR_COMPLETED, 0x03, 10),
	PMC_POWERPC_EVENT(VIU2_INSTR_COMPLETED, 0x03, 11),
	PMC_POWERPC_EVENT(MTVSCR_INSTR_COMPLETED, 0x03, 12),
	PMC_POWERPC_EVENT(MTVRSAVE_INSTR_COMPLETED, 0x03, 13),
	PMC_POWERPC_EVENT(VPU_INSTR_WAIT_CYCLES, 0x03, 14),
	PMC_POWERPC_EVENT(VFPU_INSTR_WAIT_CYCLES, 0x03, 15),
	PMC_POWERPC_EVENT(VIU1_INSTR_WAIT_CYCLES, 0x03, 16),
	PMC_POWERPC_EVENT(VIU2_INSTR_WAIT_CYCLES, 0x03, 17),
	PMC_POWERPC_EVENT(MFVSCR_SYNC_CYCLES, 0x03, 18),
	PMC_POWERPC_EVENT(VSCR_SAT_SET, 0x03, 19),
	PMC_POWERPC_EVENT(STORE_INSTR_COMPLETED, 0x03, 20),
	PMC_POWERPC_EVENT(L1_INSTR_CACHE_MISSES, 0x03, 21),
	PMC_POWERPC_EVENT(L1_DATA_SNOOPS, 0x03, 22),
	PMC_POWERPC_EVENT(UNRESOLVED_BRANCHES, 0x01, 23),
	PMC_POWERPC_EVENT(SPEC_BUFFER_CYCLES, 0x01, 24),
	PMC_POWERPC_EVENT(BRANCH_UNIT_STALL_CYCLES, 0x01, 25),
	PMC_POWERPC_EVENT(TRUE_BRANCH_TARGET_HITS, 0x01, 26),
	PMC_POWERPC_EVENT(BRANCH_LINK_STAC_PREDICTED, 0x01, 27),
	PMC_POWERPC_EVENT(GPR_ISSUE_QUEUE_DISPATCHES, 0x01, 28),
	PMC_POWERPC_EVENT(CYCLES_THREE_INSTR_DISPATCHED, 0x01, 29),
	PMC_POWERPC_EVENT(THRESHOLD_INSTR_QUEUE_ENTRIES_CYCLES, 0x01, 30),
	PMC_POWERPC_EVENT(THRESHOLD_VEC_INSTR_QUEUE_ENTRIES_CYCLES, 0x01, 31),
	PMC_POWERPC_EVENT(CYCLES_NO_COMPLETED_INSTRS, 0x01, 32),
	PMC_POWERPC_EVENT(IU2_INSTR_COMPLETED, 0x01, 33),
	PMC_POWERPC_EVENT(BRANCHES_COMPLETED, 0x01, 34),
	PMC_POWERPC_EVENT(EIEIO_INSTR_COMPLETED, 0x01, 35),
	PMC_POWERPC_EVENT(MTSPR_INSTR_COMPLETED, 0x01, 36),
	PMC_POWERPC_EVENT(SC_INSTR_COMPLETED, 0x01, 37),
	PMC_POWERPC_EVENT(LS_LM_COMPLETED, 0x01, 38),
	PMC_POWERPC_EVENT(ITLB_HW_TABLE_SEARCH_CYCLES, 0x01, 39),
	PMC_POWERPC_EVENT(DTLB_HW_SEARCH_CYCLES_OVER_THRESHOLD, 0x01, 40),
	PMC_POWERPC_EVENT(L1_INSTR_CACHE_ACCESSES, 0x01, 41),
	PMC_POWERPC_EVENT(INSTR_BKPT_MATCHES, 0x01, 42),
	PMC_POWERPC_EVENT(L1_DATA_CACHE_LOAD_MISS_CYCLES_OVER_THRESHOLD, 0x01, 43),
	PMC_POWERPC_EVENT(L1_DATA_SNOOP_HIT_ON_MODIFIED, 0x01, 44),
	PMC_POWERPC_EVENT(LOAD_MISS_ALIAS, 0x01, 45),
	PMC_POWERPC_EVENT(LOAD_MISS_ALIAS_ON_TOUCH, 0x01, 46),
	PMC_POWERPC_EVENT(TOUCH_ALIAS, 0x01, 47),
	PMC_POWERPC_EVENT(L1_DATA_SNOOP_HIT_CASTOUT_QUEUE, 0x01, 48),
	PMC_POWERPC_EVENT(L1_DATA_SNOOP_HIT_CASTOUT, 0x01, 49),
	PMC_POWERPC_EVENT(L1_DATA_SNOOP_HITS, 0x01, 50),
	PMC_POWERPC_EVENT(WRITE_THROUGH_STORES, 0x01, 51),
	PMC_POWERPC_EVENT(CACHE_INHIBITED_STORES, 0x01, 52),
	PMC_POWERPC_EVENT(L1_DATA_LOAD_HIT, 0x01, 53),
	PMC_POWERPC_EVENT(L1_DATA_TOUCH_HIT, 0x01, 54),
	PMC_POWERPC_EVENT(L1_DATA_STORE_HIT, 0x01, 55),
	PMC_POWERPC_EVENT(L1_DATA_TOTAL_HITS, 0x01, 56),
	PMC_POWERPC_EVENT(DST_INSTR_DISPATCHED, 0x01, 57),
	PMC_POWERPC_EVENT(REFRESHED_DSTS, 0x01, 58),
	PMC_POWERPC_EVENT(SUCCESSFUL_DST_TABLE_SEARCHES, 0x01, 59),
	PMC_POWERPC_EVENT(DSS_INSTR_COMPLETED, 0x01, 60),
	PMC_POWERPC_EVENT(DST_STREAM_0_CACHE_LINE_FETCHES, 0x01, 61),
	PMC_POWERPC_EVENT(VTQ_SUSPENDS_DUE_TO_CTX_CHANGE, 0x01, 62),
	PMC_POWERPC_EVENT(VTQ_LINE_FETCH_HIT, 0x01, 63),
	PMC_POWERPC_EVENT(VEC_LOAD_INSTR_COMPLETED, 0x01, 64),
	PMC_POWERPC_EVENT(FP_STORE_INSTR_COMPLETED_IN_LSU, 0x01, 65),
	PMC_POWERPC_EVENT(FPU_RENORMALIZATION, 0x01, 66),
	PMC_POWERPC_EVENT(FPU_DENORMALIZATION, 0x01, 67),
	PMC_POWERPC_EVENT(FP_STORE_CAUSES_STALL_IN_LSU, 0x01, 68),
	PMC_POWERPC_EVENT(LD_ST_TRUE_ALIAS_STALL, 0x01, 70),
	PMC_POWERPC_EVENT(LSU_INDEXED_ALIAS_STALL, 0x01, 71),
	PMC_POWERPC_EVENT(LSU_ALIAS_VS_FSQ_WB0_WB1, 0x01, 72),
	PMC_POWERPC_EVENT(LSU_ALIAS_VS_CSQ, 0x01, 73),
	PMC_POWERPC_EVENT(LSU_LOAD_HIT_LINE_ALIAS_VS_CSQ0, 0x01, 74),
	PMC_POWERPC_EVENT(LSU_LOAD_MISS_LINE_ALIAS_VS_CSQ0, 0x01, 75),
	PMC_POWERPC_EVENT(LSU_TOUCH_LINE_ALIAS_VS_FSQ_WB0_WB1, 0x01, 76),
	PMC_POWERPC_EVENT(LSU_TOUCH_ALIAS_VS_CSQ, 0x01, 77),
	PMC_POWERPC_EVENT(LSU_LMQ_FULL_STALL, 0x01, 78),
	PMC_POWERPC_EVENT(FP_LOAD_INSTR_COMPLETED_IN_LSU, 0x01, 79),
	PMC_POWERPC_EVENT(FP_LOAD_SINGLE_INSTR_COMPLETED_IN_LSU, 0x01, 80),
	PMC_POWERPC_EVENT(FP_LOAD_DOUBLE_COMPLETED_IN_LSU, 0x01, 81),
	PMC_POWERPC_EVENT(LSU_RA_LATCH_STALL, 0x01, 82),
	PMC_POWERPC_EVENT(LSU_LOAD_VS_STORE_QUEUE_ALIAS_STALL, 0x01, 83),
	PMC_POWERPC_EVENT(LSU_LMQ_INDEX_ALIAS, 0x01, 84),
	PMC_POWERPC_EVENT(LSU_STORE_QUEUE_INDEX_ALIAS, 0x01, 85),
	PMC_POWERPC_EVENT(LSU_CSQ_FORWARDING, 0x01, 86),
	PMC_POWERPC_EVENT(LSU_MISALIGNED_LOAD_FINISH, 0x01, 87),
	PMC_POWERPC_EVENT(LSU_MISALIGN_STORE_COMPLETED, 0x01, 88),
	PMC_POWERPC_EVENT(LSU_MISALIGN_STALL, 0x01, 89),
	PMC_POWERPC_EVENT(FP_ONE_QUARTER_FPSCR_RENAMES_BUSY, 0x01, 90),
	PMC_POWERPC_EVENT(FP_ONE_HALF_FPSCR_RENAMES_BUSY, 0x01, 91),
	PMC_POWERPC_EVENT(FP_THREE_QUARTERS_FPSCR_RENAMES_BUSY, 0x01, 92),
	PMC_POWERPC_EVENT(FP_ALL_FPSCR_RENAMES_BUSY, 0x01, 93),
	PMC_POWERPC_EVENT(FP_DENORMALIZED_RESULT, 0x01, 94),
	PMC_POWERPC_EVENT(L1_DATA_TOTAL_MISSES, 0x02, 23),
	PMC_POWERPC_EVENT(DISPATCHES_TO_FPR_ISSUE_QUEUE, 0x02, 24),
	PMC_POWERPC_EVENT(LSU_INSTR_COMPLETED, 0x02, 25),
	PMC_POWERPC_EVENT(LOAD_INSTR_COMPLETED, 0x02, 26),
	PMC_POWERPC_EVENT(SS_SM_INSTR_COMPLETED, 0x02, 27),
	PMC_POWERPC_EVENT(TLBIE_INSTR_COMPLETED, 0x02, 28),
	PMC_POWERPC_EVENT(LWARX_INSTR_COMPLETED, 0x02, 29),
	PMC_POWERPC_EVENT(MFSPR_INSTR_COMPLETED, 0x02, 30),
	PMC_POWERPC_EVENT(REFETCH_SERIALIZATION, 0x02, 31),
	PMC_POWERPC_EVENT(COMPLETION_QUEUE_ENTRIES_OVER_THRESHOLD, 0x02, 32),
	PMC_POWERPC_EVENT(CYCLES_ONE_INSTR_DISPATCHED, 0x02, 33),
	PMC_POWERPC_EVENT(CYCLES_TWO_INSTR_COMPLETED, 0x02, 34),
	PMC_POWERPC_EVENT(ITLB_NON_SPECULATIVE_MISSES, 0x02, 35),
	PMC_POWERPC_EVENT(CYCLES_WAITING_FROM_L1_INSTR_CACHE_MISS, 0x02, 36),
	PMC_POWERPC_EVENT(L1_DATA_LOAD_ACCESS_MISS, 0x02, 37),
	PMC_POWERPC_EVENT(L1_DATA_TOUCH_MISS, 0x02, 38),
	PMC_POWERPC_EVENT(L1_DATA_STORE_MISS, 0x02, 39),
	PMC_POWERPC_EVENT(L1_DATA_TOUCH_MISS_CYCLES, 0x02, 40),
	PMC_POWERPC_EVENT(L1_DATA_CYCLES_USED, 0x02, 41),
	PMC_POWERPC_EVENT(DST_STREAM_1_CACHE_LINE_FETCHES, 0x02, 42),
	PMC_POWERPC_EVENT(VTQ_STREAM_CANCELED_PREMATURELY, 0x02, 43),
	PMC_POWERPC_EVENT(VTQ_RESUMES_DUE_TO_CTX_CHANGE, 0x02, 44),
	PMC_POWERPC_EVENT(VTQ_LINE_FETCH_MISS, 0x02, 45),
	PMC_POWERPC_EVENT(VTQ_LINE_FETCH, 0x02, 46),
	PMC_POWERPC_EVENT(TLBIE_SNOOPS, 0x02, 47),
	PMC_POWERPC_EVENT(L1_INSTR_CACHE_RELOADS, 0x02, 48),
	PMC_POWERPC_EVENT(L1_DATA_CACHE_RELOADS, 0x02, 49),
	PMC_POWERPC_EVENT(L1_DATA_CACHE_CASTOUTS_TO_L2, 0x02, 50),
	PMC_POWERPC_EVENT(STORE_MERGE_GATHER, 0x02, 51),
	PMC_POWERPC_EVENT(CACHEABLE_STORE_MERGE_TO_32_BYTES, 0x02, 52),
	PMC_POWERPC_EVENT(DATA_BKPT_MATCHES, 0x02, 53),
	PMC_POWERPC_EVENT(FALL_THROUGH_BRANCHES_PROCESSED, 0x02, 54),
	PMC_POWERPC_EVENT(FIRST_SPECULATIVE_BRANCH_BUFFER_RESOLVED_CORRECTLY, 0x02, 55),
	PMC_POWERPC_EVENT(SECOND_SPECULATION_BUFFER_ACTIVE, 0x02, 56),
	PMC_POWERPC_EVENT(BPU_STALL_ON_LR_DEPENDENCY, 0x02, 57),
	PMC_POWERPC_EVENT(BTIC_MISS, 0x02, 58),
	PMC_POWERPC_EVENT(BRANCH_LINK_STACK_CORRECTLY_RESOLVED, 0x02, 59),
	PMC_POWERPC_EVENT(FPR_ISSUE_STALLED, 0x02, 60),
	PMC_POWERPC_EVENT(SWITCHES_BETWEEN_PRIV_USER, 0x02, 61),
	PMC_POWERPC_EVENT(LSU_COMPLETES_FP_STORE_SINGLE, 0x02, 62),
	PMC_POWERPC_EVENT(CYCLES_TWO_INSTR_COMPLETED, 0x04, 8),
	PMC_POWERPC_EVENT(CYCLES_ONE_INSTR_DISPATCHED, 0x04, 9),
	PMC_POWERPC_EVENT(VR_ISSUE_QUEUE_DISPATCHES, 0x04, 10),
	PMC_POWERPC_EVENT(VR_STALLS, 0x04, 11),
	PMC_POWERPC_EVENT(GPR_RENAME_BUFFER_ENTRIES_OVER_THRESHOLD, 0x04, 12),
	PMC_POWERPC_EVENT(FPR_ISSUE_QUEUE_ENTRIES, 0x04, 13),
	PMC_POWERPC_EVENT(FPU_INSTR_COMPLETED, 0x04, 14),
	PMC_POWERPC_EVENT(STWCX_INSTR_COMPLETED, 0x04, 15),
	PMC_POWERPC_EVENT(LS_LM_INSTR_PIECES, 0x04, 16),
	PMC_POWERPC_EVENT(ITLB_HW_SEARCH_CYCLES_OVER_THRESHOLD, 0x04, 17),
	PMC_POWERPC_EVENT(DTLB_MISSES, 0x04, 18),
	PMC_POWERPC_EVENT(CANCELLED_L1_INSTR_CACHE_MISSES, 0x04, 19),
	PMC_POWERPC_EVENT(L1_DATA_CACHE_OP_HIT, 0x04, 20),
	PMC_POWERPC_EVENT(L1_DATA_LOAD_MISS_CYCLES, 0x04, 21),
	PMC_POWERPC_EVENT(L1_DATA_PUSHES, 0x04, 22),
	PMC_POWERPC_EVENT(L1_DATA_TOTAL_MISS, 0x04, 23),
	PMC_POWERPC_EVENT(VT2_FETCHES, 0x04, 24),
	PMC_POWERPC_EVENT(TAKEN_BRANCHES_PROCESSED, 0x04, 25),
	PMC_POWERPC_EVENT(BRANCH_FLUSHES, 0x04, 26),
	PMC_POWERPC_EVENT(SECOND_SPECULATIVE_BRANCH_BUFFER_RESOLVED_CORRECTLY, 0x04, 27),
	PMC_POWERPC_EVENT(THIRD_SPECULATION_BUFFER_ACTIVE, 0x04, 28),
	PMC_POWERPC_EVENT(BRANCH_UNIT_STALL_ON_CTR_DEPENDENCY, 0x04, 29),
	PMC_POWERPC_EVENT(FAST_BTIC_HIT, 0x04, 30),
	PMC_POWERPC_EVENT(BRANCH_LINK_STACK_MISPREDICTED, 0x04, 31),
	PMC_POWERPC_EVENT(CYCLES_THREE_INSTR_COMPLETED, 0x08, 14),
	PMC_POWERPC_EVENT(CYCLES_NO_INSTR_DISPATCHED, 0x08, 15),
	PMC_POWERPC_EVENT(GPR_ISSUE_QUEUE_ENTRIES_OVER_THRESHOLD, 0x08, 16),
	PMC_POWERPC_EVENT(GPR_ISSUE_QUEUE_STALLED, 0x08, 17),
	PMC_POWERPC_EVENT(IU1_INSTR_COMPLETED, 0x08, 18),
	PMC_POWERPC_EVENT(DSSALL_INSTR_COMPLETED, 0x08, 19),
	PMC_POWERPC_EVENT(TLBSYNC_INSTR_COMPLETED, 0x08, 20),
	PMC_POWERPC_EVENT(SYNC_INSTR_COMPLETED, 0x08, 21),
	PMC_POWERPC_EVENT(SS_SM_INSTR_PIECES, 0x08, 22),
	PMC_POWERPC_EVENT(DTLB_HW_SEARCH_CYCLES, 0x08, 23),
	PMC_POWERPC_EVENT(SNOOP_RETRIES, 0x08, 24),
	PMC_POWERPC_EVENT(SUCCESSFUL_STWCX, 0x08, 25),
	PMC_POWERPC_EVENT(DST_STREAM_3_CACHE_LINE_FETCHES, 0x08, 26),
	PMC_POWERPC_EVENT(THIRD_SPECULATIVE_BRANCH_BUFFER_RESOLVED_CORRECTLY, 0x08, 27),
	PMC_POWERPC_EVENT(MISPREDICTED_BRANCHES, 0x08, 28),
	PMC_POWERPC_EVENT(FOLDED_BRANCHES, 0x08, 29),
	PMC_POWERPC_EVENT(FP_STORE_DOUBLE_COMPLETES_IN_LSU, 0x08, 30),
	PMC_POWERPC_EVENT(L2_CACHE_HITS, 0x30, 2),
	PMC_POWERPC_EVENT(L3_CACHE_HITS, 0x30, 3),
	PMC_POWERPC_EVENT(L2_INSTR_CACHE_MISSES, 0x30, 4),
	PMC_POWERPC_EVENT(L3_INSTR_CACHE_MISSES, 0x30, 5),
	PMC_POWERPC_EVENT(L2_DATA_CACHE_MISSES, 0x30, 6),
	PMC_POWERPC_EVENT(L3_DATA_CACHE_MISSES, 0x30, 7),
	PMC_POWERPC_EVENT(L2_LOAD_HITS, 0x10, 8),
	PMC_POWERPC_EVENT(L2_STORE_HITS, 0x10, 9),
	PMC_POWERPC_EVENT(L3_LOAD_HITS, 0x10, 10),
	PMC_POWERPC_EVENT(L3_STORE_HITS, 0x10, 11),
	PMC_POWERPC_EVENT(L2_TOUCH_HITS, 0x30, 13),
	PMC_POWERPC_EVENT(L3_TOUCH_HITS, 0x30, 14),
	PMC_POWERPC_EVENT(SNOOP_RETRIES, 0x30, 15),
	PMC_POWERPC_EVENT(SNOOP_MODIFIED, 0x10, 16),
	PMC_POWERPC_EVENT(SNOOP_VALID, 0x10, 17),
	PMC_POWERPC_EVENT(INTERVENTION, 0x30, 18),
	PMC_POWERPC_EVENT(L2_CACHE_MISSES, 0x10, 19),
	PMC_POWERPC_EVENT(L3_CACHE_MISSES, 0x10, 20),
	PMC_POWERPC_EVENT(L2_CACHE_CASTOUTS, 0x20, 8),
	PMC_POWERPC_EVENT(L3_CACHE_CASTOUTS, 0x20, 9),
	PMC_POWERPC_EVENT(L2SQ_FULL_CYCLES, 0x20, 10),
	PMC_POWERPC_EVENT(L3SQ_FULL_CYCLES, 0x20, 11),
	PMC_POWERPC_EVENT(RAQ_FULL_CYCLES, 0x20, 16),
	PMC_POWERPC_EVENT(WAQ_FULL_CYCLES, 0x20, 17),
	PMC_POWERPC_EVENT(L1_EXTERNAL_INTERVENTIONS, 0x20, 19),
	PMC_POWERPC_EVENT(L2_EXTERNAL_INTERVENTIONS, 0x20, 20),
	PMC_POWERPC_EVENT(L3_EXTERNAL_INTERVENTIONS, 0x20, 21),
	PMC_POWERPC_EVENT(EXTERNAL_INTERVENTIONS, 0x20, 22),
	PMC_POWERPC_EVENT(EXTERNAL_PUSHES, 0x20, 23),
	PMC_POWERPC_EVENT(EXTERNAL_SNOOP_RETRY, 0x20, 24),
	PMC_POWERPC_EVENT(DTQ_FULL_CYCLES, 0x20, 25),
	PMC_POWERPC_EVENT(BUS_RETRY, 0x20, 26),
	PMC_POWERPC_EVENT(L2_VALID_REQUEST, 0x20, 27),
	PMC_POWERPC_EVENT(BORDQ_FULL, 0x20, 28),
	PMC_POWERPC_EVENT(BUS_TAS_FOR_READS, 0x20, 42),
	PMC_POWERPC_EVENT(BUS_TAS_FOR_WRITES, 0x20, 43),
	PMC_POWERPC_EVENT(BUS_READS_NOT_RETRIED, 0x20, 44),
	PMC_POWERPC_EVENT(BUS_WRITES_NOT_RETRIED, 0x20, 45),
	PMC_POWERPC_EVENT(BUS_READS_WRITES_NOT_RETRIED, 0x20, 46),
	PMC_POWERPC_EVENT(BUS_RETRY_DUE_TO_L1_RETRY, 0x20, 47),
	PMC_POWERPC_EVENT(BUS_RETRY_DUE_TO_PREVIOUS_ADJACENT, 0x20, 48),
	PMC_POWERPC_EVENT(BUS_RETRY_DUE_TO_COLLISION, 0x20, 49),
	PMC_POWERPC_EVENT(BUS_RETRY_DUE_TO_INTERVENTION_ORDERING, 0x20, 50),
	PMC_POWERPC_EVENT(SNOOP_REQUESTS, 0x20, 51),
	PMC_POWERPC_EVENT(PREFETCH_ENGINE_REQUEST, 0x20, 52),
	PMC_POWERPC_EVENT(PREFETCH_ENGINE_COLLISION_VS_LOAD, 0x20, 53),
	PMC_POWERPC_EVENT(PREFETCH_ENGINE_COLLISION_VS_STORE, 0x20, 54),
	PMC_POWERPC_EVENT(PREFETCH_ENGINE_COLLISION_VS_INSTR_FETCH, 0x20, 55),
	PMC_POWERPC_EVENT(PREFETCH_ENGINE_COLLISION_VS_LOAD_STORE_INSTR_FETCH, 0x20, 56),
	PMC_POWERPC_EVENT(PREFETCH_ENGINE_FULL, 0x20, 57)
};

static pmc_value_t
mpc7xxx_pmcn_read(unsigned int pmc)
{
	switch (pmc) {
		case 0:
			return mfspr(SPR_PMC1);
			break;
		case 1:
			return mfspr(SPR_PMC2);
			break;
		case 2:
			return mfspr(SPR_PMC3);
			break;
		case 3:
			return mfspr(SPR_PMC4);
			break;
		case 4:
			return mfspr(SPR_PMC5);
			break;
		case 5:
			return mfspr(SPR_PMC6);
		default:
			panic("Invalid PMC number: %d\n", pmc);
	}
}

static void
mpc7xxx_pmcn_write(unsigned int pmc, uint32_t val)
{
	switch (pmc) {
		case 0:
			mtspr(SPR_PMC1, val);
			break;
		case 1:
			mtspr(SPR_PMC2, val);
			break;
		case 2:
			mtspr(SPR_PMC3, val);
			break;
		case 3:
			mtspr(SPR_PMC4, val);
			break;
		case 4:
			mtspr(SPR_PMC5, val);
			break;
		case 5:
			mtspr(SPR_PMC6, val);
			break;
		default:
			panic("Invalid PMC number: %d\n", pmc);
	}
}

static int
mpc7xxx_read_pmc(int cpu, int ri, pmc_value_t *v)
{
	struct pmc *pm;
	pmc_value_t tmp;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[powerpc,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < MPC7XXX_MAX_PMCS,
	    ("[powerpc,%d] illegal row index %d", __LINE__, ri));

	pm  = powerpc_pcpu[cpu]->pc_ppcpmcs[ri].phw_pmc;
	KASSERT(pm,
	    ("[core,%d] cpu %d ri %d pmc not configured", __LINE__, cpu,
		ri));

	tmp = mpc7xxx_pmcn_read(ri);
	PMCDBG2(MDP,REA,2,"ppc-read id=%d -> %jd", ri, tmp);
	if (PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
		*v = POWERPC_PERFCTR_VALUE_TO_RELOAD_COUNT(tmp);
	else
		*v = tmp;

	return 0;
}

static int
mpc7xxx_write_pmc(int cpu, int ri, pmc_value_t v)
{
	struct pmc *pm;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[powerpc,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < MPC7XXX_MAX_PMCS,
	    ("[powerpc,%d] illegal row-index %d", __LINE__, ri));

	pm  = powerpc_pcpu[cpu]->pc_ppcpmcs[ri].phw_pmc;

	if (PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
		v = POWERPC_RELOAD_COUNT_TO_PERFCTR_VALUE(v);
	
	PMCDBG3(MDP,WRI,1,"powerpc-write cpu=%d ri=%d v=%jx", cpu, ri, v);

	mpc7xxx_pmcn_write(ri, v);

	return 0;
}

static int
mpc7xxx_config_pmc(int cpu, int ri, struct pmc *pm)
{
	struct pmc_hw *phw;

	PMCDBG3(MDP,CFG,1, "cpu=%d ri=%d pm=%p", cpu, ri, pm);

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[powerpc,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < MPC7XXX_MAX_PMCS,
	    ("[powerpc,%d] illegal row-index %d", __LINE__, ri));

	phw = &powerpc_pcpu[cpu]->pc_ppcpmcs[ri];

	KASSERT(pm == NULL || phw->phw_pmc == NULL,
	    ("[powerpc,%d] pm=%p phw->pm=%p hwpmc not unconfigured",
	    __LINE__, pm, phw->phw_pmc));

	phw->phw_pmc = pm;

	return 0;
}

static int
mpc7xxx_start_pmc(int cpu, int ri)
{
	uint32_t config;
        struct pmc *pm;
        struct pmc_hw *phw;
	register_t pmc_mmcr;

	phw    = &powerpc_pcpu[cpu]->pc_ppcpmcs[ri];
	pm     = phw->phw_pmc;
	config = pm->pm_md.pm_powerpc.pm_powerpc_evsel & ~POWERPC_PMC_ENABLE;

	/* Enable the PMC. */
	switch (ri) {
	case 0:
		pmc_mmcr = mfspr(SPR_MMCR0);
		pmc_mmcr = PPC_SET_PMC1SEL(pmc_mmcr, config);
		mtspr(SPR_MMCR0, pmc_mmcr);
		break;
	case 1:
		pmc_mmcr = mfspr(SPR_MMCR0);
		pmc_mmcr = PPC_SET_PMC2SEL(pmc_mmcr, config);
		mtspr(SPR_MMCR0, pmc_mmcr);
		break;
	case 2:
		pmc_mmcr = mfspr(SPR_MMCR1);
		pmc_mmcr = PPC_SET_PMC3SEL(pmc_mmcr, config);
		mtspr(SPR_MMCR1, pmc_mmcr);
		break;
	case 3:
		pmc_mmcr = mfspr(SPR_MMCR0);
		pmc_mmcr = PPC_SET_PMC4SEL(pmc_mmcr, config);
		mtspr(SPR_MMCR0, pmc_mmcr);
		break;
	case 4:
		pmc_mmcr = mfspr(SPR_MMCR1);
		pmc_mmcr = PPC_SET_PMC5SEL(pmc_mmcr, config);
		mtspr(SPR_MMCR1, pmc_mmcr);
		break;
	case 5:
		pmc_mmcr = mfspr(SPR_MMCR1);
		pmc_mmcr = PPC_SET_PMC6SEL(pmc_mmcr, config);
		mtspr(SPR_MMCR1, pmc_mmcr);
		break;
	default:
		break;
	}
	
	/* The mask is inverted (enable is 1) compared to the flags in MMCR0, which
	 * are Freeze flags.
	 */
	config = ~pm->pm_md.pm_powerpc.pm_powerpc_evsel & POWERPC_PMC_ENABLE;

	pmc_mmcr = mfspr(SPR_MMCR0);
	pmc_mmcr &= ~SPR_MMCR0_FC;
	pmc_mmcr |= config;
	mtspr(SPR_MMCR0, pmc_mmcr);

	return 0;
}

static int
mpc7xxx_stop_pmc(int cpu, int ri)
{
        struct pmc *pm;
        struct pmc_hw *phw;
        register_t pmc_mmcr;

	phw    = &powerpc_pcpu[cpu]->pc_ppcpmcs[ri];
	pm     = phw->phw_pmc;

	/*
	 * Disable the PMCs.
	 */
	switch (ri) {
	case 0:
		pmc_mmcr = mfspr(SPR_MMCR0);
		pmc_mmcr = PPC_SET_PMC1SEL(pmc_mmcr, 0);
		mtspr(SPR_MMCR0, pmc_mmcr);
		break;
	case 1:
		pmc_mmcr = mfspr(SPR_MMCR0);
		pmc_mmcr = PPC_SET_PMC2SEL(pmc_mmcr, 0);
		mtspr(SPR_MMCR0, pmc_mmcr);
		break;
	case 2:
		pmc_mmcr = mfspr(SPR_MMCR1);
		pmc_mmcr = PPC_SET_PMC3SEL(pmc_mmcr, 0);
		mtspr(SPR_MMCR1, pmc_mmcr);
		break;
	case 3:
		pmc_mmcr = mfspr(SPR_MMCR0);
		pmc_mmcr = PPC_SET_PMC4SEL(pmc_mmcr, 0);
		mtspr(SPR_MMCR0, pmc_mmcr);
		break;
	case 4:
		pmc_mmcr = mfspr(SPR_MMCR1);
		pmc_mmcr = PPC_SET_PMC5SEL(pmc_mmcr, 0);
		mtspr(SPR_MMCR1, pmc_mmcr);
		break;
	case 5:
		pmc_mmcr = mfspr(SPR_MMCR1);
		pmc_mmcr = PPC_SET_PMC6SEL(pmc_mmcr, 0);
		mtspr(SPR_MMCR1, pmc_mmcr);
		break;
	default:
		break;
	}
	return 0;
}

static int
mpc7xxx_pcpu_init(struct pmc_mdep *md, int cpu)
{
	int first_ri, i;
	struct pmc_cpu *pc;
	struct powerpc_cpu *pac;
	struct pmc_hw  *phw;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[powerpc,%d] wrong cpu number %d", __LINE__, cpu));
	PMCDBG1(MDP,INI,1,"powerpc-init cpu=%d", cpu);

	powerpc_pcpu[cpu] = pac = malloc(sizeof(struct powerpc_cpu), M_PMC,
	    M_WAITOK|M_ZERO);
	pac->pc_ppcpmcs = malloc(sizeof(struct pmc_hw) * MPC7XXX_MAX_PMCS,
	    M_PMC, M_WAITOK|M_ZERO);
	pac->pc_class = PMC_CLASS_PPC7450;
	pc = pmc_pcpu[cpu];
	first_ri = md->pmd_classdep[PMC_MDEP_CLASS_INDEX_POWERPC].pcd_ri;
	KASSERT(pc != NULL, ("[powerpc,%d] NULL per-cpu pointer", __LINE__));

	for (i = 0, phw = pac->pc_ppcpmcs; i < MPC7XXX_MAX_PMCS; i++, phw++) {
		phw->phw_state    = PMC_PHW_FLAG_IS_ENABLED |
		    PMC_PHW_CPU_TO_STATE(cpu) | PMC_PHW_INDEX_TO_STATE(i);
		phw->phw_pmc      = NULL;
		pc->pc_hwpmcs[i + first_ri] = phw;
	}

	/* Clear the MMCRs, and set FC, to disable all PMCs. */
	mtspr(SPR_MMCR0, SPR_MMCR0_FC | SPR_MMCR0_PMXE |
	    SPR_MMCR0_FCECE | SPR_MMCR0_PMC1CE | SPR_MMCR0_PMCNCE);
	mtspr(SPR_MMCR1, 0);

	return 0;
}

static int
mpc7xxx_pcpu_fini(struct pmc_mdep *md, int cpu)
{
	uint32_t mmcr0 = mfspr(SPR_MMCR0);

	mtmsr(mfmsr() & ~PSL_PMM);
	mmcr0 |= SPR_MMCR0_FC;
	mtspr(SPR_MMCR0, mmcr0);

	free(powerpc_pcpu[cpu]->pc_ppcpmcs, M_PMC);
	free(powerpc_pcpu[cpu], M_PMC);

	return 0;
}

static int
mpc7xxx_allocate_pmc(int cpu, int ri, struct pmc *pm,
  const struct pmc_op_pmcallocate *a)
{
	enum pmc_event pe;
	uint32_t caps, config, counter;
	int i;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[powerpc,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < MPC7XXX_MAX_PMCS,
	    ("[powerpc,%d] illegal row index %d", __LINE__, ri));

	caps = a->pm_caps;

	pe = a->pm_ev;
	for (i = 0; i < nitems(mpc7xxx_event_codes); i++) {
		if (mpc7xxx_event_codes[i].pe_ev == pe) {
			config = mpc7xxx_event_codes[i].pe_code;
			counter =  mpc7xxx_event_codes[i].pe_counter_mask;
			break;
		}
	}
	if (i == nitems(mpc7xxx_event_codes))
		return (EINVAL);

	if ((counter & (1 << ri)) == 0)
		return (EINVAL);

	if (caps & PMC_CAP_SYSTEM)
		config |= POWERPC_PMC_KERNEL_ENABLE;
	if (caps & PMC_CAP_USER)
		config |= POWERPC_PMC_USER_ENABLE;
	if ((caps & (PMC_CAP_USER | PMC_CAP_SYSTEM)) == 0)
		config |= POWERPC_PMC_ENABLE;

	pm->pm_md.pm_powerpc.pm_powerpc_evsel = config;

	PMCDBG2(MDP,ALL,2,"powerpc-allocate ri=%d -> config=0x%x", ri, config);

	return 0;
}

static int
mpc7xxx_release_pmc(int cpu, int ri, struct pmc *pmc)
{
	struct pmc_hw *phw;

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[powerpc,%d] illegal CPU value %d", __LINE__, cpu));
	KASSERT(ri >= 0 && ri < MPC7XXX_MAX_PMCS,
	    ("[powerpc,%d] illegal row-index %d", __LINE__, ri));

	phw = &powerpc_pcpu[cpu]->pc_ppcpmcs[ri];
	KASSERT(phw->phw_pmc == NULL,
	    ("[powerpc,%d] PHW pmc %p non-NULL", __LINE__, phw->phw_pmc));

	return 0;
}

static int
mpc7xxx_intr(struct trapframe *tf)
{
	int i, error, retval, cpu;
	uint32_t config;
	struct pmc *pm;
	struct powerpc_cpu *pac;

	cpu = curcpu;
	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[powerpc,%d] out of range CPU %d", __LINE__, cpu));

	PMCDBG3(MDP,INT,1, "cpu=%d tf=%p um=%d", cpu, (void *) tf,
	    TRAPF_USERMODE(tf));

	retval = 0;

	pac = powerpc_pcpu[cpu];

	config  = mfspr(SPR_MMCR0) & ~SPR_MMCR0_FC;

	/*
	 * look for all PMCs that have interrupted:
	 * - look for a running, sampling PMC which has overflowed
	 *   and which has a valid 'struct pmc' association
	 *
	 * If found, we call a helper to process the interrupt.
	 */

	for (i = 0; i < MPC7XXX_MAX_PMCS; i++) {
		if ((pm = pac->pc_ppcpmcs[i].phw_pmc) == NULL ||
		    !PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm))) {
			continue;
		}

		if (!MPC7XXX_PMC_HAS_OVERFLOWED(i))
			continue;

		retval = 1;	/* Found an interrupting PMC. */

		if (pm->pm_state != PMC_STATE_RUNNING)
			continue;

		/* Stop the counter if logging fails. */
		error = pmc_process_interrupt(PMC_HR, pm, tf);
		if (error != 0)
			mpc7xxx_stop_pmc(cpu, i);

		/* reload count. */
		mpc7xxx_write_pmc(cpu, i, pm->pm_sc.pm_reloadcount);
	}
	if (retval)
		counter_u64_add(pmc_stats.pm_intr_processed, 1);
	else
		counter_u64_add(pmc_stats.pm_intr_ignored, 1);

	/* Re-enable PERF exceptions. */
	if (retval)
		mtspr(SPR_MMCR0, config | SPR_MMCR0_PMXE);

	return (retval);
}

int
pmc_mpc7xxx_initialize(struct pmc_mdep *pmc_mdep)
{
	struct pmc_classdep *pcd;

	pmc_mdep->pmd_cputype = PMC_CPU_PPC_7450;

	pcd = &pmc_mdep->pmd_classdep[PMC_MDEP_CLASS_INDEX_POWERPC];
	pcd->pcd_caps  = POWERPC_PMC_CAPS;
	pcd->pcd_class = PMC_CLASS_PPC7450;
	pcd->pcd_num   = MPC7XXX_MAX_PMCS;
	pcd->pcd_ri    = pmc_mdep->pmd_npmc;
	pcd->pcd_width = 32;	/* All PMCs, even in ppc970, are 32-bit */

	pcd->pcd_allocate_pmc   = mpc7xxx_allocate_pmc;
	pcd->pcd_config_pmc     = mpc7xxx_config_pmc;
	pcd->pcd_pcpu_fini      = mpc7xxx_pcpu_fini;
	pcd->pcd_pcpu_init      = mpc7xxx_pcpu_init;
	pcd->pcd_describe       = powerpc_describe;
	pcd->pcd_get_config     = powerpc_get_config;
	pcd->pcd_read_pmc       = mpc7xxx_read_pmc;
	pcd->pcd_release_pmc    = mpc7xxx_release_pmc;
	pcd->pcd_start_pmc      = mpc7xxx_start_pmc;
	pcd->pcd_stop_pmc       = mpc7xxx_stop_pmc;
 	pcd->pcd_write_pmc      = mpc7xxx_write_pmc;

	pmc_mdep->pmd_npmc   += MPC7XXX_MAX_PMCS;
	pmc_mdep->pmd_intr   =  mpc7xxx_intr;

	return (0);
}
