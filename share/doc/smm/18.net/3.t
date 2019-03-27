.\" Copyright (c) 1983, 1986, 1993
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
.\"	@(#)3.t	8.1 (Berkeley) 6/8/93
.\"
.nr H2 1
.\".ds RH Goals
.br
.ne 2i
.NH
\s+2Goals\s0
.PP
The networking system was designed with the goal of supporting
multiple \fIprotocol families\fP and addressing styles.  This required
information to be ``hidden'' in common data structures which
could be manipulated by all the pieces of the system, but which
required interpretation only by the protocols which ``controlled''
it.  The system described here attempts to minimize
the use of shared data structures to those kept by a suite of
protocols (a \fIprotocol family\fP), and those used for rendezvous
between ``synchronous'' and ``asynchronous'' portions of the
system (e.g. queues of data packets are filled at interrupt
time and emptied based on user requests).
.PP
A major goal of the system was to provide a framework within
which new protocols and hardware could be easily be supported.
To this end, a great deal of effort has been extended to
create utility routines which hide many of the more
complex and/or hardware dependent chores of networking.
Later sections describe the utility routines and the underlying
data structures they manipulate.
