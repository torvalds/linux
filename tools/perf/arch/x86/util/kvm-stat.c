// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <string.h>
#include "../../../util/kvm-stat.h"
#include "../../../util/evsel.h"
#include <asm/svm.h>
#include <asm/vmx.h>
#include <asm/kvm.h>

define_exit_reasons_table(vmx_exit_reasons, VMX_EXIT_REASONS);
define_exit_reasons_table(svm_exit_reasons, SVM_EXIT_REASONS);

static struct kvm_events_ops exit_events = {
	.is_begin_event = exit_event_begin,
	.is_end_event = exit_event_end,
	.decode_key = exit_event_decode_key,
	.name = "VM-EXIT"
};

const char *vcpu_id_str = "vcpu_id";
const char *kvm_exit_reason = "exit_reason";
const char *kvm_entry_trace = "kvm:kvm_entry";
const char *kvm_exit_trace = "kvm:kvm_exit";

/*
 * For the mmio events, we treat:
 * the time of MMIO write: kvm_mmio(KVM_TRACE_MMIO_WRITE...) -> kvm_entry
 * the time of MMIO read: kvm_exit -> kvm_mmio(KVM_TRACE_MMIO_READ...).
 */
static void mmio_event_get_key(struct evsel *evsel, struct perf_sample *sample,
			       struct event_key *key)
{
	key->key  = evsel__intval(evsel, sample, "gpa");
	key->info = evsel__intval(evsel, sample, "type");
}

#define KVM_TRACE_MMIO_READ_UNSATISFIED 0
#define KVM_TRACE_MMIO_READ 1
#define KVM_TRACE_MMIO_WRITE 2

static bool mmio_event_begin(struct evsel *evsel,
			     struct perf_sample *sample, struct event_key *key)
{
	/* MMIO read begin event in kernel. */
	if (kvm_exit_event(evsel))
		return true;

	/* MMIO write begin event in kernel. */
	if (evsel__name_is(evsel, "kvm:kvm_mmio") &&
	    evsel__intval(evsel, sample, "type") == KVM_TRACE_MMIO_WRITE) {
		mmio_event_get_key(evsel, sample, key);
		return true;
	}

	return false;
}

static bool mmio_event_end(struct evsel *evsel, struct perf_sample *sample,
			   struct event_key *key)
{
	/* MMIO write end event in kernel. */
	if (kvm_entry_event(evsel))
		return true;

	/* MMIO read end event in kernel.*/
	if (evsel__name_is(evsel, "kvm:kvm_mmio") &&
	    evsel__intval(evsel, sample, "type") == KVM_TRACE_MMIO_READ) {
		mmio_event_get_key(evsel, sample, key);
		return true;
	}

	return false;
}

static void mmio_event_decode_key(struct perf_kvm_stat *kvm __maybe_unused,
				  struct event_key *key,
				  char *decode)
{
	scnprintf(decode, KVM_EVENT_NAME_LEN, "%#lx:%s",
		  (unsigned long)key->key,
		  key->info == KVM_TRACE_MMIO_WRITE ? "W" : "R");
}

static struct kvm_events_ops mmio_events = {
	.is_begin_event = mmio_event_begin,
	.is_end_event = mmio_event_end,
	.decode_key = mmio_event_decode_key,
	.name = "MMIO Access"
};

 /* The time of emulation pio access is from kvm_pio to kvm_entry. */
static void ioport_event_get_key(struct evsel *evsel,
				 struct perf_sample *sample,
				 struct event_key *key)
{
	key->key  = evsel__intval(evsel, sample, "port");
	key->info = evsel__intval(evsel, sample, "rw");
}

static bool ioport_event_begin(struct evsel *evsel,
			       struct perf_sample *sample,
			       struct event_key *key)
{
	if (evsel__name_is(evsel, "kvm:kvm_pio")) {
		ioport_event_get_key(evsel, sample, key);
		return true;
	}

	return false;
}

static bool ioport_event_end(struct evsel *evsel,
			     struct perf_sample *sample __maybe_unused,
			     struct event_key *key __maybe_unused)
{
	return kvm_entry_event(evsel);
}

static void ioport_event_decode_key(struct perf_kvm_stat *kvm __maybe_unused,
				    struct event_key *key,
				    char *decode)
{
	scnprintf(decode, KVM_EVENT_NAME_LEN, "%#llx:%s",
		  (unsigned long long)key->key,
		  key->info ? "POUT" : "PIN");
}

static struct kvm_events_ops ioport_events = {
	.is_begin_event = ioport_event_begin,
	.is_end_event = ioport_event_end,
	.decode_key = ioport_event_decode_key,
	.name = "IO Port Access"
};

 /* The time of emulation msr is from kvm_msr to kvm_entry. */
static void msr_event_get_key(struct evsel *evsel,
				 struct perf_sample *sample,
				 struct event_key *key)
{
	key->key  = evsel__intval(evsel, sample, "ecx");
	key->info = evsel__intval(evsel, sample, "write");
}

static bool msr_event_begin(struct evsel *evsel,
			       struct perf_sample *sample,
			       struct event_key *key)
{
	if (evsel__name_is(evsel, "kvm:kvm_msr")) {
		msr_event_get_key(evsel, sample, key);
		return true;
	}

	return false;
}

static bool msr_event_end(struct evsel *evsel,
			     struct perf_sample *sample __maybe_unused,
			     struct event_key *key __maybe_unused)
{
	return kvm_entry_event(evsel);
}

static void msr_event_decode_key(struct perf_kvm_stat *kvm __maybe_unused,
				    struct event_key *key,
				    char *decode)
{
	scnprintf(decode, KVM_EVENT_NAME_LEN, "%#llx:%s",
		  (unsigned long long)key->key,
		  key->info ? "W" : "R");
}

static struct kvm_events_ops msr_events = {
	.is_begin_event = msr_event_begin,
	.is_end_event = msr_event_end,
	.decode_key = msr_event_decode_key,
	.name = "MSR Access"
};

const char *kvm_events_tp[] = {
	"kvm:kvm_entry",
	"kvm:kvm_exit",
	"kvm:kvm_mmio",
	"kvm:kvm_pio",
	"kvm:kvm_msr",
	NULL,
};

struct kvm_reg_events_ops kvm_reg_events_ops[] = {
	{ .name = "vmexit", .ops = &exit_events },
	{ .name = "mmio", .ops = &mmio_events },
	{ .name = "ioport", .ops = &ioport_events },
	{ .name = "msr", .ops = &msr_events },
	{ NULL, NULL },
};

const char * const kvm_skip_events[] = {
	"HLT",
	NULL,
};

int cpu_isa_init(struct perf_kvm_stat *kvm, const char *cpuid)
{
	if (strstr(cpuid, "Intel")) {
		kvm->exit_reasons = vmx_exit_reasons;
		kvm->exit_reasons_isa = "VMX";
	} else if (strstr(cpuid, "AMD") || strstr(cpuid, "Hygon")) {
		kvm->exit_reasons = svm_exit_reasons;
		kvm->exit_reasons_isa = "SVM";
	} else
		return -ENOTSUP;

	return 0;
}
