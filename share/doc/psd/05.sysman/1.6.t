.\" Copyright (c) 1983, 1993
.\"	The Regents of the University of California.  All rights reserved.
.\"
.\" Redistribution and use in source and binary forms, with or without
.\" modification, are permitted provided that the following conditions
.\" are met:
.\" 1. Redistributions of source code must retain the above copyright
.\"    notice, this list of conditions and the following disclaimer.
.\" 2. Redistributions in binary form must reproduce the above copyright
.\"    notice, this list of conditions and the following disclaimer in the
.\"    documentation and/or other materials provided with the distribution.
.\" 3. Neither the name of the University nor the names of its contributors
.\"    may be used to endorse or promote products derived from this software
.\"    without specific prior written permission.
.\"
.\" THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
.\" ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
.\" IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
.\" ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
.\" FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
.\" DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
.\" OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
.\" HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
.\" LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
.\" OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
.\" SUCH DAMAGE.
.\"
.\"	@(#)1.6.t	8.1 (Berkeley) 6/8/93
.\"
.sh "Resource controls
.NH 3
Process priorities
.PP
The system gives CPU scheduling priority to processes that have not used
CPU time recently.  This tends to favor interactive processes and
processes that execute only for short periods.
It is possible to determine the priority currently
assigned to a process, process group, or the processes of a specified user,
or to alter this priority using the calls:
.DS
._d
#define	PRIO_PROCESS	0	/* process */
#define	PRIO_PGRP	1	/* process group */
#define	PRIO_USER	2	/* user id */

prio = getpriority(which, who);
result int prio; int which, who;

setpriority(which, who, prio);
int which, who, prio;
.DE
The value \fIprio\fP is in the range \-20 to 20.
The default priority is 0; lower priorities cause more
favorable execution.
The \fIgetpriority\fP call returns the highest priority (lowest numerical value)
enjoyed by any of the specified processes.
The \fIsetpriority\fP call sets the priorities of all of the
specified processes to the specified value.
Only the super-user may lower priorities.
.NH 3
Resource utilization
.PP
The resources used by a process are returned by a \fIgetrusage\fP call,
returning information in a structure defined in \fI<sys/resource.h>\fP:
.DS
._d
#define	RUSAGE_SELF	0		/* usage by this process */
#define	RUSAGE_CHILDREN	-1		/* usage by all children */

getrusage(who, rusage)
int who; result struct rusage *rusage;

._f
struct rusage {
	struct	timeval ru_utime;	/* user time used */
	struct	timeval ru_stime;	/* system time used */
	int	ru_maxrss;	/* maximum core resident set size: kbytes */
	int	ru_ixrss;	/* integral shared memory size (kbytes*sec) */
	int	ru_idrss;	/* unshared data memory size */
	int	ru_isrss;	/* unshared stack memory size */
	int	ru_minflt;	/* page-reclaims */
	int	ru_majflt;	/* page faults */
	int	ru_nswap;	/* swaps */
	int	ru_inblock;	/* block input operations */
	int	ru_oublock;	/* block output operations */
	int	ru_msgsnd;	/* messages sent */
	int	ru_msgrcv;	/* messages received */
	int	ru_nsignals;	/* signals received */
	int	ru_nvcsw;	/* voluntary context switches */
	int	ru_nivcsw;	/* involuntary context switches */
};
.DE
The \fIwho\fP parameter specifies whose resource usage is to be returned.
The resources used by the current process, or by all
the terminated children of the current process may be requested.
.NH 3
Resource limits
.PP
The resources of a process for which limits are controlled by the
kernel are defined in \fI<sys/resource.h>\fP, and controlled by the
\fIgetrlimit\fP and \fIsetrlimit\fP calls:
.DS
._d
#define	RLIMIT_CPU	0	/* cpu time in milliseconds */
#define	RLIMIT_FSIZE	1	/* maximum file size */
#define	RLIMIT_DATA	2	/* maximum data segment size */
#define	RLIMIT_STACK	3	/* maximum stack segment size */
#define	RLIMIT_CORE	4	/* maximum core file size */
#define	RLIMIT_RSS	5	/* maximum resident set size */

#define	RLIM_NLIMITS	6

#define	RLIM_INFINITY	0x7f\&f\&f\&f\&f\&f\&f

._f
struct rlimit {
	int	rlim_cur;	/* current (soft) limit */
	int	rlim_max;	/* hard limit */
};

getrlimit(resource, rlp)
int resource; result struct rlimit *rlp;

setrlimit(resource, rlp)
int resource; struct rlimit *rlp;
.DE
.PP
Only the super-user can raise the maximum limits.
Other users may only
alter \fIrlim_cur\fP within the range from 0 to \fIrlim_max\fP
or (irreversibly) lower \fIrlim_max\fP.
