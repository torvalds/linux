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
.\"	@(#)0.t	8.1 (Berkeley) 6/8/93
.\"
.if n .ND
.TL
Berkeley Software Architecture Manual
.br
4.4BSD Edition
.AU
William Joy, Robert Fabry,
.AU
Samuel Leffler, M. Kirk McKusick,
.AU
Michael Karels
.AI
Computer Systems Research Group
Computer Science Division
Department of Electrical Engineering and Computer Science
University of California, Berkeley
Berkeley, CA  94720
.EH 'PSD:5-%''4.4BSD Architecture Manual'
.OH '4.4BSD Architecture Manual''PSD:5-%'
.AB
.FS
* UNIX is a trademark of Bell Laboratories.
.FE
This document summarizes the facilities
provided by the 4.4BSD version of the UNIX\|* operating system.
It does not attempt to act as a tutorial for use of the system
nor does it attempt to explain or justify the design of the
system facilities.
It gives neither motivation nor implementation details,
in favor of brevity.
.PP
The first section describes the basic kernel functions
provided to a UNIX process: process naming and protection,
memory management, software interrupts,
object references (descriptors), time and statistics functions,
and resource controls.
These facilities, as well as facilities for
bootstrap, shutdown and process accounting,
are provided solely by the kernel.
.PP
The second section describes the standard system
abstractions for
files and file systems,
communication,
terminal handling,
and process control and debugging.
These facilities are implemented by the operating system or by
network server processes.
.AE
.LP
.bp
.ft B
.br
.sv 2
.ce
TABLE OF CONTENTS
.ft R
.LP
.sp 1
.nf
.B "Introduction."
.LP
.if t .sp .5v
.nf
.B "0. Notation and types"
.LP
.if t .sp .5v
.nf
.B "1. Kernel primitives"
.LP
.if t .sp .5v
.nf
.nf
\fB1.1.  Processes and protection\fP
1.1.1.  Host and process identifiers
1.1.2.  Process creation and termination
1.1.3.  User and group ids
1.1.4.  Process groups
.LP
.nf
\fB1.2.  Memory management\fP
1.2.1.  Text, data and stack
1.2.2.  Mapping pages
1.2.3.  Page protection control
1.2.4.  Giving and getting advice
1.2.5.  Protection primitives
.LP
.if t .sp .5v
.nf
\fB1.3.  Signals\fP
1.3.1.  Overview
1.3.2.  Signal types
1.3.3.  Signal handlers
1.3.4.  Sending signals
1.3.5.  Protecting critical sections
1.3.6.  Signal stacks
.LP
.if t .sp .5v
.nf
\fB1.4.  Timing and statistics\fP
1.4.1.  Real time
1.4.2.  Interval time
.LP
.if t .sp .5v
.nf
\fB1.5.  Descriptors\fP
1.5.1.  The reference table
1.5.2.  Descriptor properties
1.5.3.  Managing descriptor references
1.5.4.  Multiplexing requests
1.5.5.  Descriptor wrapping
.LP
.if t .sp .5v
.nf
\fB1.6.  Resource controls\fP
1.6.1.  Process priorities
1.6.2.  Resource utilization
1.6.3.  Resource limits
.LP
.if t .sp .5v
.nf
\fB1.7.  System operation support\fP
1.7.1.   Bootstrap operations
1.7.2.   Shutdown operations
1.7.3.   Accounting
.bp
.LP
.if t .sp .5v
.sp 1
.nf
\fB2.  System facilities\fP
.LP
.if t .sp .5v
.nf
\fB2.1.   Generic operations\fP
2.1.1.   Read and write
2.1.2.   Input/output control
2.1.3.   Non-blocking and asynchronous operations
.LP
.if t .sp .5v
.nf
\fB2.2.  File system\fP
2.2.1   Overview
2.2.2.  Naming
2.2.3.  Creation and removal
2.2.3.1.  Directory creation and removal
2.2.3.2.  File creation
2.2.3.3.  Creating references to devices
2.2.3.4.  Portal creation
2.2.3.6.  File, device, and portal removal
2.2.4.  Reading and modifying file attributes
2.2.5.  Links and renaming
2.2.6.  Extension and truncation
2.2.7.  Checking accessibility
2.2.8.  Locking
2.2.9.  Disc quotas
.LP
.if t .sp .5v
.nf
\fB2.3.  Interprocess communication\fP
2.3.1.   Interprocess communication primitives
2.3.1.1.\0   Communication domains
2.3.1.2.\0   Socket types and protocols
2.3.1.3.\0   Socket creation, naming and service establishment
2.3.1.4.\0   Accepting connections
2.3.1.5.\0   Making connections
2.3.1.6.\0   Sending and receiving data
2.3.1.7.\0   Scatter/gather and exchanging access rights
2.3.1.8.\0   Using read and write with sockets
2.3.1.9.\0   Shutting down halves of full-duplex connections
2.3.1.10.\0  Socket and protocol options
2.3.2.   UNIX domain
2.3.2.1.    Types of sockets
2.3.2.2.    Naming
2.3.2.3.    Access rights transmission
2.3.3.   INTERNET domain
2.3.3.1.    Socket types and protocols
2.3.3.2.    Socket naming
2.3.3.3.    Access rights transmission
2.3.3.4.    Raw access
.LP
.if t .sp .5v
.nf
\fB2.4.  Terminals and devices\fP
2.4.1.   Terminals
2.4.1.1.    Terminal input
2.4.1.1.1     Input modes
2.4.1.1.2     Interrupt characters
2.4.1.1.3     Line editing
2.4.1.2.    Terminal output
2.4.1.3.    Terminal control operations
2.4.1.4.    Terminal hardware support
2.4.2.   Structured devices
2.4.3.   Unstructured devices
.LP
.if t .sp .5v
.nf
\fB2.5.  Process control and debugging\fP
.LP
.if t .sp .5v
.nf
\fBI.  Summary of facilities\fP
.LP
.de sh
.ds RH \\$1
.bp
.NH \\*(ss
\s+2\\$1\s0
.PP
.PP
..
.bp
.ds ss 1
.de _d
.if t .ta .6i 2.1i 2.6i
.\" 2.94 went to 2.6, 3.64 to 3.30
.if n .ta .84i 2.6i 3.30i
..
.de _f
.if t .ta .5i 1.25i 2.5i 3.5i
.\" 3.5i went to 3.8i
.if n .ta .7i 1.75i 3.8i 4.8i
..
.nr H1 -1
.sh "Notation and types
.PP
The notation used to describe system calls is a variant of a
C language call, consisting of a prototype call followed by
declaration of parameters and results.
An additional keyword \fBresult\fP, not part of the normal C language,
is used to indicate which of the declared entities receive results.
As an example, consider the \fIread\fP call, as described in
section 2.1:
.DS
cc = read(fd, buf, nbytes);
result int cc; int fd; result char *buf; int nbytes;
.DE
The first line shows how the \fIread\fP routine is called, with
three parameters.
As shown on the second line \fIcc\fP is an integer and \fIread\fP also
returns information in the parameter \fIbuf\fP.
.PP
Description of all error conditions arising from each system call
is not provided here; they appear in the programmer's manual.
In particular, when accessed from the C language,
many calls return a characteristic \-1 value
when an error occurs, returning the error code in the global variable
\fIerrno\fP.
Other languages may present errors in different ways.
.PP
A number of system standard types are defined in the include file
.I <sys/types.h>
and used in the specifications here and in many C programs.
These include \fBcaddr_t\fP giving a memory address (typically as
a character pointer), 
\fBoff_t\fP giving a file offset (typically as a long integer),
and a set of unsigned types \fBu_char\fP, \fBu_short\fP, \fBu_int\fP
and \fBu_long\fP, shorthand names for \fBunsigned char\fP, \fBunsigned
short\fP, etc.
