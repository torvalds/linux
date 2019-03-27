.\" Copyright (c) 1984 M. K. McKusick
.\" Copyright (c) 1984 The Regents of the University of California.
.\" All rights reserved.
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
.\"	@(#)2.t	1.3 (Berkeley) 11/8/90
.\"
.ds RH The \fIgprof\fP Profiler
.NH 1
The \fIgprof\fP Profiler
.PP
The purpose of the \fIgprof\fP profiling tool is to 
help the user evaluate alternative implementations
of abstractions.
The \fIgprof\fP design takes advantage of the fact that the kernel
though large, is structured and hierarchical.
We provide a profile in which the execution time
for a set of routines that implement an
abstraction is collected and charged
to that abstraction.
The profile can be used to compare and assess the costs of
various implementations [Graham82] [Graham83].
.NH 2
Data presentation
.PP
The data is presented to the user in two different formats.
The first presentation simply lists the routines
without regard to the amount of time their descendants use.
The second presentation incorporates the call graph of the
kernel.
.NH 3 
The Flat Profile
.PP
The flat profile consists of a list of all the routines 
that are called during execution of the kernel,
with the count of the number of times they are called
and the number of seconds of execution time for which they
are themselves accountable.
The routines are listed in decreasing order of execution time.
A list of the routines that are never called during execution of
the kernel is also available
to verify that nothing important is omitted by
this profiling run.
The flat profile gives a quick overview of the routines that are used,
and shows the routines that are themselves responsible
for large fractions of the execution time.
In practice,
this profile usually shows that no single function
is overwhelmingly responsible for 
the total time of the kernel.
Notice that for this profile,
the individual times sum to the total execution time.
.NH 3 
The Call Graph Profile
.PP
Ideally, we would like to print the call graph of the kernel,
but we are limited by the two-dimensional nature of our output
devices.
We cannot assume that a call graph is planar,
and even if it is, that we can print a planar version of it.
Instead, we choose to list each routine,
together with information about
the routines that are its direct parents and children.
This listing presents a window into the call graph.
Based on our experience,
both parent information and child information
is important,
and should be available without searching
through the output.
Figure 1 shows a sample \fIgprof\fP entry.
.KF
.DS L
.TS
box center;
c c c c c l l
c c c c c l l
c c c c c l l
l n n n c l l.
				called/total	\ \ parents
index	%time	self	descendants	called+self	name	index
				called/total	\ \ children
_
		0.20	1.20	4/10	\ \ \s-1CALLER1\s+1	[7]
		0.30	1.80	6/10	\ \ \s-1CALLER2\s+1	[1]
[2]	41.5	0.50	3.00	10+4	\s-1EXAMPLE\s+1	[2]
		1.50	1.00	20/40	\ \ \s-1SUB1\s+1 <cycle1>	[4]
		0.00	0.50	1/5	\ \ \s-1SUB2\s+1 	[9]
		0.00	0.00	0/5	\ \ \s-1SUB3\s+1 	[11]
.TE
.ce
Figure 1. Profile entry for \s-1EXAMPLE\s+1.
.DE
.KE
.PP
The major entries of the call graph profile are the entries from the
flat profile, augmented by the time propagated to each 
routine from its descendants.
This profile is sorted by the sum of the time for the routine
itself plus the time inherited from its descendants.
The profile shows which of the higher level routines 
spend large portions of the total execution time 
in the routines that they call.
For each routine, we show the amount of time passed by each child
to the routine, which includes time for the child itself
and for the descendants of the child
(and thus the descendants of the routine).
We also show the percentage these times represent of the total time
accounted to the child.
Similarly, the parents of each routine are listed, 
along with time,
and percentage of total routine time,
propagated to each one.
.PP
Cycles are handled as single entities.
The cycle as a whole is shown as though it were a single routine,
except that members of the cycle are listed in place of the children.
Although the number of calls of each member
from within the cycle are shown,
they do not affect time propagation.
When a child is a member of a cycle,
the time shown is the appropriate fraction of the time
for the whole cycle.
Self-recursive routines have their calls broken
down into calls from the outside and self-recursive calls.
Only the outside calls affect the propagation of time.
.PP
The example shown in Figure 2 is the fragment of a call graph
corresponding to the entry in the call graph profile listing
shown in Figure 1.
.KF
.DS L
.so fig2.pic
.ce
Figure 2. Example call graph fragment.
.DE
.KE
.PP
The entry is for routine \s-1EXAMPLE\s+1, which has
the Caller routines as its parents,
and the Sub routines as its children.
The reader should keep in mind that all information
is given \fIwith respect to \s-1EXAMPLE\s+1\fP.
The index in the first column shows that \s-1EXAMPLE\s+1
is the second entry in the profile listing.
The \s-1EXAMPLE\s+1 routine is called ten times, four times by \s-1CALLER1\s+1,
and six times by \s-1CALLER2\s+1.
Consequently 40% of \s-1EXAMPLE\s+1's time is propagated to \s-1CALLER1\s+1,
and 60% of \s-1EXAMPLE\s+1's time is propagated to \s-1CALLER2\s+1.
The self and descendant fields of the parents
show the amount of self and descendant time \s-1EXAMPLE\s+1
propagates to them (but not the time used by
the parents directly).
Note that \s-1EXAMPLE\s+1 calls itself recursively four times.
The routine \s-1EXAMPLE\s+1 calls routine \s-1SUB1\s+1 twenty times, \s-1SUB2\s+1 once,
and never calls \s-1SUB3\s+1.
Since \s-1SUB2\s+1 is called a total of five times,
20% of its self and descendant time is propagated to \s-1EXAMPLE\s+1's
descendant time field.
Because \s-1SUB1\s+1 is a member of \fIcycle 1\fR,
the self and descendant times
and call count fraction
are those for the cycle as a whole.
Since cycle 1 is called a total of forty times
(not counting calls among members of the cycle),
it propagates 50% of the cycle's self and descendant
time to \s-1EXAMPLE\s+1's descendant time field.
Finally each name is followed by an index that shows
where on the listing to find the entry for that routine.
.NH 2
Profiling the Kernel
.PP
It is simple to build a 4.2BSD kernel that will automatically
collect profiling information as it operates simply by specifying the
.B \-p
option to \fIconfig\fP\|(8) when configuring a kernel.
The program counter sampling can be driven by the system clock,
or by an alternate real time clock.
The latter is highly recommended as use of the system clock results
in statistical anomalies in accounting for
the time spent in the kernel clock routine.
.PP
Once a profiling system has been booted statistic gathering is
handled by \fIkgmon\fP\|(8).
\fIKgmon\fP allows profiling to be started and stopped
and the internal state of the profiling buffers to be dumped.
\fIKgmon\fP can also be used to reset the state of the internal
buffers to allow multiple experiments to be run without
rebooting the machine.
The profiling data can then be processed with \fIgprof\fP\|(1)
to obtain information regarding the system's operation.
.PP
A profiled system is about 5-10% larger in its text space because of
the calls to count the subroutine invocations.
When the system executes,
the profiling data is stored in a buffer that is 1.2
times the size of the text space.
All the information is summarized in memory,
it is not necessary to have a trace file
being continuously dumped to disk.
The overhead for running a profiled system varies;
under normal load we see anywhere from 5-25%
of the system time spent in the profiling code.
Thus the system is noticeably slower than an unprofiled system,
yet is not so bad that it cannot be used in a production environment.
This is important since it allows us to gather data
in a real environment rather than trying to
devise synthetic work loads.
