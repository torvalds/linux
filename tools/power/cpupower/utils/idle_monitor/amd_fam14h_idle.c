// SPDX-License-Identifier: GPL-2.0-only
/*
 *  (C) 2010,2011      Thomas Renninger <trenn@suse.de>, Novell Inc.
 *
 *  PCI initialization based on example code from:
 *  Andreas Herrmann <andreas.herrmann3@amd.com>
 */

#if defined(__i386__) || defined(__x86_64__)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <string.h>

#include <pci/pci.h>

#include "idle_monitor/cpupower-monitor.h"
#include "helpers/helpers.h"

#define PCI_NON_PC0_OFFSET	0xb0
#define PCI_PC1_OFFSET		0xb4
#define PCI_PC6_OFFSET		0xb8

#define PCI_MONITOR_ENABLE_REG  0xe0

#define PCI_NON_PC0_ENABLE_BIT	0
#define PCI_PC1_ENABLE_BIT	1
#define PCI_PC6_ENABLE_BIT	2

#define PCI_NBP1_STAT_OFFSET	0x98
#define PCI_NBP1_ACTIVE_BIT	2
#define PCI_NBP1_ENTERED_BIT	1

#define PCI_NBP1_CAP_OFFSET	0x90
#define PCI_NBP1_CAPABLE_BIT    31

#define OVERFLOW_MS		343597 /* 32 bit register filled at 12500 HZ
					  (1 tick per 80ns) */

enum amd_fam14h_states {NON_PC0 = 0, PC1, PC6, NBP1,
			AMD_FAM14H_STATE_NUM};

static int fam14h_get_count_percent(unsigned int self_id, double *percent,
				    unsigned int cpu);
static int fam14h_nbp1_count(unsigned int id, unsigned long long *count,
			     unsigned int cpu);

static cstate_t amd_fam14h_cstates[AMD_FAM14H_STATE_NUM] = {
	{
		.name			= "!PC0",
		.desc			= N_("Package in sleep state (PC1 or deeper)"),
		.id			= NON_PC0,
		.range			= RANGE_PACKAGE,
		.get_count_percent	= fam14h_get_count_percent,
	},
	{
		.name			= "PC1",
		.desc			= N_("Processor Package C1"),
		.id			= PC1,
		.range			= RANGE_PACKAGE,
		.get_count_percent	= fam14h_get_count_percent,
	},
	{
		.name			= "PC6",
		.desc			= N_("Processor Package C6"),
		.id			= PC6,
		.range			= RANGE_PACKAGE,
		.get_count_percent	= fam14h_get_count_percent,
	},
	{
		.name			= "NBP1",
		.desc			= N_("North Bridge P1 boolean counter (returns 0 or 1)"),
		.id			= NBP1,
		.range			= RANGE_PACKAGE,
		.get_count		= fam14h_nbp1_count,
	},
};

static struct pci_access *pci_acc;
static struct pci_dev *amd_fam14h_pci_dev;
static int nbp1_entered;

static struct timespec start_time;
static unsigned long long timediff;

#ifdef DEBUG
struct timespec dbg_time;
long dbg_timediff;
#endif

static unsigned long long *previous_count[AMD_FAM14H_STATE_NUM];
static unsigned long long *current_count[AMD_FAM14H_STATE_NUM];

static int amd_fam14h_get_pci_info(struct cstate *state,
				   unsigned int *pci_offset,
				   unsigned int *enable_bit,
				   unsigned int cpu)
{
	switch (state->id) {
	case NON_PC0:
		*enable_bit = PCI_NON_PC0_ENABLE_BIT;
		*pci_offset = PCI_NON_PC0_OFFSET;
		break;
	case PC1:
		*enable_bit = PCI_PC1_ENABLE_BIT;
		*pci_offset = PCI_PC1_OFFSET;
		break;
	case PC6:
		*enable_bit = PCI_PC6_ENABLE_BIT;
		*pci_offset = PCI_PC6_OFFSET;
		break;
	case NBP1:
		*enable_bit = PCI_NBP1_ENTERED_BIT;
		*pci_offset = PCI_NBP1_STAT_OFFSET;
		break;
	default:
		return -1;
	}
	return 0;
}

