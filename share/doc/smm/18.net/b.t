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
.\"	@(#)b.t	8.1 (Berkeley) 6/8/93
.\"
.nr H2 1
.\".ds RH "Raw sockets
.br
.ne 2i
.NH
\s+2Raw sockets\s0
.PP
A raw socket is an object which allows users direct access
to a lower-level protocol.  Raw sockets are intended for knowledgeable
processes which wish to take advantage of some protocol
feature not directly accessible through the normal interface, or 
for the development of new protocols built atop existing lower level
protocols.  For example, a new version of TCP might be developed at the
user level by utilizing a raw IP socket for delivery of packets.
The raw IP socket interface attempts to provide an identical interface
to the one a protocol would have if it were resident in the kernel.
.PP
The raw socket support is built around a generic raw socket interface,
(possibly) augmented by protocol-specific processing routines.
This section will describe the core of the raw socket interface.
.NH 2
Control blocks
.PP
Every raw socket has a protocol control block of the following form:
.DS
.ta \w'struct  'u +\w'caddr_t  'u +\w'sockproto rcb_proto;    'u 
struct rawcb {
	struct	rawcb *rcb_next;	/* doubly linked list */
	struct	rawcb *rcb_prev;
	struct	socket *rcb_socket;	/* back pointer to socket */
	struct	sockaddr rcb_faddr;	/* destination address */
	struct	sockaddr rcb_laddr;	/* socket's address */
	struct	sockproto rcb_proto;	/* protocol family, protocol */
	caddr_t	rcb_pcb;		/* protocol specific stuff */
	struct	mbuf *rcb_options;	/* protocol specific options */
	struct	route rcb_route;	/* routing information */
	short	rcb_flags;
};
.DE
All the control blocks are kept on a doubly linked list for
performing lookups during packet dispatch.  Associations may
be recorded in the control block and used by the output routine
in preparing packets for transmission.
The \fIrcb_proto\fP structure contains the protocol family and protocol
number with which the raw socket is associated.
The protocol, family and addresses are
used to filter packets on input; this will be described in more
detail shortly.  If any protocol-specific information is required,
it may be attached to the control block using the \fIrcb_pcb\fP
field.
Protocol-specific options for transmission in outgoing packets
may be stored in \fIrcb_options\fP.
.PP
A raw socket interface is datagram oriented.  That is, each send
or receive on the socket requires a destination address.  This
address may be supplied by the user or stored in the control block
and automatically installed in the outgoing packet by the output
routine.  Since it is not possible to determine whether an address
is present or not in the control block, two flags, RAW_LADDR and
RAW_FADDR, indicate if a local and foreign address are present.
Routing is expected to be performed by the underlying protocol
if necessary.
.NH 2
Input processing
.PP
Input packets are ``assigned'' to raw sockets based on a simple
pattern matching scheme.  Each network interface or protocol
gives unassigned packets
to the raw input routine with the call:
.DS
raw_input(m, proto, src, dst)
struct mbuf *m; struct sockproto *proto, struct sockaddr *src, *dst;
.DE
The data packet then has a generic header prepended to it of the
form
.DS
._f
struct raw_header {
	struct	sockproto raw_proto;
	struct	sockaddr raw_dst;
	struct	sockaddr raw_src;
};
.DE
and it is placed in a packet queue for the ``raw input protocol'' module.
Packets taken from this queue are copied into any raw sockets that
match the header according to the following rules,
.IP 1)
The protocol family of the socket and header agree.
.IP 2)
If the protocol number in the socket is non-zero, then it agrees
with that found in the packet header.
.IP 3)
If a local address is defined for the socket, the address format
of the local address is the same as the destination address's and
the two addresses agree bit for bit.
.IP 4)
The rules of 3) are applied to the socket's foreign address and the packet's
source address.
.LP
A basic assumption is that addresses present in the
control block and packet header (as constructed by the network
interface and any raw input protocol module) are in a canonical
form which may be ``block compared''.
.NH 2
Output processing
.PP
On output the raw \fIpr_usrreq\fP routine 
passes the packet and a pointer to the raw control block to the
raw protocol output routine for any processing required before
it is delivered to the appropriate network interface.  The
output routine is normally the only code required to implement
a raw socket interface.
