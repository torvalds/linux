/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2011 Juniper Networks, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert N. M. Watson under contract
 * to Juniper Networks, Inc.
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
#include <sys/sysctl.h>

#include <sys/_lock.h>
#include <sys/_mutex.h>

#define	_WANT_NETISR_INTERNAL
#include <net/netisr.h>
#include <net/netisr_internal.h>

#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <libxo/xo.h>
#include "netstat.h"
#include "nl_defs.h"

/*
 * Print statistics for the kernel netisr subsystem.
 */
static u_int				 bindthreads;
static u_int				 maxthreads;
static u_int				 numthreads;

static u_int				 defaultqlimit;
static u_int				 maxqlimit;

static char				 dispatch_policy[20];

static struct sysctl_netisr_proto	*proto_array;
static u_int				 proto_array_len;

static struct sysctl_netisr_workstream	*workstream_array;
static u_int				 workstream_array_len;

static struct sysctl_netisr_work	*work_array;
static u_int				 work_array_len;

static u_int				*nws_array;

static u_int				 maxprot;

static void
netisr_dispatch_policy_to_string(u_int policy, char *buf,
    size_t buflen)
{
	const char *str;

	switch (policy) {
	case NETISR_DISPATCH_DEFAULT:
		str = "default";
		break;
	case NETISR_DISPATCH_DEFERRED:
		str = "deferred";
		break;
	case NETISR_DISPATCH_HYBRID:
		str = "hybrid";
		break;
	case NETISR_DISPATCH_DIRECT:
		str = "direct";
		break;
	default:
		str = "unknown";
		break;
	}
	snprintf(buf, buflen, "%s", str);
}

/*
 * Load a nul-terminated string from KVM up to 'limit', guarantee that the
 * string in local memory is nul-terminated.
 */
static void
netisr_load_kvm_string(uintptr_t addr, char *dest, u_int limit)
{
	u_int i;

	for (i = 0; i < limit; i++) {
		if (kread(addr + i, &dest[i], sizeof(dest[i])) != 0)
			xo_errx(-1, "%s: kread()", __func__);
		if (dest[i] == '\0')
			break;
	}
	dest[limit - 1] = '\0';
}

static const char *
netisr_proto2name(u_int proto)
{
	u_int i;

	for (i = 0; i < proto_array_len; i++) {
		if (proto_array[i].snp_proto == proto)
			return (proto_array[i].snp_name);
	}
	return ("unknown");
}

static int
netisr_protoispresent(u_int proto)
{
	u_int i;

	for (i = 0; i < proto_array_len; i++) {
		if (proto_array[i].snp_proto == proto)
			return (1);
	}
	return (0);
}

static void
netisr_load_kvm_config(void)
{
	u_int tmp;

	kread(nl[N_NETISR_BINDTHREADS].n_value, &bindthreads, sizeof(u_int));
	kread(nl[N_NETISR_MAXTHREADS].n_value, &maxthreads, sizeof(u_int));
	kread(nl[N_NWS_COUNT].n_value, &numthreads, sizeof(u_int));
	kread(nl[N_NETISR_DEFAULTQLIMIT].n_value, &defaultqlimit,
	    sizeof(u_int));
	kread(nl[N_NETISR_MAXQLIMIT].n_value, &maxqlimit, sizeof(u_int));
	kread(nl[N_NETISR_DISPATCH_POLICY].n_value, &tmp, sizeof(u_int));

	netisr_dispatch_policy_to_string(tmp, dispatch_policy,
	    sizeof(dispatch_policy));
}

static void
netisr_load_sysctl_uint(const char *name, u_int *p)
{
	size_t retlen;

	retlen = sizeof(u_int);
	if (sysctlbyname(name, p, &retlen, NULL, 0) < 0)
		xo_err(-1, "%s", name);
	if (retlen != sizeof(u_int))
		xo_errx(-1, "%s: invalid len %ju", name, (uintmax_t)retlen);
}

static void
netisr_load_sysctl_string(const char *name, char *p, size_t len)
{
	size_t retlen;

	retlen = len;
	if (sysctlbyname(name, p, &retlen, NULL, 0) < 0)
		xo_err(-1, "%s", name);
	p[len - 1] = '\0';
}

static void
netisr_load_sysctl_config(void)
{

	netisr_load_sysctl_uint("net.isr.bindthreads", &bindthreads);
	netisr_load_sysctl_uint("net.isr.maxthreads", &maxthreads);
	netisr_load_sysctl_uint("net.isr.numthreads", &numthreads);

	netisr_load_sysctl_uint("net.isr.defaultqlimit", &defaultqlimit);
	netisr_load_sysctl_uint("net.isr.maxqlimit", &maxqlimit);

	netisr_load_sysctl_string("net.isr.dispatch", dispatch_policy,
	    sizeof(dispatch_policy));
}

