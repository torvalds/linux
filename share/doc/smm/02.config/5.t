.\" Copyright (c) 1983, 1993
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
.\"	@(#)5.t	8.1 (Berkeley) 6/8/93
.\"
.\".ds RH "Sample Configuration Files
.ne 2i
.NH
SAMPLE CONFIGURATION FILES
.PP
In this section we will consider how to configure a
sample VAX-11/780 system on which the hardware can be
reconfigured to guard against various hardware mishaps.
We then study the rules needed to configure a VAX-11/750
to run in a networking environment.
.NH 2
VAX-11/780 System
.PP
Our VAX-11/780 is configured with hardware
recommended in the document ``Hints on Configuring a VAX for 4.2BSD''
(this is one of the high-end configurations).
Table 1 lists the pertinent hardware to be configured.
.DS B
.TS
box;
l | l | l | l | l
l | l | l | l | l.
Item	Vendor	Connection	Name	Reference
_
cpu	DEC		VAX780
MASSBUS controller	Emulex	nexus ?	mba0	hp(4)
disk	Fujitsu	mba0	hp0
disk	Fujitsu	mba0	hp1
MASSBUS controller	Emulex	nexus ?	mba1
disk	Fujitsu	mba1	hp2
disk	Fujitsu	mba1	hp3
UNIBUS adapter	DEC	nexus ?
tape controller	Emulex	uba0	tm0	tm(4)
tape drive	Kennedy	tm0	te0
tape drive	Kennedy	tm0	te1
terminal multiplexor	Emulex	uba0	dh0	dh(4)
terminal multiplexor	Emulex	uba0	dh1
terminal multiplexor	Emulex	uba0	dh2
.TE
.DE
.ce
Table 1.  VAX-11/780 Hardware support.
.LP
We will call this machine ANSEL and construct a configuration
file one step at a time.
.PP
The first step is to fill in the global configuration parameters.
The machine is a VAX, so the
.I "machine type"
is ``vax''.  We will assume this system will
run only on this one processor, so the 
.I "cpu type"
is ``VAX780''.  The options are empty since this is going to
be a ``vanilla'' VAX.  The system identifier, as mentioned before,
is ``ANSEL,'' and the maximum number of users we plan to support is
about 40.  Thus the beginning of the configuration file looks like
this:
.DS
.ta 1.5i 2.5i 4.0i
#
# ANSEL VAX (a picture perfect machine)
#
machine	vax
cpu	VAX780
timezone	8 dst
ident	ANSEL
maxusers	40
.DE
.PP
To this we must then add the specifications for three
system images.  The first will be our standard system with the
root on ``hp0'' and swapping on the same drive as the root.
The second will have the root file system in the same location,
but swap space interleaved among drives on each controller.
Finally, the third will be a generic system,
to allow us to boot off any of the four disk drives.
.DS
.ta 1.5i 2.5i
config	kernel	root on hp0
config	hpkernel	root on hp0 swap on hp0 and hp2
config	genkernel	swap generic
.DE
.PP
Finally, the hardware must be specified.  Let us first just try
transcribing the information from Table 1.
.DS
.ta 1.5i 2.5i 4.0i
controller	mba0	at nexus ?
disk	hp0	at mba0 disk 0
disk	hp1	at mba0 disk 1
controller	mba1	at nexus ?
disk	hp2	at mba1 disk 2
disk	hp3	at mba1 disk 3
controller	uba0	at nexus ?
controller	tm0	at uba0 csr 0172520	vector tmintr
tape	te0	at tm0 drive 0
tape	te1	at tm0 drive 1
device	dh0	at uba0 csr 0160020	vector dhrint dhxint
device	dm0	at uba0 csr 0170500	vector dmintr
device	dh1	at uba0 csr 0160040	vector dhrint dhxint
device	dh2	at uba0 csr 0160060	vector dhrint dhxint
.DE
.LP
(Oh, I forgot to mention one panel of the terminal multiplexor
has modem control, thus the ``dm0'' device.)
.PP
This will suffice, but leaves us with little flexibility.  Suppose
our first disk controller were to break.  We would like to recable the
drives normally on the second controller so that all our disks could
still be used without reconfiguring the system.  To do this we wildcard
the MASSBUS adapter connections and also the slave numbers.  Further,
we wildcard the UNIBUS adapter connections in case we decide some time
in the future to purchase another adapter to offload the single UNIBUS
we currently have.  The revised device specifications would then be:
.DS
.ta 1.5i 2.5i 4.0i
controller	mba0	at nexus ?
disk	hp0	at mba? disk ?
disk	hp1	at mba? disk ?
controller	mba1	at nexus ?
disk	hp2	at mba? disk ?
disk	hp3	at mba? disk ?
controller	uba0	at nexus ?
controller	tm0	at uba? csr 0172520	vector tmintr
tape	te0	at tm0 drive 0
tape	te1	at tm0 drive 1
device	dh0	at uba? csr 0160020	vector dhrint dhxint
device	dm0	at uba? csr 0170500	vector dmintr
device	dh1	at uba? csr 0160040	vector dhrint dhxint
device	dh2	at uba? csr 0160060	vector dhrint dhxint
.DE
.LP
The completed configuration file for ANSEL is shown in Appendix C.
.NH 2
VAX-11/750 with network support
.PP
Our VAX-11/750 system will be located on two 10Mb/s Ethernet
local area networks and also the DARPA Internet.  The system
will have a MASSBUS drive for the root file system and two
UNIBUS drives.  Paging is interleaved among all three drives.
We have sold our standard DEC terminal multiplexors since this
machine will be accessed solely through the network.  This
machine is not intended to have a large user community, it
does not have a great deal of memory.  First the global parameters:
.DS
.ta 1.5i 2.5i 4.0i
#
# UCBVAX (Gateway to the world)
#
machine	vax
cpu	"VAX780"
cpu	"VAX750"
ident	UCBVAX
timezone	8 dst
maxusers	32
options	INET
options	NS
.DE
.PP
The multiple cpu types allow us to replace UCBVAX with a
more powerful cpu without reconfiguring the system.  The
value of 32 given for the maximum number of users is done to
force the system data structures to be over-allocated.  That
is desirable on this machine because, while it is not expected
to support many users, it is expected to perform a great deal
of work.
The ``INET'' indicates that we plan to use the
DARPA standard Internet protocols on this machine,
and ``NS'' also includes support for Xerox NS protocols.
Note that unlike 4.2BSD configuration files,
the network protocol options do not require corresponding pseudo devices.
.PP
The system images and disks are configured next.
.DS
.ta 1.5i 2.5i 4.0i
config	kernel	root on hp swap on hp and rk0 and rk1
config 	upkernel	root on up
config 	hkkernel	root on hk swap on rk0 and rk1

