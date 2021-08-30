/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2014, Michael Ellerman, IBM Corp.
 */

#ifndef _SELFTESTS_POWERPC_PMU_EBB_EBB_H
#define _SELFTESTS_POWERPC_PMU_EBB_EBB_H

#include "../event.h"
#include "../lib.h"
#include "trace.h"
#include "reg.h"

#define PMC_INDEX(pmc)	((pmc)-1)

#define NUM_PMC_VALUES	128

struct ebb_state
{
	struct {
		u64 pmc_count[6];
		volatile int ebb_count;
		int spurious;
		int negative;
		int no_overflow;
	} stats;

	bool pmc_enable[6];
	struct trace_buffer *trace;
};

extern struct ebb_state ebb_state;

#define COUNTER_OVERFLOW 0x80000000ull

static inline uint32_t pmc_sample_period(uint32_t value)
{
	return COUNTER_OVERFLOW - value;
}

static inline void ebb_enable_pmc_counting(int pmc)
{
	ebb_state.pmc_enable[PMC_INDEX(pmc)] = true;
}

bool ebb_check_count(int pmc, u64 sample_period, int fudge);
void event_leader_ebb_init(struct event *e);
void event_ebb_init(struct event *e);
void event_bhrb_init(struct event *e, unsigned ifm);
void setup_ebb_handler(void (*callee)(void));
void standard_ebb_callee(void);
int ebb_event_enable(struct event *e);
void ebb_global_enable(void);
void ebb_global_disable(void);
bool ebb_is_supported(void);
void ebb_freeze_pmcs(void);
void ebb_unfreeze_pmcs(void);
int count_pmc(int pmc, uint32_t sample_period);
void dump_ebb_state(void);
void dump_summary_ebb_state(void);
void dump_ebb_hw_state(void);
void clear_ebb_stats(void);
void write_pmc(int pmc, u64 value);
u64 read_pmc(int pmc);
void reset_ebb_with_clear_mask(unsigned long mmcr0_clear_mask);
void reset_ebb(void);
int ebb_check_mmcr0(void);

extern u64 sample_period;

int core_busy_loop(void);
int ebb_child(union pipe read_pipe, union pipe write_pipe);
int catch_sigill(void (*func)(void));
void write_pmc1(void);

#endif /* _SELFTESTS_POWERPC_PMU_EBB_EBB_H */
