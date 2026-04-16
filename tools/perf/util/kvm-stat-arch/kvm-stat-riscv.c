// SPDX-License-Identifier: GPL-2.0
/*
 * Arch specific functions for perf kvm stat.
 *
 * Copyright 2024 Beijing ESWIN Computing Technology Co., Ltd.
 *
 */
#include <errno.h>
#include <memory.h>
#include "../evsel.h"
#include "../kvm-stat.h"
#include "riscv_trap_types.h"
#include "debug.h"

define_exit_reasons_table(riscv_exit_reasons, kvm_riscv_trap_class);

static const char * const __kvm_events_tp[] = {
	"kvm:kvm_entry",
	"kvm:kvm_exit",
	NULL,
};

static void event_get_key(struct evsel *evsel,
			  struct perf_sample *sample,
			  struct event_key *key)
{
	int xlen = 64; // TODO: 32-bit support.

	key->info = 0;
	key->key = evsel__intval(evsel, sample, kvm_exit_reason(EM_RISCV)) & ~CAUSE_IRQ_FLAG(xlen);
	key->exit_reasons = riscv_exit_reasons;
}

static bool event_begin(struct evsel *evsel,
			struct perf_sample *sample __maybe_unused,
			struct event_key *key __maybe_unused)
{
	return evsel__name_is(evsel, kvm_entry_trace(EM_RISCV));
}

static bool event_end(struct evsel *evsel,
		      struct perf_sample *sample,
		      struct event_key *key)
{
	if (evsel__name_is(evsel, kvm_exit_trace(EM_RISCV))) {
		event_get_key(evsel, sample, key);
		return true;
	}
	return false;
}

static const struct kvm_events_ops exit_events = {
	.is_begin_event = event_begin,
	.is_end_event	= event_end,
	.decode_key	= exit_event_decode_key,
	.name		= "VM-EXIT"
};

static const struct kvm_reg_events_ops __kvm_reg_events_ops[] = {
	{
		.name	= "vmexit",
		.ops	= &exit_events,
	},
	{ NULL, NULL },
};

static const char * const __kvm_skip_events[] = {
	NULL,
};

int __cpu_isa_init_riscv(struct perf_kvm_stat *kvm)
{
	kvm->exit_reasons_isa = "riscv64";
	return 0;
}

const char * const *__kvm_events_tp_riscv(void)
{
	return __kvm_events_tp;
}

const struct kvm_reg_events_ops *__kvm_reg_events_ops_riscv(void)
{
	return __kvm_reg_events_ops;
}

const char * const *__kvm_skip_events_riscv(void)
{
	return __kvm_skip_events;
}
