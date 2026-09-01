/* SPDX-License-Identifier: GPL-2.0 */
#include "sample-raw.h"

#include <elf.h>
#include <linux/string.h>

#include "env.h"
#include "evlist.h"
#include "header.h"
#include "session.h"

/*
 * Check platform the perf data file was created on and perform platform
 * specific interpretation.
 */
void evlist__init_trace_event_sample_raw(struct evlist *evlist, struct perf_env *env)
{
	uint16_t e_machine = perf_env__e_machine(env, /*e_flags=*/NULL);

	if (e_machine == EM_S390) {
		evlist__set_trace_event_sample_raw(evlist, evlist__s390_sample_raw);
	} else if (e_machine == EM_X86_64 || e_machine == EM_386) {
		const char *cpuid = perf_env__cpuid(env);

		if (cpuid && strstarts(cpuid, "AuthenticAMD") && evlist__has_amd_ibs(evlist))
			evlist__set_trace_event_sample_raw(evlist, evlist__amd_sample_raw);
	}
}
