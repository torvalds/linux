// SPDX-License-Identifier: GPL-2.0
/*
 * Arch specific functions for perf kvm stat.
 *
 * Copyright 2024 Beijing ESWIN Computing Technology Co., Ltd.
 *
 */
#include <errno.h>
#include <memory.h>
#include "../../../util/evsel.h"
#include "../../../util/kvm-stat.h"
#include "riscv_trap_types.h"
#include "debug.h"

define_exit_reasons_table(riscv_exit_reasons, kvm_riscv_trap_class);

const char *vcpu_id_str = "id";
const char *kvm_exit_reason = "scause";
const char *kvm_entry_trace = "kvm:kvm_entry";
const char *kvm_exit_trace = "kvm:kvm_exit";

const char *kvm_events_tp[] = {
	"kvm:kvm_entry",
	"kvm:kvm_exit",
	NULL,
};

static void event_get_key(struct evsel *evsel,
			  struct perf_sample *sample,
			  struct event_key *key)
{
	key->info = 0;
	key->key = evsel__intval(evsel, sample, kvm_exit_reason) & ~CAUSE_IRQ_FLAG;
	key->exit_reasons = riscv_exit_reasons;
}

static bool event_begin(struct evsel *evsel,
			struct perf_sample *sample __maybe_unused,
			struct event_key *key __maybe_unused)
{
	return evsel__name_is(evsel, kvm_entry_trace);
}

static bool event_end(struct evsel *evsel,
		      struct perf_sample *sample,
		      struct event_key *key)
{
	if (evsel__name_is(evsel, kvm_exit_trace)) {
		event_get_key(evsel, sample, key);
		return true;
	}
	return false;
}

static struct kvm_events_ops exit_events = {
	.is_begin_event = event_begin,
	.is_end_event	= event_end,
	.decode_key	= exit_event_decode_key,
	.name		= "VM-EXIT"
};

struct kvm_reg_events_ops kvm_reg_events_ops[] = {
	{
		.name	= "vmexit",
		.ops	= &exit_events,
	},
	{ NULL, NULL },
};

const char * const kvm_skip_events[] = {
	NULL,
};

int cpu_isa_init(struct perf_kvm_stat *kvm, const char *cpuid __maybe_unused)
{
	kvm->exit_reasons_isa = "riscv64";
	return 0;
}
