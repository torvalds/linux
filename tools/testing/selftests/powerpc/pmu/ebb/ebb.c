/*
 * Copyright 2014, Michael Ellerman, IBM Corp.
 * Licensed under GPLv2.
 */

#define _GNU_SOURCE	/* For CPU_ZERO etc. */

#include <sched.h>
#include <sys/wait.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/auxvec.h>

#include "trace.h"
#include "reg.h"
#include "ebb.h"


void (*ebb_user_func)(void);

void ebb_hook(void)
{
	if (ebb_user_func)
		ebb_user_func();
}

struct ebb_state ebb_state;

u64 sample_period = 0x40000000ull;

void reset_ebb_with_clear_mask(unsigned long mmcr0_clear_mask)
{
	u64 val;

	/* 2) clear MMCR0[PMAO] - docs say BESCR[PMEO] should do this */
	/* 3) set MMCR0[PMAE]	- docs say BESCR[PME] should do this */
	val = mfspr(SPRN_MMCR0);
	mtspr(SPRN_MMCR0, (val & ~mmcr0_clear_mask) | MMCR0_PMAE);

	/* 4) clear BESCR[PMEO] */
	mtspr(SPRN_BESCRR, BESCR_PMEO);

	/* 5) set BESCR[PME] */
	mtspr(SPRN_BESCRS, BESCR_PME);

	/* 6) rfebb 1 - done in our caller */
}

void reset_ebb(void)
{
	reset_ebb_with_clear_mask(MMCR0_PMAO | MMCR0_FC);
}

/* Called outside of the EBB handler to check MMCR0 is sane */
int ebb_check_mmcr0(void)
{
	u64 val;

	val = mfspr(SPRN_MMCR0);
	if ((val & (MMCR0_FC | MMCR0_PMAO)) == MMCR0_FC) {
		/* It's OK if we see FC & PMAO, but not FC by itself */
		printf("Outside of loop, only FC set 0x%llx\n", val);
		return 1;
	}

	return 0;
}

bool ebb_check_count(int pmc, u64 sample_period, int fudge)
{
	u64 count, upper, lower;

	count = ebb_state.stats.pmc_count[PMC_INDEX(pmc)];

	lower = ebb_state.stats.ebb_count * (sample_period - fudge);

	if (count < lower) {
		printf("PMC%d count (0x%llx) below lower limit 0x%llx (-0x%llx)\n",
			pmc, count, lower, lower - count);
		return false;
	}

	upper = ebb_state.stats.ebb_count * (sample_period + fudge);

	if (count > upper) {
		printf("PMC%d count (0x%llx) above upper limit 0x%llx (+0x%llx)\n",
			pmc, count, upper, count - upper);
		return false;
	}

	printf("PMC%d count (0x%llx) is between 0x%llx and 0x%llx delta +0x%llx/-0x%llx\n",
		pmc, count, lower, upper, count - lower, upper - count);

	return true;
}

void standard_ebb_callee(void)
{
	int found, i;
	u64 val;

	val = mfspr(SPRN_BESCR);
	if (!(val & BESCR_PMEO)) {
		ebb_state.stats.spurious++;
		goto out;
	}

	ebb_state.stats.ebb_count++;
	trace_log_counter(ebb_state.trace, ebb_state.stats.ebb_count);

	val = mfspr(SPRN_MMCR0);
	trace_log_reg(ebb_state.trace, SPRN_MMCR0, val);

	found = 0;
	for (i = 1; i <= 6; i++) {
		if (ebb_state.pmc_enable[PMC_INDEX(i)])
			found += count_pmc(i, sample_period);
	}

	if (!found)
		ebb_state.stats.no_overflow++;

out:
	reset_ebb();
}

extern void ebb_handler(void);

void setup_ebb_handler(void (*callee)(void))
{
	u64 entry;

#if defined(_CALL_ELF) && _CALL_ELF == 2
	entry = (u64)ebb_handler;
#else
	struct opd
	{
	    u64 entry;
	    u64 toc;
	} *opd;

	opd = (struct opd *)ebb_handler;
	entry = opd->entry;
#endif
	printf("EBB Handler is at %#llx\n", entry);

	ebb_user_func = callee;

	/* Ensure ebb_user_func is set before we set the handler */
	mb();
	mtspr(SPRN_EBBHR, entry);

	/* Make sure the handler is set before we return */
	mb();
}

void clear_ebb_stats(void)
{
	memset(&ebb_state.stats, 0, sizeof(ebb_state.stats));
}

