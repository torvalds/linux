/*-
 * Copyright (c) 2003-2008 Joseph Koshy
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/cpuset.h>
#include <sys/pmc.h>

#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "libpmcstat.h"

void
pmcstat_clone_event_descriptor(struct pmcstat_ev *ev, const cpuset_t *cpumask,
    struct pmcstat_args *args)
{
	int cpu;
	struct pmcstat_ev *ev_clone;

	for (cpu = 0; cpu < CPU_SETSIZE; cpu++) {
		if (!CPU_ISSET(cpu, cpumask))
			continue;

		if ((ev_clone = malloc(sizeof(*ev_clone))) == NULL)
			errx(EX_SOFTWARE, "ERROR: Out of memory");
		(void) memset(ev_clone, 0, sizeof(*ev_clone));

		ev_clone->ev_count = ev->ev_count;
		ev_clone->ev_cpu   = cpu;
		ev_clone->ev_cumulative = ev->ev_cumulative;
		ev_clone->ev_flags = ev->ev_flags;
		ev_clone->ev_mode  = ev->ev_mode;
		ev_clone->ev_name  = strdup(ev->ev_name);
		if (ev_clone->ev_name == NULL)
			errx(EX_SOFTWARE, "ERROR: Out of memory");
		ev_clone->ev_pmcid = ev->ev_pmcid;
		ev_clone->ev_saved = ev->ev_saved;
		ev_clone->ev_spec  = strdup(ev->ev_spec);
		if (ev_clone->ev_spec == NULL)
			errx(EX_SOFTWARE, "ERROR: Out of memory");

		STAILQ_INSERT_TAIL(&args->pa_events, ev_clone, ev_next);
	}
}
