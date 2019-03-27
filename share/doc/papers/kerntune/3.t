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
.\"	@(#)3.t	1.2 (Berkeley) 11/8/90
.\"
.ds RH Techniques for Improving Performance
.NH 1 
Techniques for Improving Performance
.PP
This section gives several hints on general optimization techniques.
It then proceeds with an example of how they can be
applied to the 4.2BSD kernel to improve its performance.
.NH 2
Using the Profiler
.PP
The profiler is a useful tool for improving
a set of routines that implement an abstraction.
It can be helpful in identifying poorly coded routines,
and in evaluating the new algorithms and code that replace them.
Taking full advantage of the profiler 
requires a careful examination of the call graph profile,
and a thorough knowledge of the abstractions underlying
the kernel.
.PP
The easiest optimization that can be performed
is a small change
to a control construct or data structure.
An obvious starting point
is to expand a small frequently called routine inline.
The drawback to inline expansion is that the data abstractions
in the kernel may become less parameterized,
hence less clearly defined.
The profiling will also become less useful since the loss of 
routines will make its output more granular.
.PP
Further potential for optimization lies in routines that
implement data abstractions whose total execution
time is long.
If the data abstraction function cannot easily be speeded up,
it may be advantageous to cache its results,
and eliminate the need to rerun
it for identical inputs.
These and other ideas for program improvement are discussed in
[Bentley81].
.PP
This tool is best used in an iterative approach:
profiling the kernel,
eliminating one bottleneck,
then finding some other part of the kernel
that begins to dominate execution time.
.PP
A completely different use of the profiler is to analyze the control
flow of an unfamiliar section of the kernel.
By running an example that exercises the unfamiliar section of the kernel,
and then using \fIgprof\fR, you can get a view of the 
control structure of the unfamiliar section.
.NH 2
An Example of Tuning
.PP
The first step is to come up with a method for generating
profile data.
We prefer to run a profiling system for about a one day
period on one of our general timesharing machines.
While this is not as reproducible as a synthetic workload,
it certainly represents a realistic test.
We have run one day profiles on several
occasions over a three month period.
Despite the long period of time that elapsed
between the test runs the shape of the profiles,
as measured by the number of times each system call
entry point was called, were remarkably similar.
.PP
A second alternative is to write a small benchmark
program to repeated exercise a suspected bottleneck.
While these benchmarks are not useful as a long term profile
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
.PP
An investigation of our long term profiling showed that
the single most expensive function performed by the kernel
is path name translation.
We find that our general time sharing systems do about
500,000 name translations per day.
The cost of doing name translation in the original 4.2BSD
is 24.2 milliseconds,
representing 40% of the time processing system calls,
which is 19% of the total cycles in the kernel,
or 11% of all cycles executed on the machine.
The times are shown in Figure 3.
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
Figure 3. Call times for \fInamei\fP.
.DE
.KE
.PP
The system measurements collected showed the
pathname translation routine, \fInamei\fP,
was clearly worth optimizing.
An inspection of \fInamei\fP shows that
it consists of two nested loops.
The outer loop is traversed once per pathname component.
The inner loop performs a linear search through a directory looking
for a particular pathname component.
.PP
Our first idea was to observe that many programs 
step through a directory performing an operation on 
each entry in turn.
This caused us to modify \fInamei\fP to cache
the directory offset of the last pathname
component looked up by a process.
The cached offset is then used
as the point at which a search in the same directory
begins.  Changing directories invalidates the cache, as
does modifying the directory.
For programs that step sequentially through a directory with
$N$ files, search time decreases from $O ( N sup 2 )$
to $O(N)$.
.PP
The cost of the cache is about 20 lines of code
(about 0.2 kilobytes) 
and 16 bytes per process, with the cached data
stored in a process's \fIuser\fP vector.
.PP
As a quick benchmark to verify the effectiveness of the
cache we ran ``ls \-l''
on a directory containing 600 files.
Before the per-process cache this command
used 22.3 seconds of system time.
After adding the cache the program used the same amount
of user time, but the system time dropped to 3.3 seconds.
.PP
This change prompted our rerunning a profiled system
on a machine containing the new \fInamei\fP.
The results showed that the time in \fInamei\fP
dropped by only 2.6 ms/call and
still accounted for 36% of the system call time,
18% of the kernel, or about 10% of all the machine cycles.
This amounted to a drop in system time from 57% to about 55%.
The results are shown in Figure 4.
.KF
.DS L
.TS
center box;
l r r.
part	time	% of kernel
_
self	11.0 ms/call	9.2%
child	10.6 ms/call	8.9%
_
total	21.6 ms/call	18.1%
.TE
.ce
Figure 4. Call times for \fInamei\fP with per-process cache.
.DE
.KE
.PP
The small performance improvement
was caused by a low cache hit ratio.
Although the cache was 90% effective when hit,
it was only usable on about 25% of the names being translated.
An additional reason for the small improvement was that
although the amount of time spent in \fInamei\fP itself
decreased substantially,
more time was spent in the routines that it called
since each directory had to be accessed twice;
once to search from the middle to the end,
and once to search from the beginning to the middle.
.PP
Most missed names were caused by path name components
other than the last.
Thus Robert Elz introduced a system wide cache of most recent
name translations.
The cache is keyed on a name and the
inode and device number of the directory that contains it.
Associated with each entry is a pointer to the corresponding
entry in the inode table.
This has the effect of short circuiting the outer loop of \fInamei\fP.
For each path name component,
\fInamei\fP first looks in its cache of recent translations
for the needed name.
If it exists, the directory search can be completely eliminated.
If the name is not recognized,
then the per-process cache may still be useful in
reducing the directory search time.
The two cacheing schemes complement each other well.
.PP
The cost of the name cache is about 200 lines of code
(about 1.2 kilobytes) 
and 44 bytes per cache entry.
Depending on the size of the system,
about 200 to 1000 entries will normally be configured,
using 10-44 kilobytes of physical memory.
The name cache is resident in memory at all times.
.PP
After adding the system wide name cache we reran ``ls \-l''
on the same directory.
The user time remained the same,
however the system time rose slightly to 3.7 seconds.
This was not surprising as \fInamei\fP
now had to maintain the cache,
but was never able to make any use of it.
.PP
Another profiled system was created and measurements
were collected over a one day period.  These measurements
showed a 6 ms/call decrease in \fInamei\fP, with
\fInamei\fP accounting for only 31% of the system call time,
16% of the time in the kernel,
or about 7% of all the machine cycles.
System time dropped from 55% to about 49%.
The results are shown in Figure 5.
.KF
.DS L
.TS
center box;
l r r.
part	time	% of kernel
_
self	9.5 ms/call	9.6%
child	6.1 ms/call	6.1%
_
total	15.6 ms/call	15.7%
.TE
.ce
Figure 5.  Call times for \fInamei\fP with both caches.
.DE
.KE
.PP
Statistics on the performance of both caches show
the large performance improvement is
caused by the high hit ratio.
On the profiled system a 60% hit rate was observed in
the system wide cache.  This, coupled with the 25%
hit rate in the per-process offset cache yielded an
effective cache hit rate of 85%.
While the system wide cache reduces both the amount of time in
the routines that \fInamei\fP calls as well as \fInamei\fP itself
(since fewer directories need to be accessed or searched),
it is interesting to note that the actual percentage of system
time spent in \fInamei\fP itself increases even though the
actual time per call decreases.
This is because less total time is being spent in the kernel,
hence a smaller absolute time becomes a larger total percentage.
