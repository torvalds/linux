// SPDX-License-Identifier: GPL-2.0
#include "debug.h"
#include "evsel.h"
#include "kvm-stat.h"

#if defined(HAVE_KVM_STAT_SUPPORT) && defined(HAVE_LIBTRACEEVENT)

bool kvm_exit_event(struct evsel *evsel)
{
	return evsel__name_is(evsel, kvm_exit_trace);
}

void exit_event_get_key(struct evsel *evsel,
			struct perf_sample *sample,
			struct event_key *key)
{
	key->info = 0;
	key->key  = evsel__intval(evsel, sample, kvm_exit_reason);
}


bool exit_event_begin(struct evsel *evsel,
		      struct perf_sample *sample, struct event_key *key)
{
	if (kvm_exit_event(evsel)) {
		exit_event_get_key(evsel, sample, key);
		return true;
	}

	return false;
}

bool kvm_entry_event(struct evsel *evsel)
{
	return evsel__name_is(evsel, kvm_entry_trace);
}

bool exit_event_end(struct evsel *evsel,
		    struct perf_sample *sample __maybe_unused,
		    struct event_key *key __maybe_unused)
{
	return kvm_entry_event(evsel);
}

static const char *get_exit_reason(struct perf_kvm_stat *kvm,
				   struct exit_reasons_table *tbl,
				   u64 exit_code)
{
	while (tbl->reason != NULL) {
		if (tbl->exit_code == exit_code)
			return tbl->reason;
		tbl++;
	}

	pr_err("unknown kvm exit code:%lld on %s\n",
		(unsigned long long)exit_code, kvm->exit_reasons_isa);
	return "UNKNOWN";
}

void exit_event_decode_key(struct perf_kvm_stat *kvm,
			   struct event_key *key,
			   char *decode)
{
	const char *exit_reason = get_exit_reason(kvm, key->exit_reasons,
						  key->key);

	scnprintf(decode, KVM_EVENT_NAME_LEN, "%s", exit_reason);
}

#endif