static void
netisr_load_kvm_proto(void)
{
	struct netisr_proto *np_array, *npp;
	u_int i, protocount;
	struct sysctl_netisr_proto *snpp;
	size_t len;

	/*
	 * Kernel compile-time and user compile-time definitions of
	 * NETISR_MAXPROT must match, as we use that to size work arrays.
	 */
	kread(nl[N_NETISR_MAXPROT].n_value, &maxprot, sizeof(u_int));
	if (maxprot != NETISR_MAXPROT)
		xo_errx(-1, "%s: NETISR_MAXPROT mismatch", __func__);
	len = maxprot * sizeof(*np_array);
	np_array = malloc(len);
	if (np_array == NULL)
		xo_err(-1, "%s: malloc", __func__);
	if (kread(nl[N_NETISR_PROTO].n_value, np_array, len) != 0)
		xo_errx(-1, "%s: kread(_netisr_proto)", __func__);

	/*
	 * Size and allocate memory to hold only live protocols.
	 */
	protocount = 0;
	for (i = 0; i < maxprot; i++) {
		if (np_array[i].np_name == NULL)
			continue;
		protocount++;
	}
	proto_array = calloc(protocount, sizeof(*proto_array));
	if (proto_array == NULL)
		err(-1, "malloc");
	protocount = 0;
	for (i = 0; i < maxprot; i++) {
		npp = &np_array[i];
		if (npp->np_name == NULL)
			continue;
		snpp = &proto_array[protocount];
		snpp->snp_version = sizeof(*snpp);
		netisr_load_kvm_string((uintptr_t)npp->np_name,
		    snpp->snp_name, sizeof(snpp->snp_name));
		snpp->snp_proto = i;
		snpp->snp_qlimit = npp->np_qlimit;
		snpp->snp_policy = npp->np_policy;
		snpp->snp_dispatch = npp->np_dispatch;
		if (npp->np_m2flow != NULL)
			snpp->snp_flags |= NETISR_SNP_FLAGS_M2FLOW;
		if (npp->np_m2cpuid != NULL)
			snpp->snp_flags |= NETISR_SNP_FLAGS_M2CPUID;
		if (npp->np_drainedcpu != NULL)
			snpp->snp_flags |= NETISR_SNP_FLAGS_DRAINEDCPU;
		protocount++;
	}
	proto_array_len = protocount;
	free(np_array);
}

static void
netisr_load_sysctl_proto(void)
{
	size_t len;

	if (sysctlbyname("net.isr.proto", NULL, &len, NULL, 0) < 0)
		xo_err(-1, "net.isr.proto: query len");
	if (len % sizeof(*proto_array) != 0)
		xo_errx(-1, "net.isr.proto: invalid len");
	proto_array = malloc(len);
	if (proto_array == NULL)
		xo_err(-1, "malloc");
	if (sysctlbyname("net.isr.proto", proto_array, &len, NULL, 0) < 0)
		xo_err(-1, "net.isr.proto: query data");
	if (len % sizeof(*proto_array) != 0)
		xo_errx(-1, "net.isr.proto: invalid len");
	proto_array_len = len / sizeof(*proto_array);
	if (proto_array_len < 1)
		xo_errx(-1, "net.isr.proto: no data");
	if (proto_array[0].snp_version != sizeof(proto_array[0]))
		xo_errx(-1, "net.isr.proto: invalid version");
}