void dump_summary_ebb_state(void)
{
	printf("ebb_state:\n"			\
	       "  ebb_count    = %d\n"		\
	       "  spurious     = %d\n"		\
	       "  negative     = %d\n"		\
	       "  no_overflow  = %d\n"		\
	       "  pmc[1] count = 0x%llx\n"	\
	       "  pmc[2] count = 0x%llx\n"	\
	       "  pmc[3] count = 0x%llx\n"	\
	       "  pmc[4] count = 0x%llx\n"	\
	       "  pmc[5] count = 0x%llx\n"	\
	       "  pmc[6] count = 0x%llx\n",
		ebb_state.stats.ebb_count, ebb_state.stats.spurious,
		ebb_state.stats.negative, ebb_state.stats.no_overflow,
		ebb_state.stats.pmc_count[0], ebb_state.stats.pmc_count[1],
		ebb_state.stats.pmc_count[2], ebb_state.stats.pmc_count[3],
		ebb_state.stats.pmc_count[4], ebb_state.stats.pmc_count[5]);
}

static char *decode_mmcr0(u32 value)
{
	static char buf[16];

	buf[0] = '\0';

	if (value & (1 << 31))
		strcat(buf, "FC ");
	if (value & (1 << 26))
		strcat(buf, "PMAE ");
	if (value & (1 << 7))
		strcat(buf, "PMAO ");

	return buf;
}

static char *decode_bescr(u64 value)
{
	static char buf[16];

	buf[0] = '\0';

	if (value & (1ull << 63))
		strcat(buf, "GE ");
	if (value & (1ull << 32))
		strcat(buf, "PMAE ");
	if (value & 1)
		strcat(buf, "PMAO ");

	return buf;
}

void dump_ebb_hw_state(void)
{
	u64 bescr;
	u32 mmcr0;

	mmcr0 = mfspr(SPRN_MMCR0);
	bescr = mfspr(SPRN_BESCR);

	printf("HW state:\n"		\
	       "MMCR0 0x%016x %s\n"	\
	       "MMCR2 0x%016lx\n"	\
	       "EBBHR 0x%016lx\n"	\
	       "BESCR 0x%016llx %s\n"	\
	       "PMC1  0x%016lx\n"	\
	       "PMC2  0x%016lx\n"	\
	       "PMC3  0x%016lx\n"	\
	       "PMC4  0x%016lx\n"	\
	       "PMC5  0x%016lx\n"	\
	       "PMC6  0x%016lx\n"	\
	       "SIAR  0x%016lx\n",
	       mmcr0, decode_mmcr0(mmcr0), mfspr(SPRN_MMCR2),
	       mfspr(SPRN_EBBHR), bescr, decode_bescr(bescr),
	       mfspr(SPRN_PMC1), mfspr(SPRN_PMC2), mfspr(SPRN_PMC3),
	       mfspr(SPRN_PMC4), mfspr(SPRN_PMC5), mfspr(SPRN_PMC6),
	       mfspr(SPRN_SIAR));
}

void dump_ebb_state(void)
{
	dump_summary_ebb_state();

	dump_ebb_hw_state();

	trace_buffer_print(ebb_state.trace);
}

int count_pmc(int pmc, uint32_t sample_period)
{
	uint32_t start_value;
	u64 val;

	/* 0) Read PMC */
	start_value = pmc_sample_period(sample_period);

	val = read_pmc(pmc);
	if (val < start_value)
		ebb_state.stats.negative++;
	else
		ebb_state.stats.pmc_count[PMC_INDEX(pmc)] += val - start_value;

	trace_log_reg(ebb_state.trace, SPRN_PMC1 + pmc - 1, val);

	/* 1) Reset PMC */
	write_pmc(pmc, start_value);

	/* Report if we overflowed */
	return val >= COUNTER_OVERFLOW;
}

int ebb_event_enable(struct event *e)
{
	int rc;

	/* Ensure any SPR writes are ordered vs us */
	mb();

	rc = ioctl(e->fd, PERF_EVENT_IOC_ENABLE);
	if (rc)
		return rc;

	rc = event_read(e);

	/* Ditto */
	mb();

	return rc;
}

void ebb_freeze_pmcs(void)
{
	mtspr(SPRN_MMCR0, mfspr(SPRN_MMCR0) | MMCR0_FC);
	mb();
}

void ebb_unfreeze_pmcs(void)
{
	/* Unfreeze counters */
	mtspr(SPRN_MMCR0, mfspr(SPRN_MMCR0) & ~MMCR0_FC);
	mb();
}

void ebb_global_enable(void)
{
	/* Enable EBBs globally and PMU EBBs */
	mtspr(SPRN_BESCR, 0x8000000100000000ull);
	mb();
}

