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
.\"	@(#)8.t	8.1 (Berkeley) 6/8/93
.\"
.nr H2 1
.\".ds RH "Protocol/protocol interface
.br
.ne 2i
.NH
\s+2Protocol/protocol interface\s0
.PP
The interface between protocol modules is through the \fIpr_usrreq\fP,
\fIpr_input\fP, \fIpr_output\fP, \fIpr_ctlinput\fP, and
\fIpr_ctloutput\fP routines.  The calling conventions for all
but the \fIpr_usrreq\fP routine are expected to be specific to
the protocol
modules and are not guaranteed to be consistent across protocol
families.  We
will examine the conventions used for some of the Internet
protocols in this section as an example.
.NH 2
pr_output
.PP
The Internet protocol UDP uses the convention,
.DS
error = udp_output(inp, m);
int error; struct inpcb *inp; struct mbuf *m;
.DE
where the \fIinp\fP, ``\fIin\fP\^ternet
\fIp\fP\^rotocol \fIc\fP\^ontrol \fIb\fP\^lock'',
passed between modules conveys per connection state information, and
the mbuf chain contains the data to be sent.  UDP
performs consistency checks, appends its header, calculates a
checksum, etc. before passing the packet on.
UDP is based on the Internet Protocol, IP [Postel81a], as its transport.
UDP passes a packet to the IP module for output as follows:
.DS
error = ip_output(m, opt, ro, flags);
int error; struct mbuf *m, *opt; struct route *ro; int flags;
.DE
.PP
The call to IP's output routine is more complicated than that for
UDP, as befits the additional work the IP module must do.
The \fIm\fP parameter is the data to be sent, and the \fIopt\fP
parameter is an optional list of IP options which should
be placed in the IP packet header.  The \fIro\fP parameter is
is used in making routing decisions (and passing them back to the
caller for use in subsequent calls).  The
final parameter, \fIflags\fP contains flags indicating whether the
user is allowed to transmit a broadcast packet
and if routing is to be performed.  The broadcast flag may
be inconsequential if the underlying hardware does not support the
notion of broadcasting.
.PP
All output routines return 0 on success and a UNIX error number
if a failure occurred which could be detected immediately
(no buffer space available, no route to destination, etc.).
.NH 2
pr_input
.PP
Both UDP and TCP use the following calling convention,
.DS
(void) (*protosw[].pr_input)(m, ifp);
struct mbuf *m; struct ifnet *ifp;
.DE
Each mbuf list passed is a single packet to be processed by
the protocol module.
The interface from which the packet was received is passed as the second
parameter.
.PP
The IP input routine is a VAX software interrupt level routine,
and so is not called with any parameters.  It instead communicates
with network interfaces through a queue, \fIipintrq\fP, which is
identical in structure to the queues used by the network interfaces
for storing packets awaiting transmission.
The software interrupt is enabled by the network interfaces
when they place input data on the input queue.
.NH 2
pr_ctlinput
.PP
This routine is used to convey ``control'' information to a
protocol module (i.e. information which might be passed to the
user, but is not data).
.PP
The common calling convention for this routine is,
.DS
(void) (*protosw[].pr_ctlinput)(req, addr);
int req; struct sockaddr *addr;
.DE
The \fIreq\fP parameter is one of the following,
.DS
.ta \w'#define  'u +\w'PRC_UNREACH_NEEDFRAG   'u +8n
#define	PRC_IFDOWN	0	/* interface transition */
#define	PRC_ROUTEDEAD	1	/* select new route if possible */
#define	PRC_QUENCH	4	/* some said to slow down */
#define	PRC_MSGSIZE	5	/* message size forced drop */
#define	PRC_HOSTDEAD	6	/* normally from IMP */
#define	PRC_HOSTUNREACH	7	/* ditto */
#define	PRC_UNREACH_NET	8	/* no route to network */
#define	PRC_UNREACH_HOST	9	/* no route to host */
#define	PRC_UNREACH_PROTOCOL	10	/* dst says bad protocol */
#define	PRC_UNREACH_PORT	11	/* bad port # */
#define	PRC_UNREACH_NEEDFRAG	12	/* IP_DF caused drop */
#define	PRC_UNREACH_SRCFAIL	13	/* source route failed */
#define	PRC_REDIRECT_NET	14	/* net routing redirect */
#define	PRC_REDIRECT_HOST	15	/* host routing redirect */
#define	PRC_REDIRECT_TOSNET	14	/* redirect for type of service & net */
#define	PRC_REDIRECT_TOSHOST	15	/* redirect for tos & host */
#define	PRC_TIMXCEED_INTRANS	18	/* packet lifetime expired in transit */
#define	PRC_TIMXCEED_REASS	19	/* lifetime expired on reass q */
#define	PRC_PARAMPROB	20	/* header incorrect */
.DE
while the \fIaddr\fP parameter is the address to which the condition applies.
Many of the requests have obviously been
derived from ICMP (the Internet Control Message Protocol [Postel81c]),
and from error messages defined in the 1822 host/IMP convention
[BBN78].  Mapping tables exist to convert
control requests to UNIX error codes which are delivered
to a user.
.NH 2
pr_ctloutput
.PP
This is the routine that implements per-socket options at the protocol
level for \fIgetsockopt\fP and \fIsetsockopt\fP.
The calling convention is,
.DS
error = (*protosw[].pr_ctloutput)(op, so, level, optname, mp);
int op; struct socket *so; int level, optname; struct mbuf **mp;
.DE
where \fIop\fP is one of PRCO_SETOPT or PRCO_GETOPT,
\fIso\fP is the socket from whence the call originated,
and \fIlevel\fP and \fIoptname\fP are the protocol level and option name
supplied by the user.
The results of a PRCO_GETOPT call are returned in an mbuf whose address
is placed in \fImp\fP before return.
On a PRCO_SETOPT call, \fImp\fP contains the address of an mbuf
containing the option data; the mbuf should be freed before return.
