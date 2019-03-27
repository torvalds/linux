.\" Copyright (c) 1986, 1993
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
.\"	@(#)1.t	8.1 (Berkeley) 8/14/93
.\"
.\".ds LH "4.4BSD IPC Primer
.\".ds RH Introduction
.\".ds RF "Leffler/Fabry/Joy
.\".ds LF "\*(DY
.\".ds CF "
.nr H1 1
.LP
.bp
.LG
.B
.ce
1. INTRODUCTION
.sp 2
.R
.NL
One of the most important additions to UNIX in 4.2BSD was interprocess
communication.
These facilities were the result of
more than two years of discussion and research.  The facilities
provided in 4.2BSD incorporated many of the ideas from current
research, while trying to maintain the UNIX philosophy of
simplicity and conciseness.
The 4.3BSD release of Berkeley UNIX
improved upon some of the IPC facilities
while providing an upward-compatible interface.
4.4BSD adds support for ISO protocols and IP multicasting.
The BSD interprocess communication
facilities have become a defacto standard for UNIX.
.PP
UNIX has previously been very weak in the area of interprocess
communication.  Prior to the 4BSD facilities, the only
standard mechanism which allowed two processes to communicate were
pipes (the mpx files which were part of Version 7 were
experimental).  Unfortunately, pipes are very restrictive
in that
the two communicating processes must be related through a
common ancestor.
Further, the semantics of pipes makes them almost impossible
to maintain in a distributed environment. 
.PP
Earlier attempts at extending the IPC facilities of UNIX have
met with mixed reaction.  The majority of the problems have
been related to the fact that these facilities have been tied to
the UNIX file system, either through naming or implementation.
Consequently, the IPC facilities provided in 4.2BSD were
designed as a totally independent subsystem.  The BSD IPC
allows processes to rendezvous in many ways. 
Processes may rendezvous through a UNIX file system-like
name space (a space where all names are path names)
as well as through a
network name space.  In fact, new name spaces may
be added at a future time with only minor changes visible
to users.  Further, the communication facilities 
have been extended to include more than the simple byte stream
provided by a pipe.  These extensions have resulted
in a completely new part of the system which users will need
time to familiarize themselves with.  It is likely that as
more use is made of these facilities they will be refined;
only time will tell.
.PP
This document provides a high-level description
of the IPC facilities in 4.4BSD and their use.
It is designed to complement the manual pages for the IPC primitives
by examples of their use.
The remainder of this document is organized in four sections.  
Section 2 introduces the IPC-related system calls and the basic model
of communication.  Section 3 describes some of the supporting
library routines users may find useful in constructing distributed
applications.  Section 4 is concerned with the client/server model
used in developing applications and includes examples of the
two major types of servers.  Section 5 delves into advanced topics
which sophisticated users are likely to encounter when using
the IPC facilities.