void ebb_global_disable(void)
{
	/* Disable EBBs & freeze counters, events are still scheduled */
	mtspr(SPRN_BESCRR, BESCR_PME);
	mb();
}

bool ebb_is_supported(void)
{
#ifdef PPC_FEATURE2_EBB
	/* EBB requires at least POWER8 */
	return ((long)get_auxv_entry(AT_HWCAP2) & PPC_FEATURE2_EBB);
#else
	return false;
#endif
}

void event_ebb_init(struct event *e)
{
	e->attr.config |= (1ull << 63);
}

void event_bhrb_init(struct event *e, unsigned ifm)
{
	e->attr.config |= (1ull << 62) | ((u64)ifm << 60);
}

void event_leader_ebb_init(struct event *e)
{
	event_ebb_init(e);

	e->attr.exclusive = 1;
	e->attr.pinned = 1;
}

int ebb_child(union pipe read_pipe, union pipe write_pipe)
{
	struct event event;
	uint64_t val;

	FAIL_IF(wait_for_parent(read_pipe));

	event_init_named(&event, 0x1001e, "cycles");
	event_leader_ebb_init(&event);

	event.attr.exclude_kernel = 1;
	event.attr.exclude_hv = 1;
	event.attr.exclude_idle = 1;

	FAIL_IF(event_open(&event));

	ebb_enable_pmc_counting(1);
	setup_ebb_handler(standard_ebb_callee);
	ebb_global_enable();

	FAIL_IF(event_enable(&event));

	if (event_read(&event)) {
		/*
		 * Some tests expect to fail here, so don't report an error on
		 * this line, and return a distinguisable error code. Tell the
		 * parent an error happened.
		 */
		notify_parent_of_error(write_pipe);
		return 2;
	}

	mtspr(SPRN_PMC1, pmc_sample_period(sample_period));

	FAIL_IF(notify_parent(write_pipe));
	FAIL_IF(wait_for_parent(read_pipe));
	FAIL_IF(notify_parent(write_pipe));

	while (ebb_state.stats.ebb_count < 20) {
		FAIL_IF(core_busy_loop());

		/* To try and hit SIGILL case */
		val  = mfspr(SPRN_MMCRA);
		val |= mfspr(SPRN_MMCR2);
		val |= mfspr(SPRN_MMCR0);
	}

	ebb_global_disable();
	ebb_freeze_pmcs();

	count_pmc(1, sample_period);

	dump_ebb_state();

	event_close(&event);

	FAIL_IF(ebb_state.stats.ebb_count == 0);

	return 0;
}

static jmp_buf setjmp_env;

static void sigill_handler(int signal)
{
	printf("Took sigill\n");
	longjmp(setjmp_env, 1);
}

static struct sigaction sigill_action = {
	.sa_handler = sigill_handler,
};

int catch_sigill(void (*func)(void))
{
	if (sigaction(SIGILL, &sigill_action, NULL)) {
		perror("sigaction");
		return 1;
	}

	if (setjmp(setjmp_env) == 0) {
		func();
		return 1;
	}

	return 0;
}

void write_pmc1(void)
{
	mtspr(SPRN_PMC1, 0);
}

void write_pmc(int pmc, u64 value)
{
	switch (pmc) {
		case 1: mtspr(SPRN_PMC1, value); break;
		case 2: mtspr(SPRN_PMC2, value); break;
		case 3: mtspr(SPRN_PMC3, value); break;
		case 4: mtspr(SPRN_PMC4, value); break;
		case 5: mtspr(SPRN_PMC5, value); break;
		case 6: mtspr(SPRN_PMC6, value); break;
	}
}

u64 read_pmc(int pmc)
{
	switch (pmc) {
		case 1: return mfspr(SPRN_PMC1);
		case 2: return mfspr(SPRN_PMC2);
		case 3: return mfspr(SPRN_PMC3);
		case 4: return mfspr(SPRN_PMC4);
		case 5: return mfspr(SPRN_PMC5);
		case 6: return mfspr(SPRN_PMC6);
	}

	return 0;
}

static void term_handler(int signal)
{
	dump_summary_ebb_state();
	dump_ebb_hw_state();
	abort();
}

struct sigaction term_action = {
	.sa_handler = term_handler,
};

static void __attribute__((constructor)) ebb_init(void)
{
	clear_ebb_stats();

	if (sigaction(SIGTERM, &term_action, NULL))
		perror("sigaction");

	ebb_state.trace = trace_buffer_allocate(1 * 1024 * 1024);
}
