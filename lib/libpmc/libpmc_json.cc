/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018, Matthew Macy
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
 *
 * $FreeBSD$
 *
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/sysctl.h>
#include <stddef.h>
#include <stdlib.h>
#include <err.h>
#include <limits.h>
#include <string.h>
#include <pmc.h>
#include <pmclog.h>
#include <assert.h>
#include <string>
#include <sysexits.h>
#include <pmcformat.h>

using std::string;

static const char *typenames[] = {
	"",
	"{\"type\": \"closelog\"}\n",
	"{\"type\": \"dropnotify\"}\n",
	"{\"type\": \"initialize\"",
	"",
	"{\"type\": \"pmcallocate\"",
	"{\"type\": \"pmcattach\"",
	"{\"type\": \"pmcdetach\"",
	"{\"type\": \"proccsw\"",
	"{\"type\": \"procexec\"",
	"{\"type\": \"procexit\"",
	"{\"type\": \"procfork\"",
	"{\"type\": \"sysexit\"",
	"{\"type\": \"userdata\"",
	"{\"type\": \"map_in\"",
	"{\"type\": \"map_out\"",
	"{\"type\": \"callchain\"",
	"{\"type\": \"pmcallocatedyn\"",
	"{\"type\": \"thr_create\"",
	"{\"type\": \"thr_exit\"",
	"{\"type\": \"proc_create\"",
};

static string
startentry(struct pmclog_ev *ev)
{
	char eventbuf[128];

	snprintf(eventbuf, sizeof(eventbuf), "%s, \"tsc\": \"%jd\"",
	    typenames[ev->pl_type], (uintmax_t)ev->pl_ts.tv_sec);
	return (string(eventbuf));
}

static string
initialize_to_json(struct pmclog_ev *ev)
{
	char eventbuf[256];
	string startent;

	startent = startentry(ev);
	snprintf(eventbuf, sizeof(eventbuf),
	    "%s, \"version\": \"0x%08x\", \"arch\": \"0x%08x\", \"cpuid\": \"%s\", "
		"\"tsc_freq\": \"%jd\", \"sec\": \"%jd\", \"nsec\": \"%jd\"}\n",
		startent.c_str(), ev->pl_u.pl_i.pl_version, ev->pl_u.pl_i.pl_arch,
		ev->pl_u.pl_i.pl_cpuid, (uintmax_t)ev->pl_u.pl_i.pl_tsc_freq,
		(uintmax_t)ev->pl_u.pl_i.pl_ts.tv_sec, (uintmax_t)ev->pl_u.pl_i.pl_ts.tv_nsec);
	return string(eventbuf);
}

static string
pmcallocate_to_json(struct pmclog_ev *ev)
{
	char eventbuf[256];
	string startent;

	startent = startentry(ev);
	snprintf(eventbuf, sizeof(eventbuf),
	    "%s, \"pmcid\": \"0x%08x\", \"event\": \"0x%08x\", \"flags\": \"0x%08x\", "
	    "\"rate\": \"%jd\"}\n",
		startent.c_str(), ev->pl_u.pl_a.pl_pmcid, ev->pl_u.pl_a.pl_event,
	    ev->pl_u.pl_a.pl_flags, (intmax_t)ev->pl_u.pl_a.pl_rate);
	return string(eventbuf);
}

static string
pmcattach_to_json(struct pmclog_ev *ev)
{
	char eventbuf[2048];
	string startent;

	startent = startentry(ev);
	snprintf(eventbuf, sizeof(eventbuf),
	    "%s, \"pmcid\": \"0x%08x\", \"pid\": \"%d\", \"pathname\": \"%s\"}\n",
		startent.c_str(), ev->pl_u.pl_t.pl_pmcid, ev->pl_u.pl_t.pl_pid,
	    ev->pl_u.pl_t.pl_pathname);
	return string(eventbuf);
}

static string
pmcdetach_to_json(struct pmclog_ev *ev)
{
	char eventbuf[128];
	string startent;

	startent = startentry(ev);
	snprintf(eventbuf, sizeof(eventbuf),
		"%s, \"pmcid\": \"0x%08x\", \"pid\": \"%d\"}\n",
			 startent.c_str(), ev->pl_u.pl_d.pl_pmcid, ev->pl_u.pl_d.pl_pid);
	return string(eventbuf);
}


static string
proccsw_to_json(struct pmclog_ev *ev)
{
	char eventbuf[128];
	string startent;

	startent = startentry(ev);
	snprintf(eventbuf, sizeof(eventbuf), "%s, \"pmcid\": \"0x%08x\", \"pid\": \"%d\" "
	    "\"tid\": \"%d\", \"value\": \"0x%016jx\"}\n",
		startent.c_str(), ev->pl_u.pl_c.pl_pmcid, ev->pl_u.pl_c.pl_pid,
	    ev->pl_u.pl_c.pl_tid, (uintmax_t)ev->pl_u.pl_c.pl_value);
	return string(eventbuf);
}

