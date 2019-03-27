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
.\"	@(#)1.4.t	8.1 (Berkeley) 6/8/93
.\"
.sh "Timers
.NH 3
Real time
.PP
The system's notion of the current Greenwich time and the current time
zone is set and returned by the call by the calls:
.DS
#include <sys/time.h>

settimeofday(tvp, tzp);
struct timeval *tp;
struct timezone *tzp;

gettimeofday(tp, tzp);
result struct timeval *tp;
result struct timezone *tzp;
.DE
where the structures are defined in \fI<sys/time.h>\fP as:
.DS
._f
struct timeval {
	long	tv_sec;	/* seconds since Jan 1, 1970 */
	long	tv_usec;	/* and microseconds */
};

struct timezone {
	int	tz_minuteswest;	/* of Greenwich */
	int	tz_dsttime;	/* type of dst correction to apply */
};
.DE
The precision of the system clock is hardware dependent.
Earlier versions of UNIX contained only a 1-second resolution version
of this call, which remains as a library routine:
.DS
time(tvsec)
result long *tvsec;
.DE
returning only the tv_sec field from the \fIgettimeofday\fP call.
.NH 3
Interval time
.PP
The system provides each process with three interval timers,
defined in \fI<sys/time.h>\fP:
.DS
._d
#define	ITIMER_REAL	0	/* real time intervals */
#define	ITIMER_VIRTUAL	1	/* virtual time intervals */
#define	ITIMER_PROF	2	/* user and system virtual time */
.DE
The ITIMER_REAL timer decrements
in real time.  It could be used by a library routine to
maintain a wakeup service queue.  A SIGALRM signal is delivered
when this timer expires.
.PP
The ITIMER_VIRTUAL timer decrements in process virtual time.
It runs only when the process is executing.  A SIGVTALRM signal
is delivered when it expires.
.PP
The ITIMER_PROF timer decrements both in process virtual time and when
the system is running on behalf of the process.
It is designed to be used by processes to statistically profile
their execution.
A SIGPROF signal is delivered when it expires.
.PP
A timer value is defined by the \fIitimerval\fP structure:
.DS
._f
struct itimerval {
	struct	timeval it_interval;	/* timer interval */
	struct	timeval it_value;	/* current value */
};
.DE
and a timer is set or read by the call:
.DS
getitimer(which, value);
int which; result struct itimerval *value;

setitimer(which, value, ovalue);
int which; struct itimerval *value; result struct itimerval *ovalue;
.DE
The third argument to \fIsetitimer\fP specifies an optional structure
to receive the previous contents of the interval timer.
A timer can be disabled by specifying a timer value of 0.
.PP
The system rounds argument timer intervals to be not less than the
resolution of its clock.  This clock resolution can be determined
by loading a very small value into a timer and reading the timer back to
see what value resulted.
.PP
The \fIalarm\fP system call of earlier versions of UNIX is provided
as a library routine using the ITIMER_REAL timer.  The process
profiling facilities of earlier versions of UNIX
remain because
it is not always possible to guarantee
the automatic restart of system calls after 
receipt of a signal.
The \fIprofil\fP call arranges for the kernel to begin gathering
execution statistics for a process:
.DS
profil(buf, bufsize, offset, scale);
result char *buf; int bufsize, offset, scale;
.DE
This begins sampling of the program counter, with statistics maintained
in the user-provided buffer.
