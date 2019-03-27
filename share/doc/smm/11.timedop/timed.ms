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
.\"	@(#)timed.ms	8.1 (Berkeley) 6/8/93
.\"
.TL
Timed Installation and Operation Guide
.AU
Riccardo Gusella, Stefano Zatti, James M. Bloom
.AI
Computer Systems Research Group
Computer Science Division
Department of Electrical Engineering and Computer Science
University of California, Berkeley
Berkeley, CA 94720
.AU
Kirk Smith
.AI
Engineering Computer Network
Department of Electrical Engineering
Purdue University
West Lafayette, IN 47906
.FS
This work was sponsored by the Defense Advanced Research Projects Agency
(DoD), monitored by the Naval Electronics Systems
Command under contract No. N00039-84-C-0089, and by the CSELT 
Corporation of Italy.
The views and conclusions contained in this document are those of the
authors and should not be interpreted as representing official policies,
either expressed or implied, of the Defense Research Projects Agency,
of the US Government, or of CSELT.
.FE
.LP
.EH 'SMM:11-%''Timed Installation and Operation'
.OH 'Timed Installation and Operation''SMM:11-%'
.SH 
Introduction
.PP
The clock synchronization service for 
the UNIX 4.3BSD operating system is composed of a collection of
time daemons (\fItimed\fP) running on the machines in a local
area network.
The algorithms implemented by the service is based on a master-slave scheme.
The time daemons communicate with each other using the 
\fITime Synchronization Protocol\fP (TSP) which
is built on the DARPA UDP protocol and described in detail in [4].
.PP
A time daemon has a twofold function.
First, it supports the synchronization of the clocks 
of the various hosts in a local area network.
Second, it starts (or takes part in) the election that occurs
among slave time daemons when, for any reason, the master disappears.
The synchronization mechanism and the election procedure 
employed by the program \fItimed\fP are described 
in other documents [1,2,3].
The next paragraphs are a brief overview of how the time daemon works.
This document is mainly concerned with the administrative and technical
issues of running \fItimed\fP at a particular site.
.PP
A \fImaster time daemon\fP measures the time
differences between the clock of the machine on which it 
is running and those of all other machines.
The master computes the \fInetwork time\fP as the average of the 
times provided by nonfaulty clocks.\**
.FS
A clock is considered to be faulty when its value 
is more than a small specified
interval apart from the majority of the clocks 
of the other machines [1,2].
.FE
It then sends to each \fIslave time daemon\fP the
correction that should be performed on the clock of its machine.
This process is repeated periodically.
Since the correction is expressed as a time difference rather than an 
absolute time, transmission delays do not interfere with 
the accuracy of the synchronization.
When a machine comes up and joins the network,
it starts a slave time daemon which
will ask the master for the correct time and will reset the machine's clock
before any user activity can begin.
The time daemons are able to maintain a single network time in spite of 
the drift of clocks away from each other. 
The present implementation keeps processor clocks synchronized 
within 20 milliseconds.
.PP
To ensure that the service provided is continuous and reliable,
it is necessary to implement an election algorithm to elect a
new master should the machine running the current master crash, the master
terminate (for example, because of a run-time error), or 
the network be partitioned.
Under our algorithm, slaves are able to realize when the master has
stopped functioning and to elect a new master from among themselves.
It is important to note that, since the failure of the master results
only in a gradual divergence of clock values, the election
need not occur immediately.
.PP
The machines that are gateways between distinct local area
networks require particular care.
A time daemon on such machines may act as a \fIsubmaster\fP.
This artifact depends on the current inability of
transmission protocols to broadcast a message on a network
other than the one to which the broadcasting machine is connected.
The submaster appears as a slave on one network, and as a master
on one or more of the other networks to which it is connected.
.PP
A submaster classifies each network as one of three types.
A \fIslave network\fP is a network on which the submaster acts as a slave.
There can only be one slave network.
A \fImaster network\fP is a network on which the submaster acts as a master.
An \fIignored network\fP is any other network which already has a valid master.
The submaster tries periodically to become master on an ignored
network, but gives up immediately if a master already exists.
.SH
Guidelines
.PP
While the synchronization algorithm is quite general, the election
one, requiring a broadcast mechanism, puts constraints on
the kind of network on which time daemons can run.
The time daemon will only work on networks with broadcast capability
augmented with point-to-point links.
Machines that are only connected to point-to-point,
non-broadcast networks may not use the time daemon.
.PP
If we exclude submasters, there will normally be, at most, one master time 
daemon in a local area internetwork.
During an election, only one of the slave time daemons
will become the new master.
However, because of the characteristics of its machine,
a slave can be prevented from becoming the master.
Therefore, a subset of machines must be designated as potential 
master time daemons.
A master time daemon will require CPU resources
proportional to the number of slaves, in general, more than
a slave time daemon, so it may be advisable to limit master time
daemons to machines with more powerful processors or lighter loads.
Also, machines with inaccurate clocks should not be used as masters.
This is a purely administrative decision: an organization may
well allow all of its machines to run master time daemons.
.PP
At the administrative level, a time daemon on a machine
with multiple network interfaces, may be told to ignore all
but one network or to ignore one network.
This is done with the \fI\-n network\fP and \fI\-i network\fP
options respectively at start-up time.
Typically, the time daemon would be instructed to ignore all but
the networks belonging to the local administrative control.
.PP
There are some limitations to the current
implementation of the time daemon.
It is expected that these limitations will be removed in future releases.
The constant NHOSTS in /usr/src/etc/timed/globals.h limits the
maximum number of machines that may be directly controlled by one
master time daemon.
The current maximum is 29 (NHOSTS \- 1).
The constant  must be changed and the program recompiled if a site wishes to
run \fItimed\fP on a larger (inter)network.
.PP
In addition, there is a \fIpathological situation\fP to
be avoided at all costs, that might occur when
time daemons run on multiply-connected local area networks.
In this case, as we have seen, time daemons running on gateway machines
will be submasters and they will act on some of those 
networks as master time daemons.
Consider machines A and B that are both gateways between
networks X and Y.
If time daemons were started on both A and B without constraints, it would be
possible for submaster time daemon A to be a slave on network X
and the master on network Y, while submaster time daemon B is a slave on 
network Y and the master on network X.
This \fIloop\fP of master time daemons will not function properly
or guarantee a unique time on both networks, and will cause
the submasters to use large amounts of system resources in the form
of network bandwidth and CPU time.
In fact, this kind of \fIloop\fP can also be generated with more
than two master time daemons,
when several local area networks are interconnected.
.SH
Installation
.PP
In order to start the time daemon on a given machine,
the following lines should be
added to the \fIlocal daemons\fP section in the file \fI/etc/rc.local\fP:
.sp 2
.in 1i
.nf
if [ -f /etc/timed ]; then
	/etc/timed \fIflags\fP & echo -n ' timed' >/dev/console
