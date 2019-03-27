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
.\"	@(#)5.t	8.1 (Berkeley) 8/14/93
.\" $FreeBSD$
.\"
.\".ds RH "Advanced Topics
.bp
.nr H1 5
.nr H2 0
.LG
.B
.ce
5. ADVANCED TOPICS
.sp 2
.R
.NL
.PP
A number of facilities have yet to be discussed.  For most users
of the IPC the mechanisms already
described will suffice in constructing distributed
applications.  However, others will find the need to utilize some
of the features which we consider in this section.
.NH 2
Out of band data
.PP
The stream socket abstraction includes the notion of \*(lqout
of band\*(rq data.  Out of band data is a logically independent 
transmission channel associated with each pair of connected
stream sockets.  Out of band data is delivered to the user
independently of normal data.
The abstraction defines that the out of band data facilities
must support the reliable delivery of at least one
out of band message at a time.  This message may contain at least one
byte of data, and at least one message may be pending delivery
to the user at any one time.  For communications protocols which
support only in-band signaling (i.e. the urgent data is
delivered in sequence with the normal data), the system normally extracts
the data from the normal data stream and stores it separately.
This allows users to choose between receiving the urgent data
in order and receiving it out of sequence without having to
buffer all the intervening data.  It is possible
to ``peek'' (via MSG_PEEK) at out of band data.
If the socket has a process group, a SIGURG signal is generated
when the protocol is notified of its existence.
A process can set the process group
or process id to be informed by the SIGURG signal via the
appropriate \fIfcntl\fP call, as described below for
SIGIO.
If multiple sockets may have out of band data awaiting
delivery, a \fIselect\fP call for exceptional conditions
may be used to determine those sockets with such data pending.
Neither the signal nor the select indicate the actual arrival
of the out-of-band data, but only notification that it is pending.
.PP
In addition to the information passed, a logical mark is placed in
the data stream to indicate the point at which the out
of band data was sent.  The remote login and remote shell
applications use this facility to propagate signals between
client and server processes.  When a signal
flushs any pending output from the remote process(es), all
data up to the mark in the data stream is discarded.
.PP
To send an out of band message the MSG_OOB flag is supplied to
a \fIsend\fP or \fIsendto\fP calls,
while to receive out of band data MSG_OOB should be indicated
when performing a \fIrecvfrom\fP or \fIrecv\fP call.
To find out if the read pointer is currently pointing at
the mark in the data stream, the SIOCATMARK ioctl is provided:
.DS
ioctl(s, SIOCATMARK, &yes);
.DE
If \fIyes\fP is a 1 on return, the next read will return data
after the mark.  Otherwise (assuming out of band data has arrived), 
the next read will provide data sent by the client prior
to transmission of the out of band signal.  The routine used
in the remote login process to flush output on receipt of an
interrupt or quit signal is shown in Figure 5.
It reads the normal data up to the mark (to discard it),
then reads the out-of-band byte.
.KF
.DS
#include <sys/ioctl.h>
#include <sys/file.h>
 ...
oob()
{
	int out = FWRITE, mark;
	char waste[BUFSIZ];

	/* flush local terminal output */
	ioctl(1, TIOCFLUSH, (char *)&out);
	for (;;) {
		if (ioctl(rem, SIOCATMARK, &mark) < 0) {
			perror("ioctl");
			break;
		}
		if (mark)
			break;
		(void) read(rem, waste, sizeof (waste));
	}
	if (recv(rem, &mark, 1, MSG_OOB) < 0) {
		perror("recv");
		...
	}
	...
}
.DE
.ce
Figure 5.  Flushing terminal I/O on receipt of out of band data.
.sp
.KE
.PP
A process may also read or peek at the out-of-band data
without first reading up to the mark.
This is more difficult when the underlying protocol delivers
the urgent data in-band with the normal data, and only sends
notification of its presence ahead of time (e.g., the TCP protocol
used to implement streams in the Internet domain).
With such protocols, the out-of-band byte may not yet have arrived
when a \fIrecv\fP is done with the MSG_OOB flag.
In that case, the call will return an error of EWOULDBLOCK.
Worse, there may be enough in-band data in the input buffer
that normal flow control prevents the peer from sending the urgent data
until the buffer is cleared.
The process must then read enough of the queued data
that the urgent data may be delivered.
.PP
Certain programs that use multiple bytes of urgent data and must
handle multiple urgent signals (e.g., \fItelnet\fP\|(1C))
need to retain the position of urgent data within the stream.
This treatment is available as a socket-level option, SO_OOBINLINE;
see \fIsetsockopt\fP\|(2) for usage.
With this option, the position of urgent data (the \*(lqmark\*(rq)
is retained, but the urgent data immediately follows the mark
within the normal data stream returned without the MSG_OOB flag.
Reception of multiple urgent indications causes the mark to move,
but no out-of-band data are lost.
.NH 2
Non-Blocking Sockets
.PP
It is occasionally convenient to make use of sockets
which do not block; that is, I/O requests which
cannot complete immediately and
would therefore cause the process to be suspended awaiting completion are
not executed, and an error code is returned.
Once a socket has been created via
the \fIsocket\fP call, it may be marked as non-blocking
by \fIfcntl\fP as follows:
.DS
#include <fcntl.h>
 ...
int	s;
 ...
s = socket(AF_INET, SOCK_STREAM, 0);
 ...
if (fcntl(s, F_SETFL, FNDELAY) < 0)
	perror("fcntl F_SETFL, FNDELAY");
	exit(1);
}
 ...
.DE
.PP
When performing non-blocking I/O on sockets, one must be
careful to check for the error EWOULDBLOCK (stored in the
global variable \fIerrno\fP), which occurs when
an operation would normally block, but the socket it
was performed on is marked as non-blocking.
In particular, \fIaccept\fP, \fIconnect\fP, \fIsend\fP, \fIrecv\fP,
\fIread\fP, and \fIwrite\fP can
all return EWOULDBLOCK, and processes should be prepared
to deal with such return codes.
If an operation such as a \fIsend\fP cannot be done in its entirety,
but partial writes are sensible (for example, when using a stream socket),
the data that can be sent immediately will be processed,
and the return value will indicate the amount actually sent.
.NH 2
Interrupt driven socket I/O
.PP
The SIGIO signal allows a process to be notified
via a signal when a socket (or more generally, a file
descriptor) has data waiting to be read.  Use of
the SIGIO facility requires three steps:  First,
the process must set up a SIGIO signal handler
by use of the \fIsignal\fP or \fIsigvec\fP calls.  Second,
it must set the process id or process group id which is to receive
notification of pending input to its own process id,
or the process group id of its process group (note that
the default process group of a socket is group zero).
This is accomplished by use of an \fIfcntl\fP call.
Third, it must enable asynchronous notification of pending I/O requests
with another \fIfcntl\fP call.  Sample code to
allow a given process to receive information on
pending I/O requests as they occur for a socket \fIs\fP
is given in Figure 6.  With the addition of a handler for SIGURG,
this code can also be used to prepare for receipt of SIGURG signals.
.KF
.DS
#include <fcntl.h>
 ...
int	io_handler();
 ...
signal(SIGIO, io_handler);

/* Set the process receiving SIGIO/SIGURG signals to us */

if (fcntl(s, F_SETOWN, getpid()) < 0) {
	perror("fcntl F_SETOWN");
	exit(1);
}

/* Allow receipt of asynchronous I/O signals */

if (fcntl(s, F_SETFL, FASYNC) < 0) {
	perror("fcntl F_SETFL, FASYNC");
	exit(1);
}
.DE
.ce
Figure 6.  Use of asynchronous notification of I/O requests.
.sp
.KE
.NH 2
Signals and process groups
.PP
Due to the existence of the SIGURG and SIGIO signals each socket has an
associated process number, just as is done for terminals.
This value is initialized to zero,
but may be redefined at a later time with the F_SETOWN
\fIfcntl\fP, such as was done in the code above for SIGIO.
To set the socket's process id for signals, positive arguments
should be given to the \fIfcntl\fP call.  To set the socket's
process group for signals, negative arguments should be 
passed to \fIfcntl\fP.  Note that the process number indicates
either the associated process id or the associated process
group; it is impossible to specify both at the same time.
A similar \fIfcntl\fP, F_GETOWN, is available for determining the
current process number of a socket.
.PP
Another signal which is useful when constructing server processes
is SIGCHLD.  This signal is delivered to a process when any
child processes have changed state.  Normally servers use
the signal to \*(lqreap\*(rq child processes that have exited
without explicitly awaiting their termination
or periodic polling for exit status.
For example, the remote login server loop shown in Figure 2
may be augmented as shown in Figure 7.
.KF
.DS
int reaper();
 ...
signal(SIGCHLD, reaper);
listen(f, 5);
for (;;) {
	int g, len = sizeof (from);

	g = accept(f, (struct sockaddr *)&from, &len,);
	if (g < 0) {
		if (errno != EINTR)
			syslog(LOG_ERR, "rlogind: accept: %m");
		continue;
	}
	...
}
 ...
#include <wait.h>
reaper()
{
	union wait status;

	while (wait3(&status, WNOHANG, 0) > 0)
		;
}
.DE
.sp
.ce
Figure 7.  Use of the SIGCHLD signal.
.sp
.KE
.PP
If the parent server process fails to reap its children,
a large number of \*(lqzombie\*(rq processes may be created.
.NH 2
Pseudo terminals
.PP
Many programs will not function properly without a terminal
for standard input and output.  Since sockets do not provide
the semantics of terminals,
it is often necessary to have a process communicating over
the network do so through a \fIpseudo-terminal\fP.  A pseudo-
terminal is actually a pair of devices, master and slave,
which allow a process to serve as an active agent in communication
between processes and users.  Data written on the slave side
of a pseudo-terminal is supplied as input to a process reading
from the master side, while data written on the master side are
processed as terminal input for the slave.
In this way, the process manipulating
the master side of the pseudo-terminal has control over the
information read and written on the slave side
as if it were manipulating the keyboard and reading the screen
on a real terminal.
The purpose of this abstraction is to
preserve terminal semantics over a network connection\(em
that is, the slave side appears as a normal terminal to
any process reading from or writing to it.
.PP
For example, the remote
login server uses pseudo-terminals for remote login sessions.
A user logging in to a machine across the network is provided
a shell with a slave pseudo-terminal as standard input, output,
and error.  The server process then handles the communication
between the programs invoked by the remote shell and the user's
local client process.
When a user sends a character that generates an interrupt
on the remote machine that flushes terminal output,
the pseudo-terminal generates a control message for the server process.
The server then sends an out of band message
to the client process to signal a flush of data at the real terminal
and on the intervening data buffered in the network.
.PP
Under 4.4BSD, the name of the slave side of a pseudo-terminal is of the form
\fI/dev/ttyxy\fP, where \fIx\fP is a single letter
starting at `p' and continuing to `t'.
\fIy\fP is a hexadecimal digit (i.e., a single
character in the range 0 through 9 or `a' through `f').
The master side of a pseudo-terminal is \fI/dev/ptyxy\fP,
where \fIx\fP and \fIy\fP correspond to the
slave side of the pseudo-terminal.
.PP
In general, the method of obtaining a pair of master and
slave pseudo-terminals is to
find a pseudo-terminal which
is not currently in use.
The master half of a pseudo-terminal is a single-open device;
thus, each master may be opened in turn until an open succeeds.
The slave side of the pseudo-terminal is then opened,
and is set to the proper terminal modes if necessary.
The process then \fIfork\fPs; the child closes
the master side of the pseudo-terminal, and \fIexec\fPs the
appropriate program.  Meanwhile, the parent closes the
slave side of the pseudo-terminal and begins reading and
writing from the master side.  Sample code making use of
pseudo-terminals is given in Figure 8; this code assumes
that a connection on a socket \fIs\fP exists, connected
to a peer who wants a service of some kind, and that the
process has disassociated itself from any previous controlling terminal.
.KF
.DS
gotpty = 0;
for (c = 'p'; !gotpty && c <= 's'; c++) {
	line = "/dev/ptyXX";
	line[sizeof("/dev/pty")-1] = c;
	line[sizeof("/dev/ptyp")-1] = '0';
	if (stat(line, &statbuf) < 0)
		break;
	for (i = 0; i < 16; i++) {
		line[sizeof("/dev/ptyp")-1] = "0123456789abcdef"[i];
		master = open(line, O_RDWR);
		if (master > 0) {
			gotpty = 1;
			break;
		}
	}
}
if (!gotpty) {
	syslog(LOG_ERR, "All network ports in use");
	exit(1);
}

line[sizeof("/dev/")-1] = 't';
slave = open(line, O_RDWR);	/* \fIslave\fP is now slave side */
if (slave < 0) {
	syslog(LOG_ERR, "Cannot open slave pty %s", line);
	exit(1);
}

ioctl(slave, TIOCGETP, &b);	/* Set slave tty modes */
b.sg_flags = CRMOD|XTABS|ANYP;
ioctl(slave, TIOCSETP, &b);

i = fork();
if (i < 0) {
	syslog(LOG_ERR, "fork: %m");
	exit(1);
} else if (i) {		/* Parent */
	close(slave);
	...
} else {		 /* Child */
	(void) close(s);
	(void) close(master);
	dup2(slave, 0);
	dup2(slave, 1);
	dup2(slave, 2);
	if (slave > 2)
		(void) close(slave);
	...
}
.DE
.ce
Figure 8.  Creation and use of a pseudo terminal
.sp
.KE
.NH 2
Selecting specific protocols
.PP
If the third argument to the \fIsocket\fP call is 0,
\fIsocket\fP will select a default protocol to use with
the returned socket of the type requested.
The default protocol is usually correct, and alternate choices are not
usually available.
However, when using ``raw'' sockets to communicate directly with
lower-level protocols or hardware interfaces,
the protocol argument may be important for setting up demultiplexing.
For example, raw sockets in the Internet family may be used to implement
a new protocol above IP, and the socket will receive packets
only for the protocol specified.
To obtain a particular protocol one determines the protocol number
as defined within the communication domain.  For the Internet
domain one may use one of the library routines
discussed in section 3, such as \fIgetprotobyname\fP:
.DS
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
 ...
pp = getprotobyname("newtcp");
s = socket(AF_INET, SOCK_STREAM, pp->p_proto);
.DE
This would result in a socket \fIs\fP using a stream
based connection, but with protocol type of ``newtcp''
instead of the default ``tcp.''
.PP
In the NS domain, the available socket protocols are defined in
<\fInetns/ns.h\fP>.  To create a raw socket for Xerox Error Protocol
messages, one might use:
.DS
#include <sys/types.h>
#include <sys/socket.h>
#include <netns/ns.h>
 ...
s = socket(AF_NS, SOCK_RAW, NSPROTO_ERROR);
.DE
.NH 2
Address binding
.PP
As was mentioned in section 2, 
binding addresses to sockets in the Internet and NS domains can be
fairly complex.  As a brief reminder, these associations
are composed of local and foreign
addresses, and local and foreign ports.  Port numbers are
allocated out of separate spaces, one for each system and one
for each domain on that system.
Through the \fIbind\fP system call, a
process may specify half of an association, the
<local address, local port> part, while the
\fIconnect\fP
and \fIaccept\fP
primitives are used to complete a socket's association by
specifying the <foreign address, foreign port> part.
Since the association is created in two steps the association
uniqueness requirement indicated previously could be violated unless
care is taken.  Further, it is unrealistic to expect user
programs to always know proper values to use for the local address
and local port since a host may reside on multiple networks and
the set of allocated port numbers is not directly accessible
to a user.
.PP
To simplify local address binding in the Internet domain the notion of a
\*(lqwildcard\*(rq address has been provided.  When an address
is specified as INADDR_ANY (a manifest constant defined in
<netinet/in.h>), the system interprets the address as 
\*(lqany valid address\*(rq.  For example, to bind a specific
port number to a socket, but leave the local address unspecified,
the following code might be used:
.DS
#include <sys/types.h>
#include <netinet/in.h>
 ...
struct sockaddr_in sin;
 ...
s = socket(AF_INET, SOCK_STREAM, 0);
sin.sin_family = AF_INET;
sin.sin_addr.s_addr = htonl(INADDR_ANY);
sin.sin_port = htons(MYPORT);
bind(s, (struct sockaddr *) &sin, sizeof (sin));
.DE
Sockets with wildcarded local addresses may receive messages
directed to the specified port number, and sent to any
of the possible addresses assigned to a host.  For example,
if a host has addresses 128.32.0.4 and 10.0.0.78, and a socket is bound as
above, the process will be
able to accept connection requests which are addressed to
128.32.0.4 or 10.0.0.78.
If a server process wished to only allow hosts on a
given network connect to it, it would bind
the address of the host on the appropriate network.
.PP
In a similar fashion, a local port may be left unspecified
(specified as zero), in which case the system will select an
appropriate port number for it.  This shortcut will work
both in the Internet and NS domains.  For example, to
bind a specific local address to a socket, but to leave the
local port number unspecified:
.DS
hp = gethostbyname(hostname);
if (hp == NULL) {
	...
}
bcopy(hp->h_addr, (char *) sin.sin_addr, hp->h_length);
sin.sin_port = htons(0);
bind(s, (struct sockaddr *) &sin, sizeof (sin));
.DE
The system selects the local port number based on two criteria.
The first is that on 4BSD systems,
Internet ports below IPPORT_RESERVED (1024) (for the Xerox domain,
0 through 3000) are reserved
for privileged users (i.e., the super user);
Internet ports above IPPORT_USERRESERVED (50000) are reserved
for non-privileged servers.  The second is
that the port number is not currently bound to some other
socket.  In order to find a free Internet port number in the privileged
range the \fIrresvport\fP library routine may be used as follows
to return a stream socket in with a privileged port number:
.DS
int lport = IPPORT_RESERVED \- 1;
int s;
\&...
s = rresvport(&lport);
if (s < 0) {
	if (errno == EAGAIN)
		fprintf(stderr, "socket: all ports in use\en");
	else
		perror("rresvport: socket");
	...
}
.DE
The restriction on allocating ports was done to allow processes
executing in a \*(lqsecure\*(rq environment to perform authentication
based on the originating address and port number.  For example,
the \fIrlogin\fP(1) command allows users to log in across a network
without being asked for a password, if two conditions hold:
First, the name of the system the user
is logging in from is in the file
\fI/etc/hosts.equiv\fP on the system he is logging
in to (or the system name and the user name are in
the user's \fI.rhosts\fP file in the user's home
directory), and second, that the user's rlogin
process is coming from a privileged port on the machine from which he is
logging.  The port number and network address of the
machine from which the user is logging in can be determined either
by the \fIfrom\fP result of the \fIaccept\fP call, or
from the \fIgetpeername\fP call.
.PP
In certain cases the algorithm used by the system in selecting
port numbers is unsuitable for an application.  This is because
associations are created in a two step process.  For example,
the Internet file transfer protocol, FTP, specifies that data
connections must always originate from the same local port.  However,
duplicate associations are avoided by connecting to different foreign
ports.  In this situation the system would disallow binding the
same local address and port number to a socket if a previous data
connection's socket still existed.  To override the default port
selection algorithm, an option call must be performed prior
to address binding:
.DS
 ...
int	on = 1;
 ...
setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
bind(s, (struct sockaddr *) &sin, sizeof (sin));
.DE
With the above call, local addresses may be bound which
are already in use.  This does not violate the uniqueness
requirement as the system still checks at connect time to
be sure any other sockets with the same local address and
port do not have the same foreign address and port.
If the association already exists, the error EADDRINUSE is returned.
A related socket option, SO_REUSEPORT, which allows completely 
duplicate bindings, is described in the IP multicasting section.
.NH 2
Socket Options
.PP
It is possible to set and get a number of options on sockets
via the \fIsetsockopt\fP and \fIgetsockopt\fP system calls.
These options include such things as marking a socket for
broadcasting, not to route, to linger on close, etc.
In addition, there are protocol-specific options for IP and TCP,
as described in
.IR ip (4),
.IR tcp (4),
and in the section on multicasting below.
.PP
The general forms of the calls are:
.DS
setsockopt(s, level, optname, optval, optlen);
.DE
and
.DS
getsockopt(s, level, optname, optval, optlen);
.DE
.PP
The parameters to the calls are as follows: \fIs\fP
is the socket on which the option is to be applied.
\fILevel\fP specifies the protocol layer on which the
option is to be applied; in most cases this is
the ``socket level'', indicated by the symbolic constant
SOL_SOCKET, defined in \fI<sys/socket.h>.\fP
The actual option is specified in \fIoptname\fP, and is
a symbolic constant also defined in \fI<sys/socket.h>\fP.
\fIOptval\fP and \fIOptlen\fP point to the value of the
option (in most cases, whether the option is to be turned
on or off), and the length of the value of the option,
respectively.
For \fIgetsockopt\fP, \fIoptlen\fP is
a value-result parameter, initially set to the size of
the storage area pointed to by \fIoptval\fP, and modified
upon return to indicate the actual amount of storage used.
.PP
An example should help clarify things.  It is sometimes
useful to determine the type (e.g., stream, datagram, etc.)
of an existing socket; programs
under \fIinetd\fP (described below) may need to perform this
task.  This can be accomplished as follows via the
SO_TYPE socket option and the \fIgetsockopt\fP call:
.DS
#include <sys/types.h>
#include <sys/socket.h>

int type, size;

size = sizeof (int);

if (getsockopt(s, SOL_SOCKET, SO_TYPE, (char *) &type, &size) < 0) {
	...
}
.DE
After the \fIgetsockopt\fP call, \fItype\fP will be set
to the value of the socket type, as defined in
\fI<sys/socket.h>\fP.  If, for example, the socket were
a datagram socket, \fItype\fP would have the value
corresponding to SOCK_DGRAM.
.NH 2
Broadcasting and determining network configuration
.PP
By using a datagram socket, it is possible to send broadcast
packets on many networks supported by the system.
The network itself must support broadcast; the system
provides no simulation of broadcast in software.
Broadcast messages can place a high load on a network since they force
every host on the network to service them.  Consequently,
the ability to send broadcast packets has been limited
to sockets which are explicitly marked as allowing broadcasting.
Broadcast is typically used for one of two reasons:
it is desired to find a resource on a local network without prior
knowledge of its address,
or important functions such as routing require that information
be sent to all accessible neighbors.
.PP
Multicasting is an alternative to broadcasting.
Setting up IP multicast sockets is described in the next section.
.PP
To send a broadcast message, a datagram socket 
should be created:
.DS
s = socket(AF_INET, SOCK_DGRAM, 0);
.DE
or
.DS
s = socket(AF_NS, SOCK_DGRAM, 0);
.DE
The socket is marked as allowing broadcasting,
.DS
int	on = 1;

setsockopt(s, SOL_SOCKET, SO_BROADCAST, &on, sizeof (on));
.DE
and at least a port number should be bound to the socket:
.DS
sin.sin_family = AF_INET;
sin.sin_addr.s_addr = htonl(INADDR_ANY);
sin.sin_port = htons(MYPORT);
bind(s, (struct sockaddr *) &sin, sizeof (sin));
.DE
or, for the NS domain,
.DS
sns.sns_family = AF_NS;
netnum = htonl(net);
sns.sns_addr.x_net = *(union ns_net *) &netnum; /* insert net number */
sns.sns_addr.x_port = htons(MYPORT);
bind(s, (struct sockaddr *) &sns, sizeof (sns));
.DE
The destination address of the message to be broadcast
depends on the network(s) on which the message is to be broadcast.
The Internet domain supports a shorthand notation for broadcast
on the local network, the address INADDR_BROADCAST (defined in
<\fInetinet/in.h\fP>.
To determine the list of addresses for all reachable neighbors
requires knowledge of the networks to which the host is connected.
Since this information should
be obtained in a host-independent fashion and may be impossible
to derive, 4.4BSD provides a method of
retrieving this information from the system data structures.
The SIOCGIFCONF \fIioctl\fP call returns the interface
configuration of a host in the form of a
single \fIifconf\fP structure; this structure contains
a ``data area'' which is made up of an array of
of \fIifreq\fP structures, one for each network interface
to which the host is connected.
These structures are defined in
\fI<net/if.h>\fP as follows:
.DS
.if t .ta .5i 1.0i 1.5i 3.5i
.if n .ta .7i 1.4i 2.1i 3.4i
struct ifconf {
	int	ifc_len;		/* size of associated buffer */
	union {
		caddr_t	ifcu_buf;
		struct	ifreq *ifcu_req;
	} ifc_ifcu;
};

#define	ifc_buf	ifc_ifcu.ifcu_buf		/* buffer address */
#define	ifc_req	ifc_ifcu.ifcu_req		/* array of structures returned */

#define	IFNAMSIZ	16

struct ifreq {
	char	ifr_name[IFNAMSIZ];		/* if name, e.g. "en0" */
	union {
		struct	sockaddr ifru_addr;
		struct	sockaddr ifru_dstaddr;
		struct	sockaddr ifru_broadaddr;
		short	ifru_flags;
		caddr_t	ifru_data;
	} ifr_ifru;
};

.if t .ta \w'  #define'u +\w'  ifr_broadaddr'u +\w'  ifr_ifru.ifru_broadaddr'u
#define	ifr_addr	ifr_ifru.ifru_addr	/* address */
#define	ifr_dstaddr	ifr_ifru.ifru_dstaddr	/* other end of p-to-p link */
#define	ifr_broadaddr	ifr_ifru.ifru_broadaddr	/* broadcast address */
#define	ifr_flags	ifr_ifru.ifru_flags	/* flags */
#define	ifr_data	ifr_ifru.ifru_data	/* for use by interface */
.DE
The actual call which obtains the
interface configuration is
.DS
struct ifconf ifc;
char buf[BUFSIZ];

ifc.ifc_len = sizeof (buf);
ifc.ifc_buf = buf;
if (ioctl(s, SIOCGIFCONF, (char *) &ifc) < 0) {
	...
}
.DE
After this call \fIbuf\fP will contain one \fIifreq\fP structure for
each network to which the host is connected, and
\fIifc.ifc_len\fP will have been modified to reflect the number
of bytes used by the \fIifreq\fP structures.
.PP
For each structure
there exists a set of ``interface flags'' which tell
whether the network corresponding to that interface is
up or down, point to point or broadcast, etc.  The
SIOCGIFFLAGS \fIioctl\fP retrieves these
flags for an interface specified by an \fIifreq\fP
structure as follows:
.DS
struct ifreq *ifr;

ifr = ifc.ifc_req;

for (n = ifc.ifc_len / sizeof (struct ifreq); --n >= 0; ifr++) {
	/*
	 * We must be careful that we don't use an interface
	 * devoted to an address family other than those intended;
	 * if we were interested in NS interfaces, the
	 * AF_INET would be AF_NS.
	 */
	if (ifr->ifr_addr.sa_family != AF_INET)
		continue;
	if (ioctl(s, SIOCGIFFLAGS, (char *) ifr) < 0) {
		...
	}
	/*
	 * Skip boring cases.
	 */
	if ((ifr->ifr_flags & IFF_UP) == 0 ||
	    (ifr->ifr_flags & IFF_LOOPBACK) ||
	    (ifr->ifr_flags & (IFF_BROADCAST | IFF_POINTTOPOINT)) == 0)
		continue;
.DE
.PP
Once the flags have been obtained, the broadcast address 
must be obtained.  In the case of broadcast networks this is
done via the SIOCGIFBRDADDR \fIioctl\fP, while for point-to-point networks
the address of the destination host is obtained with SIOCGIFDSTADDR.
.DS
struct sockaddr dst;

if (ifr->ifr_flags & IFF_POINTTOPOINT) {
	if (ioctl(s, SIOCGIFDSTADDR, (char *) ifr) < 0) {
		...
	}
	bcopy((char *) ifr->ifr_dstaddr, (char *) &dst, sizeof (ifr->ifr_dstaddr));
} else if (ifr->ifr_flags & IFF_BROADCAST) {
	if (ioctl(s, SIOCGIFBRDADDR, (char *) ifr) < 0) {
		...
	}
	bcopy((char *) ifr->ifr_broadaddr, (char *) &dst, sizeof (ifr->ifr_broadaddr));
}
.DE
.PP
After the appropriate \fIioctl\fP's have obtained the broadcast
or destination address (now in \fIdst\fP), the \fIsendto\fP call may be
used:
.DS
	sendto(s, buf, buflen, 0, (struct sockaddr *)&dst, sizeof (dst));
}
.DE
In the above loop one \fIsendto\fP occurs for every
interface to which the host is connected that supports the notion of
broadcast or point-to-point addressing.
If a process only wished to send broadcast
messages on a given network, code similar to that outlined above
would be used, but the loop would need to find the
correct destination address.
.PP
Received broadcast messages contain the senders address
and port, as datagram sockets are bound before
a message is allowed to go out.
.NH 2
IP Multicasting
.PP
IP multicasting is the transmission of an IP datagram to a "host
group", a set of zero or more hosts identified by a single IP
destination address.  A multicast datagram is delivered to all
members of its destination host group with the same "best-efforts"
reliability as regular unicast IP datagrams, i.e., the datagram is
not guaranteed to arrive intact at all members of the destination
group or in the same order relative to other datagrams.
.PP
The membership of a host group is dynamic; that is, hosts may join
and leave groups at any time.  There is no restriction on the
location or number of members in a host group.  A host may be a
member of more than one group at a time.  A host need not be a member
of a group to send datagrams to it.
.PP
A host group may be permanent or transient.  A permanent group has a
well-known, administratively assigned IP address.  It is the address,
not the membership of the group, that is permanent; at any time a
permanent group may have any number of members, even zero.  Those IP
multicast addresses that are not reserved for permanent groups are
available for dynamic assignment to transient groups which exist only
as long as they have members.
.PP
In general, a host cannot assume that datagrams sent to any host
group address will reach only the intended hosts, or that datagrams
received as a member of a transient host group are intended for the
recipient.  Misdelivery must be detected at a level above IP, using
higher-level identifiers or authentication tokens.  Information
transmitted to a host group address should be encrypted or governed
by administrative routing controls if the sender is concerned about
unwanted listeners.
.PP
IP multicasting is currently supported only on AF_INET sockets of type
SOCK_DGRAM and SOCK_RAW, and only on subnetworks for which the interface
driver has been modified to support multicasting.
.PP
The next subsections describe how to send and receive multicast datagrams.
.NH 3 
Sending IP Multicast Datagrams
.PP
To send a multicast datagram, specify an IP multicast address in the range
224.0.0.0 to 239.255.255.255 as the destination address
in a
.IR sendto (2)
call.
.PP
The definitions required for the multicast-related socket options are
found in \fI<netinet/in.h>\fP.
All IP addresses are passed in network byte-order.
.PP
By default, IP multicast datagrams are sent with a time-to-live (TTL) of 1,
which prevents them from being forwarded beyond a single subnetwork.  A new
socket option allows the TTL for subsequent multicast datagrams to be set to
any value from 0 to 255, in order to control the scope of the multicasts:
.DS
u_char ttl;
setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
.DE
Multicast datagrams with a TTL of 0 will not be transmitted on any subnet,
but may be delivered locally if the sending host belongs to the destination
group and if multicast loopback has not been disabled on the sending socket
(see below).  Multicast datagrams with TTL greater than one may be delivered
to more than one subnet if there are one or more multicast routers attached
to the first-hop subnet.  To provide meaningful scope control, the multicast
routers support the notion of TTL "thresholds", which prevent datagrams with
less than a certain TTL from traversing certain subnets.  The thresholds
enforce the following convention:
.TS
center;
l | l
l | n.
_
Scope	Initial TTL
=
restricted to the same host	0
restricted to the same subnet	1
restricted to the same site	32
restricted to the same region	64
restricted to the same continent	128
unrestricted	255
_
.TE
"Sites" and "regions" are not strictly defined, and sites may be further
subdivided into smaller administrative units, as a local matter.
.PP
An application may choose an initial TTL other than the ones listed above.
For example, an application might perform an "expanding-ring search" for a
network resource by sending a multicast query, first with a TTL of 0, and
then with larger and larger TTLs, until a reply is received, perhaps using
the TTL sequence 0, 1, 2, 4, 8, 16, 32.
.PP
The multicast router
.IR mrouted (8),
refuses to forward any
multicast datagram with a destination address between 224.0.0.0 and
224.0.0.255, inclusive, regardless of its TTL.  This range of addresses is
reserved for the use of routing protocols and other low-level topology
discovery or maintenance protocols, such as gateway discovery and group
membership reporting.
.PP
The address 224.0.0.0 is
guaranteed not to be assigned to any group, and 224.0.0.1 is assigned
to the permanent group of all IP hosts (including gateways).  This is
used to address all multicast hosts on the directly connected
network.  There is no multicast address (or any other IP address) for
all hosts on the total Internet.  The addresses of other well-known,
permanent groups are published in the "Assigned Numbers" RFC,
which is available from the InterNIC.
.PP
Each multicast transmission is sent from a single network interface, even if
the host has more than one multicast-capable interface.  (If the host is
also serving as a multicast router,
a multicast may be \fIforwarded\fP to interfaces
other than originating interface, provided that the TTL is greater than 1.)
The default interface to be used for multicasting is the primary network
interface on the system.
A socket option
is available to override the default for subsequent transmissions from a
given socket:
.DS
struct in_addr addr;
setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &addr, sizeof(addr));
.DE
where "addr" is the local IP address of the desired outgoing interface.
An address of INADDR_ANY may be used to revert to the default interface.
The local IP address of an interface can be obtained via the SIOCGIFCONF
ioctl.  To determine if an interface supports multicasting, fetch the
interface flags via the SIOCGIFFLAGS ioctl and see if the IFF_MULTICAST
flag is set.  (Normal applications should not need to use this option; it
is intended primarily for multicast routers and other system services
specifically concerned with internet topology.)
The SIOCGIFCONF and SIOCGIFFLAGS ioctls are described in the previous section.
.PP
If a multicast datagram is sent to a group to which the sending host itself
belongs (on the outgoing interface), a copy of the datagram is, by default,
looped back by the IP layer for local delivery.  Another socket option gives
the sender explicit control over whether or not subsequent datagrams are
looped back:
.DS
u_char loop;
setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
.DE
where \f2loop\f1 is set to 0 to disable loopback,
and set to 1 to enable loopback.
This option
improves performance for applications that may have no more than one
instance on a single host (such as a router demon), by eliminating
the overhead of receiving their own transmissions.  It should generally not
be used by applications for which there may be more than one instance on a
single host (such as a conferencing program) or for which the sender does
not belong to the destination group (such as a time querying program).
.PP
A multicast datagram sent with an initial TTL greater than 1 may be delivered
to the sending host on a different interface from that on which it was sent,
if the host belongs to the destination group on that other interface.  The
loopback control option has no effect on such delivery.
.NH 3 
Receiving IP Multicast Datagrams
.PP
Before a host can receive IP multicast datagrams, it must become a member
of one or more IP multicast groups.  A process can ask the host to join
a multicast group by using the following socket option:
.DS
struct ip_mreq mreq;
setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq))
.DE
where "mreq" is the following structure:
.DS
struct ip_mreq {
    struct in_addr imr_multiaddr; /* \fImulticast group to join\fP */
    struct in_addr imr_interface; /* \fIinterface to join on\fP */
}
.DE
Every membership is associated with a single interface, and it is possible
to join the same group on more than one interface.  "imr_interface" should
be INADDR_ANY to choose the default multicast interface, or one of the
host's local addresses to choose a particular (multicast-capable) interface.
Up to IP_MAX_MEMBERSHIPS (currently 20) memberships may be added on a
single socket.
.PP
To drop a membership, use:
.DS
struct ip_mreq mreq;
setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
.DE
where "mreq" contains the same values as used to add the membership.  The
memberships associated with a socket are also dropped when the socket is
closed or the process holding the socket is killed.  However, more than
one socket may claim a membership in a particular group, and the host
will remain a member of that group until the last claim is dropped.
.PP
The memberships associated with a socket do not necessarily determine which
datagrams are received on that socket.  Incoming multicast packets are
accepted by the kernel IP layer if any socket has claimed a membership in the
destination group of the datagram; however, delivery of a multicast datagram
to a particular socket is based on the destination port (or protocol type, for
raw sockets), just as with unicast datagrams.  
To receive multicast datagrams
sent to a particular port, it is necessary to bind to that local port,
leaving the local address unspecified (i.e., INADDR_ANY).
To receive multicast datagrams
sent to a particular group and port, bind to the local port, with
the local address set to the multicast group address.  
Once bound to a multicast address, the socket cannot be used for sending data.
.PP
More than one process may bind to the same SOCK_DGRAM UDP port 
or the same multicast group and port if the
.I bind
call is preceded by:
.DS
int on = 1;
setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
.DE
All processes sharing the port must enable this option.
Every incoming multicast or broadcast UDP datagram destined to
the shared port is delivered to all sockets bound to the port.  
For backwards compatibility reasons, this does not apply to incoming
unicast datagrams.  Unicast
datagrams are never delivered to more than one socket, regardless of
how many sockets are bound to the datagram's destination port.
.PP
A final multicast-related extension is independent of IP:  two new ioctls,
SIOCADDMULTI and SIOCDELMULTI, are available to add or delete link-level
(e.g., Ethernet) multicast addresses accepted by a particular interface.
The address to be added or deleted is passed as a sockaddr structure of
family AF_UNSPEC, within the standard ifreq structure.
.PP
These ioctls are
for the use of protocols other than IP, and require superuser privileges.
A link-level multicast address added via SIOCADDMULTI is not automatically
deleted when the socket used to add it goes away; it must be explicitly
deleted.  It is inadvisable to delete a link-level address that may be
in use by IP.
.NH 3
Sample Multicast Program
.PP
The following program sends or receives multicast packets.
If invoked with one argument, it sends a packet containing the current
time to an arbitrarily-chosen multicast group and UDP port.
If invoked with no arguments, it receives and prints these packets.
Start it as a sender on just one host and as a receiver on all the other hosts.
.DS
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdio.h>

#define EXAMPLE_PORT    60123
#define EXAMPLE_GROUP   "224.0.0.250"

main(argc)
    int argc;
{
    struct sockaddr_in addr;
    int addrlen, fd, cnt;
    struct ip_mreq mreq;
    char message[50];

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        exit(1);
    }

    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(EXAMPLE_PORT);
    addrlen = sizeof(addr);

    if (argc > 1) {     /* Send */
        addr.sin_addr.s_addr = inet_addr(EXAMPLE_GROUP);
        while (1) {
            time_t t = time(0);
            sprintf(message, "time is %-24.24s", ctime(&t));
            cnt = sendto(fd, message, sizeof(message), 0,
                    (struct sockaddr *)&addr, addrlen);
            if (cnt < 0) {
                perror("sendto");
                exit(1);
            }
            sleep(5);
        }
    } else {            /* Receive */
        if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("bind");
            exit(1);
        }

        mreq.imr_multiaddr.s_addr = inet_addr(EXAMPLE_GROUP);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                    &mreq, sizeof(mreq)) < 0) {
            perror("setsockopt mreq");
            exit(1);
        }

        while (1) {
            cnt = recvfrom(fd, message, sizeof(message), 0,
                            (struct sockaddr *)&addr, &addrlen);
            if (cnt <= 0) {
		    if (cnt == 0) {
			break;
		    }
                    perror("recvfrom");
                    exit(1);
            } 
            printf("%s: message = \e"%s\e"\en",
                    inet_ntoa(addr.sin_addr), message);
        }
    }
}
.DE
.\"----------------------------------------------------------------------
.NH 2
NS Packet Sequences
.PP
The semantics of NS connections demand that
the user both be able to look inside the network header associated
with any incoming packet and be able to specify what should go
in certain fields of an outgoing packet.
Using different calls to \fIsetsockopt\fP, it is possible
to indicate whether prototype headers will be associated by
the user with each outgoing packet (SO_HEADERS_ON_OUTPUT),
to indicate whether the headers received by the system should be
delivered to the user (SO_HEADERS_ON_INPUT), or to indicate
default information that should be associated with all
outgoing packets on a given socket (SO_DEFAULT_HEADERS).
.PP
The contents of a SPP header (minus the IDP header) are:
.DS
.if t .ta \w"  #define"u +\w"  u_short"u +2.0i
struct sphdr {
	u_char	sp_cc;		/* connection control */
#define	SP_SP	0x80		/* system packet */
#define	SP_SA	0x40		/* send acknowledgement */
#define	SP_OB	0x20		/* attention (out of band data) */
#define	SP_EM	0x10		/* end of message */
	u_char	sp_dt;		/* datastream type */
	u_short	sp_sid;		/* source connection identifier */
	u_short	sp_did;		/* destination connection identifier */
	u_short	sp_seq;		/* sequence number */
	u_short	sp_ack;		/* acknowledge number */
	u_short	sp_alo;		/* allocation number */
};
.DE
Here, the items of interest are the \fIdatastream type\fP and
the \fIconnection control\fP fields.  The semantics of the
datastream type are defined by the application(s) in question;
the value of this field is, by default, zero, but it can be
used to indicate things such as Xerox's Bulk Data Transfer
Protocol (in which case it is set to one).  The connection control
field is a mask of the flags defined just below it.  The user may
set or clear the end-of-message bit to indicate
that a given message is the last of a given substream type,
or may set/clear the attention bit as an alternate way to
indicate that a packet should be sent out-of-band.
As an example, to associate prototype headers with outgoing
SPP packets, consider:
.DS
#include <sys/types.h>
#include <sys/socket.h>
#include <netns/ns.h>
#include <netns/sp.h>
 ...
struct sockaddr_ns sns, to;
int s, on = 1;
struct databuf {
	struct sphdr proto_spp;	/* prototype header */
	char buf[534];		/* max. possible data by Xerox std. */
} buf;
 ...
s = socket(AF_NS, SOCK_SEQPACKET, 0);
 ...
bind(s, (struct sockaddr *) &sns, sizeof (sns));
setsockopt(s, NSPROTO_SPP, SO_HEADERS_ON_OUTPUT, &on, sizeof(on));
 ...
buf.proto_spp.sp_dt = 1;	/* bulk data */
buf.proto_spp.sp_cc = SP_EM;	/* end-of-message */
strcpy(buf.buf, "hello world\en");
sendto(s, (char *) &buf, sizeof(struct sphdr) + strlen("hello world\en"),
    (struct sockaddr *) &to, sizeof(to));
 ...
.DE
Note that one must be careful when writing headers; if the prototype
header is not written with the data with which it is to be associated,
the kernel will treat the first few bytes of the data as the
header, with unpredictable results.
To turn off the above association, and to indicate that packet
headers received by the system should be passed up to the user,
one might use:
.DS
#include <sys/types.h>
#include <sys/socket.h>
#include <netns/ns.h>
#include <netns/sp.h>
 ...
struct sockaddr sns;
int s, on = 1, off = 0;
 ...
s = socket(AF_NS, SOCK_SEQPACKET, 0);
 ...
bind(s, (struct sockaddr *) &sns, sizeof (sns));
setsockopt(s, NSPROTO_SPP, SO_HEADERS_ON_OUTPUT, &off, sizeof(off));
setsockopt(s, NSPROTO_SPP, SO_HEADERS_ON_INPUT, &on, sizeof(on));
 ...
.DE
.PP
Output is handled somewhat differently in the IDP world.
The header of an IDP-level packet looks like:
.DS
.if t .ta \w'struct  'u +\w"  struct ns_addr"u +2.0i
struct idp {
	u_short	idp_sum;	/* Checksum */
	u_short	idp_len;	/* Length, in bytes, including header */
	u_char	idp_tc;		/* Transport Control (i.e., hop count) */
	u_char	idp_pt;		/* Packet Type (i.e., level 2 protocol) */
	struct ns_addr	idp_dna;	/* Destination Network Address */
	struct ns_addr	idp_sna;	/* Source Network Address */
};
.DE
The primary field of interest in an IDP header is the \fIpacket type\fP
field.  The standard values for this field are (as defined
in <\fInetns/ns.h\fP>):
.DS
.if t .ta \w"  #define"u +\w"  NSPROTO_ERROR"u +1.0i
#define NSPROTO_RI	1		/* Routing Information */
#define NSPROTO_ECHO	2		/* Echo Protocol */
#define NSPROTO_ERROR	3		/* Error Protocol */
#define NSPROTO_PE	4		/* Packet Exchange */
#define NSPROTO_SPP	5		/* Sequenced Packet */
.DE
For SPP connections, the contents of this field are
automatically set to NSPROTO_SPP; for IDP packets,
this value defaults to zero, which means ``unknown''.
.PP
Setting the value of that field with SO_DEFAULT_HEADERS is
easy:
.DS
#include <sys/types.h>
#include <sys/socket.h>
#include <netns/ns.h>
#include <netns/idp.h>
 ...
struct sockaddr sns;
struct idp proto_idp;		/* prototype header */
int s, on = 1;
 ...
s = socket(AF_NS, SOCK_DGRAM, 0);
 ...
bind(s, (struct sockaddr *) &sns, sizeof (sns));
proto_idp.idp_pt = NSPROTO_PE;	/* packet exchange */
setsockopt(s, NSPROTO_IDP, SO_DEFAULT_HEADERS, (char *) &proto_idp,
    sizeof(proto_idp));
 ...
.DE
.PP
Using SO_HEADERS_ON_OUTPUT is somewhat more difficult.  When
SO_HEADERS_ON_OUTPUT is turned on for an IDP socket, the socket
becomes (for all intents and purposes) a raw socket.  In this
case, all the fields of the prototype header (except the 
length and checksum fields, which are computed by the kernel)
must be filled in correctly in order for the socket to send and
receive data in a sensible manner.  To be more specific, the
source address must be set to that of the host sending the
data; the destination address must be set to that of the
host for whom the data is intended; the packet type must be
set to whatever value is desired; and the hopcount must be
set to some reasonable value (almost always zero).  It should
also be noted that simply sending data using \fIwrite\fP
will not work unless a \fIconnect\fP or \fIsendto\fP call
is used, in spite of the fact that it is the destination
address in the prototype header that is used, not the one
given in either of those calls.  For almost
all IDP applications , using SO_DEFAULT_HEADERS is easier and
more desirable than writing headers.
.NH 2
Three-way Handshake
.PP
The semantics of SPP connections indicates that a three-way
handshake, involving changes in the datastream type, should \(em
but is not absolutely required to \(em take place before a SPP
connection is closed.  Almost all SPP connections are
``well-behaved'' in this manner; when communicating with
any process, it is best to assume that the three-way handshake
is required unless it is known for certain that it is not
required.  In a three-way close, the closing process
indicates that it wishes to close the connection by sending
a zero-length packet with end-of-message set and with
datastream type 254.  The other side of the connection
indicates that it is OK to close by sending a zero-length
packet with end-of-message set and datastream type 255.  Finally,
the closing process replies with a zero-length packet with 
substream type 255; at this point, the connection is considered
closed.  The following code fragments are simplified examples
of how one might handle this three-way handshake at the user
level; in the future, support for this type of close will
probably be provided as part of the C library or as part of
the kernel.  The first code fragment below illustrates how a process
might handle three-way handshake if it sees that the process it
is communicating with wants to close the connection:
.DS
#include <sys/types.h>
#include <sys/socket.h>
#include <netns/ns.h>
#include <netns/sp.h>
 ...
#ifndef SPPSST_END
#define SPPSST_END 254
#define SPPSST_ENDREPLY 255
#endif
struct sphdr proto_sp;
int s;
 ...
read(s, buf, BUFSIZE);
if (((struct sphdr *)buf)->sp_dt == SPPSST_END) {
	/*
	 * SPPSST_END indicates that the other side wants to
	 * close.
	 */
	proto_sp.sp_dt = SPPSST_ENDREPLY;
	proto_sp.sp_cc = SP_EM;
	setsockopt(s, NSPROTO_SPP, SO_DEFAULT_HEADERS, (char *)&proto_sp,
	    sizeof(proto_sp));
	write(s, buf, 0);
	/*
	 * Write a zero-length packet with datastream type = SPPSST_ENDREPLY
	 * to indicate that the close is OK with us.  The packet that we
	 * don't see (because we don't look for it) is another packet
	 * from the other side of the connection, with SPPSST_ENDREPLY
	 * on it it, too.  Once that packet is sent, the connection is
	 * considered closed; note that we really ought to retransmit
	 * the close for some time if we do not get a reply.
	 */
	close(s);
}
 ...
.DE
To indicate to another process that we would like to close the
connection, the following code would suffice:
.DS
#include <sys/types.h>
#include <sys/socket.h>
#include <netns/ns.h>
#include <netns/sp.h>
 ...
#ifndef SPPSST_END
#define SPPSST_END 254
#define SPPSST_ENDREPLY 255
#endif
struct sphdr proto_sp;
int s;
 ...
proto_sp.sp_dt = SPPSST_END;
proto_sp.sp_cc = SP_EM;
setsockopt(s, NSPROTO_SPP, SO_DEFAULT_HEADERS, (char *)&proto_sp,
    sizeof(proto_sp));
write(s, buf, 0);	/* send the end request */
proto_sp.sp_dt = SPPSST_ENDREPLY;
setsockopt(s, NSPROTO_SPP, SO_DEFAULT_HEADERS, (char *)&proto_sp,
    sizeof(proto_sp));
/*
 * We assume (perhaps unwisely)
 * that the other side will send the
 * ENDREPLY, so we'll just send our final ENDREPLY
 * as if we'd seen theirs already.
 */
write(s, buf, 0);
close(s);
 ...
.DE
.NH 2
Packet Exchange
.PP
The Xerox standard protocols include a protocol that is both
reliable and datagram-oriented.  This protocol is known as
Packet Exchange (PEX or PE) and, like SPP, is layered on top
of IDP.  PEX is important for a number of things: Courier
remote procedure calls may be expedited through the use
of PEX, and many Xerox servers are located by doing a PEX
``BroadcastForServers'' operation.  Although there is no
implementation of PEX in the kernel,
it may be simulated at the user level with some clever coding
and the use of one peculiar \fIgetsockopt\fP.  A PEX packet
looks like:
.DS
.if t .ta \w'struct  'u +\w"  struct idp"u +2.0i
/*
 * The packet-exchange header shown here is not defined
 * as part of any of the system include files.
 */
struct pex {
	struct idp	p_idp;	/* idp header */
	u_short	ph_id[2];	/* unique transaction ID for pex */
	u_short	ph_client;	/* client type field for pex */
};
.DE
The \fIph_id\fP field is used to hold a ``unique id'' that
is used in duplicate suppression; the \fIph_client\fP
field indicates the PEX client type (similar to the packet
type field in the IDP header).  PEX reliability stems from the
fact that it is an idempotent (``I send a packet to you, you
send a packet to me'') protocol.  Processes on each side of
the connection may use the unique id to determine if they have
seen a given packet before (the unique id field differs on each
packet sent) so that duplicates may be detected, and to indicate
which message a given packet is in response to.  If a packet with
a given unique id is sent and no response is received in a given
amount of time, the packet is retransmitted until it is decided
that no response will ever be received.  To simulate PEX, one
must be able to generate unique ids -- something that is hard to
do at the user level with any real guarantee that the id is really
unique.  Therefore, a means (via \fIgetsockopt\fP) has been provided
for getting unique ids from the kernel.  The following code fragment
indicates how to get a unique id:
.DS
long uniqueid;
int s, idsize = sizeof(uniqueid);
 ...
s = socket(AF_NS, SOCK_DGRAM, 0);
 ...
/* get id from the kernel -- only on IDP sockets */
getsockopt(s, NSPROTO_PE, SO_SEQNO, (char *)&uniqueid, &idsize);
 ...
.DE
The retransmission and duplicate suppression code required to
simulate PEX fully is left as an exercise for the reader.
.NH 2
Inetd
.PP
One of the daemons provided with 4.4BSD is \fIinetd\fP, the
so called ``internet super-server.''  
Having one daemon listen for requests for many daemons
instead of having each daemon listen for its own requests
reduces the number of idle daemons and simplies their implementation.
.I Inetd
handles
two types of services: standard and TCPMUX.
A standard service has a well-known port assigned to it and
is listed in
.I /etc/services
(see \f2services\f1(5));
it may be a service that implements an official Internet standard or is a
BSD-specific service.
TCPMUX services are nonstandard and do not have a
well-known port assigned to them.
They are invoked from
.I inetd
when a program connects to the "tcpmux" well-known port and specifies
the service name.
This is useful for adding locally-developed servers.
.PP
\fIInetd\fP is invoked at boot
time, and determines from the file \fI/etc/inetd.conf\fP the
servers for which it is to listen.  Once this information has been
read and a pristine environment created, \fIinetd\fP proceeds
to create one socket for each service it is to listen for,
binding the appropriate port number to each socket.
.PP
\fIInetd\fP then performs a \fIselect\fP on all these
sockets for read availability, waiting for somebody wishing
a connection to the service corresponding to
that socket.  \fIInetd\fP then performs an \fIaccept\fP on
the socket in question, \fIfork\fPs, \fIdup\fPs the new
socket to file descriptors 0 and 1 (stdin and
stdout), closes other open file
descriptors, and \fIexec\fPs the appropriate server.
.PP
Servers making use of \fIinetd\fP are considerably simplified,
as \fIinetd\fP takes care of the majority of the IPC work
required in establishing a connection.  The server invoked
by \fIinetd\fP expects the socket connected to its client
on file descriptors 0 and 1, and may immediately perform
any operations such as \fIread\fP, \fIwrite\fP, \fIsend\fP,
or \fIrecv\fP.  Indeed, servers may use
buffered I/O as provided by the ``stdio'' conventions, as
long as they remember to use \fIfflush\fP when appropriate.
.PP
One call which may be of interest to individuals writing
servers under \fIinetd\fP is the \fIgetpeername\fP call,
which returns the address of the peer (process) connected
on the other end of the socket.  For example, to log the
Internet address in ``dot notation'' (e.g., ``128.32.0.4'')
of a client connected to a server under
\fIinetd\fP, the following code might be used:
.DS
struct sockaddr_in name;
int namelen = sizeof (name);
 ...
if (getpeername(0, (struct sockaddr *)&name, &namelen) < 0) {
	syslog(LOG_ERR, "getpeername: %m");
	exit(1);
} else
	syslog(LOG_INFO, "Connection from %s", inet_ntoa(name.sin_addr));
 ...
.DE
While the \fIgetpeername\fP call is especially useful when
writing programs to run with \fIinetd\fP, it can be used
under other circumstances.  Be warned, however, that \fIgetpeername\fP will
fail on UNIX domain sockets.
.PP
Standard TCP
services are assigned unique well-known port numbers in the range of
0 to 1023 by the
Internet Assigned Numbers Authority (IANA@ISI.EDU).
The limited number of ports in this range are
assigned to official Internet protocols.
The TCPMUX service allows you to add
locally-developed protocols without needing an official TCP port assignment.
The TCPMUX protocol described in RFC-1078 is simple:
.QP
``A TCP client connects to a foreign host on TCP port 1.  It sends the
service name followed by a carriage-return line-feed <CRLF>.
The service name is never case sensitive. 
The server replies with a
single character indicating positive ("+") or negative ("\-")
acknowledgment, immediately followed by an optional message of
explanation, terminated with a <CRLF>.  If the reply was positive,
the selected protocol begins; otherwise the connection is closed.''
.LP
In 4.4BSD, the TCPMUX service is built into
.IR inetd ,
that is,
.IR inetd
listens on TCP port 1 for requests for TCPMUX services listed
in \f2inetd.conf\f1.
.IR inetd (8)
describes the format of TCPMUX entries for \f2inetd.conf\f1.
.PP
The following is an example TCPMUX server and its \f2inetd.conf\f1 entry.
More sophisticated servers may want to do additional processing
before returning the positive or negative acknowledgement.
.DS
#include <sys/types.h>
#include <stdio.h>

main()
{
        time_t t;

        printf("+Go\er\en");
        fflush(stdout);
        time(&t);
        printf("%d = %s", t, ctime(&t));
        fflush(stdout);
}
.DE
The \f2inetd.conf\f1 entry is:
.DS
tcpmux/current_time stream tcp nowait nobody /d/curtime curtime
.DE
Here's the portion of the client code that handles the TCPMUX handshake:
.DS
char line[BUFSIZ];
FILE *fp;
 ...

/* Use stdio for reading data from the server */
fp = fdopen(sock, "r");
if (fp == NULL) {
    fprintf(stderr, "Can't create file pointer\en");
    exit(1);
}

/* Send service request */
sprintf(line, "%s\er\en", "current_time");
if (write(sock, line, strlen(line)) < 0) {
    perror("write");
    exit(1);
}

/* Get ACK/NAK response from the server */
if (fgets(line, sizeof(line), fp) == NULL) {
    if (feof(fp)) {
        die();
    } else {
        fprintf(stderr, "Error reading response\en");
        exit(1);
    }
}

/* Delete <CR> */
if ((lp = index(line, '\r')) != NULL) {
    *lp = '\0';
}

switch (line[0]) {
    case '+':
            printf("Got ACK: %s\en", &line[1]);
            break;
    case '-':
            printf("Got NAK: %s\en", &line[1]);
            exit(0);
    default:
            printf("Got unknown response: %s\en", line);
            exit(1);
}

/* Get rest of data from the server */
while ((fgets(line, sizeof(line), fp)) != NULL) {
    fputs(line, stdout);
}
.DE