static void
netisr_load_kvm_workstream(void)
{
	struct netisr_workstream nws;
	struct sysctl_netisr_workstream *snwsp;
	struct sysctl_netisr_work *snwp;
	struct netisr_work *nwp;
	u_int counter, cpuid, proto, wsid;
	size_t len;

	len = numthreads * sizeof(*nws_array);
	nws_array = malloc(len);
	if (nws_array == NULL)
		xo_err(-1, "malloc");
	if (kread(nl[N_NWS_ARRAY].n_value, nws_array, len) != 0)
		xo_errx(-1, "%s: kread(_nws_array)", __func__);
	workstream_array = calloc(numthreads, sizeof(*workstream_array));
	if (workstream_array == NULL)
		xo_err(-1, "calloc");
	workstream_array_len = numthreads;
	work_array = calloc(numthreads * proto_array_len, sizeof(*work_array));
	if (work_array == NULL)
		xo_err(-1, "calloc");
	counter = 0;
	for (wsid = 0; wsid < numthreads; wsid++) {
		cpuid = nws_array[wsid];
		kset_dpcpu(cpuid);
		if (kread(nl[N_NWS].n_value, &nws, sizeof(nws)) != 0)
			xo_errx(-1, "%s: kread(nw)", __func__);
		snwsp = &workstream_array[wsid];
		snwsp->snws_version = sizeof(*snwsp);
		snwsp->snws_wsid = cpuid;
		snwsp->snws_cpu = cpuid;
		if (nws.nws_intr_event != NULL)
			snwsp->snws_flags |= NETISR_SNWS_FLAGS_INTR;

		/*
		 * Extract the CPU's per-protocol work information.
		 */
		xo_emit("counting to maxprot: {:maxprot/%u}\n", maxprot);
		for (proto = 0; proto < maxprot; proto++) {
			if (!netisr_protoispresent(proto))
				continue;
			nwp = &nws.nws_work[proto];
			snwp = &work_array[counter];
			snwp->snw_version = sizeof(*snwp);
			snwp->snw_wsid = cpuid;
			snwp->snw_proto = proto;
			snwp->snw_len = nwp->nw_len;
			snwp->snw_watermark = nwp->nw_watermark;
			snwp->snw_dispatched = nwp->nw_dispatched;
			snwp->snw_hybrid_dispatched =
			    nwp->nw_hybrid_dispatched;
			snwp->snw_qdrops = nwp->nw_qdrops;
			snwp->snw_queued = nwp->nw_queued;
			snwp->snw_handled = nwp->nw_handled;
			counter++;
		}
	}
	work_array_len = counter;
}

static void
netisr_load_sysctl_workstream(void)
{
	size_t len;

	if (sysctlbyname("net.isr.workstream", NULL, &len, NULL, 0) < 0)
		xo_err(-1, "net.isr.workstream: query len");
	if (len % sizeof(*workstream_array) != 0)
		xo_errx(-1, "net.isr.workstream: invalid len");
	workstream_array = malloc(len);
	if (workstream_array == NULL)
		xo_err(-1, "malloc");
	if (sysctlbyname("net.isr.workstream", workstream_array, &len, NULL,
	    0) < 0)
		xo_err(-1, "net.isr.workstream: query data");
	if (len % sizeof(*workstream_array) != 0)
		xo_errx(-1, "net.isr.workstream: invalid len");
	workstream_array_len = len / sizeof(*workstream_array);
	if (workstream_array_len < 1)
		xo_errx(-1, "net.isr.workstream: no data");
	if (workstream_array[0].snws_version != sizeof(workstream_array[0]))
		xo_errx(-1, "net.isr.workstream: invalid version");
}

static void
netisr_load_sysctl_work(void)
{
	size_t len;

	if (sysctlbyname("net.isr.work", NULL, &len, NULL, 0) < 0)
		xo_err(-1, "net.isr.work: query len");
	if (len % sizeof(*work_array) != 0)
		xo_errx(-1, "net.isr.work: invalid len");
	work_array = malloc(len);
	if (work_array == NULL)
		xo_err(-1, "malloc");
	if (sysctlbyname("net.isr.work", work_array, &len, NULL, 0) < 0)
		xo_err(-1, "net.isr.work: query data");
	if (len % sizeof(*work_array) != 0)
		xo_errx(-1, "net.isr.work: invalid len");
	work_array_len = len / sizeof(*work_array);
	if (work_array_len < 1)
		xo_errx(-1, "net.isr.work: no data");
	if (work_array[0].snw_version != sizeof(work_array[0]))
		xo_errx(-1, "net.isr.work: invalid version");
}

static void
netisr_print_proto(struct sysctl_netisr_proto *snpp)
{
	char tmp[20];

	xo_emit("{[:-6}{k:name/%s}{]:}", snpp->snp_name);
	xo_emit(" {:protocol/%5u}", snpp->snp_proto);
	xo_emit(" {:queue-limit/%6u}", snpp->snp_qlimit);
	xo_emit(" {:policy-type/%6s}",
	    (snpp->snp_policy == NETISR_POLICY_SOURCE) ?  "source" :
	    (snpp->snp_policy == NETISR_POLICY_FLOW) ? "flow" :
	    (snpp->snp_policy == NETISR_POLICY_CPU) ? "cpu" : "-");
	netisr_dispatch_policy_to_string(snpp->snp_dispatch, tmp,
	    sizeof(tmp));
	xo_emit(" {:policy/%8s}", tmp);
	xo_emit("   {:flags/%s%s%s}\n",
	    (snpp->snp_flags & NETISR_SNP_FLAGS_M2CPUID) ?  "C" : "-",
	    (snpp->snp_flags & NETISR_SNP_FLAGS_DRAINEDCPU) ?  "D" : "-",
	    (snpp->snp_flags & NETISR_SNP_FLAGS_M2FLOW) ? "F" : "-");
}

