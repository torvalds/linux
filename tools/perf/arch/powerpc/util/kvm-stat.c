// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include "util/kvm-stat.h"
#include "util/parse-events.h"
#include "util/debug.h"
#include "util/evsel.h"
#include "util/evlist.h"
#include "util/pmu.h"

#include "book3s_hv_exits.h"
#include "book3s_hcalls.h"
#include <subcmd/parse-options.h>

#define NR_TPS 4

const char *vcpu_id_str = "vcpu_id";
const int decode_str_len = 40;
const char *kvm_entry_trace = "kvm_hv:kvm_guest_enter";
const char *kvm_exit_trace = "kvm_hv:kvm_guest_exit";

define_exit_reasons_table(hv_exit_reasons, kvm_trace_symbol_exit);
define_exit_reasons_table(hcall_reasons, kvm_trace_symbol_hcall);

/* Tracepoints specific to ppc_book3s_hv */
const char *ppc_book3s_hv_kvm_tp[] = {
	"kvm_hv:kvm_guest_enter",
	"kvm_hv:kvm_guest_exit",
	"kvm_hv:kvm_hcall_enter",
	"kvm_hv:kvm_hcall_exit",
	NULL,
};

/* 1 extra placeholder for NULL */
const char *kvm_events_tp[NR_TPS + 1];
const char *kvm_exit_reason;

static void hcall_event_get_key(struct evsel *evsel,
				struct perf_sample *sample,
				struct event_key *key)
{
	key->info = 0;
	key->key = evsel__intval(evsel, sample, "req");
}

static const char *get_hcall_exit_reason(u64 exit_code)
{
	struct exit_reasons_table *tbl = hcall_reasons;

	while (tbl->reason != NULL) {
		if (tbl->exit_code == exit_code)
			return tbl->reason;
		tbl++;
	}

	pr_debug("Unknown hcall code: %lld\n",
	       (unsigned long long)exit_code);
	return "UNKNOWN";
}

static bool hcall_event_end(struct evsel *evsel,
			    struct perf_sample *sample __maybe_unused,
			    struct event_key *key __maybe_unused)
{
	return (!strcmp(evsel->name, kvm_events_tp[3]));
}

static bool hcall_event_begin(struct evsel *evsel,
			      struct perf_sample *sample, struct event_key *key)
{
	if (!strcmp(evsel->name, kvm_events_tp[2])) {
		hcall_event_get_key(evsel, sample, key);
		return true;
	}

	return false;
}
static void hcall_event_decode_key(struct perf_kvm_stat *kvm __maybe_unused,
				   struct event_key *key,
				   char *decode)
{
	const char *hcall_reason = get_hcall_exit_reason(key->key);

	scnprintf(decode, decode_str_len, "%s", hcall_reason);
}

static struct kvm_events_ops hcall_events = {
	.is_begin_event = hcall_event_begin,
	.is_end_event = hcall_event_end,
	.decode_key = hcall_event_decode_key,
	.name = "HCALL-EVENT",
};

static struct kvm_events_ops exit_events = {
	.is_begin_event = exit_event_begin,
	.is_end_event = exit_event_end,
	.decode_key = exit_event_decode_key,
	.name = "VM-EXIT"
};

struct kvm_reg_events_ops kvm_reg_events_ops[] = {
	{ .name = "vmexit", .ops = &exit_events },
	{ .name = "hcall", .ops = &hcall_events },
	{ NULL, NULL },
};

const char * const kvm_skip_events[] = {
	NULL,
};


static int is_tracepoint_available(const char *str, struct evlist *evlist)
{
	struct parse_events_error err;
	int ret;

	bzero(&err, sizeof(err));
	ret = parse_events(evlist, str, &err);
	if (err.str)
		parse_events_print_error(&err, "tracepoint");
	return ret;
}

static int ppc__setup_book3s_hv(struct perf_kvm_stat *kvm,
				struct evlist *evlist)
{
	const char **events_ptr;
	int i, nr_tp = 0, err = -1;

	/* Check for book3s_hv tracepoints */
	for (events_ptr = ppc_book3s_hv_kvm_tp; *events_ptr; events_ptr++) {
		err = is_tracepoint_available(*events_ptr, evlist);
		if (err)
			return -1;
		nr_tp++;
	}

	for (i = 0; i < nr_tp; i++)
		kvm_events_tp[i] = ppc_book3s_hv_kvm_tp[i];

	kvm_events_tp[i] = NULL;
	kvm_exit_reason = "trap";
	kvm->exit_reasons = hv_exit_reasons;
	kvm->exit_reasons_isa = "HV";

	return 0;
}

/* Wrapper to setup kvm tracepoints */
static int ppc__setup_kvm_tp(struct perf_kvm_stat *kvm)
{
	struct evlist *evlist = evlist__new();

	if (evlist == NULL)
		return -ENOMEM;

	/* Right now, only supported on book3s_hv */
	return ppc__setup_book3s_hv(kvm, evlist);
}

int setup_kvm_events_tp(struct perf_kvm_stat *kvm)
{
	return ppc__setup_kvm_tp(kvm);
}

int cpu_isa_init(struct perf_kvm_stat *kvm, const char *cpuid __maybe_unused)
{
	int ret;

	ret = ppc__setup_kvm_tp(kvm);
	if (ret) {
		kvm->exit_reasons = NULL;
		kvm->exit_reasons_isa = NULL;
	}

	return ret;
}

/*
 * Incase of powerpc architecture, pmu registers are programmable
 * by guest kernel. So monitoring guest via host may not provide
 * valid samples with default 'cycles' event. It is better to use
 * 'trace_imc/trace_cycles' event for guest profiling, since it
 * can track the guest instruction pointer in the trace-record.
 *
 * Function to parse the arguments and return appropriate values.
 */
int kvm_add_default_arch_event(int *argc, const char **argv)
{
	const char **tmp;
	bool event = false;
	int i, j = *argc;

	const struct option event_options[] = {
		OPT_BOOLEAN('e', "event", &event, NULL),
		OPT_END()
	};

	tmp = calloc(j + 1, sizeof(char *));
	if (!tmp)
		return -EINVAL;

	for (i = 0; i < j; i++)
		tmp[i] = argv[i];

	parse_options(j, tmp, event_options, NULL, PARSE_OPT_KEEP_UNKNOWN);
	if (!event) {
		if (pmu_have_event("trace_imc", "trace_cycles")) {
			argv[j++] = strdup("-e");
			argv[j++] = strdup("trace_imc/trace_cycles/");
			*argc += 2;
		} else {
			free(tmp);
			return -EINVAL;
		}
	}

	free(tmp);
	return 0;
}
