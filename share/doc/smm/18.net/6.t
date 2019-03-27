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
.\"	@(#)6.t	8.1 (Berkeley) 6/8/93
.\"
.nr H2 1
.\".ds RH "Internal layering
.br
.ne 2i
.NH
\s+2Internal layering\s0
.PP
The internal structure of the network system is divided into
three layers.  These
layers correspond to the services provided by the socket
abstraction, those provided by the communication protocols,
and those provided by the hardware interfaces.  The communication
protocols are normally layered into two or more individual
cooperating layers, though they are collectively viewed
in the system as one layer providing services supportive
of the appropriate socket abstraction.
.PP
The following sections describe the properties of each layer
in the system and the interfaces to which each must conform.
.NH 2
Socket layer
.PP
The socket layer deals with the interprocess communication
facilities provided by the system.  A socket is a bidirectional
endpoint of communication which is ``typed'' by the semantics
of communication it supports.  The system calls described in
the \fIBerkeley Software Architecture Manual\fP [Joy86]
are used to manipulate sockets.
.PP
A socket consists of the following data structure:
.DS
._f
struct socket {
	short	so_type;		/* generic type */
	short	so_options;		/* from socket call */
	short	so_linger;		/* time to linger while closing */
	short	so_state;		/* internal state flags */
	caddr_t	so_pcb;			/* protocol control block */
	struct	protosw *so_proto;	/* protocol handle */
	struct	socket *so_head;	/* back pointer to accept socket */
	struct	socket *so_q0;		/* queue of partial connections */
	short	so_q0len;		/* partials on so_q0 */
	struct	socket *so_q;		/* queue of incoming connections */
	short	so_qlen;		/* number of connections on so_q */
	short	so_qlimit;		/* max number queued connections */
	struct	sockbuf so_rcv;		/* receive queue */
	struct	sockbuf so_snd;		/* send queue */
	short	so_timeo;		/* connection timeout */
	u_short	so_error;		/* error affecting connection */
	u_short	so_oobmark;		/* chars to oob mark */
	short	so_pgrp;		/* pgrp for signals */
};
.DE
.PP
Each socket contains two data queues, \fIso_rcv\fP and \fIso_snd\fP,
and a pointer to routines which provide supporting services. 
The type of the socket,
\fIso_type\fP is defined at socket creation time and used in selecting
those services which are appropriate to support it.  The supporting
protocol is selected at socket creation time and recorded in
the socket data structure for later use.  Protocols are defined
by a table of procedures, the \fIprotosw\fP structure, which will
be described in detail later.  A pointer to a protocol-specific
data structure,
the ``protocol control block,'' is also present in the socket structure.
Protocols control this data structure, which normally includes a
back pointer to the parent socket structure to allow easy
lookup when returning information to a user 
(for example, placing an error number in the \fIso_error\fP
field).  The other entries in the socket structure are used in
queuing connection requests, validating user requests, storing
socket characteristics (e.g.
options supplied at the time a socket is created), and maintaining
a socket's state.
.PP
Processes ``rendezvous at a socket'' in many instances.  For instance,
when a process wishes to extract data from a socket's receive queue
and it is empty, or lacks sufficient data to satisfy the request,
the process blocks, supplying the address of the receive queue as
a ``wait channel' to be used in notification.  When data arrives
for the process and is placed in the socket's queue, the blocked
process is identified by the fact it is waiting ``on the queue.''
.NH 3
Socket state
.PP
A socket's state is defined from the following:
.DS
.ta \w'#define 'u +\w'SS_ISDISCONNECTING    'u +\w'0x000     'u
#define	SS_NOFDREF	0x001	/* no file table ref any more */
#define	SS_ISCONNECTED	0x002	/* socket connected to a peer */
#define	SS_ISCONNECTING	0x004	/* in process of connecting to peer */
#define	SS_ISDISCONNECTING	0x008	/* in process of disconnecting */
#define	SS_CANTSENDMORE	0x010	/* can't send more data to peer */
#define	SS_CANTRCVMORE	0x020	/* can't receive more data from peer */
#define	SS_RCVATMARK	0x040	/* at mark on input */

