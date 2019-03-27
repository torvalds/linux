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
.\"	@(#)results.ms	6.2 (Berkeley) 4/16/91
.\"
.ds RH Results
.NH
Results
.PP
The following tables indicate the results of our
test runs.
Note that each table contains results for tests run
on two varieties of 4.2BSD file systems.
The first set of results is always for a file system
with a basic blocking factor of eight Kilobytes and a
fragment size of 1 Kilobyte. The second sets of measurements
are for file systems with a four Kilobyte block size and a
one Kilobyte fragment size.
The values in parenthesis indicate the percentage of CPU
time used by the test program.
In the case of the two disk arm tests, 
the value in parenthesis indicates the sum of the percentage
of the test programs that were run.
Entries of ``n. m.'' indicate this value was not measured.
.DS
.TS
box,center;
c s s s s
c s s s s
c s s s s
l | l s | l s
l | l s | l s
l | l l | l l
l | c c | c c.
4.2BSD File Systems Tests - \fBVAX 11/750\fR
=
Logically Sequential Transfers
from an \fB8K/1K\fR 4.2BSD File System (Kbytes/sec.)
_
Test	Emulex SC750/Eagle	UDA50/RA81

	1 Drive	2 Drives	1 Drive	2 Drives
_
read_8192	490 (69%)	620 (96%)	310 (44%)	520 (65%)
write_4096	380 (99%)	370 (99%)	370 (97%)	360 (98%)
write_8192	470 (99%)	470 (99%)	320 (71%)	410 (83%)
rewrite_8192	650 (99%)	620 (99%)	310 (50%)	450 (70%)
=
.T&
c s s s s
c s s s s
l | l s | l s
l | l s | l s
l | l l | l l
l | c c | c c.
Logically Sequential Transfers
from \fB4K/1K\fR 4.2BSD File System (Kbytes/sec.)
_
Test	Emulex SC750/Eagle	UDA50/RA81

	1 Drive	2 Drives	1 Drive	2 Drives
_
read_8192	300 (60%)	400 (84%)	210 (42%)	340 (77%)
write_4096	320 (98%)	320 (98%)	220 (67%)	290 (99%)
write_8192	340 (98%)	340 (99%)	220 (65%)	310 (98%)
rewrite_8192	450 (99%)	450 (98%)	230 (47%)	340 (78%)
.TE
.DE
.PP
Note that the rate of write operations on the VAX 11/750 are ultimately
CPU limited in some cases.
The write rates saturate the CPU at a lower bandwidth than the reads
because they must do disk allocation in addition to moving the data
from the user program to the disk.
The UDA50/RA81 saturates the CPU at a lower transfer rate for a given
operation than the SC750/Eagle because
it causes more memory contention with the CPU.
We do not know if this contention is caused by
the UNIBUS controller or the UDA50.
.PP
The following table reports the results of test runs on a VAX 11/780
with 4 Megabytes of main memory.
.DS
.TS
box,center;
c s s s s s s
c s s s s s s
c s s s s s s
l | l s | l s | l s
l | l s | l s | l s
l | l l | l l | l l
l | c c | c c | c c.
4.2BSD File Systems Tests - \fBVAX 11/780\fR
=
Logically Sequential Transfers
from an \fB8K/1K\fR 4.2BSD File System (Kbytes/sec.)
_
Test	Emulex SC780/Eagle	UDA50/RA81	Sys. Ind. 9900/Eagle

	1 Drive	2 Drives	1 Drive	2 Drives	1 Drive	2 Drives
