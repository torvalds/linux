/*-
 * Copyright (c) 2007 Robert N. M. Watson
 * Copyright (c) 2015 Allan Jude <allanjude@freebsd.org>
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
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/cpuset.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/user.h>

#include <err.h>
#include <errno.h>
#include <libprocstat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "procstat.h"

void
procstat_cs(struct procstat *procstat, struct kinfo_proc *kipp)
{
	cpusetid_t cs;
	cpuset_t mask;
	struct kinfo_proc *kip;
	struct sbuf *cpusetbuf;
	unsigned int count, i;
	int once, twice, lastcpu, cpu;

	if ((procstat_opts & PS_OPT_NOHEADER) == 0)
		xo_emit("{T:/%5s %6s %-19s %-19s %2s %4s %-7s}\n", "PID",
		    "TID", "COMM", "TDNAME", "CPU", "CSID", "CPU MASK");

	kip = procstat_getprocs(procstat, KERN_PROC_PID | KERN_PROC_INC_THREAD,
	    kipp->ki_pid, &count);
	if (kip == NULL)
		return;
	kinfo_proc_sort(kip, count);
	for (i = 0; i < count; i++) {
		kipp = &kip[i];
		xo_emit("{k:process_id/%5d/%d} ", kipp->ki_pid);
		xo_emit("{:thread_id/%6d/%d} ", kipp->ki_tid);
		xo_emit("{:command/%-19s/%s} ", strlen(kipp->ki_comm) ?
		    kipp->ki_comm : "-");
		xo_emit("{:thread_name/%-19s/%s} ",
                    kinfo_proc_thread_name(kipp));
		if (kipp->ki_oncpu != 255)
			xo_emit("{:cpu/%3d/%d} ", kipp->ki_oncpu);
		else if (kipp->ki_lastcpu != 255)
			xo_emit("{:cpu/%3d/%d} ", kipp->ki_lastcpu);
		else
			xo_emit("{:cpu/%3s/%s} ", "-");
		if (cpuset_getid(CPU_LEVEL_CPUSET, CPU_WHICH_TID,
		    kipp->ki_tid, &cs) != 0) {
			cs = CPUSET_INVALID;
		}
		xo_emit("{:cpu_set_id/%4d/%d} ", cs);
		if ((cs != CPUSET_INVALID) && 
		    (cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID,
		    kipp->ki_tid, sizeof(mask), &mask) == 0)) {
			lastcpu = -1;
			once = 0;
			twice = 0;
			cpusetbuf = sbuf_new_auto();
			for (cpu = 0; cpu < CPU_SETSIZE; cpu++) {
				if (CPU_ISSET(cpu, &mask)) {
					if (once == 0) {
						sbuf_printf(cpusetbuf, "%d",
						    cpu);
						once = 1;
					} else if (cpu == lastcpu + 1) {
						twice = 1;
					} else if (twice == 1) {
						sbuf_printf(cpusetbuf, "-%d,%d",
						    lastcpu, cpu);
						twice = 0;
					} else
						sbuf_printf(cpusetbuf, ",%d",
						    cpu);
					lastcpu = cpu;
				}
			}
			if (once && twice)
				sbuf_printf(cpusetbuf, "-%d", lastcpu);
			if (sbuf_finish(cpusetbuf) != 0)
				xo_err(1, "Could not generate output");
			xo_emit("{:cpu_set/%s}", sbuf_data(cpusetbuf));
			sbuf_delete(cpusetbuf);
		}
		xo_emit("\n");
	}
	procstat_freeprocs(procstat, kip);
}
