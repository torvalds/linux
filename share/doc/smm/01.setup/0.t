.\" Copyright (c) 1988, 1993 The Regents of the University of California.
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
.\"	@(#)0.t	8.1 (Berkeley) 7/27/93
.\" $FreeBSD$
.\"
.ds Ux \s-1UNIX\s0
.ds Bs \s-1BSD\s0
.\" Current version:
.ds 4B 4.4\*(Bs
.ds Ps 4.3\*(Bs
.\" tape and disk naming
.ds Mt mt
.ds Dk sd
.ds Dn disk
.ds Pa c
.\" block size used on the tape
.ds Bb 10240
.ds Bz 20
.\" document date
.ds Dy July 27, 1993
.de Sm
\s-1\\$1\s0\\$2
..
.de Pn		\" pathname
.ie n \fI\\$1\fP\\$2
.el \f(CW\\$1\fP\\$2
..
.de Li		\" literal
\f(CW\\$1\fP\\$2
..
.de I		\" italicize first arg
\fI\\$1\fP\^\\$2
..
.de Xr		\" manual reference
\fI\\$1\fP\^\\$2
..
.de Fn		\" function
\fI\\$1\fP\^()\\$2
..
.bd S B 3
.EH 'SMM:1-%''Installing and Operating \*(4B UNIX'
.OH 'Installing and Operating \*(4B UNIX''SMM:1-%'
.de Sh
.NH \\$1
\\$2
.nr PD .1v
.XS \\n%
.ta 0.6i
\\*(SN	\\$2
.XE
.nr PD .3v
..
.TL
Installing and Operating \*(4B UNIX
.br
\*(Dy
.AU
Marshall Kirk McKusick
.AU
Keith Bostic
.AU
Michael J. Karels
.AU
Samuel J. Leffler
.AI
Computer Systems Research Group
Department of Electrical Engineering and Computer Science
University of California, Berkeley
Berkeley, California  94720
(415) 642-7780
.AU
Mike Hibler
.AI
Center for Software Science
Department of Computer Science
University of Utah
Salt Lake City, Utah  84112
(801) 581-5017
.AB
.PP
This document contains instructions for the
installation and operation of the
\*(4B release of UNIX\**
as distributed by The University of California at Berkeley.
.FS
UNIX is a registered trademark of USL in the USA and some other countries.
.FE
.PP
It discusses procedures for installing UNIX on a new machine,
and for upgrading an existing \*(Ps UNIX system to the new release.
An explanation of how to lay out filesystems on available disks
and the space requirements for various parts of the system are given.
A brief overview of the major changes to
the system between \*(Ps and \*(4B are outlined.
An explanation of how to set up terminal lines and user accounts,
and how to do system-specific tailoring is provided.
A description of how to install and configure the \*(4B networking
facilities is included.
Finally, the document details system operation procedures:
shutdown and startup, filesystem backup procedures,
resource control, performance monitoring, and procedures for recompiling
and reinstalling system software.
.AE
.bp +3