_
read_8192	560 (70%)	480 (58%)	360 (45%)	540 (72%)	340 (41%)	520 (66%)
write_4096	440 (98%)	440 (98%)	380 (99%)	480 (96%)	490 (96%)	440 (84%)
write_8192	490 (98%)	490 (98%)	220 (58%)*	480 (92%)	490 (80%)	430 (72%)
rewrite_8192	760 (100%)	560 (72%)	220 (50%)*	180 (52%)*	490 (60%)	520 (62%)
=
.T&
c s s s s s s
c s s s s s s
l | l s | l s | l s
l | l s | l s | l s
l | l l | l l | l l
l | c c | c c | c c.
Logically Sequential Transfers
from an \fB4K/1K\fR 4.2BSD File System (Kbytes/sec.)
_
Test	Emulex SC780/Eagle	UDA50/RA81	Sys. Ind. 9900/Eagle

	1 Drive	2 Drives	1 Drive	2 Drives	1 Drive	2 Drives
_
read_8192	490 (77%)	370 (66%)	n.m.	n.m.	200 (31%)	370 (56%)
write_4096	380 (98%)	370 (98%)	n.m.	n.m.	200 (46%)	370 (88%)
write_8192	380 (99%)	370 (97%)	n.m.	n.m.	200 (45%)	320 (76%)
rewrite_8192	490 (87%)	350 (66%)	n.m.	n.m.	200 (31%)	300 (46%)
.TE
* the operation of the hardware was suspect during these tests.
.DE
.PP
The dropoff in reading and writing rates for the two drive SC780/Eagle
tests are probably due to the file system using insufficient
rotational delay for these tests.
We have not fully investigated these times.
.PP
The following table compares data rates on VAX 11/750s directly
with those of VAX 11/780s using the UDA50/RA81 storage system.
.DS
.TS
box,center;
c s s s s
c s s s s
c s s s s
l | l s | l s
l | l s | l s
l | l l | l l
l | c c | c c.
4.2BSD File Systems Tests - \fBDEC UDA50 - 750 vs. 780\fR
=
Logically Sequential Transfers
from an \fB8K/1K\fR 4.2BSD File System (Kbytes/sec.)
_
Test	VAX 11/750 UNIBUS	VAX 11/780 UNIBUS

	1 Drive	2 Drives	1 Drive	2 Drives
_
read_8192	310 (44%)	520 (84%)	360 (45%)	540 (72%)
write_4096	370 (97%)	360 (100%)	380 (99%)	480 (96%)
write_8192	320 (71%)	410 (96%)	220 (58%)*	480 (92%)
rewrite_8192	310 (50%)	450 (80%)	220 (50%)*	180 (52%)*
=
.T&
c s s s s
c s s s s
l | l s | l s
l | l s | l s
l | l l | l l
l | c c | c c.
Logically Sequential Transfers
from an \fB4K/1K\fR 4.2BSD File System (Kbytes/sec.)
_
Test	VAX 11/750 UNIBUS	VAX 11/780 UNIBUS

	1 Drive	2 Drives	1 Drive	2 Drives
_
read_8192	210 (42%)	342 (77%)	n.m.	n.m.
write_4096	215 (67%)	294 (99%)	n.m.	n.m.
write_8192	215 (65%)	305 (98%)	n.m.	n.m.
rewrite_8192	227 (47%)	336 (78%)	n.m.	n.m.
.TE
* the operation of the hardware was suspect during these tests.
.DE
.PP
The higher throughput available on VAX 11/780s is due to a number
of factors.
The larger main memory size allows a larger file system cache.
The block allocation routines run faster, raising the upper limit
on the data rates in writing new files.
.PP
The next table makes the same comparison using an Emulex controller
on both systems.
.DS
.TS
box, center;
c s s s s
c s s s s
c s s s s
l | l s | l s
l | l s | l s
l | l l | l l
l | c c | c c.
4.2BSD File Systems Tests - \fBEmulex - 750 vs. 780\fR
=
Logically Sequential Transfers
from an \fB8K/1K\fR 4.2BSD File System (Kbytes/sec.)
_
Test	VAX 11/750 CMI Bus	VAX 11/780 SBI Bus

	1 Drive	2 Drives	1 Drive	2 Drives