#define	SS_PRIV	0x080	/* privileged */
#define	SS_NBIO	0x100	/* non-blocking ops */
#define	SS_ASYNC	0x200	/* async i/o notify */
.DE
.PP
The state of a socket is manipulated both by the protocols
and the user (through system calls).
When a socket is created, the state is defined based on the type of socket.
It may change as control actions are performed, for example connection
establishment.
It may also change according to the type of
input/output the user wishes to perform, as indicated by options
set with \fIfcntl\fP.  ``Non-blocking'' I/O  implies that
a process should never be blocked to await resources.  Instead, any
call which would block returns prematurely
with the error EWOULDBLOCK, or the service request may be partially
fulfilled, e.g. a request for more data than is present.
.PP
If a process requested ``asynchronous'' notification of events
related to the socket, the SIGIO signal is posted to the process
when such events occur.
An event is a change in the socket's state;
examples of such occurrences are: space
becoming available in the send queue, new data available in the
receive queue, connection establishment or disestablishment, etc. 
.PP
A socket may be marked ``privileged'' if it was created by the
super-user.  Only privileged sockets may
bind addresses in privileged portions of an address space
or use ``raw'' sockets to access lower levels of the network.
.NH 3
Socket data queues
.PP
A socket's data queue contains a pointer to the data stored in
the queue and other entries related to the management of
the data.  The following structure defines a data queue:
.DS
._f
struct sockbuf {
	u_short	sb_cc;		/* actual chars in buffer */
	u_short	sb_hiwat;	/* max actual char count */
	u_short	sb_mbcnt;	/* chars of mbufs used */
	u_short	sb_mbmax;	/* max chars of mbufs to use */
	u_short	sb_lowat;	/* low water mark */
	short	sb_timeo;	/* timeout */
	struct	mbuf *sb_mb;	/* the mbuf chain */
	struct	proc *sb_sel;	/* process selecting read/write */
	short	sb_flags;	/* flags, see below */
};
.DE
.PP
Data is stored in a queue as a chain of mbufs.
The actual count of data characters as well as high and low water marks are
used by the protocols in controlling the flow of data.
The amount of buffer space (characters of mbufs and associated data pages)
is also recorded along with the limit on buffer allocation.
The socket routines cooperate in implementing the flow control
policy by blocking a process when it requests to send data and
the high water mark has been reached, or when it requests to
receive data and less than the low water mark is present
(assuming non-blocking I/O has not been specified).*
.FS
* The low-water mark is always presumed to be 0
in the current implementation.
.FE
.PP
When a socket is created, the supporting protocol ``reserves'' space
for the send and receive queues of the socket.
The limit on buffer allocation is set somewhat higher than the limit
on data characters
to account for the granularity of buffer allocation.
The actual storage associated with a
socket queue may fluctuate during a socket's lifetime, but it is assumed
that this reservation will always allow a protocol to acquire enough memory
to satisfy the high water marks.
.PP
The timeout and select values are manipulated by the socket routines
in implementing various portions of the interprocess communications
facilities and will not be described here.
.PP
Data queued at a socket is stored in one of two styles.
Stream-oriented sockets queue data with no addresses, headers
or record boundaries.
The data are in mbufs linked through the \fIm_next\fP field.
Buffers containing access rights may be present within the chain
if the underlying protocol supports passage of access rights.
Record-oriented sockets, including datagram sockets,
queue data as a list of packets; the sections of packets are distinguished
by the types of the mbufs containing them.
The mbufs which comprise a record are linked through the \fIm_next\fP field;
records are linked from the \fIm_act\fP field of the first mbuf
of one packet to the first mbuf of the next.
Each packet begins with an mbuf containing the ``from'' address
if the protocol provides it,
then any buffers containing access rights, and finally any buffers
containing data.
If a record contains no data,
no data buffers are required unless neither address nor access rights
are present.
.PP
A socket queue has a number of flags used in synchronizing access
to the data and in acquiring resources:
.DS
._d
#define	SB_LOCK	0x01	/* lock on data queue (so_rcv only) */
#define	SB_WANT	0x02	/* someone is waiting to lock */
#define	SB_WAIT	0x04	/* someone is waiting for data/space */
#define	SB_SEL	0x08	/* buffer is selected */
#define	SB_COLL	0x10	/* collision selecting */
.DE
The last two flags are manipulated by the system in implementing
the select mechanism.
.NH 3
Socket connection queuing
.PP
In dealing with connection oriented sockets (e.g. SOCK_STREAM)
the two ends are considered distinct.  One end is termed
\fIactive\fP, and generates connection requests.  The other
end is called \fIpassive\fP and accepts connection requests.
.PP
From the passive side, a socket is marked with
SO_ACCEPTCONN when a \fIlisten\fP call is made, 
creating two queues of sockets: \fIso_q0\fP for connections
in progress and \fIso_q\fP for connections already made and
awaiting user acceptance.
As a protocol is preparing incoming connections, it creates
a socket structure queued on \fIso_q0\fP by calling the routine
\fIsonewconn\fP().  When the connection
is established, the socket structure is then transferred
to \fIso_q\fP, making it available for an \fIaccept\fP.
.PP
If an SO_ACCEPTCONN socket is closed with sockets on either
\fIso_q0\fP or \fIso_q\fP, these sockets are dropped,
with notification to the peers as appropriate.
.NH 2
Protocol layer(s)
.PP
Each socket is created in a communications domain,
which usually implies both an addressing structure (address family)
and a set of protocols which implement various socket types within the domain
(protocol family).
Each domain is defined by the following structure:
.DS
.ta .5i +\w'struct  'u +\w'(*dom_externalize)();   'u
struct	domain {
	int	dom_family;		/* PF_xxx */
	char	*dom_name;
	int	(*dom_init)();		/* initialize domain data structures */
	int	(*dom_externalize)();	/* externalize access rights */
	int	(*dom_dispose)();	/* dispose of internalized rights */
	struct	protosw *dom_protosw, *dom_protoswNPROTOSW;
	struct	domain *dom_next;
};
.DE
.PP
At boot time, each domain configured into the kernel
is added to a linked list of domain.
The initialization procedure of each domain is then called.
After that time, the domain structure is used to locate protocols
within the protocol family.
It may also contain procedure references
for externalization of access rights at the receiving socket
and the disposal of access rights that are not received.
.PP
Protocols are described by a set of entry points and certain
socket-visible characteristics, some of which are used in
deciding which socket type(s) they may support.  
.PP
An entry in the ``protocol switch'' table exists for each
protocol module configured into the system.  It has the following form:
.DS
.ta .5i +\w'struct  'u +\w'domain *pr_domain;    'u
struct protosw {
	short	pr_type;		/* socket type used for */
	struct	domain *pr_domain;	/* domain protocol a member of */
	short	pr_protocol;		/* protocol number */
	short	pr_flags;		/* socket visible attributes */
/* protocol-protocol hooks */
	int	(*pr_input)();		/* input to protocol (from below) */
	int	(*pr_output)();		/* output to protocol (from above) */
	int	(*pr_ctlinput)();	/* control input (from below) */
	int	(*pr_ctloutput)();	/* control output (from above) */
/* user-protocol hook */
	int	(*pr_usrreq)();		/* user request */
/* utility hooks */
	int	(*pr_init)();		/* initialization routine */
	int	(*pr_fasttimo)();	/* fast timeout (200ms) */
	int	(*pr_slowtimo)();	/* slow timeout (500ms) */
	int	(*pr_drain)();		/* flush any excess space possible */
};
.DE
.PP
A protocol is called through the \fIpr_init\fP entry before any other.
Thereafter it is called every 200 milliseconds through the
\fIpr_fasttimo\fP entry and
every 500 milliseconds through the \fIpr_slowtimo\fP for timer based actions.
The system will call the \fIpr_drain\fP entry if it is low on space and
this should throw away any non-critical data.
.PP
Protocols pass data between themselves as chains of mbufs using
the \fIpr_input\fP and \fIpr_output\fP routines.  \fIPr_input\fP
passes data up (towards
the user) and \fIpr_output\fP passes it down (towards the network); control
information passes up and down on \fIpr_ctlinput\fP and \fIpr_ctloutput\fP.
The protocol is responsible for the space occupied by any of the
arguments to these entries and must either pass it onward or dispose of it.
(On output, the lowest level reached must free buffers storing the arguments;
on input, the highest level is responsible for freeing buffers.)
.PP
The \fIpr_usrreq\fP routine interfaces protocols to the socket
code and is described below.
.PP
The \fIpr_flags\fP field is constructed from the following values:
.DS
.ta \w'#define 'u +\w'PR_CONNREQUIRED   'u +8n
#define	PR_ATOMIC	0x01		/* exchange atomic messages only */
#define	PR_ADDR	0x02		/* addresses given with messages */
#define	PR_CONNREQUIRED	0x04		/* connection required by protocol */
#define	PR_WANTRCVD	0x08		/* want PRU_RCVD calls */
#define	PR_RIGHTS	0x10		/* passes capabilities */
.DE
Protocols which are connection-based specify the PR_CONNREQUIRED
flag so that the socket routines will never attempt to send data
before a connection has been established.  If the PR_WANTRCVD flag
is set, the socket routines will notify the protocol when the user
has removed data from the socket's receive queue.  This allows
the protocol to implement acknowledgement on user receipt, and
also update windowing information based on the amount of space
available in the receive queue.  The PR_ADDR field indicates that any
data placed in the socket's receive queue will be preceded by the
address of the sender.  The PR_ATOMIC flag specifies that each \fIuser\fP
request to send data must be performed in a single \fIprotocol\fP send
request; it is the protocol's responsibility to maintain record
boundaries on data to be sent.  The PR_RIGHTS flag indicates that the
protocol supports the passing of capabilities;  this is currently
used only by the protocols in the UNIX protocol family.
.PP
When a socket is created, the socket routines scan the protocol
table for the domain
looking for an appropriate protocol to support the type of
socket being created.  The \fIpr_type\fP field contains one of the
possible socket types (e.g. SOCK_STREAM), while the \fIpr_domain\fP
is a back pointer to the domain structure.
The \fIpr_protocol\fP field contains the protocol number of the
protocol, normally a well-known value.
.NH 2
Network-interface layer
.PP
Each network-interface configured into a system defines a
path through which packets may be sent and received.
Normally a hardware device is associated with this interface,
though there is no requirement for this (for example, all
systems have a software ``loopback'' interface used for 
debugging and performance analysis).
In addition to manipulating the hardware device, an interface
module is responsible
for encapsulation and decapsulation of any link-layer header
information required to deliver a message to its destination.
The selection of which interface to use in delivering packets
is a routing decision carried out at a
higher level than the network-interface layer.
An interface may have addresses in one or more address families.
The address is set at boot time using an \fIioctl\fP on a socket
in the appropriate domain; this operation is implemented by the protocol
family, after verifying the operation through the device \fIioctl\fP entry.
.PP
An interface is defined by the following structure,
.DS
.ta .5i +\w'struct   'u +\w'ifaddr *if_addrlist;   'u
struct ifnet {
	char	*if_name;		/* name, e.g. ``en'' or ``lo'' */
	short	if_unit;		/* sub-unit for lower level driver */
	short	if_mtu;			/* maximum transmission unit */
	short	if_flags;		/* up/down, broadcast, etc. */
	short	if_timer;		/* time 'til if_watchdog called */
	struct	ifaddr *if_addrlist;	/* list of addresses of interface */
	struct	ifqueue if_snd;		/* output queue */
	int	(*if_init)();		/* init routine */
	int	(*if_output)();		/* output routine */
	int	(*if_ioctl)();		/* ioctl routine */
	int	(*if_reset)();		/* bus reset routine */
	int	(*if_watchdog)();	/* timer routine */
	int	if_ipackets;		/* packets received on interface */
	int	if_ierrors;		/* input errors on interface */
	int	if_opackets;		/* packets sent on interface */
	int	if_oerrors;		/* output errors on interface */
	int	if_collisions;		/* collisions on csma interfaces */
	struct	ifnet *if_next;
};
.DE
Each interface address has the following form:
.DS
.ta \w'#define 'u +\w'struct   'u +\w'struct   'u +\w'sockaddr ifa_addr;   'u-\w'struct   'u
struct ifaddr {
	struct	sockaddr ifa_addr;	/* address of interface */
	union {
		struct	sockaddr ifu_broadaddr;
		struct	sockaddr ifu_dstaddr;
	} ifa_ifu;
	struct	ifnet *ifa_ifp;		/* back-pointer to interface */
	struct	ifaddr *ifa_next;	/* next address for interface */
};
.ta \w'#define 'u +\w'ifa_broadaddr   'u +\w'ifa_ifu.ifu_broadaddr	   'u
#define	ifa_broadaddr	ifa_ifu.ifu_broadaddr	/* broadcast address */
#define	ifa_dstaddr	ifa_ifu.ifu_dstaddr	/* other end of p-to-p link */
.DE
The protocol generally maintains this structure as part of a larger
structure containing additional information concerning the address.
.PP
Each interface has a send queue and routines used for 
initialization, \fIif_init\fP, and output, \fIif_output\fP.
If the interface resides on a system bus, the routine \fIif_reset\fP
will be called after a bus reset has been performed. 
An interface may also
specify a timer routine, \fIif_watchdog\fP;
if \fIif_timer\fP is non-zero, it is decremented once per second
until it reaches zero, at which time the watchdog routine is called.
.PP
The state of an interface and certain characteristics are stored in
the \fIif_flags\fP field.  The following values are possible:
.DS
._d
#define	IFF_UP	0x1	/* interface is up */
#define	IFF_BROADCAST	0x2	/* broadcast is possible */
#define	IFF_DEBUG	0x4	/* turn on debugging */
#define	IFF_LOOPBACK	0x8	/* is a loopback net */
#define	IFF_POINTOPOINT	0x10	/* interface is point-to-point link */
#define	IFF_NOTRAILERS	0x20	/* avoid use of trailers */
#define	IFF_RUNNING	0x40	/* resources allocated */
#define	IFF_NOARP	0x80	/* no address resolution protocol */
.DE
If the interface is connected to a network which supports transmission
of \fIbroadcast\fP packets, the IFF_BROADCAST flag will be set and
the \fIifa_broadaddr\fP field will contain the address to be used in
sending or accepting a broadcast packet.  If the interface is associated
with a point-to-point hardware link (for example, a DEC DMR-11), the
IFF_POINTOPOINT flag will be set and \fIifa_dstaddr\fP will contain the
address of the host on the other side of the connection.  These addresses
and the local address of the interface, \fIif_addr\fP, are used in
filtering incoming packets.  The interface sets IFF_RUNNING after
it has allocated system resources and posted an initial read on the
device it manages.  This state bit is used to avoid multiple allocation
requests when an interface's address is changed.  The IFF_NOTRAILERS
flag indicates the interface should refrain from using a \fItrailer\fP
encapsulation on outgoing packets, or (where per-host negotiation
of trailers is possible) that trailer encapsulations should not be requested;
\fItrailer\fP protocols are described
in section 14.  The IFF_NOARP flag indicates the interface should not
use an ``address resolution protocol'' in mapping internetwork addresses
to local network addresses.
.PP
Various statistics are also stored in the interface structure.  These
may be viewed by users using the \fInetstat\fP(1) program.
.PP
The interface address and flags may be set with the SIOCSIFADDR and
SIOCSIFFLAGS \fIioctl\fP\^s.  SIOCSIFADDR is used initially to define each
interface's address; SIOGSIFFLAGS can be used to mark
an interface down and perform site-specific configuration.
The destination address of a point-to-point link is set with SIOCSIFDSTADDR.
Corresponding operations exist to read each value.
Protocol families may also support operations to set and read the broadcast
address.
In addition, the SIOCGIFCONF \fIioctl\fP retrieves a list of interface
names and addresses for all interfaces and protocols on the host.
.NH 3
UNIBUS interfaces
.PP
All hardware related interfaces currently reside on the UNIBUS.
Consequently a common set of utility routines for dealing
with the UNIBUS has been developed.  Each UNIBUS interface
utilizes a structure of the following form:
.DS
.ta \w'#define 'u +\w'ifw_xtofree 'u +\w'pte ifu_wmap[IF_MAXNUBAMR];    'u
struct	ifubinfo {
	short	iff_uban;			/* uba number */
	short	iff_hlen;			/* local net header length */
	struct	uba_regs *iff_uba;		/* uba regs, in vm */
	short	iff_flags;			/* used during uballoc's */
};
.DE
Additional structures are associated with each receive and transmit buffer,
normally one each per interface; for read,
.DS
.ta \w'#define 'u +\w'ifw_xtofree 'u +\w'pte ifu_wmap[IF_MAXNUBAMR];    'u
struct	ifrw {
	caddr_t	ifrw_addr;			/* virt addr of header */
	short	ifrw_bdp;			/* unibus bdp */
	short	ifrw_flags;			/* type, etc. */
#define	IFRW_W	0x01				/* is a transmit buffer */
	int	ifrw_info;			/* value from ubaalloc */
	int	ifrw_proto;			/* map register prototype */
	struct	pte *ifrw_mr;			/* base of map registers */
};
.DE
and for write,
.DS
.ta \w'#define 'u +\w'ifw_xtofree 'u +\w'pte ifu_wmap[IF_MAXNUBAMR];    'u
struct	ifxmt {
	struct	ifrw ifrw;
	caddr_t	ifw_base;			/* virt addr of buffer */
	struct	pte ifw_wmap[IF_MAXNUBAMR];	/* base pages for output */
	struct	mbuf *ifw_xtofree;		/* pages being dma'd out */
	short	ifw_xswapd;			/* mask of clusters swapped */
	short	ifw_nmr;			/* number of entries in wmap */
};
.ta \w'#define 'u +\w'ifw_xtofree 'u +\w'pte ifu_wmap[IF_MAXNUBAMR];    'u
#define	ifw_addr	ifrw.ifrw_addr
#define	ifw_bdp	ifrw.ifrw_bdp
#define	ifw_flags	ifrw.ifrw_flags
#define	ifw_info	ifrw.ifrw_info
#define	ifw_proto	ifrw.ifrw_proto
#define	ifw_mr	ifrw.ifrw_mr
.DE
One of each of these structures is conveniently packaged for interfaces
with single buffers for each direction, as follows:
.DS
.ta \w'#define 'u +\w'ifw_xtofree 'u +\w'pte ifu_wmap[IF_MAXNUBAMR];    'u
struct	ifuba {
	struct	ifubinfo ifu_info;
	struct	ifrw ifu_r;
	struct	ifxmt ifu_xmt;
};
.ta \w'#define 'u +\w'ifw_xtofree 'u
#define	ifu_uban	ifu_info.iff_uban
#define	ifu_hlen	ifu_info.iff_hlen
#define	ifu_uba		ifu_info.iff_uba
#define	ifu_flags	ifu_info.iff_flags
#define	ifu_w		ifu_xmt.ifrw
#define	ifu_xtofree	ifu_xmt.ifw_xtofree
.DE
.PP
The \fIif_ubinfo\fP structure contains the general information needed
to characterize the I/O-mapped buffers for the device.
In addition, there is a structure describing each buffer, including
UNIBUS resources held by the interface.
Sufficient memory pages and bus map registers are allocated to each buffer
upon initialization according to the maximum packet size and header length.
The kernel virtual address of the buffer is held in \fIifrw_addr\fP,
and the map registers begin
at \fIifrw_mr\fP.  UNIBUS map register \fIifrw_mr\fP\^[\-1]
maps the local network header
ending on a page boundary.  UNIBUS data paths are
reserved for read and for
write, given by \fIifrw_bdp\fP.  The prototype of the map
registers for read and for write is saved in \fIifrw_proto\fP.
.PP
When write transfers are not at least half-full pages on page boundaries,
the data are just copied into the pages mapped on the UNIBUS
and the transfer is started.
If a write transfer is at least half a page long and on a page
boundary, UNIBUS page table entries are swapped to reference
the pages, and then the initial pages are
remapped from \fIifw_wmap\fP when the transfer completes.
The mbufs containing the mapped pages are placed on the \fIifw_xtofree\fP
queue to be freed after transmission.
.PP
When read transfers give at least half a page of data to be input, page
frames are allocated from a network page list and traded
with the pages already containing the data, mapping the allocated
pages to replace the input pages for the next UNIBUS data input.
.PP
The following utility routines are available for use in
writing network interface drivers; all use the
structures described above.
.LP
if_ubaminit(ifubinfo, uban, hlen, nmr, ifr, nr, ifx, nx);
.br
if_ubainit(ifuba, uban, hlen, nmr);
.IP
\fIif_ubaminit\fP allocates resources on UNIBUS adapter \fIuban\fP,
storing the information in the \fIifubinfo\fP, \fIifrw\fP and \fIifxmt\fP
structures referenced.
The \fIifr\fP and \fIifx\fP parameters are pointers to arrays
of \fIifrw\fP and \fIifxmt\fP structures whose dimensions
are \fInr\fP and \fInx\fP, respectively.
\fIif_ubainit\fP is a simpler, backwards-compatible interface used
for hardware with single buffers of each type.
They are called only at boot time or after a UNIBUS reset. 
One data path (buffered or unbuffered,
depending on the \fIifu_flags\fP field) is allocated for each buffer.
The \fInmr\fP parameter indicates
the number of UNIBUS mapping registers required to map a maximal
sized packet onto the UNIBUS, while \fIhlen\fP specifies the size
of a local network header, if any, which should be mapped separately
from the data (see the description of trailer protocols in chapter 14).
Sufficient UNIBUS mapping registers and pages of memory are allocated
to initialize the input data path for an initial read.  For the output
data path, mapping registers and pages of memory are also allocated
and mapped onto the UNIBUS.  The pages associated with the output
data path are held in reserve in the event a write requires copying
non-page-aligned data (see \fIif_wubaput\fP below).
If \fIif_ubainit\fP is called with memory pages already allocated,
they will be used instead of allocating new ones (this normally
occurs after a UNIBUS reset).
A 1 is returned when allocation and initialization are successful,
0 otherwise.
.LP
m = if_ubaget(ifubinfo, ifr, totlen, off0, ifp);
.br
m = if_rubaget(ifuba, totlen, off0, ifp);
.IP
\fIif_ubaget\fP and \fIif_rubaget\fP pull input data
out of an interface receive buffer and into an mbuf chain.
The first interface passes pointers to the \fIifubinfo\fP structure
for the interface and the \fIifrw\fP structure for the receive buffer;
the second call may be used for single-buffered devices.
\fItotlen\fP specifies the length of data to be obtained, not counting the
local network header.  If \fIoff0\fP is non-zero, it indicates
a byte offset to a trailing local network header which should be
copied into a separate mbuf and prepended to the front of the resultant mbuf
chain.  When the data amount to at least a half a page,
the previously mapped data pages are remapped
into the mbufs and swapped with fresh pages, thus avoiding
any copy.
The receiving interface is recorded as \fIifp\fP, a pointer to an \fIifnet\fP
structure, for the use of the receiving network protocol.
A 0 return value indicates a failure to allocate resources.
.LP
if_wubaput(ifubinfo, ifx, m);
.br
if_wubaput(ifuba, m);
.IP
\fIif_ubaput\fP and \fIif_wubaput\fP map a chain of mbufs
onto a network interface in preparation for output.
The first interface is used by devices with multiple transmit buffers.
The chain includes any local network
header, which is copied so that it resides in the mapped and
aligned I/O space.
Page-aligned data that are page-aligned in the output buffer
are mapped to the UNIBUS in place of the normal buffer page,
and the corresponding mbuf is placed on a queue to be freed after transmission.
Any other mbufs which contained non-page-sized
data portions are copied to the I/O space and then freed.
Pages mapped from a previous output operation (no longer needed)
are unmapped.
