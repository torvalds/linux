#!/usr/sbin/dtrace -s

/*-
 * Copyright (c) 2012-2016 Ryan Stone
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

/*
 * DTrace script for collecting schedgraph data.  Run the output
 * of this script through make_ktr before feeding it into
 * schedgraph.py
 *
 * e.g.
 * # ./schedgraph.d > /tmp/sched.out
 * # ./make_ktr.sh /tmp/sched.out /tmp/sched.ktr
 * # python schedgraph.py /tmp/sched.ktr 1
 */

#pragma D option quiet
#pragma D option bufpolicy=ring

inline int TDF_IDLETD = 0x00000020;

/*
 * Reasons that the current thread can not be run yet.
 * More than one may apply.
 */
inline int TDI_SUSPENDED = 0x0001;	/* On suspension queue. */
inline int TDI_SLEEPING = 0x0002;	/* Actually asleep! (tricky). */
inline int TDI_SWAPPED = 0x0004;	/* Stack not in mem.  Bad juju if run. */
inline int TDI_LOCK = 0x0008;		/* Stopped on a lock. */
inline int TDI_IWAIT = 0x0010;		/* Awaiting interrupt. */

inline string KTDSTATE[struct thread * td] = \
	(((td)->td_inhibitors & TDI_SLEEPING) != 0 ? "sleep"  :		\
	((td)->td_inhibitors & TDI_SUSPENDED) != 0 ? "suspended" :	\
	((td)->td_inhibitors & TDI_SWAPPED) != 0 ? "swapped" :		\
	((td)->td_inhibitors & TDI_LOCK) != 0 ? "blocked" :		\
	((td)->td_inhibitors & TDI_IWAIT) != 0 ? "iwait" : "yielding");

/* 
 * NOCPU changed from 255 to -1 at some point.  This hacky test will work on a
 * kernel compiled with either version.
 */
inline int is_nocpu[int cpu] = cpu < 0 || cpu > `mp_maxid;

sched:::load-change
/ is_nocpu[args[0]] /
{
	printf("%d %d KTRGRAPH group:\"load\", id:\"global load\", counter:\"%d\", attributes: \"none\"\n", cpu, timestamp, args[1]);
}

sched:::load-change
/ !is_nocpu[args[0]] /
{
	printf("%d %d KTRGRAPH group:\"load\", id:\"CPU %d load\", counter:\"%d\", attributes: \"none\"\n", cpu, timestamp, args[0], args[1]);

}

proc:::exit
{
	printf("%d %d KTRGRAPH group:\"thread\", id:\"%s/%s tid %d\", state:\"proc exit\", attributes: prio:td\n", cpu, timestamp, curthread->td_proc->p_comm, curthread->td_name, curthread->td_tid);
}

proc:::lwp-exit
{
	printf("%d %d KTRGRAPH group:\"thread\", id:\"%s/%s tid %d\", state:\"exit\", attributes: prio:td\n", cpu, timestamp, curthread->td_proc->p_comm, curthread->td_name, curthread->td_tid);
}

sched:::change-pri
{
	printf("%d %d KTRGRAPH group:\"thread\", id:\"%s/%s tid %d\", point:\"priority change\", attributes: prio:%d, new prio:%d, linkedto:\"%s/%s tid %d\"\n", cpu, timestamp, args[0]->td_proc->p_comm, args[0]->td_name, args[0]->td_tid, args[0]->td_priority, arg2, curthread->td_proc->p_comm, curthread->td_name, curthread->td_tid);
}

sched:::lend-pri
{
	printf("%d %d KTRGRAPH group:\"thread\", id:\"%s/%s tid %d\", point:\"lend prio\", attributes: prio:%d, new prio:%d, linkedto:\"%s/%s tid %d\"\n", cpu, timestamp, curthread->td_proc->p_comm, curthread->td_name, curthread->td_tid, args[0]->td_priority, arg2, args[0]->td_proc->p_comm, args[0]->td_name, args[0]->td_tid);
}

sched:::enqueue
{
	printf("%d %d KTRGRAPH group:\"thread\", id:\"%s/%s tid %d\", state:\"runq add\", attributes: prio:%d, linkedto:\"%s/%s tid %d\"\n", cpu, timestamp, args[0]->td_proc->p_comm, args[0]->td_name, args[0]->td_tid, args[0]->td_priority, curthread->td_proc->p_comm, curthread->td_name, curthread->td_tid);
	printf("%d %d KTRGRAPH group:\"thread\", id:\"%s/%s tid %d\", point:\"wokeup\", attributes: linkedto:\"%s/%s tid %d\"\n", cpu, timestamp, curthread->td_proc->p_comm, curthread->td_name, curthread->td_tid, args[0]->td_proc->p_comm, args[0]->td_name, args[0]->td_tid);
}

sched:::dequeue
{
	printf("%d %d KTRGRAPH group:\"thread\", id:\"%s/%s tid %d\", state:\"runq rem\", attributes: prio:%d, linkedto:\"%s/%s tid %d\"\n", cpu, timestamp, args[0]->td_proc->p_comm, args[0]->td_name, args[0]->td_tid, args[0]->td_priority, curthread->td_proc->p_comm, curthread->td_name, curthread->td_tid);
}

sched:::tick
{
	printf("%d %d KTRGRAPH group:\"thread\", id:\"%s/%s tid %d\", point:\"statclock\", attributes: prio:%d, stathz:%d\n", cpu, timestamp, args[0]->td_proc->p_comm, args[0]->td_name, args[0]->td_tid, args[0]->td_priority, `stathz ? `stathz : `hz);
}

sched:::off-cpu
/ curthread->td_flags & TDF_IDLETD /
{
	printf("%d %d KTRGRAPH group:\"thread\", id:\"%s/%s tid %d\", state:\"idle\", attributes: prio:%d\n", cpu, timestamp, curthread->td_proc->p_comm, curthread->td_name, curthread->td_tid, curthread->td_priority);
}

sched:::off-cpu
/ (curthread->td_flags & TDF_IDLETD) == 0 /
{
	printf("%d %d KTRGRAPH group:\"thread\", id:\"%s/%s tid %d\", state:\"%s\", attributes: prio:%d, wmesg:\"%s\", lockname:\"%s\"\n", cpu, timestamp, curthread->td_proc->p_comm, curthread->td_name, curthread->td_tid, KTDSTATE[curthread], curthread->td_priority, curthread->td_wmesg ? stringof(curthread->td_wmesg) : "(null)", curthread->td_lockname ? stringof(curthread->td_lockname) : "(null)");
}

sched:::on-cpu
{
	printf("%d %d KTRGRAPH group:\"thread\", id:\"%s/%s tid %d\", state:\"running\", attributes: prio:%d\n", cpu, timestamp, curthread->td_proc->p_comm, curthread->td_name, curthread->td_tid, curthread->td_priority);
}