controller	mba0	at nexus ?
controller	uba0	at nexus ?
disk	hp0	at mba? drive 0
disk	hp1	at mba? drive 1
controller	sc0	at uba? csr 0176700	vector upintr
disk	up0	at sc0 drive 0
disk	up1	at sc0 drive 1
controller	hk0	at uba? csr 0177440 	vector rkintr
disk	rk0	at hk0 drive 0
disk	rk1	at hk0 drive 1
.DE
.PP
UCBVAX requires heavy interleaving of its paging area to keep up
with all the mail traffic it handles.  The limiting factor on this
system's performance is usually the number of disk arms, as opposed
to memory or cpu cycles.  The extra UNIBUS controller, ``sc0'',
is in case the MASSBUS controller breaks and a spare controller
must be installed (most of our old UNIBUS controllers have been
replaced with the newer MASSBUS controllers, so we have a number
of these around as spares).
.PP
Finally, we add in the network devices.
Pseudo terminals are needed to allow users to
log in across the network (remember the only hardwired terminal
is the console).
The software loopback device is used for on-machine communications.
The connection to the Internet is through
an IMP, this requires yet another
.I pseudo-device
(in addition to the actual hardware device used by the
IMP software).  And, finally, there are the two Ethernet devices.
These use a special protocol, the Address Resolution Protocol (ARP),
to map between Internet and Ethernet addresses.  Thus, yet another
.I pseudo-device
is needed.  The additional device specifications are show below.
.DS
.ta 1.5i 2.5i 4.0i
pseudo-device	pty
pseudo-device	loop
pseudo-device	imp
device	acc0	at uba? csr 0167600	vector accrint accxint
pseudo-device	ether
device	ec0	at uba? csr 0164330	vector ecrint eccollide ecxint
device	il0	at uba? csr 0164000	vector ilrint ilcint
.DE
.LP
The completed configuration file for UCBVAX is shown in Appendix C.
.NH 2
Miscellaneous comments
.PP
It should be noted in these examples that neither system was
configured to use disk quotas or the 4.1BSD compatibility mode.
To use these optional facilities, and others, we would probably
clean out our current configuration, reconfigure the system, then
recompile and relink the system image(s).  This could, of course,
be avoided by figuring out which relocatable object files are 
affected by the reconfiguration, then reconfiguring and recompiling
only those files affected by the configuration change.  This technique
should be used carefully.
