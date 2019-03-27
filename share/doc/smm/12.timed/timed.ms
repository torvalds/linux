.\" $FreeBSD$
.\"
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
The Berkeley
.UX
.br
Time Synchronization Protocol
.AU
Riccardo Gusella, Stefano Zatti, and James M. Bloom
.AI
Computer Systems Research Group
Computer Science Division
Department of Electrical Engineering and Computer Science
University of California, Berkeley
Berkeley, CA 94720
.FS
This work was sponsored by the Defense Advanced Research Projects Agency
(DoD), monitored by the Naval Electronics Systems
Command under contract No. N00039-84-C-0089, and by the Italian CSELT
Corporation.
The views and conclusions contained in this document are those of the
authors and should not be interpreted as representing official policies,
either expressed or implied, of the Defense Research Projects Agency,
of the US Government, or of CSELT.
.FE
.LP
.OH 'The Berkeley UNIX Time Synchronization Protocol''SMM:12-%'
.EH 'SMM:12-%''The Berkeley UNIX Time Synchronization Protocol'
.SH
Introduction
.PP
The Time Synchronization Protocol (TSP)
has been designed for specific use by the program \fItimed\fP,
a local area network clock synchronizer for
the UNIX 4.3BSD operating
system.
Timed is built on the DARPA UDP protocol [4] and
is based on a master slave scheme.
.PP
TSP serves a dual purpose.
First, it supports messages for the synchronization of the clocks
of the various hosts in a local area network.
Second, it supports messages for the election that occurs
among slave time daemons when, for any reason, the master disappears.
The synchronization mechanism and the election procedure
employed by the program timed are described
in other documents [1,2,3].
.PP
Briefly, the synchronization software, which works in a
local area network, consists of a collection of \fItime daemons\fP
(one per machine) and is based on a master-slave
structure.
The present implementation keeps processor clocks synchronized
within 20 milliseconds.
A \fImaster time daemon\fP measures the time
difference between the clock of the machine on which it
is running and those of all other machines.  The current implementation
uses ICMP \fITime Stamp Requests\fP [5] to measure the clock difference
between machines.
The master computes the \fInetwork time\fP as the average of the
times provided by nonfaulty clocks.\**
.FS
A clock is considered to be faulty when its value
is more than a small specified
interval apart from the majority of the clocks
of the machines on the same network.
See [1,2] for more details.
.FE
It then sends to each \fIslave time daemon\fP the
correction that should be performed on the clock of its machine.
This process is repeated periodically.
Since the correction is expressed as a time difference rather than an
absolute time, transmission delays do not interfere with synchronization.
When a machine comes up and joins the network,
it starts a slave time daemon, which
will ask the master for the correct time and will reset the machine's clock
before any user activity can begin.
The time daemons therefore maintain a single network time in spite of
the drift of clocks away from each other.
.PP
Additionally, a time daemon on gateway machines may run as
a \fIsubmaster\fP.
A submaster time daemon functions as a slave on one network that
already has a master and as master on other networks.
In addition, a submaster is responsible for propagating broadcast
packets from one network to the other.
.PP
To ensure that service provided is continuous and reliable,
it is necessary to implement an election algorithm that will elect a
new master should the machine running the current master crash, the master
terminate (for example, because of a run-time error), or the network be
partitioned.
Under our algorithm, slaves are able to realize when the master has
stopped functioning and to elect a new master from among themselves.
It is important to note that since the failure of the master results
only in a gradual divergence of clock values, the election
need not occur immediately.
.PP
All the communication occurring among time daemons uses the TSP
protocol.
While some messages need not be sent in a reliable way,
most communication in TSP requires reliability not provided by the underlying
protocol.
Reliability is achieved by the use of acknowledgements, sequence numbers, and
retransmission when message losses occur.
When a message that requires acknowledgment is not acknowledged after
multiple attempts,
the time daemon that has sent the message will assume that the
addressee is down.
This document will not describe the details of how reliability is
implemented, but will only point out when
a message type requires a reliable transport mechanism.
.PP
The message format in TSP is the same for all message types;
however, in some instances, one or more fields are not used.
The next section describes the message format.
The following sections describe
in detail the different message types, their use and the contents
of each field.  NOTE:  The message format is likely to change in
future versions of timed.
.sp 2
.SH
Message Format
.PP
All fields are based upon 8-bit bytes.  Fields should be sent in
network byte order if they are more than one byte long.
The structure of a TSP message is the following:
.IP 1)
A one byte message type.
.IP 2)
A one byte version number, specifying the protocol version which the
message uses.
.IP 3)
A two byte sequence number to be used for recognizing duplicate messages
that occur when messages are retransmitted.
.IP 4)
Eight bytes of packet specific data.  This field contains two 4 byte time
values, a one byte hop count, or may be unused depending on the type
of the packet.
.IP 5)
A zero-terminated string of up to 256 \s-2ASCII\s+2 characters with the name of
the machine sending the message.
.PP
The following charts describe the message types,
show their fields, and explain their usages.
For the purpose of the following discussion, a time daemon can
be considered to be in
one of three states: slave, master, or candidate for election to master.
Also, the term \fIbroadcast\fP refers to
the sending of a message to all active time daemons.
.sp 1
.SH
Adjtime Message
.so time
.LP
Type: TSP_ADJTIME (1)
.sp 1
.PP
The master sends this message to a slave to communicate
the difference between
the clock of the slave and
the network time the master has just computed.
The slave will accordingly
adjust the time of its machine.
This message requires an acknowledgment.
.sp 1
.SH
Acknowledgment Message
.so unused
.LP
Type: TSP_ACK (2)
.sp 1
.PP
Both the master and the slaves use this message for
acknowledgment only.
It is used in several different contexts, for example
in reply to an Adjtime message.
.sp 1
.SH
Master Request Message
.so unused
.LP
Type: TSP_MASTERREQ (3)
.sp 1
.PP
A newly-started time daemon broadcasts this message to
locate a master.  No other action is implied by this packet.
It requires a Master Acknowledgment.
.sp 1
.SH
Master Acknowledgement
.so unused
.LP
Type: TSP_MASTERACK (4)
.sp 1
.PP
The master sends this message to acknowledge the Master Request message
and the Conflict Resolution Message.
.sp 1
.SH
Set Network Time Message
.so date
.LP
Type: TSP_SETTIME (5)
.sp 1
.PP
The master sends this message to slave time daemons to set their time.
This packet is sent to newly started time daemons and when the network
date is changed.
It contains the master's time as an approximation of the network time.
It requires an acknowledgment.
The next
synchronization round will eliminate the small time difference
caused by the random delay in the communication channel.
.sp 1
.SH
Master Active Message
.so unused
.LP
Type: TSP_MASTERUP (6)
.sp 1
.PP
The master broadcasts this message to
solicit the names of the active slaves.
Slaves will reply with a Slave Active message.
.sp 1
.SH
Slave Active Message
.so unused
.LP
Type: TSP_SLAVEUP (7)
.sp 1
.PP
A slave sends this message to the master in answer to a Master Active message.
This message is also sent when a new slave starts up to inform the master that
it wants to be synchronized.
.sp 1
.SH
Master Candidature Message
.so unused
.LP
Type: TSP_ELECTION (8)
.sp 1
.PP
A slave eligible to become a master broadcasts this message when its election
timer expires.
The message declares that the slave wishes to become the new master.
.sp 1
.SH
Candidature Acceptance Message
.so unused
.LP
Type: TSP_ACCEPT (9)
.sp 1
.PP
A slave sends this message to accept the candidature of the time daemon
that has broadcast an Election message.
The candidate will add the slave's name to the list of machines that it
will control should it become the master.
.sp 1
.SH
Candidature Rejection Message
.so unused
.LP
Type: TSP_REFUSE (10)
.sp 1
.PP
After a slave accepts the candidature of a time daemon, it will reply
to any election messages from other slaves
with this message.
This rejects any candidature other than the first received.
.sp 1
.SH
Multiple Master Notification Message
.so unused
.LP
Type: TSP_CONFLICT (11)
.sp 1
.PP
When two or more masters reply to a Master Request message, the slave
uses this message to inform one of them that more than one master exists.
.sp 1
.SH
Conflict Resolution Message
.so unused
.LP
Type: TSP_RESOLVE (12)
.sp 1
.PP
A master which has been informed of the existence of other masters
broadcasts this message to determine who the other masters are.
.sp 1
.SH
Quit Message
.so unused
.LP
Type: TSP_QUIT (13)
.sp 1
.PP
This message is sent by the master in three different contexts:
1) to a candidate that broadcasts a Master Candidature message,
2) to another master when notified of its existence,
3) to another master if a loop is detected.
In all cases, the recipient time daemon will become a slave.
This message requires an acknowledgement.
.sp 1
.SH
Set Date Message
.so date
.LP
Type: TSP_SETDATE (22)
.sp 1
.PP
The program \fIdate\fP\|(1) sends this message to the local time daemon
when a super-user wants to set the network date.
If the local time daemon is the master, it will set the date;
if it is a slave, it will communicate the desired date to the master.
.sp 1
.SH
Set Date Request Message
.so date
.LP
Type: TSP_SETDATEREQ (23)
.sp 1
.PP
A slave that has received a Set Date message will communicate the
desired date to the master using this message.
.sp 1
.SH
Set Date Acknowledgment Message
.so unused
.LP
Type: TSP_DATEACK (16)
.sp 1
.PP
The master sends this message to a slave in acknowledgment of a
Set Date Request Message.
The same message is sent by the local time daemon to the program
\fIdate(1)\fP to confirm that the network date has been set by the
master.
.sp 1
.SH
Start Tracing Message
.so unused
.LP
Type: TSP_TRACEON (17)
.sp 1
.PP
The controlling program \fItimedc\fP sends this message to the local
time daemon to start the recording in a system file of
all messages received.
.sp 1
.SH
Stop Tracing Message
.so unused
.LP
Type: TSP_TRACEOFF (18)
.sp 1
.PP
\fITimedc\fP sends this message to the local
time daemon to stop the recording of
messages received.
.sp 1
.SH
Master Site Message
.so unused
.LP
Type: TSP_MSITE (19)
.sp 1
.PP
\fITimedc\fP sends this message to the local time daemon to find out
where the master is running.
.sp 1
.SH
Remote Master Site Message
.so unused
.LP
Type: TSP_MSITEREQ (20)
.sp 1
.PP
A local time daemon broadcasts this message to find the location
of the master.
It then uses the Acknowledgement message to
communicate this location to \fItimedc\fP.
.sp 1
.SH
Test Message
.so unused
.LP
Type: TSP_TEST (21)
.sp 1
.PP
For testing purposes, \fItimedc\fP sends this message to a slave
to cause its election timer to expire.  NOTE: \fItimed\fP
is not normally compiled to support this.
.sp 1
.SH
.SH
Loop Detection Message
.so loop
.LP
Type: TSP_LOOP (24)
.sp 1
.PP
This packet is initiated by all masters occasionally to attempt to detect loops.
All submasters forward this packet onto the networks over which they are master.
If a master receives a packet it sent out initially,
it knows that a loop exists and tries to correct the problem.
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
Postel, J., \fIUser Datagram Protocol\fP, RFC 768.
Network Information Center, SRI International, Menlo Park, California,
August 1980.
.IP 5.
Postel, J., \fIInternet Control Message Protocol\fP, RFC 792.
Network Information Center, SRI International, Menlo Park, California,
September 1981.
