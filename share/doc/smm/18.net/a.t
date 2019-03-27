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
.\"	@(#)a.t	8.1 (Berkeley) 6/8/93
.\"
.nr H2 1
.\".ds RH "Gateways and routing
.br
.ne 2i
.NH
\s+2Gateways and routing issues\s0
.PP
The system has been designed with the expectation that it will
be used in an internetwork environment.  The ``canonical''
environment was envisioned to be a collection of local area
networks connected at one or more points through hosts with
multiple network interfaces (one on each local area network),
and possibly a connection to a long haul network (for example,
the ARPANET).  In such an environment, issues of
gatewaying and packet routing become very important.  Certain
of these issues, such as congestion
control, have been handled in a simplistic manner or specifically
not addressed.
Instead, where possible, the network system
attempts to provide simple mechanisms upon which more involved
policies may be implemented.  As some of these problems become
better understood, the solutions developed will be incorporated
into the system.
.PP
This section will describe the facilities provided for packet
routing.  The simplistic mechanisms provided for congestion
control are described in chapter 12.
.NH 2
Routing tables
.PP
The network system maintains a set of routing tables for
selecting a network interface to use in delivering a 
packet to its destination.  These tables are of the form:
.DS
.ta \w'struct   'u +\w'u_long   'u +\w'sockaddr rt_gateway;    'u
struct rtentry {
	u_long	rt_hash;		/* hash key for lookups */
	struct	sockaddr rt_dst;	/* destination net or host */
	struct	sockaddr rt_gateway;	/* forwarding agent */
	short	rt_flags;		/* see below */
	short	rt_refcnt;		/* no. of references to structure */
	u_long	rt_use;			/* packets sent using route */
	struct	ifnet *rt_ifp;		/* interface to give packet to */
};
.DE
.PP
The routing information is organized in two separate tables, one
for routes to a host and one for routes to a network.  The
distinction between hosts and networks is necessary so
that a single mechanism may be used
for both broadcast and multi-drop type networks, and
also for networks built from point-to-point links (e.g
DECnet [DEC80]).
.PP
Each table is organized as a hashed set of linked lists.
Two 32-bit hash values are calculated by routines defined for
each address family; one based on the destination being
a host, and one assuming the target is the network portion
of the address.  Each hash value is used to
locate a hash chain to search (by taking the value modulo the
hash table size) and the entire 32-bit value is then
used as a key in scanning the list of routes.  Lookups are
applied first to the routing
table for hosts, then to the routing table for networks.
If both lookups fail, a final lookup is made for a ``wildcard''
route (by convention, network 0).
The first appropriate route discovered is used.
By doing this, routes to a specific host on a network may be
present as well as routes to the network.  This also allows a
``fall back'' network route to be defined to a ``smart'' gateway
which may then perform more intelligent routing.
.PP
Each routing table entry contains a destination (the desired final destination),
a gateway to which to send the packet,
and various flags which indicate the route's status and type (host or
network).  A count
of the number of packets sent using the route is kept, along
with a count of ``held references'' to the dynamically
allocated structure to insure that memory reclamation
occurs only when the route is not in use.  Finally, a pointer to the
a network interface is kept; packets sent using
the route should be handed to this interface.
.PP
Routes are typed in two ways: either as host or network, and as
``direct'' or ``indirect''.  The host/network
distinction determines how to compare the \fIrt_dst\fP field
during lookup.  If the route is to a network, only a packet's
destination network is compared to the \fIrt_dst\fP entry stored
in the table.  If the route is to a host, the addresses must
match bit for bit.
.PP
The distinction between ``direct'' and ``indirect'' routes indicates
whether the destination is directly connected to the source.
This is needed when performing local network encapsulation.  If
a packet is destined for a peer at a host or network which is
not directly connected to the source, the internetwork packet
header will
contain the address of the eventual destination, while
the local network header will address the intervening
gateway.  Should the destination be directly connected, these addresses
are likely to be identical, or a mapping between the two exists.
The RTF_GATEWAY flag indicates that the route is to an ``indirect''
gateway agent, and that the local network header should be filled in
from the \fIrt_gateway\fP field instead of
from the final internetwork destination address.
.PP
It is assumed that multiple routes to the same destination will not
be present; only one of multiple routes, that most recently installed,
will be used.
.PP
Routing redirect control messages are used to dynamically
modify existing routing table entries as well as dynamically
create new routing table entries.  On hosts where exhaustive
routing information is too expensive to maintain (e.g. work
stations), the
combination of wildcard routing entries and routing redirect
messages can be used to provide a simple routing management
scheme without the use of a higher level policy process. 
Current connections may be rerouted after notification of the protocols
by means of their \fIpr_ctlinput\fP entries.
Statistics are kept by the routing table routines
on the use of routing redirect messages and their
affect on the routing tables.  These statistics may be viewed using
.IR netstat (1).
.PP
Status information other than routing redirect control messages
may be used in the future, but at present they are ignored.
Likewise, more intelligent ``metrics'' may be used to describe
routes in the future, possibly based on bandwidth and monetary
costs.
.NH 2
Routing table interface
.PP
A protocol accesses the routing tables through
three routines,
one to allocate a route, one to free a route, and one
to process a routing redirect control message.
The routine \fIrtalloc\fP performs route allocation; it is
called with a pointer to the following structure containing
the desired destination:
.DS
._f
struct route {
	struct	rtentry *ro_rt;
	struct	sockaddr ro_dst;
};
.DE
The route returned is assumed ``held'' by the caller until
released with an \fIrtfree\fP call.  Protocols which implement
virtual circuits, such as TCP, hold onto routes for the duration
of the circuit's lifetime, while connection-less protocols,
such as UDP, allocate and free routes whenever their destination address
changes.
.PP
The routine \fIrtredirect\fP is called to process a routing redirect
control message.  It is called with a destination address,
the new gateway to that destination, and the source of the redirect.
Redirects are accepted only from the current router for the destination.
If a non-wildcard route
exists to the destination, the gateway entry in the route is modified 
to point at the new gateway supplied.  Otherwise, a new routing
table entry is inserted reflecting the information supplied.  Routes
to interfaces and routes to gateways which are not directly accessible
from the host are ignored.
.NH 2
User level routing policies
.PP
Routing policies implemented in user processes manipulate the
kernel routing tables through two \fIioctl\fP calls.  The
commands SIOCADDRT and SIOCDELRT add and delete routing entries,
respectively; the tables are read through the /dev/kmem device.
The decision to place policy decisions in a user process implies
that routing table updates may lag a bit behind the identification of
new routes, or the failure of existing routes, but this period
of instability is normally very small with proper implementation
of the routing process.  Advisory information, such as ICMP
error messages and IMP diagnostic messages, may be read from
raw sockets (described in the next section).
.PP
Several routing policy processes have already been implemented.  The
system standard
``routing daemon'' uses a variant of the Xerox NS Routing Information
Protocol [Xerox82] to maintain up-to-date routing tables in our local
environment.  Interaction with other existing routing protocols,
such as the Internet EGP (Exterior Gateway Protocol), has been
accomplished using a similar process.
