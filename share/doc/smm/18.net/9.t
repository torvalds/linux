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
.\"	@(#)9.t	8.1 (Berkeley) 6/8/93
.\"
.nr H2 1
.\".ds RH "Protocol/network-interface
.br
.ne 2i
.NH
\s+2Protocol/network-interface interface\s0
.PP
The lowest layer in the set of protocols which comprise a
protocol family must interface itself to one or more network
interfaces in order to transmit and receive
packets.  It is assumed that
any routing decisions have been made before handing a packet
to a network interface, in fact this is absolutely necessary
in order to locate any interface at all (unless, of course,
one uses a single ``hardwired'' interface).  There are two
cases with which to be concerned, transmission of a packet
and receipt of a packet; each will be considered separately.
.NH 2
Packet transmission
.PP
Assuming a protocol has a handle on an interface, \fIifp\fP,
a (struct ifnet\ *),
it transmits a fully formatted packet with the following call,
.DS
error = (*ifp->if_output)(ifp, m, dst)
int error; struct ifnet *ifp; struct mbuf *m; struct sockaddr *dst;
.DE
The output routine for the network interface transmits the packet
\fIm\fP to the \fIdst\fP address, or returns an error indication
(a UNIX error number).  In reality transmission may
not be immediate or successful; normally the output
routine simply queues the packet on its send queue and primes
an interrupt driven routine to actually transmit the packet.
For unreliable media, such as the Ethernet, ``successful''
transmission simply means that the packet has been placed on the cable
without a collision.  On the other hand, an 1822 interface guarantees
proper delivery or an error indication for each message transmitted.
The model employed in the networking system attaches no promises
of delivery to the packets handed to a network interface, and thus
corresponds more closely to the Ethernet.  Errors returned by the
output routine are only those that can be detected immediately,
and are normally trivial in nature (no buffer space, 
address format not handled, etc.).
No indication is received if errors are detected after the call has returned.
.NH 2
Packet reception
.PP
Each protocol family must have one or more ``lowest level'' protocols.
These protocols deal with internetwork addressing and are responsible
for the delivery of incoming packets to the proper protocol processing
modules.  In the PUP model [Boggs78] these protocols are termed Level
1 protocols,
in the ISO model, network layer protocols.  In this system each such
protocol module has an input packet queue assigned to it.  Incoming
packets received by a network interface are queued for the protocol
module, and a VAX software interrupt is posted to initiate processing.  
.PP
Three macros are available for queuing and dequeuing packets:
.IP "IF_ENQUEUE(ifq, m)"
.br
This places the packet \fIm\fP at the tail of the queue \fIifq\fP.
.IP "IF_DEQUEUE(ifq, m)"
.br
This places a pointer to the packet at the head of queue \fIifq\fP 
in \fIm\fP
and removes the packet from the queue.
A zero value will be returned in \fIm\fP if the queue is empty.
.IP "IF_DEQUEUEIF(ifq, m, ifp)"
.br
Like IF_DEQUEUE, this removes the next packet from the head of a queue
and returns it in \fIm\fP.
A pointer to the interface on which the packet was received
is placed in \fIifp\fP, a (struct ifnet\ *).
.IP "IF_PREPEND(ifq, m)"
.br
This places the packet \fIm\fP at the head of the queue \fIifq\fP.
.PP
Each queue has a maximum length associated with it as a simple form
of congestion control.  The macro IF_QFULL(ifq) returns 1 if the queue
is filled, in which case the macro IF_DROP(ifq) should be used to
increment the count of the number of packets dropped, and the offending
packet is dropped.  For example, the following code fragment is commonly
found in a network interface's input routine,
.DS 
._f
if (IF_QFULL(inq)) {
	IF_DROP(inq);
	m_freem(m);
} else
	IF_ENQUEUE(inq, m);
.DE
