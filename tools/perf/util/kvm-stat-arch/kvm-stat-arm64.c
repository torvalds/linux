// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <memory.h>
#include "../debug.h"
#include "../evsel.h"
#include "../kvm-stat.h"
#include "arm64_exception_types.h"

define_exit_reasons_table(arm64_exit_reasons, kvm_arm_exception_type);
define_exit_reasons_table(arm64_trap_exit_reasons, kvm_arm_exception_class);

static const char *kvm_trap_exit_reason = "esr_ec";

static const char * const __kvm_events_tp[] = {
	"kvm:kvm_entry",
	"kvm:kvm_exit",
	NULL,
};

static void event_get_key(struct evsel *evsel,
			  struct perf_sample *sample,
			  struct event_key *key)
{
	key->info = 0;
	key->key = evsel__intval(evsel, sample, kvm_exit_reason(EM_AARCH64));
	key->exit_reasons = arm64_exit_reasons;

	/*
	 * TRAP exceptions carry exception class info in esr_ec field
	 * and, hence, we need to use a different exit_reasons table to
	 * properly decode event's est_ec.
	 */
	if (key->key == ARM_EXCEPTION_TRAP) {
		key->key = evsel__intval(evsel, sample, kvm_trap_exit_reason);
		key->exit_reasons = arm64_trap_exit_reasons;
	}
}

static bool event_begin(struct evsel *evsel,
			struct perf_sample *sample __maybe_unused,
			struct event_key *key __maybe_unused)
{
	return evsel__name_is(evsel, kvm_entry_trace(EM_AARCH64));
}

static bool event_end(struct evsel *evsel,
		      struct perf_sample *sample,
		      struct event_key *key)
{
	if (evsel__name_is(evsel, kvm_exit_trace(EM_AARCH64))) {
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

int __cpu_isa_init_arm64(struct perf_kvm_stat *kvm)
{
	kvm->exit_reasons_isa = "arm64";
	return 0;
}

const char * const *__kvm_events_tp_arm64(void)
{
	return __kvm_events_tp;
}

const struct kvm_reg_events_ops *__kvm_reg_events_ops_arm64(void)
{
	return __kvm_reg_events_ops;
}

const char * const *__kvm_skip_events_arm64(void)
{
	return __kvm_skip_events;
}
