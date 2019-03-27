.\" Copyright (c) 1985 The Regents of the University of California.
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
.\"	@(#)5.t	5.1 (Berkeley) 4/17/91
.\"
.\" $FreeBSD$
.\"
.ds RH Functional Extensions
.NH
Functional Extensions
.PP
Some of the facilities introduced in 4.2BSD were not completely
implemented.  An important part of the effort that went into
4.3BSD was to clean up and unify both new and old facilities.
.NH 2
Kernel Extensions
.PP
A significant effort went into improving
the networking part of the kernel.
The work consisted of fixing bugs,
tuning the algorithms,
and revamping the lowest levels of the system
to better handle heterogeneous network topologies.
.NH 3
Subnets, Broadcasts and Gateways
.PP
To allow sites to expand their network in an autonomous
and orderly fashion, subnetworks have been introduced in 4.3BSD [GADS85].
This facility allows sites to subdivide their local Internet address
space into multiple subnetwork address spaces that are visible
only by hosts at that site.  To off-site hosts machines on a site's
subnetworks appear to reside on a single network.  The routing daemon
has been reworked to provide routing support in this type of
environment.
.PP
The default Internet broadcast address is now specified with a host part
of all one's, rather than all zero's.
The broadcast address may be set at boot time on a per-interface basis.
.NH 3
Interface Addressing
.PP
The organization of network interfaces has been
reworked to more cleanly support multiple
network protocols.  Network interfaces no longer
contain a host's address on that network; instead
each interface contains a pointer to a list of addresses
assigned to that interface.  This permits a single
interface to support, for example, Internet protocols
at the same time as XNS protocols.
.PP
The Address Resolution Protocol (ARP) support
for 10 megabyte/second Ethernet\(dg
.FS
\(dg Ethernet is a trademark of Xerox.
.FE
has been made more flexible by allowing hosts to
act as a ``clearing house'' for hosts that do
not support ARP.  In addition, system managers have
more control over the contents of the ARP translation
cache and may interactively interrogate and modify
the cache's contents.
.NH 3
User Control of Network Buffering
.PP
Although the system allocates reasonable default amounts of buffering
for most connections, certain operations such as file system dumps
to remote machines benefit from significant increases in buffering [Walsh84].
The \fIsetsockopt\fP system call has been extended to allow such requests.
In addition, \fIgetsockopt\fP and \fIsetsockopt\fP,
are now interfaced to the protocol level allowing protocol-specific
options to be manipulated by the user.
.NH 3
Number of File Descriptors
.PP
To allow full use of the many descriptor based services available,
the previous hard limit of 30 open files per process has been relaxed.
The changes entailed generalizing \fIselect\fP to handle arrays of
32-bit words, removing the dependency on file descriptors from
the page table entries,
and limiting most of the linear scans of a process's file table.
The default per-process descriptor limit was raised from 20 to 64,
though there are no longer any hard upper limits on the number
of file descriptors.
.NH 3
Kernel Limits
.PP
Many internal kernel configuration limits have been increased by suitable
modifications to data structures.
The limit on physical memory has been changed from 8 megabyte to 64 megabyte,
and the limit of 15 mounted file systems has been changed to 255.
The maximum file system size has been increased to 8 gigabyte,
number of processes to 65536,
and per process size to 64 megabyte of data and 64 megabyte of stack.
Note that these are upper bounds,
the default limits for these quantities are tuned for systems
with 4-8 megabyte of physical memory.
.NH 3
Memory Management
.PP
The global clock page replacement algorithm used to have a single
hand that was used both to mark and to reclaim memory.
The first time that it encountered a page it would clear its reference bit.
If the reference bit was still clear on its next pass across the page,
it would reclaim the page.
The use of a single hand does not work well with large physical
memories as the time to complete a single revolution of the hand
can take up to a minute or more.
By the time the hand gets around to the marked pages,
the information is usually no longer pertinent.
During periods of sudden shortages,
the page daemon will not be able to find any reclaimable pages until
it has completed a full revolution.
To alleviate this problem,
the clock hand has been split into two separate hands.
The front hand clears the reference bits,
the back hand follows a constant number of pages behind
reclaiming pages that still have cleared reference bits.
While the code has been written to allow the distance between
the hands to be varied, we have not found any algorithms
suitable for determining how to dynamically adjust this distance.
.PP
The configuration of the virtual memory system used to require
a significant understanding of its operation to do such
simple tasks as increasing the maximum process size.
This process has been significantly improved so that the most
common configuration parameters, such as the virtual memory sizes,
can be specified using a single option in the configuration file.
Standard configurations support data and stack segments
of 17, 33 and 64 megabytes.
.NH 3
Signals
.PP
The 4.2BSD signal implementation would push several words
onto the normal run-time stack before switching to an
alternate signal stack.
The 4.3BSD implementation has been corrected so that
the entire signal handler's state is now pushed onto the signal stack.
Another limitation in the original signal implementation was
that it used an undocumented system call to return from signals.
Users could not write their own return from exceptions;
4.3BSD formally specifies the \fIsigreturn\fP system call.
.PP
Many existing programs depend on interrupted system calls.
The restartable system call semantics of 4.2BSD signals caused
many of these programs to break.
To simplify porting of programs from inferior versions of
.UX
the \fIsigvec\fP system call has been extended so that
programmers may specify that system calls are not to be
restarted after particular signals.
.NH 3
System Logging
.PP
A system logging facility has been added
that sends kernel messages to the
syslog daemon for logging in /usr/adm/messages and possibly for
printing on the system console.
The revised scheme for logging messages
eliminates the time lag in updating the messages file,
unifies the format of kernel messages,
provides a finer granularity of control over the messages
that get printed on the console,
and eliminates the degradation in response during the printing of
low-priority kernel messages.
Recoverable system errors and common resource limitations are logged
using this facility.
Most system utilities such as init and login,
have been modified to log errors to syslog
rather than writing directly on the console.
.NH 3
Windows
.PP
The tty structure has been augmented to hold
information about the size
of an associated window or terminal.
These sizes can be obtained by programs such as editors that want
to know the size of the screen they are manipulating.
When these sizes are changed,
a new signal, SIGWINCH, is sent the current process group.
The editors have been modified to catch this signal and reshape
their view of the world, and the remote login program and server
now cooperate to propagate window sizes and window size changes
across a network.
Other programs and libraries such as curses that need the width
or height of the screen have been modified to use this facility as well.
.NH 3
Configuration of UNIBUS Devices
.PP
The UNIBUS configuration routines have been extended to allow auto-configuration
of dedicated UNIBUS memory held by devices.
The new routines simplify the configuration of memory-mapped devices
and correct problems occurring on reset of the UNIBUS.
.NH 3
Disk Recovery from Errors
.PP
The MASSBUS disk driver's error recovery routines have been fixed to
retry before correcting ECC errors, support ECC on bad-sector replacements,
and correctly attempt retries after earlier
corrective actions in the same transfer.
The error messages are more accurate.
.NH 2
Functional Extensions to Libraries and Utilities
.PP
Most of the changes to the utilities and libraries have been to
allow them to handle a more general set of problems,
or to handle the same set of problems more quickly.
.NH 3
Name Server
.PP
In 4.2BSD the name resolution routines (\fIgethostbyname\fP,
\fIgetservbyname\fP,
etc.) were implemented by a set of database files maintained on the
local machine.
Inconsistencies or obsolescence in these files resulted in inaccessibility of
hosts or services.
In 4.3BSD these files may be replaced by a network name server that can
insure a consistent view of the name space in a multimachine environment.
This name server operates in accordance with Internet standards
for service on the ARPANET [Mockapetris83].
.NH 3
System Management
.PP
A new utility, \fIrdist\fP,
has been provided to assist system managers in keeping
all their machines up to date with a consistent set of sources and binaries.
A master set of sources may reside on a single central machine,
or be distributed at (known) locations throughout the environment.
New versions of \fIgetty\fP, \fIinit\fP, and \fIlogin\fP
merge the functions of several
files into a single place, and allow more flexibility in the
startup of processes such as window managers.
.PP
The new utility \fItimed\fP keeps the time on a group of cooperating machines
(within a single LAN) synchronized to within 30 milliseconds.
It does its corrections using a new system call that changes
the rate of time advance without stopping or reversing the system clock.
It normally selects one machine to act as a master.
If the master dies or is partitioned, a new master is elected.
Other machines may participate in a purely slave role.
.NH 3
Routing
.PP
Many bugs in the routing daemon have been fixed;
it is considerably more robust,
and now understands how to properly deal with
subnets and point-to-point networks.
Its operation has been made more efficient by tuning with the use
of execution profiles, along with inline expansion of common operations
using the kernel's \fIinline\fP optimizer.
.NH 3
Compilers
.PP
The symbolic debugger \fIdbx\fP has had many new features added,
and all the known bugs fixed.  In addition \fIdbx\fP
has been extended to work with the Pascal compiler.
The fortran compiler \fIf77\fP has had numerous bugs fixed.
The C compiler has been modified so that it can, optionally,
generate single precision floating point instructions when operating
on single precision variables.
