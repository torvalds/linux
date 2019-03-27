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
.\"	@(#)7.t	8.1 (Berkeley) 6/8/93
.\"
.\"	$FreeBSD$
.\"
.nr H2 1
.br
.ne 30v
.\".ds RH "Socket/protocol interface
.NH
\s+2Socket/protocol interface\s0
.PP
The interface between the socket routines and the communication
protocols is through the \fIpr_usrreq\fP routine defined in the
protocol switch table.  The following requests to a protocol
module are possible:
.DS
._d
#define	PRU_ATTACH	0	/* attach protocol */
#define	PRU_DETACH	1	/* detach protocol */
#define	PRU_BIND	2	/* bind socket to address */
#define	PRU_LISTEN	3	/* listen for connection */
#define	PRU_CONNECT	4	/* establish connection to peer */
#define	PRU_ACCEPT	5	/* accept connection from peer */
#define	PRU_DISCONNECT	6	/* disconnect from peer */
#define	PRU_SHUTDOWN	7	/* won't send any more data */
#define	PRU_RCVD	8	/* have taken data; more room now */
#define	PRU_SEND	9	/* send this data */
#define	PRU_ABORT	10	/* abort (fast DISCONNECT, DETATCH) */
#define	PRU_CONTROL	11	/* control operations on protocol */
#define	PRU_SENSE	12	/* return status into m */
#define	PRU_RCVOOB	13	/* retrieve out of band data */
#define	PRU_SENDOOB	14	/* send out of band data */
#define	PRU_SOCKADDR	15	/* fetch socket's address */
#define	PRU_PEERADDR	16	/* fetch peer's address */
#define	PRU_CONNECT2	17	/* connect two sockets */
/* begin for protocols internal use */
#define	PRU_FASTTIMO	18	/* 200ms timeout */
#define	PRU_SLOWTIMO	19	/* 500ms timeout */
#define	PRU_PROTORCV	20	/* receive from below */
#define	PRU_PROTOSEND	21	/* send to below */
.DE
A call on the user request routine is of the form,
.DS
._f
error = (*protosw[].pr_usrreq)(so, req, m, addr, rights);
int error; struct socket *so; int req; struct mbuf *m, *addr, *rights;
.DE
The mbuf data chain \fIm\fP is supplied for output operations
and for certain other operations where it is to receive a result.
The address \fIaddr\fP is supplied for address-oriented requests
such as PRU_BIND and PRU_CONNECT.
The \fIrights\fP parameter is an optional pointer to an mbuf
chain containing user-specified capabilities (see the \fIsendmsg\fP
and \fIrecvmsg\fP system calls).  The protocol is responsible for
disposal of the data mbuf chains on output operations.
A non-zero return value gives a
UNIX error number which should be passed to higher level software.
The following paragraphs describe each
of the requests possible.
.IP PRU_ATTACH
.br
When a protocol is bound to a socket (with the \fIsocket\fP
system call) the protocol module is called with this
request.  It is the responsibility of the protocol module to
allocate any resources necessary.
The ``attach'' request
will always precede any of the other requests, and should not
occur more than once.
.IP PRU_DETACH
.br
This is the antithesis of the attach request, and is used
at the time a socket is deleted.  The protocol module may
deallocate any resources assigned to the socket.
.IP PRU_BIND
.br
When a socket is initially created it has no address bound
to it.  This request indicates that an address should be bound to
an existing socket.  The protocol module must verify that the
requested address is valid and available for use.
.IP PRU_LISTEN
.br
The ``listen'' request indicates the user wishes to listen
for incoming connection requests on the associated socket.
The protocol module should perform any state changes needed
to carry out this request (if possible).  A ``listen'' request
always precedes a request to accept a connection.
.IP PRU_CONNECT
.br
The ``connect'' request indicates the user wants to establish
an association.  The \fIaddr\fP parameter supplied describes
the peer to be connected to.  The effect of a connect request
may vary depending on the protocol.  Virtual circuit protocols,
such as TCP [Postel81b], use this request to initiate establishment of a
TCP connection.  Datagram protocols, such as UDP [Postel80], simply
record the peer's address in a private data structure and use
it to tag all outgoing packets.  There are no restrictions
on how many times a connect request may be used after an attach.
If a protocol supports the notion of \fImulti-casting\fP, it
is possible to use multiple connects to establish a multi-cast
group.  Alternatively, an association may be broken by a
PRU_DISCONNECT request, and a new association created with a
subsequent connect request; all without destroying and creating
a new socket.
.IP PRU_ACCEPT
.br
Following a successful PRU_LISTEN request and the arrival
of one or more connections, this request is made to
indicate the user
has accepted the first connection on the queue of
pending connections.  The protocol module should fill
in the supplied address buffer with the address of the
connected party.
.IP PRU_DISCONNECT
.br
Eliminate an association created with a PRU_CONNECT request.
.IP PRU_SHUTDOWN
.br
This call is used to indicate no more data will be sent and/or
received (the \fIaddr\fP parameter indicates the direction of
the shutdown, as encoded in the \fIsoshutdown\fP system call).
The protocol may, at its discretion, deallocate any data
structures related to the shutdown and/or notify a connected peer
of the shutdown.
.IP PRU_RCVD
.br
This request is made only if the protocol entry in the protocol
switch table includes the PR_WANTRCVD flag.
When a user removes data from the receive queue this request
will be sent to the protocol module.  It may be used to trigger
acknowledgements, refresh windowing information, initiate
data transfer, etc.
.IP PRU_SEND
.br
Each user request to send data is translated into one or more
PRU_SEND requests (a protocol may indicate that a single user
send request must be translated into a single PRU_SEND request by
specifying the PR_ATOMIC flag in its protocol description).
The data to be sent is presented to the protocol as a list of
mbufs and an address is, optionally, supplied in the \fIaddr\fP
parameter.  The protocol is responsible for preserving the data
in the socket's send queue if it is not able to send it immediately,
or if it may need it at some later time (e.g. for retransmission).
.IP PRU_ABORT
.br
This request indicates an abnormal termination of service.  The
protocol should delete any existing association(s).
.IP PRU_CONTROL
.br
The ``control'' request is generated when a user performs a
UNIX \fIioctl\fP system call on a socket (and the ioctl is not
intercepted by the socket routines).  It allows protocol-specific
operations to be provided outside the scope of the common socket
interface.  The \fIaddr\fP parameter contains a pointer to a static
kernel data area where relevant information may be obtained or returned.
The \fIm\fP parameter contains the actual \fIioctl\fP request code
(note the non-standard calling convention).
The \fIrights\fP parameter contains a pointer to an \fIifnet\fP structure
if the \fIioctl\fP operation pertains to a particular network interface.
.IP PRU_SENSE
.br
The ``sense'' request is generated when the user makes an \fIfstat\fP
system call on a socket; it requests status of the associated socket. 
This currently returns a standard \fIstat\fP structure.
It typically contains only the
optimal transfer size for the connection (based on buffer size,
windowing information and maximum packet size).
The \fIm\fP parameter contains a pointer
to a static kernel data area where the status buffer should be placed.
.IP PRU_RCVOOB
.br
Any ``out-of-band'' data presently available is to be returned.  An
mbuf is passed to the protocol module, and the protocol
should either place
data in the mbuf or attach new mbufs to the one supplied if there is
insufficient space in the single mbuf.
An error may be returned if out-of-band data is not (yet) available
or has already been consumed.
The \fIaddr\fP parameter contains any options such as MSG_PEEK
to examine data without consuming it.
.IP PRU_SENDOOB
.br
Like PRU_SEND, but for out-of-band data.
.IP PRU_SOCKADDR
.br
The local address of the socket is returned, if any is currently
bound to it.  The address (with protocol specific format) is returned
in the \fIaddr\fP parameter.
.IP PRU_PEERADDR
.br
The address of the peer to which the socket is connected is returned.
The socket must be in a SS_ISCONNECTED state for this request to
be made to the protocol.  The address format (protocol specific) is
returned in the \fIaddr\fP parameter.
.IP PRU_CONNECT2
.br
The protocol module is supplied two sockets and requested to
establish a connection between the two without binding any
addresses, if possible.  This call is used in implementing
the
.IR socketpair (2)
system call.
.PP
The following requests are used internally by the protocol modules
and are never generated by the socket routines.  In certain instances,
they are handed to the \fIpr_usrreq\fP routine solely for convenience
in tracing a protocol's operation (e.g. PRU_SLOWTIMO).
.IP PRU_FASTTIMO
.br
A ``fast timeout'' has occurred.  This request is made when a timeout
occurs in the protocol's \fIpr_fastimo\fP routine.  The \fIaddr\fP
parameter indicates which timer expired.
.IP PRU_SLOWTIMO
.br
A ``slow timeout'' has occurred.  This request is made when a timeout
occurs in the protocol's \fIpr_slowtimo\fP routine.  The \fIaddr\fP
parameter indicates which timer expired.
.IP PRU_PROTORCV
.br
This request is used in the protocol-protocol interface, not by the
routines.  It requests reception of data destined for the protocol and
not the user.  No protocols currently use this facility.
.IP PRU_PROTOSEND
.br
This request allows a protocol to send data destined for another
protocol module, not a user.  The details of how data is marked
``addressed to protocol'' instead of ``addressed to user'' are
left to the protocol modules.  No protocols currently use this facility.
