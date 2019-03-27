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
.\"	@(#)2.t	8.1 (Berkeley) 6/8/93
.\"
.nr H2 1
.\".ds RH Overview
.br
.ne 2i
.NH
\s+2Overview\s0
.PP
If we consider
the International Standards Organization's (ISO)
Open System Interconnection (OSI) model of
network communication [ISO81] [Zimmermann80],
the networking facilities
described here correspond to a portion of the
session layer (layer 3) and all of the transport and
network layers (layers 2 and 1, respectively).
.PP
The network layer provides possibly imperfect
data transport services with minimal addressing
structure.
Addressing at this level is normally host to host,
with implicit or explicit routing optionally supported
by the communicating agents. 
.PP
At the transport
layer the notions of reliable transfer, data sequencing,
flow control, and service addressing are normally
included.  Reliability is usually managed by 
explicit acknowledgement of data delivered.  Failure
to acknowledge a transfer results in retransmission of
the data.  Sequencing may be handled by tagging
each message handed to the network layer by a
\fIsequence number\fP and maintaining
state at the endpoints of communication to utilize
received sequence numbers in reordering data which
arrives out of order.
.PP
The session layer facilities may provide forms of
addressing which are mapped into formats required
by the transport layer, service authentication
and client authentication, etc.  Various systems
also provide services such as data encryption and
address and protocol translation.
.PP
The following sections begin by describing some of the common
data structures and utility routines, then examine
the internal layering.  The contents of each layer
and its interface are considered.  Certain of the
interfaces are protocol implementation specific.  For
these cases examples have been drawn from the Internet [Cerf78]
protocol family.  Later sections cover routing issues,
the design of the raw socket interface and other
miscellaneous topics.
