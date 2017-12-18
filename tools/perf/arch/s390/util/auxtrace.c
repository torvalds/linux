#include <stdbool.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/log2.h>

#include "../../util/evlist.h"
#include "../../util/auxtrace.h"
#include "../../util/evsel.h"

#define PERF_EVENT_CPUM_SF		0xB0000 /* Event: Basic-sampling */
#define PERF_EVENT_CPUM_SF_DIAG		0xBD000 /* Event: Combined-sampling */
#define DEFAULT_AUX_PAGES		128
#define DEFAULT_FREQ			4000

static void cpumsf_free(struct auxtrace_record *itr)
{
	free(itr);
}

static size_t cpumsf_info_priv_size(struct auxtrace_record *itr __maybe_unused,
				    struct perf_evlist *evlist __maybe_unused)
{
	return 0;
}

static int
cpumsf_info_fill(struct auxtrace_record *itr __maybe_unused,
		 struct perf_session *session __maybe_unused,
		 struct auxtrace_info_event *auxtrace_info __maybe_unused,
		 size_t priv_size __maybe_unused)
{
	return 0;
}

static unsigned long
cpumsf_reference(struct auxtrace_record *itr __maybe_unused)
{
	return 0;
}

static int
cpumsf_recording_options(struct auxtrace_record *ar __maybe_unused,
			 struct perf_evlist *evlist __maybe_unused,
			 struct record_opts *opts)
{
	unsigned int factor = 1;
	unsigned int pages;

	opts->full_auxtrace = true;

	/*
	 * The AUX buffer size should be set properly to avoid
	 * overflow of samples if it is not set explicitly.
	 * DEFAULT_AUX_PAGES is an proper size when sampling frequency
	 * is DEFAULT_FREQ. It is expected to hold about 1/2 second
	 * of sampling data. The size used for AUX buffer will scale
	 * according to the specified frequency and DEFAULT_FREQ.
	 */
	if (!opts->auxtrace_mmap_pages) {
		if (opts->user_freq != UINT_MAX)
			factor = (opts->user_freq + DEFAULT_FREQ
				  - 1) / DEFAULT_FREQ;
		pages = DEFAULT_AUX_PAGES * factor;
		opts->auxtrace_mmap_pages = roundup_pow_of_two(pages);
	}

	return 0;
}

static int
cpumsf_parse_snapshot_options(struct auxtrace_record *itr __maybe_unused,
			      struct record_opts *opts __maybe_unused,
			      const char *str __maybe_unused)
{
	return 0;
}

/*
 * auxtrace_record__init is called when perf record
 * check if the event really need auxtrace
 */
struct auxtrace_record *auxtrace_record__init(struct perf_evlist *evlist,
					      int *err)
{
	struct auxtrace_record *aux;
	struct perf_evsel *pos;
	int diagnose = 0;

	if (evlist->nr_entries == 0)
		return NULL;

	evlist__for_each_entry(evlist, pos) {
		if (pos->attr.config == PERF_EVENT_CPUM_SF_DIAG) {
			diagnose = 1;
			break;
		}
	}

	if (!diagnose)
		return NULL;

	/* sampling in diagnose mode. alloc aux buffer */
	aux = zalloc(sizeof(*aux));
	if (aux == NULL) {
		*err = -ENOMEM;
		return NULL;
	}

	aux->parse_snapshot_options = cpumsf_parse_snapshot_options;
	aux->recording_options = cpumsf_recording_options;
	aux->info_priv_size = cpumsf_info_priv_size;
	aux->info_fill = cpumsf_info_fill;
	aux->free = cpumsf_free;
	aux->reference = cpumsf_reference;

	return aux;
}
