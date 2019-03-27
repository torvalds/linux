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
.\"	@(#)c.t	8.1 (Berkeley) 6/8/93
.\"
.\".ds RH "Sample Config Files
.bp
.LG
.B
.ce
APPENDIX C. SAMPLE CONFIGURATION FILES
.sp
.R
.NL
.PP
The following configuration files are developed in section 5;
they are included here for completeness.
.sp 2
.nf
.ta 1.5i 2.5i 4.0i
#
# ANSEL VAX (a picture perfect machine)
#
machine	vax
cpu	VAX780
timezone	8 dst
ident	ANSEL
maxusers	40

config	kernel	root on hp0
config	hpkernel	root on hp0 swap on hp0 and hp2
config	genkernel	swap generic

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
.bp
#
# UCBVAX - Gateway to the world
#
machine	vax
cpu	"VAX780"
cpu	"VAX750"
ident	UCBVAX
timezone	8 dst
maxusers	32
options	INET
options	NS

config	kernel	root on hp swap on hp and rk0 and rk1
config	upkernel	root on up
config	hkkernel	root on hk swap on rk0 and rk1

controller	mba0	at nexus ?
controller	uba0	at nexus ?
disk	hp0	at mba? drive 0
disk	hp1	at mba? drive 1
controller	sc0	at uba? csr 0176700	vector upintr
disk	up0	at sc0 drive 0
disk	up1	at sc0 drive 1
controller	hk0	at uba? csr 0177440	vector rkintr
disk	rk0	at hk0 drive 0
disk	rk1	at hk0 drive 1
pseudo-device	pty
pseudo-device	loop
pseudo-device	imp
device	acc0	at uba? csr 0167600	vector accrint accxint
pseudo-device	ether
device	ec0	at uba? csr 0164330	vector ecrint eccollide ecxint
device	il0	at uba? csr 0164000	vector ilrint ilcint
