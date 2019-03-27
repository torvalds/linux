.\" Copyright (c) 1985 The Regents of the University of California.
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
.\"	@(#)2.t	5.1 (Berkeley) 4/17/91
.\"
.ds RH Observation techniques
.NH
Observation techniques
.PP
There are many tools available for monitoring the performance
of the system.
Those that we found most useful are described below.
.NH 2
System maintenance tools
.PP
Several standard maintenance programs are invaluable in
observing the basic actions of the system.  
The \fIvmstat\fP(1)
program is designed to be an aid to monitoring
systemwide activity.  Together with the
\fIps\fP\|(1)
command (as in ``ps av''), it can be used to investigate systemwide
virtual memory activity.
By running \fIvmstat\fP
when the system is active you can judge the system activity in several
dimensions: job distribution, virtual memory load, paging and swapping
activity, disk and cpu utilization.
Ideally, to have a balanced system in activity,
there should be few blocked (b) jobs,
there should be little paging or swapping activity, there should
be available bandwidth on the disk devices (most single arms peak
out at 25-35 tps in practice), and the user cpu utilization (us) should
be high (above 50%).
.PP
If the system is busy, then the count of active jobs may be large,
and several of these jobs may often be blocked (b).  If the virtual
memory is active, then the paging demon will be running (sr will
be non-zero).  It is healthy for the paging demon to free pages when
the virtual memory gets active; it is triggered by the amount of free
memory dropping below a threshold and increases its pace as free memory
goes to zero.
.PP
If you run \fIvmstat\fP
when the system is busy (a ``vmstat 5'' gives all the
numbers computed by the system), you can find
imbalances by noting abnormal job distributions.  If many
processes are blocked (b), then the disk subsystem
is overloaded or imbalanced.  If you have several non-dma
devices or open teletype lines that are ``ringing'', or user programs
that are doing high-speed non-buffered input/output, then the system
time may go high (60-80% or higher).
It is often possible to pin down the cause of high system time by
looking to see if there is excessive context switching (cs), interrupt
activity (in) or system call activity (sy).  Long term measurements
on one of
our large machines show
an average of 60 context switches and interrupts
per second and an average of 90 system calls per second.
.PP
If the system is heavily loaded, or if you have little memory
for your load (1 megabyte is little in our environment), then the system
may be forced to swap.  This is likely to be accompanied by a noticeable
reduction in the system responsiveness and long pauses when interactive
jobs such as editors swap out.
.PP
A second important program is \fIiostat\fP\|(1).
\fIIostat\fP
iteratively reports the number of characters read and written to terminals,
and, for each disk, the number of transfers per second, kilobytes
transferred per second,
and the milliseconds per average seek.
It also gives the percentage of time the system has
spent in user mode, in user mode running low priority (niced) processes,
in system mode, and idling.
.PP
To compute this information, for each disk, seeks and data transfer completions
and the number of words transferred are counted;
for terminals collectively, the number
of input and output characters are counted.
Also, every 100 ms,
the state of each disk is examined
and a tally is made if the disk is active.
From these numbers and the transfer rates
of the devices it is possible to determine
average seek times for each device.
.PP
When filesystems are poorly placed on the available
disks, figures reported by \fIiostat\fP can be used
to pinpoint bottlenecks.  Under heavy system load, disk
traffic should be spread out among the drives with
higher traffic expected to the devices where the root, swap, and
/tmp filesystems are located.  When multiple disk drives are
attached to the same controller, the system will
attempt to overlap seek operations with I/O transfers.  When
seeks are performed, \fIiostat\fP will show
non-zero average seek times.  Most modern disk drives should
exhibit an average seek time of 25-35 ms.
.PP
Terminal traffic reported by \fIiostat\fP should be heavily
output oriented unless terminal lines are being used for
data transfer by programs such as \fIuucp\fP.  Input and
output rates are system specific.  Screen editors
such as \fIvi\fP and \fIemacs\fP tend to exhibit output/input
ratios of anywhere from 5/1 to 8/1.  On one of our largest
systems, 88 terminal lines plus 32 pseudo terminals, we observed
an average of 180 characters/second input and 450 characters/second
output over 4 days of operation.
.NH 2
Kernel profiling
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
.PP
The profiling data is processed with \fIgprof\fP\|(1)
to obtain information regarding the system's operation.
Profiled systems maintain histograms of the kernel program counter,
the number of invocations of each routine,
and a dynamic call graph of the executing system.
The postprocessing propagates the time spent in each
routine along the arcs of the call graph.
\fIGprof\fP then generates a listing for each routine in the kernel,
sorted according to the time it uses
including the time of its call graph descendents.
Below each routine entry is shown its (direct) call graph children,
and how their times are propagated to this routine.
A similar display above the routine shows how this routine's time and the
time of its descendents is propagated to its (direct) call graph parents.
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
.NH 2
Kernel tracing
.PP
The kernel can be configured to trace certain operations by
specifying ``options TRACE'' in the configuration file.  This
forces the inclusion of code that records the occurrence of
events in \fItrace records\fP in a circular buffer in kernel
memory.  Events may be enabled/disabled selectively while the
system is operating.  Each trace record contains a time stamp
(taken from the VAX hardware time of day clock register), an
event identifier, and additional information that is interpreted
according to the event type.  Buffer cache operations, such as
initiating a read, include 
the disk drive, block number, and transfer size in the trace record.
Virtual memory operations, such as a pagein completing, include
the virtual address and process id in the trace record.  The circular
buffer is normally configured to hold 256 16-byte trace records.\**
.FS
\** The standard trace facilities distributed with 4.2
differ slightly from those described here.  The time stamp in the
distributed system is calculated from the kernel's time of day
variable instead of the VAX hardware register, and the buffer cache
trace points do not record the transfer size.
.FE
.PP
Several user programs were written to sample and interpret the
tracing information.  One program runs in the background and
periodically reads the circular buffer of trace records.  The
trace information is compressed, in some instances interpreted
to generate additional information, and a summary is written to a
file.  In addition, the sampling program can also record
information from other kernel data structures, such as those
interpreted by the \fIvmstat\fP program.  Data written out to
a file is further buffered to minimize I/O load. 
.PP
Once a trace log has been created, programs that compress
and interpret the data may be run to generate graphs showing the
data and relationships between traced events and
system load.
.PP
The trace package was used mainly to investigate the operation of
the file system buffer cache.  The sampling program maintained a
history of read-ahead blocks and used the trace information to
calculate, for example, percentage of read-ahead blocks used.
.NH 2
Benchmark programs
.PP
Benchmark programs were used in two ways.  First, a suite of
programs was constructed to calculate the cost of certain basic
system operations.  Operations such as system call overhead and
context switching time are critically important in evaluating the
overall performance of a system.  Because of the drastic changes in
the system between 4.1BSD and 4.2BSD, it was important to verify
the overhead of these low level operations had not changed appreciably.
.PP
The second use of benchmarks was in exercising
suspected bottlenecks.
When we suspected a specific problem with the system,
a small benchmark program was written to repeatedly use
the facility.
While these benchmarks are not useful as a general tool
they can give quick feedback on whether a hypothesized
improvement is really having an effect.
It is important to realize that the only real assurance
that a change has a beneficial effect is through
long term measurements of general timesharing.
We have numerous examples where a benchmark program
suggests vast improvements while the change
in the long term system performance is negligible,
and conversely examples in which the benchmark program run more slowly, 
but the long term system performance improves significantly.