static int amd_fam14h_init(cstate_t *state, unsigned int cpu)
{
	int enable_bit, pci_offset, ret;
	uint32_t val;

	ret = amd_fam14h_get_pci_info(state, &pci_offset, &enable_bit, cpu);
	if (ret)
		return ret;

	/* NBP1 needs extra treating -> write 1 to D18F6x98 bit 1 for init */
	if (state->id == NBP1) {
		val = pci_read_long(amd_fam14h_pci_dev, pci_offset);
		val |= 1 << enable_bit;
		val = pci_write_long(amd_fam14h_pci_dev, pci_offset, val);
		return ret;
	}

	/* Enable monitor */
	val = pci_read_long(amd_fam14h_pci_dev, PCI_MONITOR_ENABLE_REG);
	dprint("Init %s: read at offset: 0x%x val: %u\n", state->name,
	       PCI_MONITOR_ENABLE_REG, (unsigned int) val);
	val |= 1 << enable_bit;
	pci_write_long(amd_fam14h_pci_dev, PCI_MONITOR_ENABLE_REG, val);

	dprint("Init %s: offset: 0x%x enable_bit: %d - val: %u (%u)\n",
	       state->name, PCI_MONITOR_ENABLE_REG, enable_bit,
	       (unsigned int) val, cpu);

	/* Set counter to zero */
	pci_write_long(amd_fam14h_pci_dev, pci_offset, 0);
	previous_count[state->id][cpu] = 0;

	return 0;
}

static int amd_fam14h_disable(cstate_t *state, unsigned int cpu)
{
	int enable_bit, pci_offset, ret;
	uint32_t val;

	ret = amd_fam14h_get_pci_info(state, &pci_offset, &enable_bit, cpu);
	if (ret)
		return ret;

	val = pci_read_long(amd_fam14h_pci_dev, pci_offset);
	dprint("%s: offset: 0x%x %u\n", state->name, pci_offset, val);
	if (state->id == NBP1) {
		/* was the bit whether NBP1 got entered set? */
		nbp1_entered = (val & (1 << PCI_NBP1_ACTIVE_BIT)) |
			(val & (1 << PCI_NBP1_ENTERED_BIT));

		dprint("NBP1 was %sentered - 0x%x - enable_bit: "
		       "%d - pci_offset: 0x%x\n",
		       nbp1_entered ? "" : "not ",
		       val, enable_bit, pci_offset);
		return ret;
	}
	current_count[state->id][cpu] = val;

	dprint("%s: Current -  %llu (%u)\n", state->name,
	       current_count[state->id][cpu], cpu);
	dprint("%s: Previous - %llu (%u)\n", state->name,
	       previous_count[state->id][cpu], cpu);

	val = pci_read_long(amd_fam14h_pci_dev, PCI_MONITOR_ENABLE_REG);
	val &= ~(1 << enable_bit);
	pci_write_long(amd_fam14h_pci_dev, PCI_MONITOR_ENABLE_REG, val);

	return 0;
}

static int fam14h_nbp1_count(unsigned int id, unsigned long long *count,
			     unsigned int cpu)
{
	if (id == NBP1) {
		if (nbp1_entered)
			*count = 1;
		else
			*count = 0;
		return 0;
	}
	return -1;
}
static int fam14h_get_count_percent(unsigned int id, double *percent,
				    unsigned int cpu)
{
	unsigned long diff;

	if (id >= AMD_FAM14H_STATE_NUM)
		return -1;
	/* residency count in 80ns -> divide through 12.5 to get us residency */
	diff = current_count[id][cpu] - previous_count[id][cpu];

	if (timediff == 0)
		*percent = 0.0;
	else
		*percent = 100.0 * diff / timediff / 12.5;

	dprint("Timediff: %llu - res~: %lu us - percent: %.2f %%\n",
	       timediff, diff * 10 / 125, *percent);

	return 0;
}

