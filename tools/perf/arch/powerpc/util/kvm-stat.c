#include "util/kvm-stat.h"
#include "util/parse-events.h"
#include "util/debug.h"

#include "book3s_hv_exits.h"
#include "book3s_hcalls.h"

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

static void hcall_event_get_key(struct perf_evsel *evsel,
				struct perf_sample *sample,
				struct event_key *key)
{
	key->info = 0;
	key->key = perf_evsel__intval(evsel, sample, "req");
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

static bool hcall_event_end(struct perf_evsel *evsel,
			    struct perf_sample *sample __maybe_unused,
			    struct event_key *key __maybe_unused)
{
	return (!strcmp(evsel->name, kvm_events_tp[3]));
}

static bool hcall_event_begin(struct perf_evsel *evsel,
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


static int is_tracepoint_available(const char *str, struct perf_evlist *evlist)
{
	struct parse_events_error err;
	int ret;

	err.str = NULL;
	ret = parse_events(evlist, str, &err);
	if (err.str)
		pr_err("%s : %s\n", str, err.str);
	return ret;
}

static int ppc__setup_book3s_hv(struct perf_kvm_stat *kvm,
				struct perf_evlist *evlist)
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
	struct perf_evlist *evlist = perf_evlist__new();

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
