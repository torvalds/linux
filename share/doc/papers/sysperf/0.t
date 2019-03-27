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
.\"	@(#)0.t	5.1 (Berkeley) 4/17/91
.\"
.if n .ND
.TL
Measuring and Improving the Performance of Berkeley UNIX*
.sp
April 17, 1991
.AU
Marshall Kirk McKusick,
Samuel J. Leffler\(dg,
Michael J. Karels
.AI
Computer Systems Research Group
Computer Science Division
Department of Electrical Engineering and Computer Science
University of California, Berkeley
Berkeley, CA  94720
.AB
.FS
* UNIX is a trademark of AT&T Bell Laboratories.
.FE
.FS
\(dg Samuel J. Leffler is currently employed by:
Silicon Graphics, Inc.
.FE
.FS
This work was done under grants from
the National Science Foundation under grant MCS80-05144,
and the Defense Advance Research Projects Agency (DoD) under
ARPA Order No. 4031 monitored by Naval Electronic System Command under
Contract No. N00039-82-C-0235.
.FE
The 4.2 Berkeley Software Distribution of 
.UX
for the VAX\(dd
.FS
\(dd VAX, MASSBUS, UNIBUS, and DEC are trademarks of
Digital Equipment Corporation.
.FE
had several problems that could severely affect the overall
performance of the system.
These problems were identified with
kernel profiling and system tracing during day to day use.
Once potential problem areas had been identified
benchmark programs were devised to highlight the bottlenecks.
These benchmarks verified that the problems existed and provided
a metric against which to validate proposed solutions.
This paper examines 
the performance problems encountered and describes
modifications that have been made
to the system since the initial distribution.
.PP
The changes to the system have consisted of improvements to the
performance of the existing facilities,
as well as enhancements to the current facilities.
Performance improvements in the kernel include cacheing of path name
translations, reductions in clock handling and scheduling overhead,
and improved throughput of the network subsystem.
Performance improvements in the libraries and utilities include replacement of
linear searches of system databases with indexed lookup,
merging of most network services into a single daemon,
and conversion of system utilities to use the more efficient
facilities available in 4.2BSD.
Enhancements in the kernel include the addition of subnets and gateways,
increases in many kernel limits,
cleanup of the signal and autoconfiguration implementations,
and support for windows and system logging.
Functional extensions in the libraries and utilities include
the addition of an Internet name server,
new system management tools,
and extensions to \fIdbx\fP to work with Pascal.
The paper concludes with a brief discussion of changes made to
the system to enhance security.
All of these enhancements are present in Berkeley UNIX 4.3BSD.
.AE
.LP
.sp 2
CR Categories and Subject Descriptors:
D.4.3
.B "[Operating Systems]":
File Systems Management \-
.I "file organization, directory structures, access methods";
D.4.8
.B "[Operating Systems]":
Performance \-
.I "measurements, operational analysis";
.sp
Additional Keywords and Phrases:
Berkeley UNIX,
system performance,
application program interface.
.sp
General Terms:
UNIX operating system,
measurement,
performance.
.de PT
.lt \\n(LLu
.pc %
.nr PN \\n%
.tl '\\*(LH'\\*(CH'\\*(RH'
.lt \\n(.lu
..
.af PN i
.ds LH Performance
.ds RH Contents
.bp 1
.if t .ds CF April 17, 1991
.if t .ds LF DRAFT
.if t .ds RF McKusick, et. al.
.ce
.B "TABLE OF CONTENTS"
.LP
.sp 1
.nf
.B "1.  Introduction"
.LP
.sp .5v
.nf
.B "2.  Observation techniques
\0.1.    System maintenance tools
\0.2.    Kernel profiling
\0.3.    Kernel tracing
\0.4.    Benchmark programs
.LP
.sp .5v
.nf
.B "3.  Results of our observations
\0.1.    User programs
\0.1.1.    Mail system
\0.1.2.    Network servers
\0.2.    System overhead
\0.2.1.    Micro-operation benchmarks
\0.2.2.    Path name translation
\0.2.3.    Clock processing
\0.2.4.    Terminal multiplexors
\0.2.5.    Process table management
\0.2.6.    File system buffer cache
\0.2.7.    Network subsystem
\0.2.8.    Virtual memory subsystem
.LP
.sp .5v
.nf
.B "4.  Performance Improvements
\0.1.    Performance Improvements in the Kernel
\0.1.1.    Name Cacheing
\0.1.2.    Intelligent Auto Siloing
\0.1.3.    Process Table Management
\0.1.4.    Scheduling
\0.1.5.    Clock Handling
\0.1.6.    File System
\0.1.7.    Network
\0.1.8.    Exec
\0.1.9.    Context Switching
\0.1.10.   Setjmp and Longjmp
\0.1.11.   Compensating for Lack of Compiler Technology
\0.2.    Improvements to Libraries and Utilities
\0.2.1.    Hashed Databases
\0.2.2.    Buffered I/O
\0.2.3.    Mail System
\0.2.4.    Network Servers
\0.2.5.    The C Run-time Library
\0.2.6.    Csh
.LP
.sp .5v
.nf
.B "5.  Functional Extensions
\0.1.    Kernel Extensions
\0.1.1.    Subnets, Broadcasts, and Gateways
\0.1.2.    Interface Addressing
\0.1.3.    User Control of Network Buffering
\0.1.4.    Number of File Descriptors
\0.1.5.    Kernel Limits
\0.1.6.    Memory Management
\0.1.7.    Signals
\0.1.8.    System Logging
\0.1.9.    Windows
\0.1.10.   Configuration of UNIBUS Devices
\0.1.11.   Disk Recovery from Errors
\0.2.    Functional Extensions to Libraries and Utilities
\0.2.1.    Name Server
\0.2.2.    System Management
\0.2.3.    Routing
\0.2.4.    Compilers
.LP
.sp .5v
.nf
.B "6.  Security Tightening
\0.1.    Generic Kernel
\0.2.    Security Problems in Utilities
.LP
.sp .5v
.nf
.B "7.  Conclusions
.LP
.sp .5v
.nf
.B Acknowledgements
.LP
.sp .5v
.nf
.B References
.LP
.sp .5v
.nf
.B "Appendix \- Benchmark Programs"
.de _d
.if t .ta .6i 2.1i 2.6i
.\" 2.94 went to 2.6, 3.64 to 3.30
.if n .ta .84i 2.6i 3.30i
..
.de _f
.if t .ta .5i 1.25i 2.5i
.\" 3.5i went to 3.8i
.if n .ta .7i 1.75i 3.8i
..
