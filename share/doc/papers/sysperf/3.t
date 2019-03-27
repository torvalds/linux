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
.\"	@(#)3.t	5.1 (Berkeley) 4/17/91
.\"
.ds RH Results of our observations
.NH
Results of our observations
.PP
When 4.2BSD was first installed on several large timesharing systems
the degradation in performance was significant.
Informal measurements showed 4.2BSD providing 80% of the throughput
of 4.1BSD (based on load averages observed under a normal timesharing load).
Many of the initial problems found were because of programs that were
not part of 4.1BSD.  Using the techniques described in the previous
section and standard process profiling several problems were identified.
Later work concentrated on the operation of the kernel itself.
In this section we discuss the problems uncovered;  in the next
section we describe the changes made to the system.
.NH 2
User programs
.PP
.NH 3
Mail system		
.PP
The mail system was the first culprit identified as a major
contributor to the degradation in system performance.
At Lucasfilm the mail system is heavily used
on one machine, a VAX-11/780 with eight megabytes of memory.\**
.FS
\** During part of these observations the machine had only four
megabytes of memory.
.FE
Message
traffic is usually between users on the same machine and ranges from
person-to-person telephone messages to per-organization distribution
lists.  After conversion to 4.2BSD, it was
immediately noticed that mail to distribution lists of 20 or more people
caused the system load to jump by anywhere from 3 to 6 points.
The number of processes spawned by the \fIsendmail\fP program and
the messages sent from \fIsendmail\fP to the system logging
process, \fIsyslog\fP, generated significant load both from their
execution and their interference with basic system operation.  The
number of context switches and disk transfers often doubled while
\fIsendmail\fP operated; the system call rate jumped dramatically.
System accounting information consistently
showed \fIsendmail\fP as the top cpu user on the system.
.NH 3
Network servers
.PP
The network services provided in 4.2BSD add new capabilities to the system,
but are not without cost.  The system uses one daemon process to accept
requests for each network service provided.  The presence of many
such daemons increases the numbers of active processes and files,
and requires a larger configuration to support the same number of users.
The overhead of the routing and status updates can consume
several percent of the cpu.
Remote logins and shells incur more overhead
than their local equivalents.
For example, a remote login uses three processes and a
pseudo-terminal handler in addition to the local hardware terminal
handler.  When using a screen editor, sending and echoing a single
character involves four processes on two machines.
The additional processes, context switching, network traffic, and
terminal handler overhead can roughly triple the load presented by one
local terminal user.
.NH 2
System overhead
.PP
To measure the costs of various functions in the kernel,
a profiling system was run for a 17 hour
period on one of our general timesharing machines.
While this is not as reproducible as a synthetic workload,
it certainly represents a realistic test.
This test was run on several occasions over a three month period.
Despite the long period of time that elapsed
between the test runs the shape of the profiles,
as measured by the number of times each system call
entry point was called, were remarkably similar.
.PP
These profiles turned up several bottlenecks that are
discussed in the next section.
Several of these were new to 4.2BSD,
but most were caused by overloading of mechanisms
which worked acceptably well in previous BSD systems.
The general conclusion from our measurements was that
the ratio of user to system time had increased from
45% system / 55% user in 4.1BSD to 57% system / 43% user
in 4.2BSD.
.NH 3
Micro-operation benchmarks
.PP
To compare certain basic system operations
between 4.1BSD and 4.2BSD a suite of benchmark
programs was constructed and run on a VAX-11/750 with 4.5 megabytes
of physical memory and two disks on a MASSBUS controller.
Tests were run with the machine operating in single user mode
under both 4.1BSD and 4.2BSD.   Paging was localized to the drive
where the root file system was located.
.PP
The benchmark programs were modeled after the Kashtan benchmarks,
[Kashtan80], with identical sources compiled under each system.
The programs and their intended purpose are described briefly
before the presentation of the results.  The benchmark scripts
were run twice with the results shown as the average of
the two runs.
The source code for each program and the shell scripts used during
the benchmarks are included in the Appendix.
.PP
The set of tests shown in Table 1 was concerned with
system operations other than paging.  The intent of most
benchmarks is clear.  The result of running \fIsignocsw\fP is
deducted from the \fIcsw\fP benchmark to calculate the context
switch overhead.  The \fIexec\fP tests use two different jobs to gauge
the cost of overlaying a larger program with a smaller one
and vice versa.  The
``null job'' and ``big job'' differ solely in the size of their data
segments, 1 kilobyte versus 256 kilobytes.  In both cases the
text segment of the parent is larger than that of the child.\**
.FS
\** These tests should also have measured the cost of expanding the
text segment; unfortunately time did not permit running additional tests.
.FE
All programs were compiled into the default load format that causes
the text segment to be demand paged out of the file system and shared
between processes.
.KF
.DS L
.TS
center box;
l | l.
Test	Description
_
syscall	perform 100,000 \fIgetpid\fP system calls
csw	perform 10,000 context switches using signals
signocsw	send 10,000 signals to yourself
pipeself4	send 10,000 4-byte messages to yourself
pipeself512	send 10,000 512-byte messages to yourself
pipediscard4	send 10,000 4-byte messages to child who discards
pipediscard512	send 10,000 512-byte messages to child who discards
pipeback4	exchange 10,000 4-byte messages with child
pipeback512	exchange 10,000 512-byte messages with child
forks0	fork-exit-wait 1,000 times
forks1k	sbrk(1024), fault page, fork-exit-wait 1,000 times
forks100k	sbrk(102400), fault pages, fork-exit-wait 1,000 times
vforks0	vfork-exit-wait 1,000 times
vforks1k	sbrk(1024), fault page, vfork-exit-wait 1,000 times
vforks100k	sbrk(102400), fault pages, vfork-exit-wait 1,000 times
execs0null	fork-exec ``null job''-exit-wait 1,000 times
execs0null (1K env)	execs0null above, with 1K environment added
execs1knull	sbrk(1024), fault page, fork-exec ``null job''-exit-wait 1,000 times
execs1knull (1K env)	execs1knull above, with 1K environment added
execs100knull	sbrk(102400), fault pages, fork-exec ``null job''-exit-wait 1,000 times
vexecs0null	vfork-exec ``null job''-exit-wait 1,000 times
vexecs1knull	sbrk(1024), fault page, vfork-exec ``null job''-exit-wait 1,000 times
vexecs100knull	sbrk(102400), fault pages, vfork-exec ``null job''-exit-wait 1,000 times
execs0big	fork-exec ``big job''-exit-wait 1,000 times
execs1kbig	sbrk(1024), fault page, fork-exec ``big job''-exit-wait 1,000 times
execs100kbig	sbrk(102400), fault pages, fork-exec ``big job''-exit-wait 1,000 times
vexecs0big	vfork-exec ``big job''-exit-wait 1,000 times
vexecs1kbig	sbrk(1024), fault pages, vfork-exec ``big job''-exit-wait 1,000 times
vexecs100kbig	sbrk(102400), fault pages, vfork-exec ``big job''-exit-wait 1,000 times
.TE
.ce
Table 1. Kernel Benchmark programs.
.DE
.KE
.PP
The results of these tests are shown in Table 2.  If the 4.1BSD results
are scaled to reflect their being run on a VAX-11/750, they
correspond closely to those found in [Joy80].\**
.FS
\** We assume that a VAX-11/750 runs at 60% of the speed of a VAX-11/780
(not considering floating point operations).
.FE
.KF
.DS L
.TS
center box;
c s s s s s s s s s
c || c s s || c s s || c s s
c || c s s || c s s || c s s
c || c | c | c || c | c | c || c | c | c
l || n | n | n || n | n | n || n | n | n.
Berkeley Software Distribution UNIX Systems
_
Test	Elapsed Time	User Time	System Time
\^	_	_	_
\^	4.1	4.2	4.3	4.1	4.2	4.3	4.1	4.2	4.3
=
syscall	28.0	29.0	23.0	4.5	5.3	3.5	23.9	23.7	20.4
csw	45.0	60.0	45.0	3.5	4.3	3.3	19.5	25.4	19.0
signocsw	16.5	23.0	16.0	1.9	3.0	1.1	14.6	20.1	15.2
pipeself4	21.5	29.0	26.0	1.1	1.1	0.8	20.1	28.0	25.6
pipeself512	47.5	59.0	55.0	1.2	1.2	1.0	46.1	58.3	54.2
pipediscard4	32.0	42.0	36.0	3.2	3.7	3.0	15.5	18.8	15.6
pipediscard512	61.0	76.0	69.0	3.1	2.1	2.0	29.7	36.4	33.2
pipeback4	57.0	75.0	66.0	2.9	3.2	3.3	25.1	34.2	29.7
pipeback512	110.0	138.0	125.0	3.1	3.4	2.2	52.2	65.7	57.7
forks0	37.5	41.0	22.0	0.5	0.3	0.3	34.5	37.6	21.5
forks1k	40.0	43.0	22.0	0.4	0.3	0.3	36.0	38.8	21.6
forks100k	217.5	223.0	176.0	0.7	0.6	0.4	214.3	218.4	175.2
vforks0	34.5	37.0	22.0	0.5	0.6	0.5	27.3	28.5	17.9
vforks1k	35.0	37.0	22.0	0.6	0.8	0.5	27.2	28.6	17.9
vforks100k	35.0	37.0	22.0	0.6	0.8	0.6	27.6	28.9	17.9
execs0null	97.5	92.0	66.0	3.8	2.4	0.6	68.7	82.5	48.6
execs0null (1K env)	197.0	229.0	75.0	4.1	2.6	0.9	167.8	212.3	62.6
execs1knull	99.0	100.0	66.0	4.1	1.9	0.6	70.5	86.8	48.7
execs1knull (1K env)	199.0	230.0	75.0	4.2	2.6	0.7	170.4	214.9	62.7
execs100knull	283.5	278.0	216.0	4.8	2.8	1.1	251.9	269.3	202.0
vexecs0null	100.0	92.0	66.0	5.1	2.7	1.1	63.7	76.8	45.1
vexecs1knull	100.0	91.0	66.0	5.2	2.8	1.1	63.2	77.1	45.1
vexecs100knull	100.0	92.0	66.0	5.1	3.0	1.1	64.0	77.7	45.6
execs0big	129.0	201.0	101.0	4.0	3.0	1.0	102.6	153.5	92.7
execs1kbig	130.0	202.0	101.0	3.7	3.0	1.0	104.7	155.5	93.0
execs100kbig	318.0	385.0	263.0	4.8	3.1	1.1	286.6	339.1	247.9
vexecs0big	128.0	200.0	101.0	4.6	3.5	1.6	98.5	149.6	90.4
vexecs1kbig	125.0	200.0	101.0	4.7	3.5	1.3	98.9	149.3	88.6
vexecs100kbig	126.0	200.0	101.0	4.2	3.4	1.3	99.5	151.0	89.0
.TE
.ce
Table 2. Kernel Benchmark results (all times in seconds).
.DE
.KE
.PP
In studying the measurements we found that the basic system call
and context switch overhead did not change significantly
between 4.1BSD and 4.2BSD.  The \fIsignocsw\fP results were caused by
the changes to the \fIsignal\fP interface, resulting
in an additional subroutine invocation for each call, not
to mention additional complexity in the system's implementation.
.PP
The times for the use of pipes are significantly higher under
4.2BSD because of their implementation on top of the interprocess
communication facilities.  Under 4.1BSD pipes were implemented
without the complexity of the socket data structures and with
simpler code.  Further, while not obviously a factor here,
4.2BSD pipes have less system buffer space provided them than
4.1BSD pipes.
.PP
The \fIexec\fP tests shown in Table 2 were performed with 34 bytes of
environment information under 4.1BSD and 40 bytes under 4.2BSD.
To figure the cost of passing data through the environment,
the execs0null and execs1knull tests were rerun with
1065 additional bytes of data.  The results are show in Table 3.
.KF
.DS L
.TS
center box;
c || c s || c s || c s
c || c s || c s || c s
c || c | c || c | c || c | c
l || n | n || n | n || n | n.
Test	Real	User	System
\^	_	_	_
\^	4.1	4.2	4.1	4.2	4.1	4.2
=
execs0null	197.0	229.0	4.1	2.6	167.8	212.3
execs1knull	199.0	230.0	4.2	2.6	170.4	214.9
.TE
.ce
Table 3. Benchmark results with ``large'' environment (all times in seconds).
.DE
.KE
These results show that passing argument data is significantly
slower than under 4.1BSD: 121 ms/byte versus 93 ms/byte.  Even using
this factor to adjust the basic overhead of an \fIexec\fP system
call, this facility is more costly under 4.2BSD than under 4.1BSD.
.NH 3
Path name translation
.PP
The single most expensive function performed by the kernel
is path name translation.
This has been true in almost every UNIX kernel [Mosher80];
we find that our general time sharing systems do about
500,000 name translations per day.
.PP
Name translations became more expensive in 4.2BSD for several reasons.
The single most expensive addition was the symbolic link.
Symbolic links
have the effect of increasing the average number of components
in path names to be translated.
As an insidious example,
consider the system manager that decides to change /tmp
to be a symbolic link to /usr/tmp.
A name such as /tmp/tmp1234 that previously required two component
translations,
now requires four component translations plus the cost of reading 
the contents of the symbolic link.
.PP
The new directory format also changes the characteristics of
name translation.
The more complex format requires more computation to determine
where to place new entries in a directory.
Conversely the additional information allows the system to only
look at active entries when searching,
hence searches of directories that had once grown large
but currently have few active entries are checked quickly.
The new format also stores the length of each name so that
costly string comparisons are only done on names that are the
same length as the name being sought.
.PP
The net effect of the changes is that the average time to
translate a path name in 4.2BSD is 24.2 milliseconds,
representing 40% of the time processing system calls,
that is 19% of the total cycles in the kernel,
or 11% of all cycles executed on the machine.
The times are shown in Table 4.  We have no comparable times
for \fInamei\fP under 4.1 though they are certain to
be significantly less.
.KF
.DS L
.TS
center box;
l r r.
part	time	% of kernel
_
self	14.3 ms/call	11.3%
child	9.9 ms/call	7.9%
_
total	24.2 ms/call	19.2%
.TE
.ce
Table 4. Call times for \fInamei\fP in 4.2BSD.
.DE
.KE
.NH 3
Clock processing
.PP
Nearly 25% of the time spent in the kernel is spent in the clock
processing routines.
(This is a clear indication that to avoid sampling bias when profiling the
kernel with our tools
we need to drive them from an independent clock.)
These routines are responsible for implementing timeouts,
scheduling the processor,
maintaining kernel statistics,
and tending various hardware operations such as
draining the terminal input silos.
Only minimal work is done in the hardware clock interrupt
routine (at high priority), the rest is performed (at a lower priority)
in a software interrupt handler scheduled by the hardware interrupt
handler.
In the worst case, with a clock rate of 100 Hz
and with every hardware interrupt scheduling a software
interrupt, the processor must field 200 interrupts per second.
The overhead of simply trapping and returning
is 3% of the machine cycles,
figuring out that there is nothing to do
requires an additional 2%.
.NH 3
Terminal multiplexors
.PP
The terminal multiplexors supported by 4.2BSD have programmable receiver
silos that may be used in two ways.
With the silo disabled, each character received causes an interrupt
to the processor.
Enabling the receiver silo allows the silo to fill before
generating an interrupt, allowing multiple characters to be read
for each interrupt.
At low rates of input, received characters will not be processed
for some time unless the silo is emptied periodically.
The 4.2BSD kernel uses the input silos of each terminal multiplexor,
and empties each silo on each clock interrupt.
This allows high input rates without the cost of per-character interrupts
while assuring low latency.
However, as character input rates on most machines are usually
low (about 25 characters per second),
this can result in excessive overhead.
At the current clock rate of 100 Hz, a machine with 5 terminal multiplexors
configured makes 500 calls to the receiver interrupt routines per second.
In addition, to achieve acceptable input latency
for flow control, each clock interrupt must schedule
a software interrupt to run the silo draining routines.\**
.FS
\** It is not possible to check the input silos at
the time of the actual clock interrupt without modifying the terminal
line disciplines, as the input queues may not be in a consistent state \**.
.FE
\** This implies that the worst case estimate for clock processing
is the basic overhead for clock processing.
.NH 3
Process table management
.PP
In 4.2BSD there are numerous places in the kernel where a linear search
of the process table is performed: 
.IP \(bu 3
in \fIexit\fP to locate and wakeup a process's parent;
.IP \(bu 3
in \fIwait\fP when searching for \fB\s-2ZOMBIE\s+2\fP and
\fB\s-2STOPPED\s+2\fP processes;
.IP \(bu 3
in \fIfork\fP when allocating a new process table slot and
counting the number of processes already created by a user;
.IP \(bu 3
in \fInewproc\fP, to verify
that a process id assigned to a new process is not currently
in use;
.IP \(bu 3
in \fIkill\fP and \fIgsignal\fP to locate all processes to
which a signal should be delivered;
.IP \(bu 3
in \fIschedcpu\fP when adjusting the process priorities every
second; and
.IP \(bu 3
in \fIsched\fP when locating a process to swap out and/or swap
in.
.LP
These linear searches can incur significant overhead.  The rule
for calculating the size of the process table is:
.ce
nproc = 20 + 8 * maxusers
.sp
that means a 48 user system will have a 404 slot process table.
With the addition of network services in 4.2BSD, as many as a dozen
server processes may be maintained simply to await incoming requests.
These servers are normally created at boot time which causes them
to be allocated slots near the beginning of the process table.  This
means that process table searches under 4.2BSD are likely to take
significantly longer than under 4.1BSD.  System profiling shows
that as much as 20% of the time spent in the kernel on a loaded
system (a VAX-11/780) can be spent in \fIschedcpu\fP and, on average,
5-10% of the kernel time is spent in \fIschedcpu\fP.
The other searches of the proc table are similarly affected.
This shows the system can no longer tolerate using linear searches of
the process table.
.NH 3
File system buffer cache
.PP
The trace facilities described in section 2.3 were used
to gather statistics on the performance of the buffer cache.
We were interested in measuring the effectiveness of the
cache and the read-ahead policies.
With the file system block size in 4.2BSD four to
eight times that of a 4.1BSD file system, we were concerned
that large amounts of read-ahead might be performed without
being used.  Also, we were interested in seeing if the
rules used to size the buffer cache at boot time were severely
affecting the overall cache operation.
.PP
The tracing package was run over a three hour period during
a peak mid-afternoon period on a VAX 11/780 with four megabytes
of physical memory.
This resulted in a buffer cache containing 400 kilobytes of memory
spread among 50 to 200 buffers
(the actual number of buffers depends on the size mix of
disk blocks being read at any given time).
The pertinent configuration information is shown in Table 5.
.KF
.DS L
.TS
center box;
l l l l.
Controller	Drive	Device	File System
_
DEC MASSBUS	DEC RP06	hp0d	/usr
		hp0b	swap
Emulex SC780	Fujitsu Eagle	hp1a	/usr/spool/news
		hp1b	swap
		hp1e	/usr/src
		hp1d	/u0 (users)
	Fujitsu Eagle	hp2a	/tmp
		hp2b	swap
		hp2d	/u1 (users)
	Fujitsu Eagle	hp3a	/
.TE
.ce
Table 5. Active file systems during buffer cache tests.
.DE
.KE
.PP
During the test period the load average ranged from 2 to 13
with an average of 5.
The system had no idle time, 43% user time, and 57% system time.
The system averaged 90 interrupts per second 
(excluding the system clock interrupts),
220 system calls per second,
and 50 context switches per second (40 voluntary, 10 involuntary).
.PP
The active virtual memory (the sum of the address space sizes of
all jobs that have run in the previous twenty seconds)
over the period ranged from 2 to 6 megabytes with an average
of 3.5 megabytes.
There was no swapping, though the page daemon was inspecting
about 25 pages per second.
.PP
On average 250 requests to read disk blocks were initiated
per second.
These include read requests for file blocks made by user 
programs as well as requests initiated by the system.
System reads include requests for indexing information to determine
where a file's next data block resides,
file system layout maps to allocate new data blocks,
and requests for directory contents needed to do path name translations.
.PP
On average, an 85% cache hit rate was observed for read requests.
Thus only 37 disk reads were initiated per second.
In addition, 5 read-ahead requests were made each second
filling about 20% of the buffer pool.
Despite the policies to rapidly reuse read-ahead buffers
that remain unclaimed, more than 90% of the read-ahead
buffers were used.
.PP
These measurements showed that the buffer cache was working
effectively.  Independent tests have also showed that the size
of the buffer cache may be reduced significantly on memory-poor
system without severe effects;
we have not yet tested this hypothesis [Shannon83].
.NH 3
Network subsystem
.PP
The overhead associated with the 
network facilities found in 4.2BSD is often
difficult to gauge without profiling the system.
This is because most input processing is performed
in modules scheduled with software interrupts.
As a result, the system time spent performing protocol
processing is rarely attributed to the processes that
really receive the data.  Since the protocols supported
by 4.2BSD can involve significant overhead this was a serious
concern.  Results from a profiled kernel show an average
of 5% of the system time is spent
performing network input and timer processing in our environment
(a 3Mb/s Ethernet with most traffic using TCP).
This figure can vary significantly depending on
the network hardware used, the average message
size, and whether packet reassembly is required at the network
layer.  On one machine we profiled over a 17 hour
period (our gateway to the ARPANET)
206,000 input messages accounted for 2.4% of the system time,
while another 0.6% of the system time was spent performing
protocol timer processing.
This machine was configured with an ACC LH/DH IMP interface
and a DMA 3Mb/s Ethernet controller.
.PP
The performance of TCP over slower long-haul networks
was degraded substantially by two problems.
The first problem was a bug that prevented round-trip timing measurements
from being made, thus increasing retransmissions unnecessarily.
The second was a problem with the maximum segment size chosen by TCP,
that was well-tuned for Ethernet, but was poorly chosen for
the ARPANET, where it causes packet fragmentation.  (The maximum
segment size was actually negotiated upwards to a value that
resulted in excessive fragmentation.)
.PP
When benchmarked in Ethernet environments the main memory buffer management
of the network subsystem presented some performance anomalies.
The overhead of processing small ``mbufs'' severely affected throughput for a
substantial range of message sizes.
In spite of the fact that most system ustilities made use of the throughput
optimal 1024 byte size, user processes faced large degradations for some
arbitrary sizes. This was specially true for TCP/IP transmissions [Cabrera84,
Cabrera85].
.NH 3
Virtual memory subsystem
.PP
We ran a set of tests intended to exercise the virtual
memory system under both 4.1BSD and 4.2BSD.
The tests are described in Table 6.
The test programs dynamically allocated
a 7.3 Megabyte array (using \fIsbrk\fP\|(2)) then referenced
pages in the array either: sequentially, in a purely random
fashion, or such that the distance between
successive pages accessed was randomly selected from a Gaussian
distribution.  In the last case, successive runs were made with
increasing standard deviations.
.KF
.DS L
.TS
center box;
l | l.
Test	Description
_
seqpage	sequentially touch pages, 10 iterations
seqpage-v	as above, but first make \fIvadvise\fP\|(2) call
randpage	touch random page 30,000 times
randpage-v	as above, but first make \fIvadvise\fP call
gausspage.1	30,000 Gaussian accesses, standard deviation of 1
gausspage.10	as above, standard deviation of 10
gausspage.30	as above, standard deviation of 30
gausspage.40	as above, standard deviation of 40
gausspage.50	as above, standard deviation of 50
gausspage.60	as above, standard deviation of 60
gausspage.80	as above, standard deviation of 80
gausspage.inf	as above, standard deviation of 10,000
.TE
.ce
Table 6. Paging benchmark programs.
.DE
.KE
.PP
The results in Table 7 show how the additional
memory requirements
of 4.2BSD can generate more work for the paging system.
Under 4.1BSD,
the system used 0.5 of the 4.5 megabytes of physical memory
on the test machine;
under 4.2BSD it used nearly 1 megabyte of physical memory.\**
.FS
\** The 4.1BSD system used for testing was really a 4.1a 
system configured
with networking facilities and code to support
remote file access.  The
4.2BSD system also included the remote file access code.
Since both
systems would be larger than similarly configured ``vanilla''
4.1BSD or 4.2BSD system, we consider out conclusions to still be valid.
.FE
This resulted in more page faults and, hence, more system time.
To establish a common ground on which to compare the paging
routines of each system, we check instead the average page fault
service times for those test runs that had a statistically significant
number of random page faults.  These figures, shown in Table 8, show
no significant difference between the two systems in
the area of page fault servicing.  We currently have
no explanation for the results of the sequential
paging tests.
.KF
.DS L
.TS
center box;
l || c s || c s || c s || c s
l || c s || c s || c s || c s
l || c | c || c | c || c | c || c | c
l || n | n || n | n || n | n || n | n.
Test	Real	User	System	Page Faults
\^	_	_	_	_
\^	4.1	4.2	4.1	4.2	4.1	4.2	4.1	4.2
=
seqpage	959	1126	16.7	12.8	197.0	213.0	17132	17113
seqpage-v	579	812	3.8	5.3	216.0	237.7	8394	8351
randpage	571	569	6.7	7.6	64.0	77.2	8085	9776
randpage-v	572	562	6.1	7.3	62.2	77.5	8126	9852
gausspage.1	25	24	23.6	23.8	0.8	0.8	8	8
gausspage.10	26	26	22.7	23.0	3.2	3.6	2	2
gausspage.30	34	33	25.0	24.8	8.6	8.9	2	2
gausspage.40	42	81	23.9	25.0	11.5	13.6	3	260
gausspage.50	113	175	24.2	26.2	19.6	26.3	784	1851
gausspage.60	191	234	27.6	26.7	27.4	36.0	2067	3177
gausspage.80	312	329	28.0	27.9	41.5	52.0	3933	5105
gausspage.inf	619	621	82.9	85.6	68.3	81.5	8046	9650
.TE
.ce
Table 7. Paging benchmark results (all times in seconds).
.DE
.KE
.KF
.DS L
.TS
center box;
c || c s || c s
c || c s || c s
c || c | c || c | c
l || n | n || n | n.
Test	Page Faults	PFST
\^	_	_
\^	4.1	4.2	4.1	4.2
=
randpage	8085	9776	791	789
randpage-v	8126	9852	765	786
gausspage.inf	8046	9650	848	844
.TE
.ce
Table 8. Page fault service times (all times in microseconds).
.DE
.KE
