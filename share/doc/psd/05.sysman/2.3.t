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
.\"	@(#)2.3.t	8.1 (Berkeley) 6/8/93
.\" $FreeBSD$
.\"
.sh "Interprocess communications
.NH 3
Interprocess communication primitives
.NH 4
Communication domains
.PP
The system provides access to an extensible set of 
communication \fIdomains\fP.  A communication domain
is identified by a manifest constant defined in the
file \fI<sys/socket.h>\fP.
Important standard domains supported by the system are the ``unix''
domain, AF_UNIX, for communication within the system, the ``Internet''
domain for communication in the DARPA Internet, AF_INET,
and the ``NS'' domain, AF_NS, for communication
using the Xerox Network Systems protocols.
Other domains can be added to the system.
.NH 4
Socket types and protocols
.PP
Within a domain, communication takes place between communication endpoints
known as \fIsockets\fP.  Each socket has the potential to exchange
information with other sockets of an appropriate type within the domain.
.PP
Each socket has an associated
abstract type, which describes the semantics of communication using that
socket.  Properties such as reliability, ordering, and prevention
of duplication of messages are determined by the type.
The basic set of socket types is defined in \fI<sys/socket.h>\fP:
.DS
/* Standard socket types */
._d
#define	SOCK_DGRAM	1	/* datagram */
#define	SOCK_STREAM	2	/* virtual circuit */
#define	SOCK_RAW	3	/* raw socket */
#define	SOCK_RDM	4	/* reliably-delivered message */
#define	SOCK_SEQPACKET	5	/* sequenced packets */
.DE
The SOCK_DGRAM type models the semantics of datagrams in network communication:
messages may be lost or duplicated and may arrive out-of-order.
A datagram socket may send messages to and receive messages from multiple
peers.
The SOCK_RDM type models the semantics of reliable datagrams: messages
arrive unduplicated and in-order, the sender is notified if
messages are lost.
The \fIsend\fP and \fIreceive\fP operations (described below)
generate reliable/unreliable datagrams.
The SOCK_STREAM type models connection-based virtual circuits: two-way
byte streams with no record boundaries.
Connection setup is required before data communication may begin.
The SOCK_SEQPACKET type models a connection-based,
full-duplex, reliable, sequenced packet exchange;
the sender is notified if messages are lost, and messages are never
duplicated or presented out-of-order.
Users of the last two abstractions may use the facilities for
out-of-band transmission to send out-of-band data.
.PP
SOCK_RAW is used for unprocessed access to internal network layers
and interfaces; it has no specific semantics.
.PP
Other socket types can be defined.
.PP
Each socket may have a specific \fIprotocol\fP associated with it.
This protocol is used within the domain to provide the semantics
required by the socket type.
Not all socket types are supported by each domain;
support depends on the existence and the implementation
of a suitable protocol within the domain.
For example, within the ``Internet'' domain, the SOCK_DGRAM type may be
implemented by the UDP user datagram protocol, and the SOCK_STREAM
type may be implemented by the TCP transmission control protocol, while
no standard protocols to provide SOCK_RDM or SOCK_SEQPACKET sockets exist.
.NH 4
Socket creation, naming and service establishment
.PP
Sockets may be \fIconnected\fP or \fIunconnected\fP.  An unconnected
socket descriptor is obtained by the \fIsocket\fP call:
.DS
s = socket(domain, type, protocol);
result int s; int domain, type, protocol;
.DE
The socket domain and type are as described above,
and are specified using the definitions from \fI<sys/socket.h>\fP.
The protocol may be given as 0, meaning any suitable protocol.
One of several possible protocols may be selected using identifiers
obtained from a library routine, \fIgetprotobyname\fP.
.PP
An unconnected socket descriptor of a connection-oriented type
may yield a connected socket descriptor
in one of two ways: either by actively connecting to another socket,
or by becoming associated with a name in the communications domain and
\fIaccepting\fP a connection from another socket.
Datagram sockets need not establish connections before use.
.PP
To accept connections or to receive datagrams,
a socket must first have a binding
to a name (or address) within the communications domain.
Such a binding may be established by a \fIbind\fP call:
.DS
bind(s, name, namelen);
int s; struct sockaddr *name; int namelen;
.DE
Datagram sockets may have default bindings established when first
sending data if not explicitly bound earlier.
In either case,
a socket's bound name may be retrieved with a \fIgetsockname\fP call:
.DS
getsockname(s, name, namelen);
int s; result struct sockaddr *name; result int *namelen;
.DE
while the peer's name can be retrieved with \fIgetpeername\fP:
.DS
getpeername(s, name, namelen);
int s; result struct sockaddr *name; result int *namelen;
.DE
Domains may support sockets with several names.
.NH 4
Accepting connections
.PP
Once a binding is made to a connection-oriented socket,
it is possible to \fIlisten\fP for connections:
.DS
listen(s, backlog);
int s, backlog;
.DE
The \fIbacklog\fP specifies the maximum count of connections
that can be simultaneously queued awaiting acceptance.
.PP
An \fIaccept\fP call:
.DS
t = accept(s, name, anamelen);
result int t; int s; result struct sockaddr *name; result int *anamelen;
.DE
returns a descriptor for a new, connected, socket
from the queue of pending connections on \fIs\fP.
If no new connections are queued for acceptance,
the call will wait for a connection unless non-blocking I/O has been enabled.
.NH 4
Making connections
.PP
An active connection to a named socket is made by the \fIconnect\fP call:
.DS
connect(s, name, namelen);
int s; struct sockaddr *name; int namelen;
.DE
Although datagram sockets do not establish connections,
the \fIconnect\fP call may be used with such sockets
to create an \fIassociation\fP with the foreign address.
The address is recorded for use in future \fIsend\fP calls,
which then need not supply destination addresses.
Datagrams will be received only from that peer,
and asynchronous error reports may be received.
.PP
It is also possible to create connected pairs of sockets without
using the domain's name space to rendezvous; this is done with the
\fIsocketpair\fP call\(dg:
.FS
\(dg 4.3BSD supports \fIsocketpair\fP creation only in the ``unix''
communication domain.
.FE
.DS
socketpair(domain, type, protocol, sv);
int domain, type, protocol; result int sv[2];
.DE
Here the returned \fIsv\fP descriptors correspond to those obtained with
\fIaccept\fP and \fIconnect\fP.
.PP
The call
.DS
pipe(pv)
result int pv[2];
.DE
creates a pair of SOCK_STREAM sockets in the UNIX domain,
with pv[0] only writable and pv[1] only readable.
.NH 4
Sending and receiving data
.PP
Messages may be sent from a socket by:
.DS
cc = sendto(s, buf, len, flags, to, tolen);
result int cc; int s; caddr_t buf; int len, flags; caddr_t to; int tolen;
.DE
if the socket is not connected or:
.DS
cc = send(s, buf, len, flags);
result int cc; int s; caddr_t buf; int len, flags;
.DE
if the socket is connected.
The corresponding receive primitives are:
.DS
msglen = recvfrom(s, buf, len, flags, from, fromlenaddr);
result int msglen; int s; result caddr_t buf; int len, flags;
result caddr_t from; result int *fromlenaddr;
.DE
and
.DS
msglen = recv(s, buf, len, flags);
result int msglen; int s; result caddr_t buf; int len, flags;
.DE
.PP
In the unconnected case,
the parameters \fIto\fP and \fItolen\fP
specify the destination or source of the message, while
the \fIfrom\fP parameter stores the source of the message,
and \fI*fromlenaddr\fP initially gives the size of the \fIfrom\fP
buffer and is updated to reflect the true length of the \fIfrom\fP
address.
.PP
All calls cause the message to be received in or sent from
the message buffer of length \fIlen\fP bytes, starting at address \fIbuf\fP.
The \fIflags\fP specify
peeking at a message without reading it or sending or receiving
high-priority out-of-band messages, as follows:
.DS
._d
#define	MSG_PEEK	0x1	/* peek at incoming message */
#define	MSG_OOB	0x2	/* process out-of-band data */
.DE
.NH 4
Scatter/gather and exchanging access rights
.PP
It is possible scatter and gather data and to exchange access rights
with messages.  When either of these operations is involved,
the number of parameters to the call becomes large.
Thus the system defines a message header structure, in \fI<sys/socket.h>\fP,
which can be
used to conveniently contain the parameters to the calls:
.DS
.if t .ta .5i 1.25i 2i 2.7i
.if n ._f
struct msghdr {
	caddr_t	msg_name;		/* optional address */
	int	msg_namelen;	/* size of address */
	struct	iov *msg_iov;	/* scatter/gather array */
	int	msg_iovlen;		/* # elements in msg_iov */
	caddr_t	msg_accrights;	/* access rights sent/received */
	int	msg_accrightslen;	/* size of msg_accrights */
};
.DE
Here \fImsg_name\fP and \fImsg_namelen\fP specify the source or destination
address if the socket is unconnected; \fImsg_name\fP may be given as
a null pointer if no names are desired or required.
The \fImsg_iov\fP and \fImsg_iovlen\fP describe the scatter/gather
locations, as described in section 2.1.3.
Access rights to be sent along with the message are specified
in \fImsg_accrights\fP, which has length \fImsg_accrightslen\fP.
In the ``unix'' domain these are an array of integer descriptors,
taken from the sending process and duplicated in the receiver.
.PP
This structure is used in the operations \fIsendmsg\fP and \fIrecvmsg\fP:
.DS
sendmsg(s, msg, flags);
int s; struct msghdr *msg; int flags;

msglen = recvmsg(s, msg, flags);
result int msglen; int s; result struct msghdr *msg; int flags;
.DE
.NH 4
Using read and write with sockets
.PP
The normal UNIX \fIread\fP and \fIwrite\fP calls may be
applied to connected sockets and translated into \fIsend\fP and \fIreceive\fP
calls from or to a single area of memory and discarding any rights
received.  A process may operate on a virtual circuit socket, a terminal
or a file with blocking or non-blocking input/output
operations without distinguishing the descriptor type.
.NH 4
Shutting down halves of full-duplex connections
.PP
A process that has a full-duplex socket such as a virtual circuit
and no longer wishes to read from or write to this socket can
give the call:
.DS
shutdown(s, direction);
int s, direction;
.DE
where \fIdirection\fP is 0 to not read further, 1 to not
write further, or 2 to completely shut the connection down.
If the underlying protocol supports unidirectional or bidirectional shutdown,
this indication will be passed to the peer.
For example, a shutdown for writing might produce an end-of-file
condition at the remote end.
.NH 4
Socket and protocol options
.PP
Sockets, and their underlying communication protocols, may
support \fIoptions\fP.  These options may be used to manipulate
implementation- or protocol-specific facilities. 
The \fIgetsockopt\fP
and \fIsetsockopt\fP calls are used to control options:
.DS
getsockopt(s, level, optname, optval, optlen)
int s, level, optname; result caddr_t optval; result int *optlen;

setsockopt(s, level, optname, optval, optlen)
int s, level, optname; caddr_t optval; int optlen;
.DE
The option \fIoptname\fP is interpreted at the indicated
protocol \fIlevel\fP for socket \fIs\fP.  If a value is specified
with \fIoptval\fP and \fIoptlen\fP, it is interpreted by
the software operating at the specified \fIlevel\fP.  The \fIlevel\fP
SOL_SOCKET is reserved to indicate options maintained
by the socket facilities.  Other \fIlevel\fP values indicate
a particular protocol which is to act on the option request;
these values are normally interpreted as a ``protocol number''.
.NH 3
UNIX domain
.PP
This section describes briefly the properties of the UNIX communications
domain.
.NH 4
Types of sockets
.PP
In the UNIX domain,
the SOCK_STREAM abstraction provides pipe-like
facilities, while SOCK_DGRAM provides (usually)
reliable message-style communications.
.NH 4
Naming
.PP
Socket names are strings and may appear in the UNIX file
system name space through portals\(dg.
.FS
\(dg The 4.3BSD implementation of the UNIX domain embeds
bound sockets in the UNIX file system name space;
this may change in future releases.
.FE
.NH 4
Access rights transmission
.PP
The ability to pass UNIX descriptors with messages in this domain
allows migration of service within the system and allows
user processes to be used in building system facilities.
.NH 3
INTERNET domain
.PP
This section describes briefly how the Internet domain is
mapped to the model described in this section.  More
information will be found in the document describing the
network implementation in 4.3BSD.
.NH 4
Socket types and protocols
.PP
SOCK_STREAM is supported by the Internet TCP protocol;
SOCK_DGRAM by the UDP protocol.
Each is layered atop the transport-level Internet Protocol (IP).
The Internet Control Message Protocol is implemented atop/beside IP
and is accessible via a raw socket.
The SOCK_SEQPACKET
has no direct Internet family analogue; a protocol
based on one from the XEROX NS family and layered on
top of IP could be implemented to fill this gap.
.NH 4
Socket naming
.PP
Sockets in the Internet domain have names composed of the 32 bit
Internet address, and a 16 bit port number.
Options may be used to
provide IP source routing or security options.
The 32-bit address is composed of network and host parts;
the network part is variable in size and is frequency encoded.
The host part may optionally be interpreted as a subnet field
plus the host on subnet; this is enabled by setting a network address
mask at boot time.
.NH 4
Access rights transmission
.PP
No access rights transmission facilities are provided in the Internet domain.
.NH 4
Raw access
.PP
The Internet domain allows the super-user access to the raw facilities
of IP.
These interfaces are modeled as SOCK_RAW sockets.
Each raw socket is associated with one IP protocol number,
and receives all traffic received for that protocol.
This allows administrative and debugging
functions to occur,
and enables user-level implementations of special-purpose protocols
such as inter-gateway routing protocols.
