.\" Copyright (c) 1983 The Regents of the University of California.
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
.\"	@(#)conclusions.ms	6.2 (Berkeley) 4/16/91
.\" $FreeBSD$
.\"
.ds RH Conclusions
.NH
Conclusions
.PP
Peak available throughput is only one criterion
in most storage system purchasing decisions.
Most of the VAX UNIX systems we are familiar with
are not I/O bandwidth constrained.
Nevertheless, an adequate disk bandwidth is necessary for
good performance and especially to preserve snappy
response time.
All of the disk systems we tested provide more than
adequate bandwidth for typical VAX UNIX system application.
Perhaps in some I/O-intensive applications such as
image processing, more consideration should be given
to the peak throughput available.
In most situations, we feel that other factors are more
important in making a storage choice between the systems we
tested.
Cost, reliability, availability, and support are some of these
factors.
The maturity of the technology purchased must also be weighed
against the future value and expandability of newer technologies.
.PP
Two important conclusions about storage systems in general
can be drawn from these tests.
The first is that buffering can be effective in smoothing
the effects of lower bus speeds and bus contention.
Even though the UDA50 is located on the relatively slow
UNIBUS, its performance is similar to controllers located on
the faster processor busses.
However, the SC780 with only one sector of buffering shows that
little buffering is needed if the underlying bus is fast enough.
.PP
Placing more intelligence in the controller seems to hinder UNIX system
performance more than it helps.
Our profiling tests have indicated that UNIX spends about
the same percentage of time in the SC780 driver and the UDA50 driver
(about 10-14%).
Normally UNIX uses a disk sort algorithm that separates reads and
writes into two seek order queues.
The read queue has priority over the write queue,
since reads cause processes to block,
while writes can be done asynchronously.
This is particularly useful when generating large files,
as it allows the disk allocator to read
new disk maps and begin doing new allocations
while the blocks allocated out of the previous map are written to disk.
Because the UDA50 handles all block ordering,
and because it keeps all requests in a single queue,
there is no way to force the longer seek needed to get the next disk map.
This disfunction causes all the writes to be done before the disk map read,
which idles the disk until a new set of blocks can be allocated.
.PP
The additional functionality of the UDA50 controller that allows it
to transfer simultaneously from two drives at once tends to make
the two drive transfer tests run much more effectively.
Tuning for the single drive case works more effectively in the two
drive case than when controllers that cannot handle simultaneous
transfers are used.
.ds RH Acknowledgements
.nr H2 1
.sp 1
.NH
\s+2Acknowledgements\s0
.PP
We thank Paul Massigilia and Bill Grace
of Digital Equipment Corp for helping us run our
disk tests on their UDA50/RA81.
We also thank Rich Notari and Paul Ritkowski
of Emulex for making their machines available
to us to run our tests of the SC780/Eagles.
Dan McKinster, then of Systems Industries,
arranged to make their equipment available for the tests.
We appreciate the time provided by Bob Gross, Joe Wolf, and
Sam Leffler on their machines to refine our benchmarks.
Finally we thank our sponsors,
the National Science Foundation under grant MCS80-05144,
and the Defense Advance Research Projects Agency (DoD) under
Arpa Order No. 4031 monitored by Naval Electronic System Command under
Contract No. N00039-82-C-0235.
.ds RH References
.nr H2 1
.sp 1
.NH
\s+2References\s0
.LP
.IP [McKusick83] 20
M. McKusick, W. Joy, S. Leffler, R. Fabry,
``A Fast File System for UNIX'',
\fIACM Transactions on Computer Systems 2\fP, 3.
pp 181-197, August 1984.
.ds RH Appendix A
.bp
