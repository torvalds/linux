// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <memory.h>
#include "util/kvm-stat.h"
#include "util/parse-events.h"
#include "util/debug.h"
#include "util/evsel.h"
#include "util/evlist.h"
#include "util/pmus.h"

#define LOONGARCH_EXCEPTION_INT		0
#define LOONGARCH_EXCEPTION_PIL		1
#define LOONGARCH_EXCEPTION_PIS		2
#define LOONGARCH_EXCEPTION_PIF		3
#define LOONGARCH_EXCEPTION_PME		4
#define LOONGARCH_EXCEPTION_FPD		15
#define LOONGARCH_EXCEPTION_SXD		16
#define LOONGARCH_EXCEPTION_ASXD	17
#define LOONGARCH_EXCEPTION_GSPR	22
#define  LOONGARCH_EXCEPTION_CPUCFG	100
#define  LOONGARCH_EXCEPTION_CSR	101
#define  LOONGARCH_EXCEPTION_IOCSR	102
#define  LOONGARCH_EXCEPTION_IDLE	103
#define  LOONGARCH_EXCEPTION_OTHERS	104
#define LOONGARCH_EXCEPTION_HVC		23

#define loongarch_exception_type				\
	{LOONGARCH_EXCEPTION_INT,  "Interrupt" },		\
	{LOONGARCH_EXCEPTION_PIL,  "Mem Read" },		\
	{LOONGARCH_EXCEPTION_PIS,  "Mem Store" },		\
	{LOONGARCH_EXCEPTION_PIF,  "Inst Fetch" },		\
	{LOONGARCH_EXCEPTION_PME,  "Mem Modify" },		\
	{LOONGARCH_EXCEPTION_FPD,  "FPU" },			\
	{LOONGARCH_EXCEPTION_SXD,  "LSX" },			\
	{LOONGARCH_EXCEPTION_ASXD, "LASX" },			\
	{LOONGARCH_EXCEPTION_GSPR, "Privilege Error" },		\
	{LOONGARCH_EXCEPTION_HVC,  "Hypercall" },		\
	{LOONGARCH_EXCEPTION_CPUCFG, "CPUCFG" },		\
	{LOONGARCH_EXCEPTION_CSR,    "CSR" },			\
	{LOONGARCH_EXCEPTION_IOCSR,  "IOCSR" },			\
	{LOONGARCH_EXCEPTION_IDLE,   "Idle" },			\
	{LOONGARCH_EXCEPTION_OTHERS, "Others" }

define_exit_reasons_table(loongarch_exit_reasons, loongarch_exception_type);

const char *vcpu_id_str = "vcpu_id";
const char *kvm_exit_reason = "reason";
const char *kvm_entry_trace = "kvm:kvm_enter";
const char *kvm_reenter_trace = "kvm:kvm_reenter";
const char *kvm_exit_trace = "kvm:kvm_exit";
const char *kvm_events_tp[] = {
	"kvm:kvm_enter",
	"kvm:kvm_reenter",
	"kvm:kvm_exit",
	"kvm:kvm_exit_gspr",
	NULL,
};

static bool event_begin(struct evsel *evsel,
			struct perf_sample *sample, struct event_key *key)
{
	return exit_event_begin(evsel, sample, key);
}

static bool event_end(struct evsel *evsel,
		      struct perf_sample *sample __maybe_unused,
		      struct event_key *key __maybe_unused)
{
	/*
	 * LoongArch kvm is different with other architectures
	 *
	 * There is kvm:kvm_reenter or kvm:kvm_enter event adjacent with
	 * kvm:kvm_exit event.
	 *   kvm:kvm_enter   means returning to vmm and then to guest
	 *   kvm:kvm_reenter means returning to guest immediately
	 */
	return evsel__name_is(evsel, kvm_entry_trace) || evsel__name_is(evsel, kvm_reenter_trace);
}

static void event_gspr_get_key(struct evsel *evsel,
			       struct perf_sample *sample, struct event_key *key)
{
	unsigned int insn;

	key->key = LOONGARCH_EXCEPTION_OTHERS;
	insn = evsel__intval(evsel, sample, "inst_word");

	switch (insn >> 24) {
	case 0:
		/* CPUCFG inst trap */
		if ((insn >> 10) == 0x1b)
			key->key = LOONGARCH_EXCEPTION_CPUCFG;
		break;
	case 4:
		/* CSR inst trap */
		key->key = LOONGARCH_EXCEPTION_CSR;
		break;
	case 6:
		/* IOCSR inst trap */
		if ((insn >> 15) == 0xc90)
			key->key = LOONGARCH_EXCEPTION_IOCSR;
		else if ((insn >> 15) == 0xc91)
			/* Idle inst trap */
			key->key = LOONGARCH_EXCEPTION_IDLE;
		break;
	default:
		key->key = LOONGARCH_EXCEPTION_OTHERS;
		break;
	}
}

static struct child_event_ops child_events[] = {
	{ .name = "kvm:kvm_exit_gspr", .get_key = event_gspr_get_key },
	{ NULL, NULL },
};

static struct kvm_events_ops exit_events = {
	.is_begin_event = event_begin,
	.is_end_event = event_end,
	.child_ops = child_events,
	.decode_key = exit_event_decode_key,
	.name = "VM-EXIT"
};

struct kvm_reg_events_ops kvm_reg_events_ops[] = {
	{ .name	= "vmexit", .ops = &exit_events, },
	{ NULL, NULL },
};

const char * const kvm_skip_events[] = {
	NULL,
};

int cpu_isa_init(struct perf_kvm_stat *kvm, const char *cpuid __maybe_unused)
{
	kvm->exit_reasons_isa = "loongarch64";
	kvm->exit_reasons = loongarch_exit_reasons;
	return 0;
}
