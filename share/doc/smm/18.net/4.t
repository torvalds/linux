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
.\"	@(#)4.t	8.1 (Berkeley) 6/8/93
.\"
.nr H2 1
.\".ds RH "Address representation
.br
.ne 2i
.NH
\s+2Internal address representation\s0
.PP
Common to all portions of the system are two data structures.
These structures are used to represent
addresses and various data objects.
Addresses, internally are described by the \fIsockaddr\fP structure,
.DS
._f
struct sockaddr {
	short	sa_family;	/* data format identifier */
	char	sa_data[14];	/* address */
};
.DE
All addresses belong to one or more \fIaddress families\fP
which define their format and interpretation.
The \fIsa_family\fP field indicates the address family to which the address
belongs, and the \fIsa_data\fP field contains the actual data value.
The size of the data field, 14 bytes, was selected based on a study
of current address formats.*
Specific address formats use private structure definitions
that define the format of the data field.
The system interface supports larger address structures,
although address-family-independent support facilities, for example routing
and raw socket interfaces, provide only 14 bytes for address storage.
Protocols that do not use those facilities (e.g, the current Unix domain)
may use larger data areas.
.FS
* Later versions of the system may support variable length addresses.
.FE
