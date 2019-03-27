.\" Copyright (c) 1993
.\"	The Regents of the University of California.  All rights reserved.
.\"
.\" This document is derived from software contributed to Berkeley by
.\" Rick Macklem at The University of Guelph.
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
.\"	@(#)1.t	8.1 (Berkeley) 6/8/93
.\"
.\" $FreeBSD$
.\"
.sh 1 "NFS Implementation"
.pp
The 4.4BSD implementation of NFS and the alternate protocol nicknamed
Not Quite NFS (NQNFS) are kernel resident, but make use of a few system
daemons.
The kernel implementation does not use an RPC library, handling the RPC
request and reply messages directly in \fImbuf\fR data areas. NFS
interfaces to the network using
sockets via. the kernel interface available in
\fIsys/kern/uipc_syscalls.c\fR as \fIsosend(), soreceive(),\fR...
There are connection management routines for support of sockets for connection
oriented protocols and timeout/retransmit support for datagram sockets on
the client side.
For connection oriented transport protocols,
such as TCP/IP, there is one connection
for each client to server mount point that is maintained until an umount.
If the connection breaks, the client will attempt a reconnect with a new
socket.
The client side can operate without any daemons running, but performance
will be improved by running nfsiod daemons that perform read-aheads
and write-behinds.
For the server side to function, the daemons portmap, mountd and
nfsd must be running.
The mountd daemon performs two important functions.
.ip 1)
Upon startup and after a hangup signal, mountd reads the exports
file and pushes the export information for each local file system down
into the kernel via. the mount system call.
.ip 2)
Mountd handles remote mount protocol (RFC1094, Appendix A) requests.
.lp
The nfsd master daemon forks off children that enter the kernel
via. the nfssvc system call. The children normally remain kernel
resident, providing a process context for the NFS RPC servers.
Meanwhile, the master nfsd waits to accept new connections from clients
using connection oriented transport protocols and passes the new sockets down
into the kernel.
The client side mount_nfs along with portmap and
mountd are the only parts of the NFS subsystem that make any
use of the Sun RPC library.
.sh 1 "Mount Problems"
.pp
There are several problems that can be encountered at the time of an NFS
mount, ranging from an unresponsive NFS server (crashed, network partitioned
from client, etc.) to various interoperability problems between different
NFS implementations.
.pp
On the server side,
if the 4.4BSD NFS server will be handling any PC clients, mountd will
require the \fB-n\fR option to enable non-root mount request servicing.
Running of a pcnfsd\** daemon will also be necessary.
.(f
\** Pcnfsd is available in source form from Sun Microsystems and many
anonymous ftp sites.
.)f
The server side requires that the daemons
mountd and nfsd be running and that
they be registered with portmap properly.
If problems are encountered,
the safest fix is to kill all the daemons and then restart them in
the order portmap, mountd and nfsd.
Other server side problems are normally caused by problems with the format
of the exports file, which is covered under
Security and in the exports man page.
.pp
On the client side, there are several mount options useful for dealing
with server problems.
In cases where a file system is not critical for system operation, the
\fB-b\fR
mount option may be specified so that mount_nfs will go into the
background for a mount attempt on an unresponsive server.
This is useful for mounts specified in
\fIfstab(5)\fR,
so that the system will not get hung while booting doing
\fBmount -a\fR
because a file server is not responsive.
On the other hand, if the file system is critical to system operation, this
option should not be used so that the client will wait for the server to
come up before completing bootstrapping.
There are also three mount options to help deal with interoperability issues
with various non-BSD NFS servers. The
\fB-P\fR
option specifies that the NFS
client use a reserved IP port number to satisfy some servers' security
requirements.\**
.(f
\**Any security benefit of this is highly questionable and as
such the BSD server does not require a client to use a reserved port number.
.)f
The
\fB-c\fR
option stops the NFS client from doing a \fIconnect\fR on the UDP
socket, so that the mount works with servers that send NFS replies from
port numbers other than the standard 2049.\**
.(f
\**The Encore Multimax is known
to require this.
.)f
Finally, the
\fB-g=\fInum\fR
option sets the maximum size of the group list in the credentials passed
to an NFS server in every RPC request. Although RFC1057 specifies a maximum
size of 16 for the group list, some servers can't handle that many.
If a user, particularly root doing a mount,
keeps getting access denied from a file server, try temporarily
reducing the number of groups that user is in to less than 5
by editing /etc/group. If the user can then access the file system, slowly
increase the number of groups for that user until the limit is found and
then peg the limit there with the
\fB-g=\fInum\fR
option.
This implies that the server will only see the first \fInum\fR
groups that the user is in, which can cause some accessibility problems.
.pp
For sites that have many NFS servers, amd [Pendry93]
is a useful administration tool.
It also reduces the number of actual NFS mount points, alleviating problems
with commands such as df(1) that hang when any of the NFS servers is
unreachable.
.sh 1 "Dealing with Hung Servers"
.pp
There are several mount options available to help a client deal with
being hung waiting for response from a crashed or unreachable\** server.
.(f
\**Due to a network partitioning or similar.
.)f
By default, a hard mount will continue to try to contact the server
``forever'' to complete the system call. This type of mount is appropriate
when processes on the client that access files in the file system do not
tolerate file I/O systems calls that return -1 with \fIerrno == EINTR\fR
and/or access to the file system is critical for normal system operation.
.lp
There are two other alternatives:
.ip 1)
A soft mount (\fB-s\fR option) retries an RPC \fIn\fR
times and then the corresponding
system call returns -1 with errno set to EINTR.
For TCP transport, the actual RPC request is not retransmitted, but the
timeout intervals waiting for a reply from the server are done
in the same manner as UDP for this purpose.
The problem with this type of mount is that most applications do not
expect an EINTR error return from file I/O system calls (since it never
occurs for a local file system) and get confused by the error return
from the I/O system call.
The option
\fB-x=\fInum\fR
is used to set the RPC retry limit and if set too low, the error returns
will start occurring whenever the NFS server is slow due to heavy load.
Alternately, a large retry limit can result in a process hung for a long
time, due to a crashed server or network partitioning.
.ip 2)
An interruptible mount (\fB-i\fR option) checks to see if a termination signal
is pending for the process when waiting for server response and if it is,
the I/O system call posts an EINTR. Normally this results in the process
being terminated by the signal when returning from the system call.
This feature allows you to ``^C'' out of processes that are hung
due to unresponsive servers.
The problem with this approach is that signals that are caught by
a process are not recognized as termination signals
and the process will remain hung.\**
.(f
\**Unfortunately, there are also some resource allocation situations in the
BSD kernel where the termination signal will be ignored and the process
will not terminate.
.)f
.sh 1 "RPC Transport Issues"
.pp
The NFS Version 2 protocol runs over UDP/IP transport by
sending each Sun Remote Procedure Call (RFC1057)
request/reply message in a single UDP
datagram. Since UDP does not guarantee datagram delivery, the
Remote Procedure Call (RPC) layer
times out and retransmits an RPC request if
no RPC reply has been received. Since this round trip timeout (RTO) value
is for the entire RPC operation, including RPC message transmission to the
server, queuing at the server for an nfsd, performing the RPC and
sending the RPC reply message back to the client, it can be highly variable
for even a moderately loaded NFS server.
As a result, the RTO interval must be a conservation (large) estimate, in
order to avoid extraneous RPC request retransmits.\**
.(f
\**At best, an extraneous RPC request retransmit increases
the load on the server and at worst can result in damaged files
on the server when non-idempotent RPCs are redone [Juszczak89].
.)f
Also, with an 8Kbyte read/write data size
(the default), the read/write reply/request will be an 8+Kbyte UDP datagram
that must normally be fragmented at the IP layer for transmission.\**
.(f
\**6 IP fragments for an Ethernet,
which has a maximum transmission unit of 1500bytes.
.)f
For IP fragments to be successfully reassembled into
the IP datagram at the receive end, all
fragments must be received within a fairly short ``time to live''.
If one fragment is lost/damaged in transit,
the entire RPC must be retransmitted and redone.
This problem can be exaggerated by a network interface on the receiver that
cannot handle the reception of back to back network packets. [Kent87a]
.pp
There are several tuning mount
options on the client side that can prove useful when trying to
alleviate performance problems related to UDP RPC transport.
The options
\fB-r=\fInum\fR
and
\fB-w=\fInum\fR
specify the maximum read or write data size respectively.
The size \fInum\fR
should be a power of 2 (4K, 2K, 1K) and adjusted downward from the
maximum of 8Kbytes
whenever IP fragmentation is causing problems. The best indicator of
IP fragmentation problems is a significant number of
\fIfragments dropped after timeout\fR
reported by the \fIip:\fR section of a \fBnetstat -s\fR
command on either the client or server.
Of course, if the fragments are being dropped at the server, it can be
fun figuring out which client(s) are involved.
The most likely candidates are clients that are not
on the same local area network as the
server or have network interfaces that do not receive several
back to back network packets properly.
.pp
By default, the 4.4BSD NFS client dynamically estimates the retransmit
timeout interval for the RPC and this appears to work reasonably well for
many environments. However, the
\fB-d\fR
flag can be specified to turn off
the dynamic estimation of retransmit timeout, so that the client will
use a static initial timeout interval.\**
.(f
\**After the first retransmit timeout, the initial interval is backed off
exponentially.
.)f
The
\fB-t=\fInum\fR
option can be used with
\fB-d\fR
to set the initial timeout interval to other than the default of 2 seconds.
The best indicator that dynamic estimation should be turned off would
be a significant number\** in the \fIX Replies\fR field and a
.(f
\**Even 0.1% of the total RPCs is probably significant.
.)f
large number in the \fIRetries\fR field
in the \fIRpc Info:\fR section as reported
by the \fBnfsstat\fR command.
On the server, there would be significant numbers of \fIInprog\fR recent
request cache hits in the \fIServer Cache Stats:\fR section as reported
by the \fBnfsstat\fR command, when run on the server.
.pp
The tradeoff is that a smaller timeout interval results in a better
average RPC response time, but increases the risk of extraneous retries
that in turn increase server load and the possibility of damaged files
on the server. It is probably best to err on the safe side and use a large
(>= 2sec) fixed timeout if the dynamic retransmit timeout estimation
seems to be causing problems.
.pp
An alternative to all this fiddling is to run NFS over TCP transport instead
of UDP.
Since the 4.4BSD TCP implementation provides reliable
delivery with congestion control, it avoids all of the above problems.
It also permits the use of read and write data sizes greater than the 8Kbyte
limit for UDP transport.\**
.(f
\**Read/write data sizes greater than 8Kbytes will not normally improve
performance unless the kernel constant MAXBSIZE is increased and the
file system on the server has a block size greater than 8Kbytes.
.)f
NFS over TCP usually delivers comparable to significantly better performance
than NFS over UDP
unless the client or server processor runs at less than 5-10MIPS. For a
slow processor, the extra CPU overhead of using TCP transport will become
significant and TCP transport may only be useful when the client
to server interconnect traverses congested gateways.
The main problem with using TCP transport is that it is only supported
between BSD clients and servers.\**
.(f
\**There are rumors of commercial NFS over TCP implementations on the horizon
and these may well be worth exploring.
.)f
.sh 1 "Other Tuning Tricks"
.pp
Another mount option that may improve performance over
certain network interconnects is \fB-a=\fInum\fR
which sets the number of blocks that the system will
attempt to read-ahead during sequential reading of a file. The default value
of 1 seems to be appropriate for most situations, but a larger value might
achieve better performance for some environments, such as a mount to a server
across a ``high bandwidth * round trip delay'' interconnect.
.pp
For the adventurous, playing with the size of the buffer cache
can also improve performance for some environments that use NFS heavily.
Under some workloads, a buffer cache of 4-6Mbytes can result in significant
performance improvements over 1-2Mbytes, both in client side system call
response time and reduced server RPC load.
The buffer cache size defaults to 10% of physical memory,
but this can be overridden by specifying the BUFPAGES option
in the machine's config file.\**
.(f
BUFPAGES is the number of physical machine pages allocated to the buffer cache.
ie. BUFPAGES * NBPG = buffer cache size in bytes
.)f
When increasing the size of BUFPAGES, it is also advisable to increase the
number of buffers NBUF by a corresponding amount.
Note that there is a tradeoff of memory allocated to the buffer cache versus
available for paging, which implies that making the buffer cache larger
will increase paging rate, with possibly disastrous results.
.sh 1 "Security Issues"
.pp
When a machine is running an NFS server it opens up a great big security hole.
For ordinary NFS, the server receives client credentials
in the RPC request as a user id
and a list of group ids and trusts them to be authentic!
The only tool available to restrict remote access to
file systems with is the exports(5) file,
so file systems should be exported with great care.
The exports file is read by mountd upon startup and after a hangup signal
is posted for it and then as much of the access specifications as possible are
pushed down into the kernel for use by the nfsd(s).
The trick here is that the kernel information is stored on a per
local file system mount point and client host address basis and cannot refer to
individual directories within the local server file system.
It is best to think of the exports file as referring to the various local
file systems and not just directory paths as mount points.
A local file system may be exported to a specific host, all hosts that
match a subnet mask or all other hosts (the world). The latter is very
dangerous and should only be used for public information. It is also
strongly recommended that file systems exported to ``the world'' be exported
read-only.
For each host or group of hosts, the file system can be exported read-only or
read/write.
You can also define one of three client user id to server credential
mappings to help control access.
Root (user id == 0) can be mapped to some default credentials while all other
user ids are accepted as given.
If the default credentials for user id equal zero
are root, then there is essentially no remapping.
Most NFS file systems are exported this way, most commonly mapping
user id == 0 to the credentials for the user nobody.
Since the client user id and group id list is used unchanged on the server
(except for root), this also implies that
the user id and group id space must be common between the client and server.
(ie. user id N on the client must refer to the same user on the server)
All user ids can be mapped to a default set of credentials, typically that of
the user nobody. This essentially gives world access to all
users on the corresponding hosts.
.pp
As well as the standard NFS Version 2 protocol (RFC1094) implementation, BSD
systems can use a variant of the protocol called Not Quite NFS (NQNFS) that
supports a variety of protocol extensions.
This protocol uses 64bit file offsets
and sizes, an \fIaccess rpc\fR, an \fIappend\fR option on the write rpc
and extended file attributes to support 4.4BSD file system functionality
more fully.
It also makes use of a variant of short term
\fIleases\fR [Gray89] with delayed write client caching,
in an effort to provide full cache consistency and better performance.
This protocol is available between 4.4BSD systems only and is used when
the \fB-q\fR mount option is specified.
It can be used with any of the aforementioned options for NFS, such as TCP
transport (\fB-T\fR).
Although this protocol is experimental, it is recommended over NFS for
mounts between 4.4BSD systems.\**
.(f
\**I would appreciate email from anyone who can provide
NFS vs. NQNFS performance measurements,
particularly fast clients, many clients or over an internetwork
connection with a large ``bandwidth * RTT'' product.
.)f
.sh 1 "Monitoring NFS Activity"
.pp
The basic command for monitoring NFS activity on clients and servers is
nfsstat. It reports cumulative statistics of various NFS activities,
such as counts of the various different RPCs and cache hit rates on the client
and server. Of particular interest on the server are the fields in the
\fIServer Cache Stats:\fR section, which gives numbers for RPC retries received
in the first three fields and total RPCs in the fourth. The first three fields
should remain a very small percentage of the total. If not, it
would indicate one or more clients doing retries too aggressively and the fix
would be to isolate these clients,
disable the dynamic RTO estimation on them and
make their initial timeout interval a conservative (ie. large) value.
.pp
On the client side, the fields in the \fIRpc Info:\fR section are of particular
interest, as they give an overall picture of NFS activity.
The \fITimedOut\fR field is the number of I/O system calls that returned -1
for ``soft'' mounts and can be reduced
by increasing the retry limit or changing
the mount type to ``intr'' or ``hard''.
The \fIInvalid\fR field is a count of trashed RPC replies that are received
and should remain zero.\**
.(f
\**Some NFS implementations run with UDP checksums disabled, so garbage RPC
messages can be received.
.)f
The \fIX Replies\fR field counts the number of repeated RPC replies received
from the server and is a clear indication of a too aggressive RTO estimate.
Unfortunately, a good NFS server implementation will use a ``recent request
cache'' [Juszczak89] that will suppress the extraneous replies.
A large value for \fIRetries\fR indicates a problem, but
it could be any of:
.ip \(bu
a too aggressive RTO estimate
.ip \(bu
an overloaded NFS server
.ip \(bu
IP fragments being dropped (gateway, client or server)
.lp
and requires further investigation.
The \fIRequests\fR field is the total count of RPCs done on all servers.
.pp
The \fBnetstat -s\fR comes in useful during investigation of RPC transport
problems.
The field \fIfragments dropped after timeout\fR in
the \fIip:\fR section indicates IP fragments are
being lost and a significant number of these occurring indicates that the
use of TCP transport or a smaller read/write data size is in order.
A significant number of \fIbad checksums\fR reported in the \fIudp:\fR
section would suggest network problems of a more generic sort.
(cabling, transceiver or network hardware interface problems or similar)
.pp
There is a RPC activity logging facility for both the client and
server side in the kernel.
When logging is enabled by setting the kernel variable nfsrtton to
one, the logs in the kernel structures nfsrtt (for the client side)
and nfsdrt (for the server side) are updated upon the completion
of each RPC in a circular manner.
The pos element of the structure is the index of the next element
of the log array to be updated.
In other words, elements of the log array from \fIlog\fR[pos] to
\fIlog\fR[pos - 1] are in chronological order.
The include file <sys/nfsrtt.h> should be consulted for details on the
fields in the two log structures.\**
.(f
\**Unfortunately, a monitoring tool that uses these logs is still in the
planning (dreaming) stage.
.)f
.sh 1 "Diskless Client Support"
.pp
The NFS client does include kernel support for diskless/dataless operation
where the root file system and optionally the swap area is remote NFS mounted.
A diskless/dataless client is configured using a version of the
``swapkernel.c'' file as provided in the directory \fIcontrib/diskless.nfs\fR.
If the swap device == NODEV, it specifies an NFS mounted swap area and should
be configured the same size as set up by diskless_setup when run on the server.
This file must be put in the \fIsys/compile/<machine_name>\fR kernel build
directory after the config command has been run, since config does
not know about specifying NFS root and swap areas.
The kernel variable mountroot must be set to nfs_mountroot instead of
ffs_mountroot and the kernel structure nfs_diskless must be filled in
properly.
There are some primitive system administration tools in the \fIcontrib/diskless.nfs\fR directory to assist in filling in
the nfs_diskless structure and in setting up an NFS server for
diskless/dataless clients.
The tools were designed to provide a bare bones capability, to allow maximum
flexibility when setting up different servers.
.lp
The tools are as follows:
.ip \(bu
diskless_offset.c - This little program reads a ``kernel'' object file and
writes the file byte offset of the nfs_diskless structure in it to
standard out. It was kept separate because it sometimes has to
be compiled/linked in funny ways depending on the client architecture.
(See the comment at the beginning of it.)
.ip \(bu
diskless_setup.c - This program is run on the server and sets up files for a
given client. It mostly just fills in an nfs_diskless structure and
writes it out to either the "kernel" file or a separate file called
/var/diskless/setup.<official-hostname>
.ip \(bu
diskless_boot.c - There are two functions in here that may be used
by a bootstrap server such as tftpd to permit sharing of the ``kernel''
object file for similar clients. This saves disk space on the bootstrap
server and simplify organization, but are not critical for correct operation.
They read the ``kernel''
file, but optionally fill in the nfs_diskless structure from a
separate "setup.<official-hostname>" file so that there is only
one copy of "kernel" for all similar (same arch etc.) clients.
These functions use a text file called
/var/diskless/boot.<official-hostname> to control the netboot.
.lp
The basic setup steps are:
.ip \(bu
make a "kernel" for the client(s) with mountroot() == nfs_mountroot()
and swdevt[0].sw_dev == NODEV if it is to do nfs swapping as well
(See the same swapkernel.c file)
.ip \(bu
run diskless_offset on the kernel file to find out the byte offset
of the nfs_diskless structure
.ip \(bu
Run diskless_setup on the server to set up the server and fill in the
nfs_diskless structure for that client.
The nfs_diskless structure can either be written into the
kernel file (the -x option) or
saved in /var/diskless/setup.<official-hostname>.
.ip \(bu
Set up the bootstrap server. If the nfs_diskless structure was written into
the ``kernel'' file, any vanilla bootstrap protocol such as bootp/tftp can
be used. If the bootstrap server has been modified to use the functions in
diskless_boot.c, then a
file called /var/diskless/boot.<official-hostname>
must be created.
It is simply a two line text file, where the first line is the pathname
of the correct ``kernel'' file and the second line has the pathname of
the nfs_diskless structure file and its byte offset in it.
For example:
.br
	/var/diskless/kernel.pmax
.br
	/var/diskless/setup.rickers.cis.uoguelph.ca 642308
.br
.ip \(bu
Create a /var subtree for each client in an appropriate place on the server,
such as /var/diskless/var/<client-hostname>/...
By using the <client-hostname> to differentiate /var for each host,
/etc/rc can be modified to mount the correct /var from the server.
