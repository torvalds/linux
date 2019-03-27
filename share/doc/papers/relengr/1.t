.\" Copyright (c) 1989 The Regents of the University of California.
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
.NH
Introduction
.PP
The Computer Systems Research Group (\c
.SM CSRG )
has always been a small group of software developers.
This resource limitation requires careful software-engineering management
as well as careful coordination of both
.SM CSRG
personnel and the members of the general community who
contribute to the development of the system.
.PP
Releases from Berkeley alternate between those that introduce
major new facilities and those that provide bug fixes and efficiency
improvements.
This alternation allows timely releases, while providing for refinement,
tuning, and correction of the new facilities.
The timely followup of ``cleanup'' releases reflects the importance
.SM CSRG
places on providing a reliable and robust system on which its
user community can depend.
.PP
The development of the Berkeley Software Distribution (\c
.SM BSD )
illustrates an \fIadvantage\fP of having a few
principal developers:
the developers all understand the entire system thoroughly enough
to be able to coordinate their own work with
that of other people to produce a coherent final system.
Companies with large development organizations find
this result difficult to duplicate.
This paper describes the process by which
the development effort for \*(b3 was managed.
.[
design and implementation
.]
