.\" Copyright (c) 1986, 1993
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
.\"	@(#)2.t	8.1 (Berkeley) 8/14/93
.\"
.\".ds RH "Basics
.bp
.nr H1 2
.nr H2 0
.\" The next line is a major hack to get around internal changes in the groff
.\" implementation of .NH.
.nr nh*hl 1
.bp
.LG
.B
.ce
2. BASICS
.sp 2
.R
.NL
.PP
The basic building block for communication is the \fIsocket\fP.
A socket is an endpoint of communication to which a name may
be \fIbound\fP.  Each socket in use has a \fItype\fP
and one or more associated processes.  Sockets exist within
\fIcommunication domains\fP.  
A communication domain is an
abstraction introduced to bundle common properties of
processes communicating through sockets.
One such property is the scheme used to name sockets.  For
example, in the UNIX communication domain sockets are
named with UNIX path names; e.g. a
socket may be named \*(lq/dev/foo\*(rq.  Sockets normally
exchange data only with
sockets in the same domain (it may be possible to cross domain
boundaries, but only if some translation process is
performed).  The
4.4BSD IPC facilities support four separate communication domains:
the UNIX domain, for on-system communication;
the Internet domain, which is used by
processes which communicate
using the Internet standard communication protocols;
the NS domain, which is used by processes which
communicate using the Xerox standard communication
protocols*;
.FS
* See \fIInternet Transport Protocols\fP, Xerox System Integration
Standard (XSIS)028112 for more information.  This document is
almost a necessity for one trying to write NS applications.
.FE
and the ISO OSI protocols, which are not documented in this tutorial.
The underlying communication
facilities provided by these domains have a significant influence
on the internal system implementation as well as the interface to
socket facilities available to a user.  An example of the
latter is that a socket \*(lqoperating\*(rq in the UNIX domain
sees a subset of the error conditions which are possible
when operating in the Internet (or NS) domain.
.NH 2
Socket types
.PP
Sockets are
typed according to the communication properties visible to a
user. 
Processes are presumed to communicate only between sockets of
the same type, although there is
nothing that prevents communication between sockets of different
types should the underlying communication
protocols support this.
.PP
Four types of sockets currently are available to a user.
A \fIstream\fP socket provides for the bidirectional, reliable,
sequenced, and unduplicated flow of data without record boundaries.
Aside from the bidirectionality of data flow, a pair of connected
stream sockets provides an interface nearly identical to that of pipes\(dg.
.FS
\(dg In the UNIX domain, in fact, the semantics are identical and,
as one might expect, pipes have been implemented internally
as simply a pair of connected stream sockets.
.FE
.PP
A \fIdatagram\fP socket supports bidirectional flow of data which
is not promised to be sequenced, reliable, or unduplicated. 
That is, a process
receiving messages on a datagram socket may find messages duplicated, 
and, possibly,
in an order different from the order in which it was sent. 
An important characteristic of a datagram
socket is that record boundaries in data are preserved.  Datagram
sockets closely model the facilities found in many contemporary
packet switched networks such as the Ethernet.
.PP
A \fIraw\fP socket provides users access to
the underlying communication
protocols which support socket abstractions.
These sockets are normally datagram oriented, though their
exact characteristics are dependent on the interface provided by
the protocol.  Raw sockets are not intended for the general user; they
have been provided mainly for those interested in developing new 
communication protocols, or for gaining access to some of the more
esoteric facilities of an existing protocol.  The use of raw sockets
is considered in section 5.
.PP
A \fIsequenced packet\fP socket is similar to a stream socket,
with the exception that record boundaries are preserved.  This 
interface is provided only as part of the NS socket abstraction,
and is very important in most serious NS applications.
Sequenced-packet sockets allow the user to manipulate the
SPP or IDP headers on a packet or a group of packets either
by writing a prototype header along with whatever data is
to be sent, or by specifying a default header to be used with
all outgoing data, and allows the user to receive the headers
on incoming packets.  The use of these options is considered in
section 5.
.PP
Another potential socket type which has interesting properties is
the \fIreliably delivered
message\fP socket.
The reliably delivered message socket has
similar properties to a datagram socket, but with
reliable delivery.  There is currently no support for this
type of socket, but a reliably delivered message protocol
similar to Xerox's Packet Exchange Protocol (PEX) may be
simulated at the user level.  More information on this topic
can be found in section 5.
.NH 2
Socket creation
.PP
To create a socket the \fIsocket\fP system call is used:
.DS
s = socket(domain, type, protocol);
.DE
This call requests that the system create a socket in the specified
\fIdomain\fP and of the specified \fItype\fP.  A particular protocol may
also be requested.  If the protocol is left unspecified (a value
of 0), the system will select an appropriate protocol from those
protocols which comprise the communication domain and which
may be used to support the requested socket type.  The user is
returned a descriptor (a small integer number) which may be used
in later system calls which operate on sockets.  The domain is specified as
one of the manifest constants defined in the file <\fIsys/socket.h\fP>.
For the UNIX domain the constant is AF_UNIX*;  for the Internet
.FS
* The manifest constants are named AF_whatever as they indicate
the ``address format'' to use in interpreting names.
.FE
domain AF_INET; and for the NS domain, AF_NS.  
The socket types are also defined in this file
and one of SOCK_STREAM, SOCK_DGRAM, SOCK_RAW, or SOCK_SEQPACKET
must be specified.
To create a stream socket in the Internet domain the following
call might be used:
.DS
s = socket(AF_INET, SOCK_STREAM, 0);
.DE
This call would result in a stream socket being created with the TCP
protocol providing the underlying communication support.  To
create a datagram socket for on-machine use the call might
be:
.DS
s = socket(AF_UNIX, SOCK_DGRAM, 0);
.DE
.PP
The default protocol (used when the \fIprotocol\fP argument to the
\fIsocket\fP call is 0) should be correct for most every
situation.  However, it is possible to specify a protocol
other than the default; this will be covered in
section 5.
.PP
There are several reasons a socket call may fail.  Aside from
the rare occurrence of lack of memory (ENOBUFS), a socket
request may fail due to a request for an unknown protocol
(EPROTONOSUPPORT), or a request for a type of socket for
which there is no supporting protocol (EPROTOTYPE). 
.NH 2
Binding local names
.PP
A socket is created without a name.  Until a name is bound
to a socket, processes have no way to reference it and, consequently,
no messages may be received on it.
Communicating processes are bound
by an \fIassociation\fP.  In the Internet and NS domains,
an association 
is composed of local and foreign
addresses, and local and foreign ports,
while in the UNIX domain, an association is composed of
local and foreign path names (the phrase ``foreign pathname''
means a pathname created by a foreign process, not a pathname
on a foreign system).
In most domains, associations must be unique.
In the Internet domain there
may never be duplicate <protocol, local address, local port, foreign
address, foreign port> tuples.  UNIX domain sockets need not always
be bound to a name, but when bound
there may never be duplicate <protocol, local pathname, foreign
pathname> tuples.
The pathnames may not refer to files
already existing on the system
in 4.3; the situation may change in future releases.
.PP
The \fIbind\fP system call allows a process to specify half of
an association, <local address, local port>
(or <local pathname>), while the \fIconnect\fP
and \fIaccept\fP primitives are used to complete a socket's association.
.PP
In the Internet domain,
binding names to sockets can be fairly complex.
Fortunately, it is usually not necessary to specifically bind an
address and port number to a socket, because the
\fIconnect\fP and \fIsend\fP calls will automatically
bind an appropriate address if they are used with an
unbound socket.  The process of binding names to NS
sockets is similar in most ways to that of
binding names to Internet sockets.
.PP
The \fIbind\fP system call is used as follows:
.DS
bind(s, name, namelen);
.DE
The bound name is a variable length byte string which is interpreted
by the supporting protocol(s).  Its interpretation may vary from
communication domain to communication domain (this is one of
the properties which comprise the \*(lqdomain\*(rq).
As mentioned, in the
Internet domain names contain an Internet address and port
number.  NS domain names contain an NS address and
port number.  In the UNIX domain, names contain a path name and
a family, which is always AF_UNIX.  If one wanted to bind
the name \*(lq/tmp/foo\*(rq to a UNIX domain socket, the
following code would be used*:
.FS
* Note that, although the tendency here is to call the \*(lqaddr\*(rq
structure \*(lqsun\*(rq, doing so would cause problems if the code
were ever ported to a Sun workstation.
.FE
.DS
#include <sys/un.h>
 ...
struct sockaddr_un addr;
 ...
strcpy(addr.sun_path, "/tmp/foo");
addr.sun_family = AF_UNIX;
bind(s, (struct sockaddr *) &addr, strlen(addr.sun_path) +
    sizeof (addr.sun_len) + sizeof (addr.sun_family));
.DE
Note that in determining the size of a UNIX domain address null
bytes are not counted, which is why \fIstrlen\fP is used.  In
the current implementation of UNIX domain IPC,
the file name
referred to in \fIaddr.sun_path\fP is created as a socket
in the system file space.
The caller must, therefore, have
write permission in the directory where
\fIaddr.sun_path\fP is to reside, and this file should be deleted by the
caller when it is no longer needed.  Future versions of 4BSD
may not create this file.
.PP
In binding an Internet address things become more
complicated.  The actual call is similar,
.DS
#include <sys/types.h>
#include <netinet/in.h>
 ...
struct sockaddr_in sin;
 ...
bind(s, (struct sockaddr *) &sin, sizeof (sin));
.DE
but the selection of what to place in the address \fIsin\fP
requires some discussion.  We will come back to the problem
of formulating Internet addresses in section 3 when 
the library routines used in name resolution are discussed.
.PP
Binding an NS address to a socket is even more
difficult,
especially since the Internet library routines do not
work with NS hostnames.  The actual call is again similar:
.DS
#include <sys/types.h>
#include <netns/ns.h>
 ...
struct sockaddr_ns sns;
 ...
bind(s, (struct sockaddr *) &sns, sizeof (sns));
.DE
Again, discussion of what to place in a \*(lqstruct sockaddr_ns\*(rq
will be deferred to section 3.
.NH 2
Connection establishment
.PP
Connection establishment is usually asymmetric,
with one process a \*(lqclient\*(rq and the other a \*(lqserver\*(rq.
The server, when willing to offer its advertised services,
binds a socket to a well-known address associated with the service
and then passively \*(lqlistens\*(rq on its socket.
It is then possible for an unrelated process to rendezvous
with the server.
The client requests services from the server by initiating a
\*(lqconnection\*(rq to the server's socket.
On the client side the \fIconnect\fP call is
used to initiate a connection.  Using the UNIX domain, this
might appear as,
.DS
struct sockaddr_un server;
 ...
connect(s, (struct sockaddr *)&server, strlen(server.sun_path) +
    sizeof (server.sun_family));
.DE
while in the Internet domain,
.DS
struct sockaddr_in server;
 ...
connect(s, (struct sockaddr *)&server, sizeof (server));
.DE
and in the NS domain,
.DS
struct sockaddr_ns server;
 ...
connect(s, (struct sockaddr *)&server, sizeof (server));
.DE
where \fIserver\fP in the example above would contain either the UNIX
pathname, Internet address and port number, or NS address and
port number of the server to which the
client process wishes to speak.
If the client process's socket is unbound at the time of
the connect call,
the system will automatically select and bind a name to
the socket if necessary; c.f. section 5.4.
This is the usual way that local addresses are bound
to a socket.
.PP
An error is returned if the connection was unsuccessful
(any name automatically bound by the system, however, remains).
Otherwise, the socket is associated with the server and
data transfer may begin.  Some of the more common errors returned
when a connection attempt fails are:
.IP ETIMEDOUT
.br
After failing to establish a connection for a period of time,
the system decided there was no point in retrying the
connection attempt any more.  This usually occurs because
the destination host is down, or because problems in
the network resulted in transmissions being lost.
.IP ECONNREFUSED
.br
The host refused service for some reason.
This is usually
due to a server process
not being present at the requested name.
.IP "ENETDOWN or EHOSTDOWN"
.br
These operational errors are 
returned based on status information delivered to
the client host by the underlying communication services.
.IP "ENETUNREACH or EHOSTUNREACH"
.br
These operational errors can occur either because the network
or host is unknown (no route to the network or host is present),
or because of status information returned by intermediate
gateways or switching nodes.  Many times the status returned
is not sufficient to distinguish a network being down from a
host being down, in which case the system
indicates the entire network is unreachable.
.PP
For the server to receive a client's connection it must perform
two steps after binding its socket.
The first is to indicate a willingness to listen for
incoming connection requests:
.DS
listen(s, 5);
.DE
The second parameter to the \fIlisten\fP call specifies the maximum
number of outstanding connections which may be queued awaiting 
acceptance by the server process; this number
may be limited by the system.  Should a connection be
requested while the queue is full, the connection will not be
refused, but rather the individual messages which comprise the
request will be ignored.  This gives a harried server time to 
make room in its pending connection queue while the client
retries the connection request.  Had the connection been returned
with the ECONNREFUSED error, the client would be unable to tell
if the server was up or not.  As it is now it is still possible
to get the ETIMEDOUT error back, though this is unlikely.  The
backlog figure supplied with the listen call is currently limited
by the system to a maximum of 5 pending connections on any
one queue.  This avoids the problem of processes hogging system
resources by setting an infinite backlog, then ignoring
all connection requests.
.PP
With a socket marked as listening, a server may \fIaccept\fP
a connection:
.DS
struct sockaddr_in from;
 ...
fromlen = sizeof (from);
newsock = accept(s, (struct sockaddr *)&from, &fromlen);
.DE
(For the UNIX domain, \fIfrom\fP would be declared as a
\fIstruct sockaddr_un\fP, and for the NS domain, \fIfrom\fP
would be declared as a \fIstruct sockaddr_ns\fP,
but nothing different would need
to be done as far as \fIfromlen\fP is concerned.  In the examples
which follow, only Internet routines will be discussed.)  A new
descriptor is returned on receipt of a connection (along with
a new socket).  If the server wishes to find out who its client is,
it may supply a buffer for the client socket's name.  The value-result
parameter \fIfromlen\fP is initialized by the server to indicate how
much space is associated with \fIfrom\fP, then modified on return
to reflect the true size of the name.  If the client's name is not
of interest, the second parameter may be a null pointer.
.PP
\fIAccept\fP normally blocks.  That is, \fIaccept\fP
will not return until a connection is available or the system call
is interrupted by a signal to the process.  Further, there is no
way for a process to indicate it will accept connections from only
a specific individual, or individuals.  It is up to the user process
to consider who the connection is from and close down the connection
if it does not wish to speak to the process.  If the server process
wants to accept connections on more than one socket, or wants to avoid blocking
on the accept call, there are alternatives; they will be considered
in section 5.
.NH 2
Data transfer
.PP
With a connection established, data may begin to flow.  To send
and receive data there are a number of possible calls.
With the peer entity at each end of a connection
anchored, a user can send or receive a message without specifying
the peer.  As one might expect, in this case, then
the normal \fIread\fP and \fIwrite\fP system calls are usable,
.DS
write(s, buf, sizeof (buf));
read(s, buf, sizeof (buf));
.DE
In addition to \fIread\fP and \fIwrite\fP,
the new calls \fIsend\fP and \fIrecv\fP
may be used:
.DS
send(s, buf, sizeof (buf), flags);
recv(s, buf, sizeof (buf), flags);
.DE
While \fIsend\fP and \fIrecv\fP are virtually identical to
\fIread\fP and \fIwrite\fP,
the extra \fIflags\fP argument is important.  The flags,
defined in \fI<sys/socket.h>\fP, may be
specified as a non-zero value if one or more
of the following is required:
.DS
.TS
l l.
MSG_OOB	send/receive out of band data
MSG_PEEK	look at data without reading
MSG_DONTROUTE	send data without routing packets
.TE
.DE
Out of band data is a notion specific to stream sockets, and one
which we will not immediately consider.  The option to have data
sent without routing applied to the outgoing packets is currently 
used only by the routing table management process, and is
unlikely to be of interest to the casual user.  The ability
to preview data is, however, of interest.  When MSG_PEEK
is specified with a \fIrecv\fP call, any data present is returned
to the user, but treated as still \*(lqunread\*(rq.  That
is, the next \fIread\fP or \fIrecv\fP call applied to the socket will
return the data previously previewed.
.NH 2
Discarding sockets
.PP
Once a socket is no longer of interest, it may be discarded
by applying a \fIclose\fP to the descriptor,
.DS
close(s);
.DE
If data is associated with a socket which promises reliable delivery
(e.g. a stream socket) when a close takes place, the system will
continue to attempt to transfer the data. 
However, after a fairly long period of
time, if the data is still undelivered, it will be discarded.
Should a user have no use for any pending data, it may 
perform a \fIshutdown\fP on the socket prior to closing it.
This call is of the form:
.DS
shutdown(s, how);
.DE
where \fIhow\fP is 0 if the user is no longer interested in reading
data, 1 if no more data will be sent, or 2 if no data is to
be sent or received.
.NH 2
Connectionless sockets
.PP
To this point we have been concerned mostly with sockets which
follow a connection oriented model.  However, there is also
support for connectionless interactions typical of the datagram
facilities found in contemporary packet switched networks.
A datagram socket provides a symmetric interface to data
exchange.  While processes are still likely to be client
and server, there is no requirement for connection establishment.
Instead, each message includes the destination address.
.PP
Datagram sockets are created as before.
If a particular local address is needed,
the \fIbind\fP operation must precede the first data transmission.
Otherwise, the system will set the local address and/or port
when data is first sent.
To send data, the \fIsendto\fP primitive is used,
.DS
sendto(s, buf, buflen, flags, (struct sockaddr *)&to, tolen);
.DE
The \fIs\fP, \fIbuf\fP, \fIbuflen\fP, and \fIflags\fP
parameters are used as before. 
The \fIto\fP and \fItolen\fP
values are used to indicate the address of the intended recipient of the
message.  When
using an unreliable datagram interface, it is
unlikely that any errors will be reported to the sender.  When
information is present locally to recognize a message that can
not be delivered (for instance when a network is unreachable),
the call will return \-1 and the global value \fIerrno\fP will
contain an error number. 
.PP
To receive messages on an unconnected datagram socket, the
\fIrecvfrom\fP primitive is provided:
.DS
recvfrom(s, buf, buflen, flags, (struct sockaddr *)&from, &fromlen);
.DE
Once again, the \fIfromlen\fP parameter is handled in
a value-result fashion, initially containing the size of
the \fIfrom\fP buffer, and modified on return to indicate
the actual size of the address from which the datagram was received.
.PP
In addition to the two calls mentioned above, datagram
sockets may also use the \fIconnect\fP call to associate
a socket with a specific destination address.  In this case, any
data sent on the socket will automatically be addressed
to the connected peer, and only data received from that
peer will be delivered to the user.  Only one connected
address is permitted for each socket at one time;
a second connect will change the destination address,
and a connect to a null address (family AF_UNSPEC)
will disconnect.
Connect requests on datagram sockets return immediately,
as this simply results in the system recording
the peer's address (as compared to a stream socket, where a
connect request initiates establishment of an end to end
connection).  \fIAccept\fP and \fIlisten\fP are not
used with datagram sockets.
.PP
While a datagram socket socket is connected,
errors from recent \fIsend\fP calls may be returned
asynchronously.
These errors may be reported on subsequent operations
on the socket,
or a special socket option used with \fIgetsockopt\fP, SO_ERROR,
may be used to interrogate the error status.
A \fIselect\fP for reading or writing will return true
when an error indication has been received.
The next operation will return the error, and the error status is cleared.
Other of the less
important details of datagram sockets are described
in section 5.
.NH 2
Input/Output multiplexing
.PP
One last facility often used in developing applications
is the ability to multiplex i/o requests among multiple
sockets and/or files.  This is done using the \fIselect\fP
call:
.DS
#include <sys/time.h>
#include <sys/types.h>
 ...

fd_set readmask, writemask, exceptmask;
struct timeval timeout;
 ...
select(nfds, &readmask, &writemask, &exceptmask, &timeout);
.DE
\fISelect\fP takes as arguments pointers to three sets, one for
the set of file descriptors for which the caller wishes to
be able to read data on, one for those descriptors to which
data is to be written, and one for which exceptional conditions
are pending; out-of-band data is the only
exceptional condition currently implemented by the socket
If the user is not interested
in certain conditions (i.e., read, write, or exceptions),
the corresponding argument to the \fIselect\fP should
be a null pointer.
.PP
Each set is actually a structure containing an array of
long integer bit masks; the size of the array is set
by the definition FD_SETSIZE.
The array is be
long enough to hold one bit for each of FD_SETSIZE file descriptors.
.PP
The macros FD_SET(\fIfd, &mask\fP) and
FD_CLR(\fIfd, &mask\fP)
have been provided for adding and removing file descriptor
\fIfd\fP in the set \fImask\fP.  The
set should be zeroed before use, and
the macro FD_ZERO(\fI&mask\fP) has been provided
to clear the set \fImask\fP.
The parameter \fInfds\fP in the \fIselect\fP call specifies the range
of file descriptors  (i.e. one plus the value of the largest
descriptor) to be examined in a set. 
.PP
A timeout value may be specified if the selection
is not to last more than a predetermined period of time.  If
the fields in \fItimeout\fP are set to 0, the selection takes
the form of a
\fIpoll\fP, returning immediately.  If the last parameter is
a null pointer, the selection will block indefinitely*.
.FS
* To be more specific, a return takes place only when a
descriptor is selectable, or when a signal is received by
the caller, interrupting the system call.
.FE
\fISelect\fP normally returns the number of file descriptors selected;
if the \fIselect\fP call returns due to the timeout expiring, then
the value 0 is returned.
If the \fIselect\fP terminates because of an error or interruption,
a \-1 is returned with the error number in \fIerrno\fP,
and with the file descriptor masks unchanged.
.PP
Assuming a successful return, the three sets will
indicate which
file descriptors are ready to be read from, written to, or
have exceptional conditions pending.
The status of a file descriptor in a select mask may be
tested with the \fIFD_ISSET(fd, &mask)\fP macro, which
returns a non-zero value if \fIfd\fP is a member of the set
\fImask\fP, and 0 if it is not.
.PP
To determine if there are connections waiting 
on a socket to be used with an \fIaccept\fP call,
\fIselect\fP can be used, followed by
a \fIFD_ISSET(fd, &mask)\fP macro to check for read
readiness on the appropriate socket.  If \fIFD_ISSET\fP
returns a non-zero value, indicating permission to read, then a
connection is pending on the socket.
.PP
As an example, to read data from two sockets, \fIs1\fP and
\fIs2\fP as it is available from each and with a one-second
timeout, the following code
might be used:
.DS
#include <sys/time.h>
#include <sys/types.h>
 ...
fd_set read_template;
struct timeval wait;
 ...
for (;;) {
	wait.tv_sec = 1;		/* one second */
	wait.tv_usec = 0;

	FD_ZERO(&read_template);

	FD_SET(s1, &read_template);
	FD_SET(s2, &read_template);

	nb = select(FD_SETSIZE, &read_template, (fd_set *) 0, (fd_set *) 0, &wait);
	if (nb <= 0) {
		\fIAn error occurred during the \fPselect\fI, or
		the \fPselect\fI timed out.\fP
	}

	if (FD_ISSET(s1, &read_template)) {
		\fISocket #1 is ready to be read from.\fP
	}

	if (FD_ISSET(s2, &read_template)) {
		\fISocket #2 is ready to be read from.\fP
	}
}
.DE
.PP
In 4.2, the arguments to \fIselect\fP were pointers to integers
instead of pointers to \fIfd_set\fPs.  This type of call
will still work as long as the number of file descriptors
being examined is less than the number of bits in an
integer; however, the methods illustrated above should
be used in all current programs.
.PP
\fISelect\fP provides a synchronous multiplexing scheme.
Asynchronous notification of output completion, input availability,
and exceptional conditions is possible through use of the
SIGIO and SIGURG signals described in section 5.
