.\" Copyright (c) 1983 The Regents of the University of California.
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
.\"	@(#)equip.ms	6.2 (Berkeley) 4/16/91
.\"
.ds RH Equipment
.NH
Equipment
.PP
Various combinations of the three manufacturers disk controllers,
and two pairs of Winchester disk drives were tested on both
VAX 11/780 and VAX 11/750 CPUs. The Emulex and Systems Industries
disk controllers were interfaced to Fujitsu 2351A 
``Eagle''
404 Megabyte disk drives.
The DEC UDA50 disk controller was interfaced to two DEC RA81 
456 Megabyte Winchester disk drives.
All three controllers were tested on the VAX 780 although
only the Emulex and DEC controllers were benchmarked on the VAX 11/750.
Systems Industries makes a VAX 11/750 CMI interface for
their controller, but we did not have time to test this device.
In addition, not all the storage systems were tested for
two drive throughput.
Each of the controllers and disk drives used in the benchmarks
is described briefly below.
.NH 2
DEC UDA50 disk controller
.PP
This is a new controller design which is part of a larger, long range
storage architecture referred to as
``DSA''
or \fBD\fRigital \fBS\fRtorage \fBA\fRrchetecture.
An important aspect of DSA is migrating a large part
of the storage management previously handled in the operating
system to the storage system. Thus, the UDA50 is a much more
intelligent controller than previous interfaces like the RH750 or
RH780.
The UDA50 handles all error correction.
It also deals with most of the physical storage parameters.
Typically, system software requests a logical block or
sequence of blocks.
The physical locations of these blocks, 
their head, track, and cylinder indices,
are determined by the controller.
The UDA50 also orders disk requests to maximize throughput
where possible, minimizing total seek and rotational delays.
Where multiple drives are attached to a single controller,
the UDA50 can interleave
simultaneous
data transfers from multiple drives.
.PP
The UDA50 is a UNIBUS implementation of a DSA controller.
It contains 52 sectors of internal buffering to minimize
the effects of a slow UNIBUS such as the one on the VAX-11/780.
This buffering also minimizes the effects of contention with
other UNIBUS peripherals.
.NH 2
Emulex SC750/SC780 disk controllers
.PP
These two models of the same controller interface to the CMI bus
of a VAX 11/750 and the SBI bus of a 11/VAX 780, respectively.
To the operating system, they emulate either an RH750 or
and RH780.
The controllers install in the
MASSBUS
locations in the CPU cabinets and operate from the
VAX power suplies.
They provide an
``SMD''
or \fBS\fRtorage \fBM\fRodule \fBD\fRrive
interface to the disk drives.
Although a large number of disk drives use this interface, we tested
the controller exclusively connected to Fujitsu 2351A disks.
.PP
The controller ws first implemented for the VAX-11/750 as the SC750
model several years ago. Although the SC780 was introduced more
recently, both are stable products with no bugs known to us.
.NH 2
System Industries 9900 disk controller
.PP
This controller is an evolution of the S.I. 9400 first introduced 
as a UNIBUS SMD interface.
The 9900 has been enhanced to include an interface to the VAX 11/780 native
bus, the SBI.
It has also been upgraded to operate with higher data rate drives such
as the Fujitsu 2351As we used in this test.
The controller is contained in its own rack-mounted drawer with an integral
power supply.
The interface to the SMD is a four module set which mounts in a
CPU cabinet slot normally occupied by an RH780.
The SBI interface derives power from the VAX CPU cabinet power
supplies.
.NH 2
DEC RA81 disk drives
.PP
The RA81 is a rack-mountable 456 Megabyte (formatted) Winchester
disk drive manufactured by DEC.
It includes a great deal of technology which is an integral part
of the DEC \fBDSA\fR scheme.
The novel technology includes a serial packet based communications
protocol with the controller over a pair of mini-coaxial cables.
The physical characteristics of the RA81 are shown in the
table below:
.DS
.TS
box,center;
c s
l l.
DEC RA81 Disk Drive Characteristics
_
Peak Transfer Rate	2.2 Mbytes/sec.
Rotational Speed	3,600 RPM
Data Sectors/Track	51
Logical Cylinders	1,248
Logical Data Heads	14
Data Capacity	456 Mbytes
Minimum Seek Time	6 milliseconds
Average Seek Time	28 milliseconds
Maximum Seek Time	52 milliseconds
.TE
.DE
.NH 2
Fujitsu 2351A disk drives
.PP
The Fujitsu 2351A disk drive is a Winchester disk drive
with an SMD controller interface.
Fujitsu has developed a very good reputation for
reliable storage products over the last several years.
The 2351A has the following physical characteristics:
.DS
.TS
box,center;
c s
l l.
Fujitsu 2351A Disk Drive Characteristics
_
Peak Transfer Rate	1.859 Mbytes/sec.
Rotational Speed	3,961 RPM
Data Sectors/Track	48
Cylinders	842
Data Heads	20
Data Capacity	404 Mbytes
Minimum Seek Time	5 milliseconds
Average Seek Time	18 milliseconds
Maximum Seek Time	35 milliseconds
.TE
.DE
.ds RH Methodology
.bp
