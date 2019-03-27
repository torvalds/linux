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
.\"	@(#)abs.ms	6.2 (Berkeley) 4/16/91
.\"
.if n .ND
.TL
Performance Effects of Disk Subsystem Choices
for VAX\(dg Systems Running 4.2BSD UNIX*
.sp
Revised July 27, 1983
.AU
Bob Kridle
.AI
mt Xinu
2560 9th Street
Suite #312
Berkeley, California  94710
.AU
Marshall Kirk McKusick\(dd
.AI
Computer Systems Research Group
Computer Science Division
Department of Electrical Engineering and Computer Science
University of California, Berkeley
Berkeley, CA  94720
.AB
.FS
\(dgVAX, UNIBUS, and MASSBUS are trademarks of Digital Equipment Corporation.
.FE
.FS
* UNIX is a trademark of Bell Laboratories.
.FE
.FS
\(ddThis work was supported under grants from
the National Science Foundation under grant MCS80-05144,
and the Defense Advance Research Projects Agency (DoD) under
Arpa Order No. 4031 monitored by Naval Electronic System Command under
Contract No. N00039-82-C-0235.
.FE
Measurements were made of the UNIX file system
throughput for various I/O operations using the most attractive currently
available Winchester disks and controllers attached to both the
native busses (SBI/CMI) and the UNIBUS on both VAX 11/780s and VAX 11/750s.
The tests were designed to highlight the performance of single
and dual drive subsystems operating in the 4.2BSD
.I
fast file system
.R
environment.
Many of the results of the tests were initially counter-intuitive
and revealed several important aspects of the VAX implementations
which were surprising to us.
.PP
The hardware used included two  Fujitsu 2351A 
``Eagle''
disk drives on each of two foreign-vendor disk controllers
and two DEC RA-81 disk drives on a DEC UDA-50 disk controller.
The foreign-vendor controllers were Emulex SC750, SC780
and Systems Industries 9900 native bus interfaced controllers.
The DEC UDA-50 controller is a UNIBUS interfaced, heavily buffered
controller which is the first implementation of a new DEC storage
system architecture, DSA.
.PP
One of the most important results of our testing was the correction
of several timing parameters in our device handler for devices
with an RH750/RH780 type interface and having high burst transfer
rates.
The correction of these parameters resulted in an increase in
performance of over twenty percent in some cases.
In addition, one of the controller manufacturers altered their bus
arbitration scheme to produce another increase in throughput.
.AE
.LP
.de PT
.lt \\n(LLu
.pc %
.nr PN \\n%
.tl '\\*(LH'\\*(CH'\\*(RH'
.lt \\n(.lu
..
.af PN i
.ds LH Performance
.ds RH Contents
.bp 1
.\".if t .ds CF July 27, 1983
.\".if t .ds LF CSRG TR/8
.\".if t .ds RF Kridle, et. al.
.ce
.B "TABLE OF CONTENTS"
.LP
.sp 1
.nf
.B "1.  Motivation"
.LP
.sp .5v
.nf
.B "2.  Equipment
2.1.    DEC UDA50 disk controller
2.2.    Emulex SC750/SC780 disk controllers
2.3.    Systems Industries 9900 disk controller
2.4.    DEC RA81 disk drives
2.5.    Fujitsu 2351A disk drives
.LP
.sp .5v
.nf
.B "3.  Methodology
.LP
.sp .5v
.nf
.B "4.  Tests
.LP
.sp .5v
.nf
.B "5.  Results
.LP
.sp .5v
.nf
.B "6.  Conclusions
.LP
.sp .5v
.nf
.B Acknowledgements
.LP
.sp .5v
.nf
.B References
.LP
.sp .5v
.nf
.B "Appendix A
A.1.     read_8192
A.2.     write_4096
A.3.     write_8192
A.4.     rewrite_8192
.ds RH Motivation
.af PN 1
.bp 1
.de _d
.if t .ta .6i 2.1i 2.6i
.\" 2.94 went to 2.6, 3.64 to 3.30
.if n .ta .84i 2.6i 3.30i
..
.de _f
.if t .ta .5i 1.25i 2.5i
.\" 3.5i went to 3.8i
.if n .ta .7i 1.75i 3.8i
..
