.\" Copyright (c) 1980, 1986, 1988, 1993 The Regents of the University of California.
.\" All rights reserved.
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
.\"	@(#)5.t	8.1 (Berkeley) 7/27/93
.\" $FreeBSD$
.\"
.ds lq ``
.ds rq ''
.ds LH "Installing/Operating \*(4B
.ds RH Network setup
.ds CF \*(Dy
.Sh 1 "Network setup"
.PP
\*(4B provides support for the standard Internet
protocols IP, ICMP, TCP, and UDP.  These protocols may be used
on top of a variety of hardware devices ranging from
serial lines to local area network controllers
for the Ethernet.  Network services are split between the
kernel (communication protocols) and user programs (user
services such as TELNET and FTP).  This section describes
how to configure your system to use the Internet networking support.
\*(4B also supports the Xerox Network Systems (NS) protocols.
IDP and SPP are implemented in the kernel,
and other protocols such as Courier run at the user level.
\*(4B provides some support for the ISO OSI protocols CLNP
TP4, and ESIS.  User level process
complete the application protocols such as X.400 and X.500.
.Sh 2 "System configuration"
.PP
To configure the kernel to include the Internet communication
protocols, define the INET option.
Xerox NS support is enabled with the NS option.
ISO OSI support is enabled with the ISO option.
In either case, include the pseudo-devices
``pty'', and ``loop'' in your machine's configuration
file. 
The ``pty'' pseudo-device forces the pseudo terminal device driver
to be configured into the system, see
.Xr pty (4),
while the ``loop'' pseudo-device forces inclusion of the software loopback
interface driver.
The loop driver is used in network testing
and also by the error logging system.
.PP
If you are planning to use the Internet network facilities on a 10Mb/s
Ethernet, the pseudo-device ``ether'' should also be included
in the configuration; this forces inclusion of the Address Resolution
Protocol module used in mapping between 48-bit Ethernet
and 32-bit Internet addresses.
.PP
Before configuring the appropriate networking hardware, you should
consult the manual pages in section 4 of the Programmer's Manual
selecting the appropriate interfaces for your architecture.
.PP
All network interface drivers including the loopback interface,
require that their host address(es) be defined at boot time.
This is done with
.Xr ifconfig (8)
commands included in the
.Pn /etc/netstart
file.
Interfaces that are able to dynamically deduce the host
part of an address may check that the host part of the address is correct.
The manual page for each network interface
describes the method used to establish a host's address.
.Xr Ifconfig (8)
can also be used to set options for the interface at boot time.
Options are set independently for each interface, and
apply to all packets sent using that interface.
Alternatively, translations for such hosts may be set in advance
or ``published'' by a \*(4B host by use of the
.Xr arp (8)
command.
Note that the use of trailer link-level is now negotiated between \*(4B hosts
using ARP,
and it is thus no longer necessary to disable the use of trailers
with
.Xr ifconfig .
.PP
The OSI equivalent to ARP is ESIS (End System to Intermediate System Routing
Protocol); running this protocol is mandatory, however one can manually add
translations for machines that do not participate by use of the
.Xr route (8)
command.
Additional information is provided in the manual page describing
.Xr ESIS (4).
.Sh 2 "Local subnets"
.PP
In \*(4B the Internet support
includes the notion of ``subnets''.  This is a mechanism
by which multiple local networks may appears as a single Internet
network to off-site hosts.  Subnetworks are useful because
they allow a site to hide their local topology, requiring only a single
route in external gateways;
it also means that local network numbers may be locally administered.
The standard describing this change in Internet addressing is RFC-950.
.PP
To set up local subnets one must first decide how the available
address space (the Internet ``host part'' of the 32-bit address)
is to be partitioned.
Sites with a class A network
number have a 24-bit host address space with which to work, sites with a
class B network number have a 16-bit host address space, while sites with
a class C network number have an 8-bit host address space\**.
.FS
If you are unfamiliar with the Internet addressing structure, consult
``Address Mappings'', Internet RFC-796, J. Postel; available from
the Internet Network Information Center at SRI.
.FE
To define local subnets you must steal some bits
from the local host address space for use in extending the network
portion of the Internet address.  This reinterpretation of Internet
addresses is done only for local networks; i.e. it is not visible
to hosts off-site.  For example, if your site has a class B network
number, hosts on this network have an Internet address that contains
the network number, 16 bits, and the host number, another
16 bits.  To define 254 local subnets, each
possessing at most 255 hosts, 8 bits may be taken from the local part.
(The use of subnets 0 and all-1's, 255 in this example, is discouraged
to avoid confusion about broadcast addresses.)
These new network
numbers are then constructed by concatenating the original 16-bit network
number with the extra 8 bits containing the local subnet number.
.PP
The existence of local subnets is communicated to the system at the time a
network interface is configured with the
.I netmask
option to the
.Xr ifconfig
program.  A ``network mask'' is specified to define the
portion of the Internet address that is to be considered the network part
for that network.
This mask normally contains the bits corresponding to the standard
network part as well as the portion of the local part
that has been assigned to subnets.
If no mask is specified when the address is set,
it will be set according to the class of the network.
For example, at Berkeley (class B network 128.32) 8 bits
of the local part have been reserved for defining subnets;
consequently the
.Pn /etc/netstart
file contains lines of the form
.DS
.ft CW
/sbin/ifconfig le0 netmask 0xffffff00 128.32.1.7
.DE
This specifies that for interface ``le0'', the upper 24 bits of
the Internet address should be used in calculating network numbers
(netmask 0xffffff00), and the interface's Internet address is
``128.32.1.7'' (host 7 on network 128.32.1).  Hosts \fIm\fP on
sub-network \fIn\fP of this network would then have addresses of
the form ``128.32.\fIn\fP.\fIm\fP'';  for example, host
99 on network 129 would have an address ``128.32.129.99''.
For hosts with multiple interfaces, the network mask should
be set for each interface,
although in practice only the mask of the first interface on each network
is really used.
.Sh 2 "Internet broadcast addresses"
.PP
The address defined as the broadcast address for Internet networks
according to RFC-919 is the address with a host part of all 1's.
The address used by 4.2BSD was the address with a host part of 0.
\*(4B uses the standard broadcast address (all 1's) by default,
but allows the broadcast address to be set (with
.Xr ifconfig )
for each interface.
This allows networks consisting of both 4.2BSD, \*(Ps and \*(4B hosts
to coexist while the upgrade process proceeds.
In the presence of subnets, the broadcast address uses the subnet field
as for normal host addresses, with the remaining host part set to 1's
(or 0's, on a network that has not yet been converted).
\*(4B hosts recognize and accept packets
sent to the logical-network broadcast address as well as those sent
to the subnet broadcast address, and when using an all-1's broadcast,
also recognize and receive packets sent to host 0 as a broadcast.
.Sh 2 "Routing"
.PP
If your environment allows access to networks not directly
attached to your host you will need to set up routing information
to allow packets to be properly routed.  Two schemes are
supported by the system.  The first scheme
employs a routing table management daemon.
Optimally, you should use the routing daemon
.Xr gated
available from Cornell university.
We use it on our systems and it works well,
especially for multi-homed hosts using Serial Line IP (SLIP).
Unfortunately, we were not able to obtain permission to
include it on \*(4B.
.PP
If you do not wish to or cannot obtain
.Xr gated ,
the distribution does include
.Xr routed (8)
to maintain the system routing tables.  The routing daemon
uses a variant of the Xerox Routing Information Protocol
to maintain up to date routing tables in a cluster of local
area networks.  By using the
.Pn /etc/gateways
file, the routing daemon can also be used to initialize static routes
to distant networks (see the next section for further discussion).
When the routing daemon is started up
(usually from
.Pn /etc/rc )
it reads
.Pn /etc/gateways
if it exists and installs those routes defined there,
then broadcasts on each local network
to which the host is attached to find other instances of the routing
daemon.  If any responses are received, the routing daemons
cooperate in maintaining a globally consistent view of routing
in the local environment.  This view can be extended to include
remote sites also running the routing daemon by setting up suitable
entries in
.Pn /etc/gateways ;
consult
.Xr routed (8)
for a more thorough discussion.
.PP
The second approach is to define a default or wildcard
route to a smart
gateway and depend on the gateway to provide ICMP routing
redirect information to dynamically create a routing data
base.  This is done by adding an entry of the form
.DS
.ft CW
/sbin/route add default \fIsmart-gateway\fP 1
.DE
to
.Pn /etc/netstart ;
see
.Xr route (8)
for more information.  The default route
will be used by the system as a ``last resort''
in routing packets to their destination.  Assuming the gateway
to which packets are directed is able to generate the proper
routing redirect messages, the system will then add routing
table entries based on the information supplied.  This approach
has certain advantages over the routing daemon, but is
unsuitable in an environment where there are only bridges (i.e.
pseudo gateways that, for instance, do not generate routing
redirect messages).  Further, if the
smart gateway goes down there is no alternative, save manual
alteration of the routing table entry, to maintaining service.
.PP
The system always listens, and processes, routing redirect
information, so it is possible to combine both of the above
facilities.  For example, the routing table management process
might be used to maintain up to date information about routes
to geographically local networks, while employing the wildcard
routing techniques for ``distant'' networks.  The
.Xr netstat (1)
program may be used to display routing table contents as well
as various routing oriented statistics.  For example,
.DS
\fB#\fP \fInetstat \-r\fP
.DE
will display the contents of the routing tables, while
.DS
\fB#\fP \fInetstat \-r \-s\fP
.DE
will show the number of routing table entries dynamically
created as a result of routing redirect messages, etc.
.Sh 2 "Use of \*(4B machines as gateways"
.PP
Several changes have been made in \*(4B in the area of gateway support
(or packet forwarding, if one prefers).
A new configuration option, GATEWAY, is used when configuring
a machine to be used as a gateway.
This option increases the size of the routing hash tables in the kernel.
Unless configured with that option,
hosts with only a single non-loopback interface never attempt
to forward packets or to respond with ICMP error messages to misdirected
packets.
This change reduces the problems that may occur when different hosts
on a network disagree on the network number or broadcast address.
Another change is that \*(4B machines that forward packets back through
the same interface on which they arrived
will send ICMP redirects to the source host if it is on the same network.
This improves the interaction of \*(4B gateways with hosts that configure
their routes via default gateways and redirects.
The generation of redirects may be disabled with the configuration option
IPSENDREDIRECTS=0 or while the system is running by using the command:
.DS
.ft CW
sysctl -w net.inet.ip.redirect=0
.DE
in environments where it may cause difficulties.
.Sh 2 "Network databases"
.PP
Several data files are used by the network library routines
and server programs.  Most of these files are host independent
and updated only rarely.
.br
.ne 1i
.TS
lfC l l.
File	Manual reference	Use
_
/etc/hosts	\fIhosts\fP\|(5)	local host names
/etc/networks	\fInetworks\fP\|(5)	network names
/etc/services	\fIservices\fP\|(5)	list of known services
/etc/protocols	\fIprotocols\fP\|(5)	protocol names
/etc/hosts.equiv	\fIrshd\fP\|(8)	list of ``trusted'' hosts
/etc/netstart	\fIrc\fP\|(8)	command script for initializing network
/etc/rc	\fIrc\fP\|(8)	command script for starting standard servers
/etc/rc.local	\fIrc\fP\|(8)	command script for starting local servers
/etc/ftpusers	\fIftpd\fP\|(8)	list of ``unwelcome'' ftp users
/etc/hosts.lpd	\fIlpd\fP\|(8)	list of hosts allowed to access printers
/etc/inetd.conf	\fIinetd\fP\|(8)	list of servers started by \fIinetd\fP
.TE
The files distributed are set up for Internet hosts.
Local networks and hosts should be added to describe the local
configuration; the Berkeley entries may serve as examples
(see also the section on
.Pn /etc/hosts ).
Network numbers will have to be chosen for each Ethernet.
For sites connected to the Internet,
the normal channels should be used for allocation of network
numbers (contact hostmaster@SRI-NIC.ARPA).
For other sites,
these could be chosen more or less arbitrarily,
but it is generally better to request official numbers
to avoid conversion if a connection to the Internet (or others on the Internet)
is ever established.
.Sh 3 "Network servers"
.PP
Most network servers are automatically started up at boot time
by the command file
.Pn /etc/rc
or by the Internet daemon (see below).
These include the following:
.TS
lfC l l.
Program	Server	Started by
_
/usr/sbin/syslogd	error logging server	\f(CW/etc/rc\fP
/usr/sbin/named	Internet name server	\f(CW/etc/rc\fP
/sbin/routed	routing table management daemon	\f(CW/etc/rc\fP
/usr/sbin/rwhod	system status daemon	\f(CW/etc/rc\fP
/usr/sbin/timed	time synchronization daemon	\f(CW/etc/rc\fP
/usr/sbin/sendmail	SMTP server	\f(CW/etc/rc\fP
/usr/libexec/rshd	shell server	inetd
/usr/libexec/rexecd	exec server	inetd
/usr/libexec/rlogind	login server	inetd
/usr/libexec/telnetd	TELNET server	inetd
/usr/libexec/ftpd	FTP server	inetd
/usr/libexec/fingerd	Finger server	inetd
/usr/libexec/tftpd	TFTP server	inetd
.TE
Consult the manual pages and accompanying documentation (particularly
for named and sendmail) for details about their operation.
.PP
The use of
.Xr routed
and
.Xr rwhod
is controlled by shell
variables set in
.Pn /etc/netstart .
By default,
.Xr routed
is used, but
.Xr rwhod
is not; they are enabled by setting the variables \fIroutedflags\fP and
.Xr rwhod
to strings other than ``NO.''
The value of \fIroutedflags\fP provides host-specific options to
.Xr routed .
For example,
.DS
.ft CW
routedflags=-q
rwhod=NO
.DE
would run
.Xr "routed -q"
and would not run
.Xr rwhod .
.PP
To have other network servers started as well,
commands of the following sort should be placed in the site-dependent file
.Pn /etc/rc.local .
.DS
.ft CW
if [ -f /usr/sbin/timed ]; then
	/usr/sbin/timed & echo -n ' timed'			>/dev/console
f\&i
.DE
.Sh 3 "Internet daemon"
.PP
In \*(4B most of the servers for user-visible services are started up by a
``super server'', the Internet daemon.  The Internet
daemon,
.Pn /usr/sbin/inetd ,
acts as a master server for
programs specified in its configuration file,
.Pn /etc/inetd.conf ,
listening for service requests for these servers, and starting
up the appropriate program whenever a request is received.
The configuration file contains lines containing a service
name (as found in
.Pn /etc/services ),
the type of socket the
server expects (e.g. stream or dgram), the protocol to be
used with the socket (as found in
.Pn /etc/protocols ),
whether to wait for each server to complete before starting up another,
the user name by which the server should run, the server
program's name, and at most five arguments to pass to the
server program.
Some trivial services are implemented internally in
.Xr inetd ,
and their servers are listed as ``internal.''
For example, an entry for the file
transfer protocol server would appear as
.DS
.ft CW
ftp	stream	tcp	nowait	root	/usr/libexec/ftpd	ftpd
.DE
Consult
.Xr inetd (8)
for more detail on the format of the configuration file
and the operation of the Internet daemon.
.Sh 3 "The \f(CW/etc/hosts.equiv\fP file"
.PP
The remote login and shell servers use an
authentication scheme based on trusted hosts.  The
.Pn hosts.equiv
file contains a list of hosts that are considered trusted
and, under a single administrative control.  When a user
contacts a remote login or shell server requesting service,
the client process passes the user's name and the official
name of the host on which the client is located.  In the simple
case, if the host's name is located in
.Pn hosts.equiv
and the user has an account on the server's machine, then service
is rendered (i.e. the user is allowed to log in, or the command
is executed).  Users may expand this ``equivalence'' of
machines by installing a
.Pn \&.rhosts
file in their login directory.
The root login is handled specially, bypassing the
.Pn hosts.equiv
file, and using only the
.Pn /.rhosts
file.
.PP
Thus, to create a class of equivalent machines, the
.Pn hosts.equiv
file should contain the \fIofficial\fP names for those machines.
If you are running the name server, you may omit the domain part
of the host name for machines in your local domain.
For example, four machines on our local
network are considered trusted, so the
.Pn hosts.equiv
file is of the form:
.DS
.ft CW
vangogh.CS.Berkeley.EDU
picasso.CS.Berkeley.EDU
okeeffe.CS.Berkeley.EDU
.DE
.Sh 3 "The \f(CW/etc/ftpusers\fP file"
.PP
The FTP server included in the system provides support for an
anonymous FTP account.  Because of the inherent security problems
with such a facility you should read this section carefully if
you consider providing such a service.
.PP
An anonymous account is enabled by creating a user
.Xr ftp .
When a client uses the anonymous account a
.Xr chroot (2)
system call is performed by the server to restrict the client
from moving outside that part of the filesystem where the
user ftp home directory is located.  Because a
.Xr chroot
call is used, certain programs and files used by the server
process must be placed in the ftp home directory.
Further, one must be
sure that all directories and executable images are unwritable.
The following directory setup is recommended.  The
use of the
.Xr awk
commands to copy the
.Pn /etc/passwd
and
.Pn /etc/group
files are \fBSTRONGLY\fP recommended.
.DS
\fB#\fP \fIcd ~ftp\fP
\fB#\fP \fIchmod 555 .; chown ftp .; chgrp ftp .\fP
\fB#\fP \fImkdir bin etc pub\fP
\fB#\fP \fIchown root bin etc\fP
\fB#\fP \fIchmod 555 bin etc\fP
\fB#\fP \fIchown ftp pub\fP
\fB#\fP \fIchmod 777 pub\fP
\fB#\fP \fIcd bin\fP
\fB#\fP \fIcp /bin/sh /bin/ls .\fP
\fB#\fP \fIchmod 111 sh ls\fP
\fB#\fP \fIcd ../etc\fP
\fB#\fP \fIawk -F: '{$2="*";print$1":"$2":"$3":"$4":"$5":"$6":"}' < /etc/passwd > passwd\fP
\fB#\fP \fIawk -F: '{$2="*";print$1":"$2":"}' < /etc/group > group\fP
\fB#\fP \fIchmod 444 passwd group\fP
.DE
When local users wish to place files in the anonymous
area, they must be placed in a subdirectory.  In the
setup here, the directory
.Pn ~ftp/pub
is used.
.PP
Aside from the problems of directory modes and such,
the ftp server may provide a loophole for interlopers
if certain user accounts are allowed.
The file
.Pn /etc/ftpusers
is checked on each connection.
If the requested user name is located in the file, the
request for service is denied.  This file normally has
the following names on our systems.
.DS
uucp
root
.DE
Accounts without passwords need not be listed in this file as the ftp
server will refuse service to these users.
Accounts with nonstandard shells (any not listed in
.Pn /etc/shells )
will also be denied access via ftp.