static void
netisr_print_workstream(struct sysctl_netisr_workstream *snwsp)
{
	struct sysctl_netisr_work *snwp;
	u_int i;

	xo_open_list("work");
	for (i = 0; i < work_array_len; i++) {
		snwp = &work_array[i];
		if (snwp->snw_wsid != snwsp->snws_wsid)
			continue;
		xo_open_instance("work");
		xo_emit("{t:workstream/%4u} ", snwsp->snws_wsid);
		xo_emit("{t:cpu/%3u} ", snwsp->snws_cpu);
		xo_emit("{P:  }");
		xo_emit("{t:name/%-6s}", netisr_proto2name(snwp->snw_proto));
		xo_emit(" {t:length/%5u}", snwp->snw_len);
		xo_emit(" {t:watermark/%5u}", snwp->snw_watermark);
		xo_emit(" {t:dispatched/%8ju}", snwp->snw_dispatched);
		xo_emit(" {t:hybrid-dispatched/%8ju}",
		    snwp->snw_hybrid_dispatched);
		xo_emit(" {t:queue-drops/%8ju}", snwp->snw_qdrops);
		xo_emit(" {t:queued/%8ju}", snwp->snw_queued);
		xo_emit(" {t:handled/%8ju}", snwp->snw_handled);
		xo_emit("\n");
		xo_close_instance("work");
	}
	xo_close_list("work");
}

void
netisr_stats(void)
{
	struct sysctl_netisr_workstream *snwsp;
	struct sysctl_netisr_proto *snpp;
	u_int i;

	if (live) {
		netisr_load_sysctl_config();
		netisr_load_sysctl_proto();
		netisr_load_sysctl_workstream();
		netisr_load_sysctl_work();
	} else {
		netisr_load_kvm_config();
		netisr_load_kvm_proto();
		netisr_load_kvm_workstream();		/* Also does work. */
	}

	xo_open_container("netisr");

	xo_emit("{T:Configuration}:\n");
	xo_emit("{T:/%-25s} {T:/%12s} {T:/%12s}\n",
	    "Setting", "Current", "Limit");
	xo_emit("{T:/%-25s} {T:/%12u} {T:/%12u}\n",
	    "Thread count", numthreads, maxthreads);
	xo_emit("{T:/%-25s} {T:/%12u} {T:/%12u}\n",
	    "Default queue limit", defaultqlimit, maxqlimit);
	xo_emit("{T:/%-25s} {T:/%12s} {T:/%12s}\n",
	    "Dispatch policy", dispatch_policy, "n/a");
	xo_emit("{T:/%-25s} {T:/%12s} {T:/%12s}\n",
	    "Threads bound to CPUs", bindthreads ? "enabled" : "disabled",
	    "n/a");
	xo_emit("\n");

	xo_emit("{T:Protocols}:\n");
	xo_emit("{T:/%-6s} {T:/%5s} {T:/%6s} {T:/%-6s} {T:/%-8s} {T:/%-5s}\n",
	    "Name", "Proto", "QLimit", "Policy", "Dispatch", "Flags");
	xo_open_list("protocol");
	for (i = 0; i < proto_array_len; i++) {
		xo_open_instance("protocol");
		snpp = &proto_array[i];
		netisr_print_proto(snpp);
		xo_close_instance("protocol");
	}
	xo_close_list("protocol");
	xo_emit("\n");

	xo_emit("{T:Workstreams}:\n");
	xo_emit("{T:/%4s} {T:/%3s} ", "WSID", "CPU");
	xo_emit("{P:/%2s}", "");
	xo_emit("{T:/%-6s} {T:/%5s} {T:/%5s} {T:/%8s} {T:/%8s} {T:/%8s} "
	    "{T:/%8s} {T:/%8s}\n",
	    "Name", "Len", "WMark", "Disp'd", "HDisp'd", "QDrops", "Queued",
	    "Handled");
	xo_open_list("workstream");
	for (i = 0; i < workstream_array_len; i++) {
		xo_open_instance("workstream");
		snwsp = &workstream_array[i];
		netisr_print_workstream(snwsp);
		xo_close_instance("workstream");
	}
	xo_close_list("workstream");
	xo_close_container("netisr");
}