_
read_8192	490 (69%)	620 (96%)	560 (70%)	480 (58%)
write_4096	380 (99%)	370 (99%)	440 (98%)	440 (98%)
write_8192	470 (99%)	470 (99%)	490 (98%)	490 (98%)
rewrite_8192	650 (99%)	620 (99%)	760 (100%)	560 (72%)
=
.T&
c s s s s
c s s s s
l | l s | l s
l | l s | l s
l | l l | l l
l | c c | c c.
Logically Sequential Transfers
from an \fB4K/1K\fR 4.2BSD File System (Kbytes/sec.)
_
Test	VAX 11/750 CMI Bus	VAX 11/780 SBI Bus

	1 Drive	2 Drives	1 Drive	2 Drives
_
read_8192	300 (60%)	400 (84%)	490 (77%)	370 (66%)
write_4096	320 (98%)	320 (98%)	380 (98%)	370 (98%)
write_8192	340 (98%)	340 (99%)	380 (99%)	370 (97%)
rewrite_8192	450 (99%)	450 (98%)	490 (87%)	350 (66%)
.TE
.DE
.PP
The following table illustrates the evolution of our testing
process as both hardware and software problems effecting
the performance of the Emulex SC780 were corrected.
The software change was suggested to us by George Goble
of Purdue University.
.PP
The 4.2BSD handler for RH750/RH780 interfaced disk drives
contains several constants which to determine how
much time is provided between an interrupt signaling the completion
of a positioning command and the subsequent start of a data transfer
operation. These lead times are expressed as sectors of rotational delay.
If they are too small, an extra complete rotation will often be required
between a seek and subsequent read or write operation.
The higher bit rate and rotational speed of the 2351A Fujitsu
disk drives required
increasing these constants.
.PP
The hardware change involved allowing for slightly longer
delays in arbitrating for cycles on the SBI bus by 
starting the bus arbitration cycle a little further ahead of
when the data was ready for transfer.
Finally we had to increase the rotational delay between consecutive
blocks in the file because
the higher bandwidth from the disk generated more memory contention,
which slowed down the processor.
.DS
.TS
box,center,expand;
c s s s s s s
c s s s s s s
c s s s s s s
l | l s | l s | l s
l | l s | l s | l s
l | l s | l s | l s
l | c c | c c | c c
l | c c | c c | c c.
4.2BSD File Systems Tests - \fBEmulex SC780 Disk Controller Evolution\fR
=
Logically Sequential Transfers
from an \fB8K/1K\fR 4.2BSD File System (Kbytes/sec.)
_
Test	Inadequate Search Lead	OK Search Lead	OK Search Lead
	Initial SBI Arbitration	Init SBI Arb.	Improved SBI Arb.	

	1 Drive	2 Drives	1 Drive	2 Drives	1 Drive	2 Drives
_
read_8192	320	370	440 (60%)	n.m.	560 (70%)	480 (58%)
write_4096	250	270	300 (63%)	n.m.	440 (98%)	440 (98%)
write_8192	250	280	340 (60%)	n.m.	490 (98%)	490 (98%)
rewrite_8192	250	290	380 (48%)	n.m.	760 (100%)	560 (72%)
=
.T&
c s s s s s s
c s s s s s s
l | l s | l s | l s
l | l s | l s | l s
l | l s | l s | l s
l | c c | c c | c c
l | c c | c c | c c.
Logically Sequential Transfers
from an \fB4K/1K\fR 4.2BSD File System (Kbytes/sec.)
_
Test	Inadequate Search Lead	OK Search Lead	OK Search Lead
	Initial SBI Arbitration	Init SBI Arb.	Improved SBI Arb.	

	1 Drive	2 Drives	1 Drive	2 Drives	1 Drive	2 Drives
_
read_8192	200	220	280	n.m.	490 (77%)	370 (66%)
write_4096	180	190	300	n.m.	380 (98%)	370 (98%)
write_8192	180	200	320	n.m.	380 (99%)	370 (97%)
rewrite_8192	190	200	340	n.m.	490 (87%)	350 (66%)
.TE
.DE
.ds RH Conclusions
.bp
