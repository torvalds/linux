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
.\"	@(#)f.t	8.1 (Berkeley) 6/8/93
.\"
.nr H2 1
.\".ds RH Acknowledgements
.br
.ne 2i
.SH
\s+2Acknowledgements\s0
.PP
The internal structure of the system is patterned
after the Xerox PUP architecture [Boggs79], while in certain
places the Internet
protocol family has had a great deal of influence in the design.
The use of software interrupts for process invocation
is based on similar facilities found in
the VMS operating system.
Many of the
ideas related to protocol modularity, memory management, and network
interfaces are based on Rob Gurwitz's TCP/IP implementation for the 
4.1BSD version of UNIX on the VAX [Gurwitz81].
Greg Chesson explained his use of trailer encapsulations in Datakit,
instigating their use in our system.
.\".ds RH References
.nr H2 1
.sp 2
.ne 2i
.SH
\s+2References\s0
.LP
.IP [Boggs79] 20
Boggs, D. R., J. F. Shoch, E. A. Taft, and R. M. Metcalfe;
\fIPUP: An Internetwork Architecture\fP.  Report CSL-79-10.
XEROX Palo Alto Research Center, July 1979.
.IP [BBN78] 20
Bolt Beranek and Newman;
Specification for the Interconnection of Host and IMP.
BBN Technical Report 1822.  May 1978.
.IP [Cerf78] 20
Cerf, V. G.;  The Catenet Model for Internetworking.
Internet Working Group, IEN 48.  July 1978.
.IP [Clark82] 20
Clark, D. D.;  Window and Acknowledgement Strategy in TCP, RFC-813.
Network Information Center, SRI International.  July 1982.
.IP [DEC80] 20
Digital Equipment Corporation;  \fIDECnet DIGITAL Network
Architecture \- General Description\fP.  Order No.
AA-K179A-TK.  October 1980.
.IP [Gurwitz81] 20
Gurwitz, R. F.;  VAX-UNIX Networking Support Project \- Implementation
Description.  Internetwork Working Group, IEN 168.
January 1981.
.IP [ISO81] 20
International Organization for Standardization.
\fIISO Open Systems Interconnection \- Basic Reference Model\fP.
ISO/TC 97/SC 16 N 719.  August 1981.
.IP [Joy86] 20
Joy, W.; Fabry, R.; Leffler, S.; McKusick, M.; and Karels, M.;
Berkeley Software Architecture Manual, 4.4BSD Edition.
\fIUNIX Programmer's Supplementary Documents\fP, Vol. 1 (PSD:5).
Computer Systems Research Group,
University of California, Berkeley.
May, 1986.
.IP [Leffler84] 20
Leffler, S.J. and Karels, M.J.; Trailer Encapsulations, RFC-893.
Network Information Center, SRI International.
April 1984.
.IP [Postel80] 20
Postel, J.  User Datagram Protocol, RFC-768.
Network Information Center, SRI International.  May 1980.
.IP [Postel81a] 20
Postel, J., ed.  Internet Protocol, RFC-791.
Network Information Center, SRI International.  September 1981.
.IP [Postel81b] 20
Postel, J., ed.  Transmission Control Protocol, RFC-793.
Network Information Center, SRI International.  September 1981.
.IP [Postel81c] 20
Postel, J.  Internet Control Message Protocol, RFC-792.
Network Information Center, SRI International.  September 1981.
.IP [Xerox81] 20
Xerox Corporation.  \fIInternet Transport Protocols\fP. 
Xerox System Integration Standard 028112.  December 1981.
.IP [Zimmermann80] 20
Zimmermann, H.  OSI Reference Model \- The ISO Model of
Architecture for Open Systems Interconnection.
\fIIEEE Transactions on Communications\fP.  Com-28(4); 425-432.
April 1980.
