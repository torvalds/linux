// SPDX-License-Identifier: GPL-2.0-only
/*
 * Arch specific functions for perf kvm stat.
 *
 * Copyright 2014 IBM Corp.
 * Author(s): Alexander Yarygin <yarygin@linux.vnet.ibm.com>
 */

#include <errno.h>
#include <string.h>
#include "../kvm-stat.h"
#include "../evsel.h"
#include "../../../arch/s390/include/uapi/asm/sie.h"

define_exit_reasons_table(sie_exit_reasons, sie_intercept_code);
define_exit_reasons_table(sie_icpt_insn_codes, icpt_insn_codes);
define_exit_reasons_table(sie_sigp_order_codes, sigp_order_codes);
define_exit_reasons_table(sie_diagnose_codes, diagnose_codes);
define_exit_reasons_table(sie_icpt_prog_codes, icpt_prog_codes);

static void event_icpt_insn_get_key(struct evsel *evsel,
				    struct perf_sample *sample,
				    struct event_key *key)
{
	u64 insn;

	insn = evsel__intval(evsel, sample, "instruction");
	key->key = icpt_insn_decoder(insn);
	key->exit_reasons = sie_icpt_insn_codes;
}

static void event_sigp_get_key(struct evsel *evsel,
			       struct perf_sample *sample,
			       struct event_key *key)
{
	key->key = evsel__intval(evsel, sample, "order_code");
	key->exit_reasons = sie_sigp_order_codes;
}

static void event_diag_get_key(struct evsel *evsel,
			       struct perf_sample *sample,
			       struct event_key *key)
{
	key->key = evsel__intval(evsel, sample, "code");
	key->exit_reasons = sie_diagnose_codes;
}

static void event_icpt_prog_get_key(struct evsel *evsel,
				    struct perf_sample *sample,
				    struct event_key *key)
{
	key->key = evsel__intval(evsel, sample, "code");
	key->exit_reasons = sie_icpt_prog_codes;
}

static const struct child_event_ops child_events[] = {
	{ .name = "kvm:kvm_s390_intercept_instruction",
	  .get_key = event_icpt_insn_get_key },
	{ .name = "kvm:kvm_s390_handle_sigp",
	  .get_key = event_sigp_get_key },
	{ .name = "kvm:kvm_s390_handle_diag",
	  .get_key = event_diag_get_key },
	{ .name = "kvm:kvm_s390_intercept_prog",
	  .get_key = event_icpt_prog_get_key },
	{ NULL, NULL },
};

static const struct kvm_events_ops exit_events = {
	.is_begin_event = exit_event_begin,
	.is_end_event = exit_event_end,
	.child_ops = child_events,
	.decode_key = exit_event_decode_key,
	.name = "VM-EXIT"
};

static const char * const __kvm_events_tp[] = {
	"kvm:kvm_s390_sie_enter",
	"kvm:kvm_s390_sie_exit",
	"kvm:kvm_s390_intercept_instruction",
	"kvm:kvm_s390_handle_sigp",
	"kvm:kvm_s390_handle_diag",
	"kvm:kvm_s390_intercept_prog",
	NULL,
};

static const struct kvm_reg_events_ops __kvm_reg_events_ops[] = {
	{ .name = "vmexit", .ops = &exit_events },
	{ NULL, NULL },
};

static const char * const __kvm_skip_events[] = {
	"Wait state",
	NULL,
};

int __cpu_isa_init_s390(struct perf_kvm_stat *kvm, const char *cpuid)
{
	if (strstr(cpuid, "IBM")) {
		kvm->exit_reasons = sie_exit_reasons;
		kvm->exit_reasons_isa = "SIE";
	} else
		return -ENOTSUP;

	return 0;
}

const char * const *__kvm_events_tp_s390(void)
{
	return __kvm_events_tp;
}

const struct kvm_reg_events_ops *__kvm_reg_events_ops_s390(void)
{
	return __kvm_reg_events_ops;
}

const char * const *__kvm_skip_events_s390(void)
{
	return __kvm_skip_events;
}