static int amd_fam14h_start(void)
{
	int num, cpu;
	clock_gettime(CLOCK_REALTIME, &start_time);
	for (num = 0; num < AMD_FAM14H_STATE_NUM; num++) {
		for (cpu = 0; cpu < cpu_count; cpu++)
			amd_fam14h_init(&amd_fam14h_cstates[num], cpu);
	}
#ifdef DEBUG
	clock_gettime(CLOCK_REALTIME, &dbg_time);
	dbg_timediff = timespec_diff_us(start_time, dbg_time);
	dprint("Enabling counters took: %lu us\n",
	       dbg_timediff);
#endif
	return 0;
}

static int amd_fam14h_stop(void)
{
	int num, cpu;
	struct timespec end_time;

	clock_gettime(CLOCK_REALTIME, &end_time);

	for (num = 0; num < AMD_FAM14H_STATE_NUM; num++) {
		for (cpu = 0; cpu < cpu_count; cpu++)
			amd_fam14h_disable(&amd_fam14h_cstates[num], cpu);
	}
#ifdef DEBUG
	clock_gettime(CLOCK_REALTIME, &dbg_time);
	dbg_timediff = timespec_diff_us(end_time, dbg_time);
	dprint("Disabling counters took: %lu ns\n", dbg_timediff);
#endif
	timediff = timespec_diff_us(start_time, end_time);
	if (timediff / 1000 > OVERFLOW_MS)
		print_overflow_err((unsigned int)timediff / 1000000,
				   OVERFLOW_MS / 1000);

	return 0;
}

static int is_nbp1_capable(void)
{
	uint32_t val;
	val = pci_read_long(amd_fam14h_pci_dev, PCI_NBP1_CAP_OFFSET);
	return val & (1 << 31);
}

struct cpuidle_monitor *amd_fam14h_register(void)
{
	int num;

	if (cpupower_cpu_info.vendor != X86_VENDOR_AMD)
		return NULL;

	if (cpupower_cpu_info.family == 0x14)
		strncpy(amd_fam14h_monitor.name, "Fam_14h",
			MONITOR_NAME_LEN - 1);
	else if (cpupower_cpu_info.family == 0x12)
		strncpy(amd_fam14h_monitor.name, "Fam_12h",
			MONITOR_NAME_LEN - 1);
	else
		return NULL;

	/* We do not alloc for nbp1 machine wide counter */
	for (num = 0; num < AMD_FAM14H_STATE_NUM - 1; num++) {
		previous_count[num] = calloc(cpu_count,
					      sizeof(unsigned long long));
		current_count[num]  = calloc(cpu_count,
					      sizeof(unsigned long long));
	}

	/* We need PCI device: Slot 18, Func 6, compare with BKDG
	   for fam 12h/14h */
	amd_fam14h_pci_dev = pci_slot_func_init(&pci_acc, 0x18, 6);
	if (amd_fam14h_pci_dev == NULL || pci_acc == NULL)
		return NULL;

	if (!is_nbp1_capable())
		amd_fam14h_monitor.hw_states_num = AMD_FAM14H_STATE_NUM - 1;

	amd_fam14h_monitor.name_len = strlen(amd_fam14h_monitor.name);
	return &amd_fam14h_monitor;
}

static void amd_fam14h_unregister(void)
{
	int num;
	for (num = 0; num < AMD_FAM14H_STATE_NUM - 1; num++) {
		free(previous_count[num]);
		free(current_count[num]);
	}
	pci_cleanup(pci_acc);
}

struct cpuidle_monitor amd_fam14h_monitor = {
	.name			= "",
	.hw_states		= amd_fam14h_cstates,
	.hw_states_num		= AMD_FAM14H_STATE_NUM,
	.start			= amd_fam14h_start,
	.stop			= amd_fam14h_stop,
	.do_register		= amd_fam14h_register,
	.unregister		= amd_fam14h_unregister,
	.flags.needs_root	= 1,
	.overflow_s		= OVERFLOW_MS / 1000,
};
#endif /* #if defined(__i386__) || defined(__x86_64__) */
