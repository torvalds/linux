/*
 * Copyright (c) 2004 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/cpuset.h>
#include <sys/proc.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <err.h>
#include <inttypes.h>
#include <kvm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <defs.h>
#include <frame-unwind.h>

#include "kgdb.h"

static CORE_ADDR dumppcb;
static int dumptid;

static cpuset_t stopped_cpus;

static struct kthr *first;
struct kthr *curkthr;

CORE_ADDR
kgdb_lookup(const char *sym)
{
	CORE_ADDR addr;
	char *name;

	asprintf(&name, "&%s", sym);
	addr = kgdb_parse(name);
	free(name);
	return (addr);
}

struct kthr *
kgdb_thr_first(void)
{
	return (first);
}

static void
kgdb_thr_add_procs(uintptr_t paddr)
{
	struct proc p;
	struct thread td;
	struct kthr *kt;
	CORE_ADDR addr;

	while (paddr != 0) {
		if (kvm_read(kvm, paddr, &p, sizeof(p)) != sizeof(p)) {
			warnx("kvm_read: %s", kvm_geterr(kvm));
			break;
		}
		addr = (uintptr_t)TAILQ_FIRST(&p.p_threads);
		while (addr != 0) {
			if (kvm_read(kvm, addr, &td, sizeof(td)) !=
			    sizeof(td)) {
				warnx("kvm_read: %s", kvm_geterr(kvm));
				break;
			}
			kt = malloc(sizeof(*kt));
			kt->next = first;
			kt->kaddr = addr;
			if (td.td_tid == dumptid)
				kt->pcb = dumppcb;
			else if (td.td_oncpu != NOCPU &&
			    CPU_ISSET(td.td_oncpu, &stopped_cpus))
				kt->pcb = kgdb_trgt_core_pcb(td.td_oncpu);
			else
				kt->pcb = (uintptr_t)td.td_pcb;
			kt->kstack = td.td_kstack;
			kt->tid = td.td_tid;
			kt->pid = p.p_pid;
			kt->paddr = paddr;
			kt->cpu = td.td_oncpu;
			first = kt;
			addr = (uintptr_t)TAILQ_NEXT(&td, td_plist);
		}
		paddr = (uintptr_t)LIST_NEXT(&p, p_list);
	}
}

struct kthr *
kgdb_thr_init(void)
{
	long cpusetsize;
	struct kthr *kt;
	CORE_ADDR addr;
	uintptr_t paddr;
	
	while (first != NULL) {
		kt = first;
		first = kt->next;
		free(kt);
	}

	addr = kgdb_lookup("allproc");
	if (addr == 0)
		return (NULL);
	kvm_read(kvm, addr, &paddr, sizeof(paddr));

	dumppcb = kgdb_lookup("dumppcb");
	if (dumppcb == 0)
		return (NULL);

	addr = kgdb_lookup("dumptid");
	if (addr != 0)
		kvm_read(kvm, addr, &dumptid, sizeof(dumptid));
	else
		dumptid = -1;

	addr = kgdb_lookup("stopped_cpus");
	CPU_ZERO(&stopped_cpus);
	cpusetsize = sysconf(_SC_CPUSET_SIZE);
	if (cpusetsize != -1 && (u_long)cpusetsize <= sizeof(cpuset_t) &&
	    addr != 0)
		kvm_read(kvm, addr, &stopped_cpus, cpusetsize);

	kgdb_thr_add_procs(paddr);
	addr = kgdb_lookup("zombproc");
	if (addr != 0) {
		kvm_read(kvm, addr, &paddr, sizeof(paddr));
		kgdb_thr_add_procs(paddr);
	}
	curkthr = kgdb_thr_lookup_tid(dumptid);
	if (curkthr == NULL)
		curkthr = first;
	return (first);
}

struct kthr *
kgdb_thr_lookup_tid(int tid)
{
	struct kthr *kt;

	kt = first;
	while (kt != NULL && kt->tid != tid)
		kt = kt->next;
	return (kt);
}

struct kthr *
kgdb_thr_lookup_taddr(uintptr_t taddr)
{
	struct kthr *kt;

	kt = first;
	while (kt != NULL && kt->kaddr != taddr)
		kt = kt->next;
	return (kt);
}

struct kthr *
kgdb_thr_lookup_pid(int pid)
{
	struct kthr *kt;

	kt = first;
	while (kt != NULL && kt->pid != pid)
		kt = kt->next;
	return (kt);
}

struct kthr *
kgdb_thr_lookup_paddr(uintptr_t paddr)
{
	struct kthr *kt;

	kt = first;
	while (kt != NULL && kt->paddr != paddr)
		kt = kt->next;
	return (kt);
}

struct kthr *
kgdb_thr_next(struct kthr *kt)
{
	return (kt->next);
}

struct kthr *
kgdb_thr_select(struct kthr *kt)
{
	struct kthr *pcur;

	pcur = curkthr;
	curkthr = kt;
	return (pcur);
}

char *
kgdb_thr_extra_thread_info(int tid)
{
	char comm[MAXCOMLEN + 1];
	char td_name[MAXCOMLEN + 1];
	struct kthr *kt;
	struct proc *p;
	struct thread *t;
	static char buf[64];

	kt = kgdb_thr_lookup_tid(tid);
	if (kt == NULL)
		return (NULL);	
	snprintf(buf, sizeof(buf), "PID=%d", kt->pid);
	p = (struct proc *)kt->paddr;
	if (kvm_read(kvm, (uintptr_t)&p->p_comm[0], &comm, sizeof(comm)) !=
	    sizeof(comm))
		return (buf);
	strlcat(buf, ": ", sizeof(buf));
	strlcat(buf, comm, sizeof(buf));
	t = (struct thread *)kt->kaddr;
	if (kvm_read(kvm, (uintptr_t)&t->td_name[0], &td_name,
	    sizeof(td_name)) == sizeof(td_name) &&
	    strcmp(comm, td_name) != 0) {
		strlcat(buf, "/", sizeof(buf));
		strlcat(buf, td_name, sizeof(buf));
	}
	return (buf);
}
