.\" Copyright (c) 1996 FreeBSD Inc.
.\"
.\" Redistribution and use in source and binary forms, with or without
.\" modification, are permitted provided that the following conditions
.\" are met:
.\" 1. Redistributions of source code must retain the above copyright
.\"    notice, this list of conditions and the following disclaimer.
.\" 2. Redistributions in binary form must reproduce the above copyright
.\"    notice, this list of conditions and the following disclaimer in the
.\"    documentation and/or other materials provided with the distribution.
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
.\" $FreeBSD$
.\"
.OH '''Papers Contents'
.EH 'Papers Contents'''
.TL
UNIX Papers coming with FreeBSD
.PP
These papers are of both historic and current interest, but most of them are
many years old.
More recent documentation is available from
.>> <a href="https://www.freebsd.org/docs/">
https://www.FreeBSD.org/docs/
.>> </a>

.IP
.tl '\fBBerkeley Pascal''px\fP'
.if !r.U .nr .U 0
.if \n(.U \{\
.br
.>> <a href="px.html">px.html</a>
.\}
.QP
Berkeley Pascal
PX Implementation Notes
.br
Version 2.0
.sp
Performance Effects of Disk Subsystem Choices
for VAX\(dg Systems Running 4.2BSD UNIX. 
.sp
William N. Joy, M. Kirk McKusick.
.sp
Revised  January, 1979.

.sp
.IP
.tl '\fBDisk Performance''diskperf\fP'
.if \n(.U \{\
.br
.>> <a href="diskperf.html">diskperf.html</a>
.\}
.QP
Performance Effects of Disk Subsystem Choices
for VAX\(dg Systems Running 4.2BSD UNIX. 
.sp
Bob Kridle, Marshall Kirk McKusick. 
.sp
Revised July 27, 1983.

.sp
.IP
.tl '\fBTune the 4.2BSD Kernel''kerntune\fP'
.if \n(.U \{\
.br
.>> <a href="kerntune.html">kerntune.html</a>
.\}
.QP
Using gprof to Tune the 4.2BSD Kernel.
.sp
Marshall Kirk McKusick. 
.sp
Revised May 21, 1984 (?).

.sp
.IP
.tl '\fBNew Virtual Memory''newvm\fP'
.if \n(.U \{\
.br
.>> <a href="newvm.html">newvm.html</a>
.\}
.QP
A New Virtual Memory Implementation for Berkeley.
.sp
Marshall Kirk McKusick, Michael J. Karels.
.sp
Revised 1986.

.sp
.IP
.tl '\fBKernel Malloc''kernmalloc\fP'
.if \n(.U \{\
.br
.>> <a href="kernmalloc.html">kernmalloc.html</a>
.\}
.QP
Design of a General Purpose Memory Allocator for the 4.3BSD UNIX Kernel.
.sp
Marshall Kirk McKusick, Michael J. Karels.
.sp
Reprinted from:
\fIProceedings of the San Francisco USENIX Conference\fP,
pp. 295-303, June 1988.

.sp
.IP
.tl '\fBRelease Engineering''relengr\fP'
.if \n(.U \{\
.br
.>> <a href="releng.html">releng.html</a>
.\}
.QP
The Release Engineering of 4.3\s-1BSD\s0.
.sp
Marshall Kirk McKusick, Michael J. Karels, Keith Bostic.
.sp
Revised 1989.

.sp
.IP
.tl '\fBBeyond 4.3BSD''beyond4.3\fP'
.if \n(.U \{\
.br
.>> <a href="beyond43.html">beyond43.html</a>
.\}
.QP
Current Research by The Computer Systems Research Group of Berkeley.
.sp
Marshall Kirk McKusick, Michael J Karels, Keith Sklower, Kevin Fall, 
Marc Teitelbaum, Keith Bostic. 
.sp
Revised February 2, 1989.

.sp
.IP
.tl '\fBFilesystem Interface''fsinterface\fP'
.if \n(.U \{\
.br
.>> <a href="fsinterface.html">fsinterface.html</a>
.\}
.QP
Toward a Compatible Filesystem Interface.
.sp
Michael J. Karels, Marshall Kirk McKusick.
.sp
Conference of the European Users' Group, September 1986. 
Last modified April 16, 1991.

.sp
.IP
.tl '\fBSystem Performance''sysperf\fP'
.if \n(.U \{\
.br
.>> <a href="sysperf.html">sysperf.html</a>
.\}
.QP
Measuring and Improving the Performance of Berkeley UNIX.
.sp
Marshall Kirk McKusick, Samuel J. Leffler, Michael J. Karels.
.sp
Revised April 17, 1991.

.sp
.IP
.tl '\fBNot Quite NFS''nqnfs\fP'
.if \n(.U \{\
.br
.>> <a href="nqnfs.html">nqnfs.html</a>
.\}
.QP
Not Quite NFS, Soft Cache Consistency for NFS.
.sp
Rick Macklem.
.sp
Reprinted with permission from the "Proceedings of the Winter 1994 Usenix
Conference", January 1994, San Francisco.

.sp
.IP
.tl '\fBMalloc(3)''malloc\fP'
.if \n(.U \{\
.br
.>> <a href="malloc.html">malloc.html</a>
.\}
.QP
Malloc(3) in modern Virtual Memory environments.
.sp
Poul-Henning Kamp.
.sp
Revised April 5, 1996.

.sp
.IP
.tl '\fBJails: Confining the omnipotent root''jail\fP'
.if \n(.U \{\
.br
.>> <a href="jail.html">jail.html</a>
.\}
.QP
The jail system call sets up a jail and locks the current process in it.
.sp
Poul-Henning Kamp, Robert N. M. Watson.
.sp
This paper was presented at the 2nd International System Administration
and Networking Conference "SANE 2000" May 22-25, 2000 in Maastricht,
The Netherlands and is published in the proceedings.
