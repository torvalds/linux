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
.\"	@(#)1.t	5.1 (Berkeley) 4/17/91
.\"
.ds RH Introduction
.af PN 1
.bp 1
.NH
Introduction
.PP
The Berkeley Software Distributions of
.UX
for the VAX have added many new capabilities that were
previously unavailable under
.UX .
The development effort for 4.2BSD concentrated on providing new
facilities, and in getting them to work correctly.
Many new data structures were added to the system to support
these new capabilities.
In addition,
many of the existing data structures and algorithms
were put to new uses or their old functions placed under increased demand.
The effect of these changes was that
mechanisms that were well tuned under 4.1BSD
no longer provided adequate performance for 4.2BSD.
The increased user feedback that came with the release of
4.2BSD and a growing body of experience with the system
highlighted the performance shortcomings of 4.2BSD.
.PP
This paper details the work that we have done since
the release of 4.2BSD to measure the performance of the system,
detect the bottlenecks,
and find solutions to remedy them.
Most of our tuning has been in the context of the real
timesharing systems in our environment.
Rather than using simulated workloads,
we have sought to analyze our tuning efforts under
realistic conditions.
Much of the work has been done in the machine independent parts
of the system, hence these improvements could be applied to
other variants of UNIX with equal success.
All of the changes made have been included in 4.3BSD.
.PP
Section 2 of the paper describes the tools and techniques
available to us for measuring system performance.
In Section 3 we present the results of using these tools, while Section 4
has the performance improvements
that have been made to the system based on our measurements.
Section 5 highlights the functional enhancements that have
been made to Berkeley UNIX 4.2BSD.
Section 6 discusses some of the security problems that
have been addressed.
