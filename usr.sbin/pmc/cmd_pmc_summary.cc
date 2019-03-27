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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/cpuset.h>
#include <sys/event.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/ttycom.h>
#include <sys/user.h>
#include <sys/wait.h>

#include <assert.h>
#include <curses.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <kvm.h>
#include <libgen.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <pmc.h>
#include <pmclog.h>
#include <regex.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <libpmcstat.h>
#include "cmd_pmc.h"

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>

using	std::unordered_map;
typedef unordered_map <int, std::string> idmap;
typedef unordered_map <uint32_t, uint64_t> intmap;
typedef unordered_map <std::string, intmap> strintmap;
typedef std::pair<uint64_t, uint32_t> sampleid;
typedef std::pair<uint64_t, std::string> samplename;
typedef unordered_map <uint32_t, std::vector<samplename>> eventcountmap;

#define	P_KPROC		0x00004	/* Kernel process. */

static void __dead2
usage(void)
{
	errx(EX_USAGE,
	    "\t summarize log file\n"
		 "\t -k <k>, --topk <k> show topk processes for each counter\n"
	    );
}

static int
pmc_summary_handler(int logfd, int k, bool do_full)
{
	struct pmclog_parse_state *ps;
	struct pmclog_ev ev;
	idmap pidmap, tidmap, eventnamemap;
	strintmap tideventmap, pideventmap;
	intmap eventmap, pmcidmap, ratemap;
	intmap kerntidmap, kernpidmap;
	eventcountmap countmap;

	ps = static_cast<struct pmclog_parse_state*>(pmclog_open(logfd));
	if (ps == NULL)
		errx(EX_OSERR, "ERROR: Cannot allocate pmclog parse state: %s\n",
			 strerror(errno));
	while (pmclog_read(ps, &ev) == 0) {
		if (ev.pl_type == PMCLOG_TYPE_PMCALLOCATE) {
			pmcidmap[ev.pl_u.pl_a.pl_pmcid] = ev.pl_u.pl_a.pl_event;
			ratemap[ev.pl_u.pl_a.pl_event] = ev.pl_u.pl_a.pl_rate;
			eventnamemap[ev.pl_u.pl_a.pl_event] = ev.pl_u.pl_a.pl_evname;
		}
		if (ev.pl_type == PMCLOG_TYPE_THR_CREATE) {
			tidmap[ev.pl_u.pl_tc.pl_tid] = ev.pl_u.pl_tc.pl_tdname;
			kerntidmap[ev.pl_u.pl_tc.pl_tid] = !!(ev.pl_u.pl_tc.pl_flags & P_KPROC);
			if (tideventmap.find(ev.pl_u.pl_tc.pl_tdname) == tideventmap.end())
				tideventmap[ev.pl_u.pl_tc.pl_tdname] = intmap();
		}
		if (ev.pl_type == PMCLOG_TYPE_PROC_CREATE) {
			pidmap[ev.pl_u.pl_pc.pl_pid] = ev.pl_u.pl_pc.pl_pcomm;
			kernpidmap[ev.pl_u.pl_pc.pl_pid] = !!(ev.pl_u.pl_pc.pl_flags & P_KPROC);
			if (pideventmap.find(ev.pl_u.pl_pc.pl_pcomm) == pideventmap.end())
				pideventmap[ev.pl_u.pl_pc.pl_pcomm] = intmap();
		}
		if (ev.pl_type == PMCLOG_TYPE_CALLCHAIN) {
			auto event = pmcidmap[ev.pl_u.pl_cc.pl_pmcid];

			if (event == 0)
				continue;
			eventmap[event]++;
			auto tidname = tidmap.find(ev.pl_u.pl_cc.pl_tid);
			auto pidname = pidmap.find(ev.pl_u.pl_cc.pl_pid);
			if (tidname != tidmap.end()) {
				auto &teventmap = tideventmap[tidname->second];
				teventmap[event]++;
			}
			if (pidname != pidmap.end()) {
				auto &peventmap = pideventmap[pidname->second];
				peventmap[event]++;
			}
		}
	}
	for (auto &pkv : pideventmap)
		for (auto &ekv : pkv.second) {
			auto &samplevec = countmap[ekv.first];
			samplevec.emplace_back(ekv.second, pkv.first);
		}
	for (auto &kv : countmap)
		std::sort(kv.second.begin(), kv.second.end(), [](auto &a, auto &b) {return (a.first < b.first);});
	if (do_full) {
		for (auto &kv : countmap) {
			auto &name = eventnamemap[kv.first];
			auto rate = ratemap[kv.first];
			std::cout << "idx: " << kv.first << " name: " << name << " rate: " << rate << std::endl;
			while (!kv.second.empty()) {
				auto &val = kv.second.back();
				kv.second.pop_back();
				std::cout << val.second << ": " << val.first << std::endl;
			}
		}
		return (0);
	}
	for (auto &kv : countmap) {
		auto &name = eventnamemap[kv.first];
		auto rate = ratemap[kv.first];
		std::cout << name << ":" << std::endl;
		for (auto i = 0; i < k; i++) {
			auto largest = kv.second.back();
			kv.second.pop_back();
			std::cout << "\t" << largest.second << ": " << largest.first*rate << std::endl;
		}
	}
	return (0);
}

static struct option longopts[] = {
	{"full", no_argument, NULL, 'f'},
	{"topk", required_argument, NULL, 'k'},
	{NULL, 0, NULL, 0}
};

int
cmd_pmc_summary(int argc, char **argv)
{
	int option, logfd, k;
	bool do_full;

	do_full = false;
	k = 5;
	while ((option = getopt_long(argc, argv, "k:f", longopts, NULL)) != -1) {
		switch (option) {
		case 'f':
			do_full = 1;
			break;
		case 'k':
			k = atoi(optarg);
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1) {
		printf("argc: %d\n", argc);
		for (int i = 0; i < argc; i++)
			printf("%s\n", argv[i]);
		usage();
	}
	if ((logfd = open(argv[0], O_RDONLY,
	    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0)
		errx(EX_OSERR, "ERROR: Cannot open \"%s\" for reading: %s.", argv[0],
		    strerror(errno));

	return (pmc_summary_handler(logfd, k, do_full));
}
