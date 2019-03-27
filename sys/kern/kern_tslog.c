/*-
 * Copyright (c) 2017 Colin Percival
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

#include <sys/param.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/tslog.h>

#include <machine/atomic.h>
#include <machine/cpu.h>

#ifndef TSLOGSIZE
#define TSLOGSIZE 262144
#endif

static volatile long nrecs = 0;
static struct timestamp {
	void * td;
	int type;
	const char * f;
	const char * s;
	uint64_t tsc;
} timestamps[TSLOGSIZE];

void
tslog(void * td, int type, const char * f, const char * s)
{
	uint64_t tsc = get_cyclecount();
	long pos;

	/* Grab a slot. */
	pos = atomic_fetchadd_long(&nrecs, 1);

	/* Store record. */
	if (pos < nitems(timestamps)) {
		timestamps[pos].td = td;
		timestamps[pos].type = type;
		timestamps[pos].f = f;
		timestamps[pos].s = s;
		timestamps[pos].tsc = tsc;
	}
}

static int
sysctl_debug_tslog(SYSCTL_HANDLER_ARGS)
{
	int error;
	struct sbuf *sb;
	size_t i, limit;

	/*
	 * This code can race against the code in tslog() which stores
	 * records: Theoretically we could end up reading a record after
	 * its slots have been reserved but before it has been written.
	 * Since this code takes orders of magnitude longer to run than
	 * tslog() takes to write a record, it is highly unlikely that
	 * anyone will ever experience this race.
	 */
	sb = sbuf_new_for_sysctl(NULL, NULL, 1024, req);
	limit = MIN(nrecs, nitems(timestamps));
	for (i = 0; i < limit; i++) {
		sbuf_printf(sb, "%p", timestamps[i].td);
		sbuf_printf(sb, " %llu",
		    (unsigned long long)timestamps[i].tsc);
		switch (timestamps[i].type) {
		case TS_ENTER:
			sbuf_printf(sb, " ENTER");
			break;
		case TS_EXIT:
			sbuf_printf(sb, " EXIT");
			break;
		case TS_THREAD:
			sbuf_printf(sb, " THREAD");
			break;
		case TS_EVENT:
			sbuf_printf(sb, " EVENT");
			break;
		}
		sbuf_printf(sb, " %s", timestamps[i].f ? timestamps[i].f : "(null)");
		if (timestamps[i].s)
			sbuf_printf(sb, " %s\n", timestamps[i].s);
		else
			sbuf_printf(sb, "\n");
	}
	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

SYSCTL_PROC(_debug, OID_AUTO, tslog, CTLTYPE_STRING|CTLFLAG_RD|CTLFLAG_MPSAFE,
    0, 0, sysctl_debug_tslog, "", "Dump recorded event timestamps");