static string
procexec_to_json(struct pmclog_ev *ev)
{
	char eventbuf[2048];
	string startent;

	startent = startentry(ev);
	snprintf(eventbuf, sizeof(eventbuf),
		"%s, \"pmcid\": \"0x%08x\", \"pid\": \"%d\", "
	    "\"start\": \"0x%016jx\", \"pathname\": \"%s\"}\n",
		startent.c_str(), ev->pl_u.pl_x.pl_pmcid, ev->pl_u.pl_x.pl_pid,
		(uintmax_t)ev->pl_u.pl_x.pl_entryaddr, ev->pl_u.pl_x.pl_pathname);
	return string(eventbuf);
}

static string
procexit_to_json(struct pmclog_ev *ev)
{
	char eventbuf[128];
	string startent;

	startent = startentry(ev);
	snprintf(eventbuf, sizeof(eventbuf),
		"%s, \"pmcid\": \"0x%08x\", \"pid\": \"%d\", "
	    "\"value\": \"0x%016jx\"}\n",
		startent.c_str(), ev->pl_u.pl_e.pl_pmcid, ev->pl_u.pl_e.pl_pid,
	    (uintmax_t)ev->pl_u.pl_e.pl_value);
	return string(eventbuf);
}

static string
procfork_to_json(struct pmclog_ev *ev)
{
	char eventbuf[128];
	string startent;

	startent = startentry(ev);
	snprintf(eventbuf, sizeof(eventbuf),
		"%s, \"oldpid\": \"%d\", \"newpid\": \"%d\"}\n",
		startent.c_str(), ev->pl_u.pl_f.pl_oldpid, ev->pl_u.pl_f.pl_newpid);
	return string(eventbuf);
}

static string
sysexit_to_json(struct pmclog_ev *ev)
{
	char eventbuf[128];
	string startent;

	startent = startentry(ev);
	snprintf(eventbuf, sizeof(eventbuf), "%s, \"pid\": \"%d\"}\n",
		startent.c_str(), ev->pl_u.pl_se.pl_pid);
	return string(eventbuf);
}

static string
userdata_to_json(struct pmclog_ev *ev)
{
	char eventbuf[128];
	string startent;

	startent = startentry(ev);
	snprintf(eventbuf, sizeof(eventbuf), "%s, \"userdata\": \"0x%08x\"}\n",
	    startent.c_str(), ev->pl_u.pl_u.pl_userdata);
	return string(eventbuf);
}

static string
map_in_to_json(struct pmclog_ev *ev)
{
	char eventbuf[2048];
	string startent;

	startent = startentry(ev);
	snprintf(eventbuf, sizeof(eventbuf), "%s, \"pid\": \"%d\", "
	    "\"start\": \"0x%016jx\", \"pathname\": \"%s\"}\n",
	    startent.c_str(), ev->pl_u.pl_mi.pl_pid,
	    (uintmax_t)ev->pl_u.pl_mi.pl_start, ev->pl_u.pl_mi.pl_pathname);
	return string(eventbuf);
}

static string
map_out_to_json(struct pmclog_ev *ev)
{
	char eventbuf[256];
	string startent;

	startent = startentry(ev);
	snprintf(eventbuf, sizeof(eventbuf), "%s, \"pid\": \"%d\", "
	    "\"start\": \"0x%016jx\", \"end\": \"0x%016jx\"}\n",
	    startent.c_str(), ev->pl_u.pl_mi.pl_pid,
	    (uintmax_t)ev->pl_u.pl_mi.pl_start,
	    (uintmax_t)ev->pl_u.pl_mo.pl_end);
	return string(eventbuf);
}

static string
callchain_to_json(struct pmclog_ev *ev)
{
	char eventbuf[1024];
	string result;
	uint32_t i;
	string startent;

	startent = startentry(ev);
	snprintf(eventbuf, sizeof(eventbuf),
	    "%s, \"pmcid\": \"0x%08x\", \"pid\": \"%d\", \"tid\": \"%d\", "
	    "\"cpuflags\": \"0x%08x\", \"cpuflags2\": \"0x%08x\", \"pc\": [ ",
		startent.c_str(), ev->pl_u.pl_cc.pl_pmcid, ev->pl_u.pl_cc.pl_pid,
	    ev->pl_u.pl_cc.pl_tid, ev->pl_u.pl_cc.pl_cpuflags, ev->pl_u.pl_cc.pl_cpuflags2);
	result = string(eventbuf);
	for (i = 0; i < ev->pl_u.pl_cc.pl_npc - 1; i++) {
		snprintf(eventbuf, sizeof(eventbuf), "\"0x%016jx\", ", (uintmax_t)ev->pl_u.pl_cc.pl_pc[i]);
		result += string(eventbuf);
	}
	snprintf(eventbuf, sizeof(eventbuf), "\"0x%016jx\"]}\n", (uintmax_t)ev->pl_u.pl_cc.pl_pc[i]);
	result += string(eventbuf);
	return (result);
}

