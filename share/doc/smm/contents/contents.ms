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
.\"	@(#)00.contents	8.1 (Berkeley) 7/5/93
.\" $FreeBSD$
.\"
.OH '''SMM Contents'
.EH 'SMM Contents'''
.TL
UNIX System Manager's Manual (SMM)
.if !r.U .nr .U 0
.if \n(.U \{\
.br
.>> <a href="Title.html">Title.html</a>
.\}
.sp
\s-2 4.4 Berkeley Software Distribution\s+2
.sp
\fRJune, 1993\fR
.PP
This volume contains manual pages and supplementary documents useful to system
administrators.
The information in these documents applies to
the 4.4BSD system as distributed by U.C. Berkeley.
.SH
Reference Manual \- Section 8
.tl '''(8)'
.IP
Section 8 of the UNIX Programmer's Manual contains information related to
system operation, administration,  and maintenance.
.SH
System Installation and Administration
.IP
.tl 'Installing and Operating 4.4BSD''SMM:1'
.if \n(.U \{\
.br
.>> <a href="01.setup/paper.html">01.setup/paper.html</a>
.\}
.QP
The definitive reference document for those occasions when
you find you need to start over again.

.IP
.tl 'Building 4.4BSD Kernels with \fIConfig\fP''SMM:2'
.if \n(.U \{\
.br
.>> <a href="02.config/paper.html">02.config/paper.html</a>
.\}
.QP
In-depth discussions of the use and operation of the \fIconfig\fP
program, and how to build your very own Unix kernel.

.IP
.tl 'Fsck \- The UNIX File System Check Program''SMM:3'
.if \n(.U \{\
.br
.>> <a href="03.fsck/paper.html">03.fsck/paper.html</a>
.\}
.QP
A reference document for using the \fIfsck\fP program during
times of file system distress.

.IP
.tl 'Disc Quotas in a UNIX Environment''SMM:4'
.if \n(.U \{\
.br
.>> <a href="04.quotas/paper.html">04.quotas/paper.html</a>
.\}
.QP
A light introduction to the techniques
for limiting the use of disc resources.

.IP
.tl 'A Fast File System for UNIX''SMM:5'
.if \n(.U \{\
.br
.>> <a href="05.fastfs/paper.html">05.fastfs/paper.html</a>
.\}
.QP
A description of the 4.4BSD file system organization,
design and implementation.

.IP
.tl 'The 4.4BSD NFS Implementation''SMM:6'
.if \n(.U \{\
.br
.>> <a href="06.nfs/paper.html">06.nfs/paper.html</a>
.\}
.QP
An overview of the design, implementation, and use of NFS on 4.4BSD.

.IP
.tl 'Line Printer Spooler Manual''SMM:7'
.QP
This document describes the structure and installation procedure
for the line printer spooling system.  

.IP
.tl 'Sendmail Installation and Operation Guide''SMM:8'
.if \n(.U \{\
.br
.>> <a href="08.sendmailop/paper.html">08.sendmailop/paper.html</a>
.\}
.QP
The last word in installing and operating the \fIsendmail\fP program.

.IP
.tl 'Timed Installation and Operation Guide''SMM:11'
.if \n(.U \{\
.br
.>> <a href="11.timedop/paper.html">11.timedop/paper.html</a>
.\}
.QP
Describes how to maintain time synchronization between machines
in a local network.

.IP
.tl 'The Berkeley UNIX Time Synchronization Protocol''SMM:12'
.if \n(.U \{\
.br
.>> <a href="12.timed/paper.html">12.timed/paper.html</a>
.\}
.QP
The protocols and algorithms used by timed,
the network time synchronization daemon.

.IP
.tl 'AMD \- The 4.4BSD Automounter''SMM:13'
.QP
Automatically mounting file systems on demand.

.IP
.tl 'Installation and Operation of UUCP''SMM:14'
.QP
Describes the implementation of uucp; for the installer and administrator.

.IP
.tl 'A Dial\-Up Network of UNIX Systems''SMM:15'
.QP
Describes UUCP, a program for communicating files between UNIX systems.

.IP
.tl 'On the Security of UNIX''SMM:16'
.QP
Hints on how to break UNIX, and how to avoid your system being broken.

.IP
.tl 'Password Security \- A Case History''SMM:17'
.QP
How the bad guys used to be able to break the password algorithm, and why
they cannot now (at least not so easily).

.IP
.tl 'Networking Implementation Notes, 4.4BSD Edition''SMM:18'
.if \n(.U \{\
.br
.>> <a href="18.net/paper.html">18.net/paper.html</a>
.\}
.QP
A concise description of the system interfaces used within the
networking subsystem.

.IP
.tl 'The PERL Programming Language''SMM:19'
.QP
The Practical Extraction and Report Language is ideal for
writing those pesky adminitration scripts.
