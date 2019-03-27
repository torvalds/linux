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
.\"	@(#)0.t	8.1 (Berkeley) 6/10/93
.\"
.de IR
\fI\\$1\fP\\$2
..
.if n .ND
.TL
Networking Implementation Notes
.br
4.4BSD Edition
.AU
Samuel J. Leffler, William N. Joy, Robert S. Fabry, and Michael J. Karels
.AI
Computer Systems Research Group
Computer Science Division
Department of Electrical Engineering and Computer Science
University of California, Berkeley
Berkeley, CA  94720
.AB
.FS
* UNIX is a trademark of Bell Laboratories.
.FE
This report describes the internal structure of the
networking facilities developed for the 4.4BSD version
of the UNIX* operating system
for the VAX\(dg.  These facilities
.FS
\(dg DEC, VAX, DECnet, and UNIBUS are trademarks of
Digital Equipment Corporation.
.FE
are based on several central abstractions which
structure the external (user) view of network communication
as well as the internal (system) implementation.
.PP
The report documents the internal structure of the networking system.
The ``Berkeley Software Architecture Manual, 4.4BSD Edition'' (PSD:5)
provides a description of the user interface to the networking facilities.
.sp
.LP
Revised June 10, 1993
.AE
.LP
.\".de PT
.\".lt \\n(LLu
.\".pc %
.\".nr PN \\n%
.\".tl '\\*(LH'\\*(CH'\\*(RH'
.\".lt \\n(.lu
.\"..
.\".ds RH Contents
.OH 'Networking Implementation Notes''SMM:18-%'
.EH 'SMM:18-%''Networking Implementation Notes'
.bp
.ce
.B "TABLE OF CONTENTS"
.LP
.sp 1
.nf
.B "1.  Introduction"
.LP
.sp .5v
.nf
.B "2.  Overview"
.LP
.sp .5v
.nf
.B "3.  Goals
.LP
.sp .5v
.nf
.B "4.  Internal address representation"
.LP
.sp .5v
.nf
.B "5.  Memory management"
.LP
.sp .5v
.nf
.B "6.  Internal layering
6.1.    Socket layer
6.1.1.    Socket state
6.1.2.    Socket data queues
6.1.3.    Socket connection queuing
6.2.    Protocol layer(s)
6.3.    Network-interface layer
6.3.1.    UNIBUS interfaces
.LP
.sp .5v
.nf
.B "7.  Socket/protocol interface"
.LP
.sp .5v
.nf
.B "8.  Protocol/protocol interface"
8.1.     pr_output
8.2.     pr_input
8.3.     pr_ctlinput
8.4.     pr_ctloutput
.LP
.sp .5v
.nf
.B "9.  Protocol/network-interface interface"
9.1.     Packet transmission
9.2.     Packet reception
.LP
.sp .5v
.nf
.B "10. Gateways and routing issues
10.1.     Routing tables
10.2.     Routing table interface
10.3.     User level routing policies
.LP
.sp .5v
.nf
.B "11. Raw sockets"
11.1.     Control blocks
11.2.     Input processing
11.3.     Output processing
.LP
.sp .5v
.nf
.B "12. Buffering and congestion control"
12.1.     Memory management
12.2.     Protocol buffering policies
12.3.     Queue limiting
12.4.     Packet forwarding
.LP
.sp .5v
.nf
.B "13. Out of band data"
.LP
.sp .5v
.nf
.B "14. Trailer protocols"
.LP
.sp .5v
.nf
.B Acknowledgements
.LP
.sp .5v
.nf
.B References
.bp
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