fi
.fi
.in -1i
.sp
.LP
In any case, they must appear after the network 
is configured via ifconfig(8).
.PP
Also, the file \fI/etc/services\fP should contain the following
line:
.sp 2
.ti 1i
timed		525/udp		timeserver
.sp
.LP
The \fIflags\fP are:
.IP "-n network" 13
to consider the named network.
.IP "-i network"
to ignore the named network.
.IP -t
to place tracing information in \fI/usr/adm/timed.log\fP.
.IP -M
to allow this time daemon to become a master.
A time daemon run without this option will be forced in the state of
slave during an election.
.SH
Daily Operation
.PP
\fITimedc(8)\fP is used to control the operation of the time daemon.
It may be used to:
.IP \(bu
measure the differences between machines' clocks,
.IP \(bu
find the location where the master \fItimed\fP is running,
.IP \(bu
cause election timers on several machines to expire at the same time,
.IP \(bu
enable or  disable  tracing  of  messages  received  by \fItimed\fP.
.LP
See the manual page on \fItimed\fP\|(8) and \fItimedc\fP\|(8)
for more detailed information.
.PP
The \fIdate(1)\fP command can be used to set the network date.
In order to set the time on a single machine, the \fI-n\fP flag
can be given to date(1).
.bp
.SH
References
.IP 1.
R. Gusella and S. Zatti, 
\fITEMPO: A Network Time Controller for Distributed Berkeley UNIX System\fP,
USENIX Summer Conference Proceedings, Salt Lake City, June 1984.
.IP 2.
R. Gusella and S. Zatti, \fIClock Synchronization in a Local Area Network\fP,
University of California, Berkeley, Technical Report, \fIto appear\fP.
.IP 3.
R. Gusella and S. Zatti, 
\fIAn Election Algorithm for a Distributed Clock Synchronization Program\fP,
University of California, Berkeley, CS Technical Report #275, Dec. 1985.
.IP 4.
R. Gusella and S. Zatti,
\fIThe Berkeley UNIX 4.3BSD Time Synchronization Protocol\fP,
UNIX Programmer's Manual, 4.3 Berkeley Software Distribution, Volume 2c.