static string
pmcallocatedyn_to_json(struct pmclog_ev *ev)
{
	char eventbuf[2048];
	string startent;

	startent = startentry(ev);
	snprintf(eventbuf, sizeof(eventbuf),
	    "%s, \"pmcid\": \"0x%08x\", \"event\": \"%d\", \"flags\": \"0x%08x\", \"evname\": \"%s\"}\n",
	    startent.c_str(), ev->pl_u.pl_ad.pl_pmcid, ev->pl_u.pl_ad.pl_event,
	    ev->pl_u.pl_ad.pl_flags, ev->pl_u.pl_ad.pl_evname);
	return string(eventbuf);
}

static string
proccreate_to_json(struct pmclog_ev *ev)
{
	char eventbuf[2048];
	string startent;

	startent = startentry(ev);
	snprintf(eventbuf, sizeof(eventbuf),
	    "%s, \"pid\": \"%d\", \"flags\": \"0x%08x\", \"pcomm\": \"%s\"}\n",
	    startent.c_str(), ev->pl_u.pl_pc.pl_pid,
	    ev->pl_u.pl_pc.pl_flags, ev->pl_u.pl_pc.pl_pcomm);
	return string(eventbuf);
}

static string
threadcreate_to_json(struct pmclog_ev *ev)
{
	char eventbuf[2048];
	string startent;

	startent = startentry(ev);
	snprintf(eventbuf, sizeof(eventbuf),
	    "%s, \"tid\": \"%d\", \"pid\": \"%d\", \"flags\": \"0x%08x\", \"tdname\": \"%s\"}\n",
	    startent.c_str(), ev->pl_u.pl_tc.pl_tid, ev->pl_u.pl_tc.pl_pid,
	    ev->pl_u.pl_tc.pl_flags, ev->pl_u.pl_tc.pl_tdname);
	return string(eventbuf);
}

static string
threadexit_to_json(struct pmclog_ev *ev)
{
	char eventbuf[256];
	string startent;

	startent = startentry(ev);
	snprintf(eventbuf, sizeof(eventbuf), "%s, \"tid\": \"%d\"}\n",
	    startent.c_str(), ev->pl_u.pl_te.pl_tid);
	return string(eventbuf);
}

static string
stub_to_json(struct pmclog_ev *ev)
{
	string startent;

	startent = startentry(ev);
	startent += string("}\n");
	return startent;
}

typedef string (*jconv) (struct pmclog_ev*);

static jconv jsonconvert[] = {
	NULL,
	stub_to_json,
	stub_to_json,
	initialize_to_json,
	NULL,
	pmcallocate_to_json,
	pmcattach_to_json,
	pmcdetach_to_json,
	proccsw_to_json,
	procexec_to_json,
	procexit_to_json,
	procfork_to_json,
	sysexit_to_json,
	userdata_to_json,
	map_in_to_json,
	map_out_to_json,
	callchain_to_json,
	pmcallocatedyn_to_json,
	threadcreate_to_json,
	threadexit_to_json,
	proccreate_to_json,
};

string
event_to_json(struct pmclog_ev *ev){

	switch (ev->pl_type) {
	case PMCLOG_TYPE_DROPNOTIFY:
	case PMCLOG_TYPE_CLOSELOG:
	case PMCLOG_TYPE_INITIALIZE:
	case PMCLOG_TYPE_PMCALLOCATE:
	case PMCLOG_TYPE_PMCATTACH:
	case PMCLOG_TYPE_PMCDETACH:
	case PMCLOG_TYPE_PROCCSW:
	case PMCLOG_TYPE_PROCEXEC:
	case PMCLOG_TYPE_PROCEXIT:
	case PMCLOG_TYPE_PROCFORK:
	case PMCLOG_TYPE_SYSEXIT:
	case PMCLOG_TYPE_USERDATA:
	case PMCLOG_TYPE_MAP_IN:
	case PMCLOG_TYPE_MAP_OUT:
	case PMCLOG_TYPE_CALLCHAIN:
	case PMCLOG_TYPE_PMCALLOCATEDYN:
	case PMCLOG_TYPE_THR_CREATE:
	case PMCLOG_TYPE_THR_EXIT:
	case PMCLOG_TYPE_PROC_CREATE:
		return jsonconvert[ev->pl_type](ev);
	default:
		errx(EX_USAGE, "ERROR: unrecognized event type: %d\n", ev->pl_type);
	}
}

