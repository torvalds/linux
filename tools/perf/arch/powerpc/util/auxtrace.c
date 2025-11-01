// SPDX-License-Identifier: GPL-2.0
/*
 * VPA support
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>

#include "../../util/evlist.h"
#include "../../util/debug.h"
#include "../../util/auxtrace.h"
#include "../../util/powerpc-vpadtl.h"
#include "../../util/record.h"
#include <internal/lib.h> // page_size

#define KiB(x) ((x) * 1024)

static int
powerpc_vpadtl_recording_options(struct auxtrace_record *ar __maybe_unused,
			struct evlist *evlist __maybe_unused,
			struct record_opts *opts)
{
	opts->full_auxtrace = true;

	/*
	 * Set auxtrace_mmap_pages to minimum
	 * two pages
	 */
	if (!opts->auxtrace_mmap_pages) {
		opts->auxtrace_mmap_pages = KiB(128) / page_size;
		if (opts->mmap_pages == UINT_MAX)
			opts->mmap_pages = KiB(256) / page_size;
	}

	return 0;
}

static size_t powerpc_vpadtl_info_priv_size(struct auxtrace_record *itr __maybe_unused,
					struct evlist *evlist __maybe_unused)
{
	return VPADTL_AUXTRACE_PRIV_SIZE;
}

static int
powerpc_vpadtl_info_fill(struct auxtrace_record *itr __maybe_unused,
		struct perf_session *session __maybe_unused,
		struct perf_record_auxtrace_info *auxtrace_info,
		size_t priv_size __maybe_unused)
{
	auxtrace_info->type = PERF_AUXTRACE_VPA_DTL;

	return 0;
}

static void powerpc_vpadtl_free(struct auxtrace_record *itr)
{
	free(itr);
}

static u64 powerpc_vpadtl_reference(struct auxtrace_record *itr __maybe_unused)
{
	return 0;
}

struct auxtrace_record *auxtrace_record__init(struct evlist *evlist,
						int *err)
{
	struct auxtrace_record *aux;
	struct evsel *pos;
	int found = 0;

	evlist__for_each_entry(evlist, pos) {
		if (strstarts(pos->name, "vpa_dtl")) {
			found = 1;
			pos->needs_auxtrace_mmap = true;
			break;
		}
	}

	if (!found)
		return NULL;

	/*
	 * To obtain the auxtrace buffer file descriptor, the auxtrace event
	 * must come first.
	 */
	evlist__to_front(pos->evlist, pos);

	aux = zalloc(sizeof(*aux));
	if (aux == NULL) {
		pr_debug("aux record is NULL\n");
		*err = -ENOMEM;
		return NULL;
	}

	aux->recording_options = powerpc_vpadtl_recording_options;
	aux->info_priv_size = powerpc_vpadtl_info_priv_size;
	aux->info_fill = powerpc_vpadtl_info_fill;
	aux->free = powerpc_vpadtl_free;
	aux->reference = powerpc_vpadtl_reference;
	return aux;
}
