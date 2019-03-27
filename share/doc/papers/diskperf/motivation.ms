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
.\"	@(#)motivation.ms	6.2 (Berkeley) 4/16/91
.\"
.\"	$FreeBSD$
.\"
.ds RH Motivation
.NH
Motivation
.PP
These benchmarks were performed for several reasons.
Foremost was our desire to obtain guideline to aid
in choosing one the most expensive components of any
VAX UNIX configuration, the disk storage system.
The range of choices in this area has increased dramatically
in the last year.
DEC has become, with the introduction of the UDA50/RA81 system,
cost competitive
in the area of disk storage for the first time.
Emulex's entry into the VAX 11/780 SBI controller
field, the SC780, represented an important choice for us to examine, given
our previous success with their VAX 11/750 SC750 controller and
their UNIBUS controllers.
The Fujitsu 2351A
Winchester disk drive represents the lowest cost-per-byte disk storage
known to us.
In addition, Fujitsu's reputation for reliability was appealing.
The many attractive aspects of these components justified a more
careful examination of their performance aspects under UNIX.
.PP
In addition to the direct motivation of developing an effective
choice of storage systems, we hoped to gain more insight into
VAX UNIX file system and I/O performance in general.
What generic characteristics of I/O subsystems are most
important?
How important is the location of the controller on the SBI/CMI versus
the UNIBUS?
Is extensive buffering in the controller essential or even important?
How much can be gained by putting more of the storage system
management and optimization function in the controller as
DEC does with the UDA50?
.PP
We also wanted to resolve particular speculation about the value of
storage system optimization by a controller in a UNIX
environment.
Is the access optimization as effective as that already provided
by the existing 4.2BSD UNIX device handlers for traditional disks?
VMS disk handlers do no seek optimization.
This gives the UDA50 controller an advantage over other controllers
under VMS which is not likely to be as important to UNIX.
Are there penalties associated with greater intelligence in the controller?
.PP
A third and last reason for evaluating this equipment is comparable
to the proverbial mountain climbers answer when asked why he climbs
a particular mountain,
``It was there.''
In our case the equipment
was there.
We were lucky enough to assemble all the desired disks and controllers
and get them installed on a temporarily idle VAX 11/780.
This got us started collecting data.
Although many of the tests were later rerun on a variety of other systems,
this initial test bed was essential for working out the testing bugs
and getting our feet wet.
.ds RH Equipment
.bp
