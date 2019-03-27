.\" Copyright (c) 1986 The Regents of the University of California.
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
.\"	@(#)abstract.ms	5.2 (Berkeley) 4/16/91
.\"
.TL
Toward a Compatible Filesystem Interface
.AU
Michael J. Karels
Marshall Kirk McKusick
.AI
Computer Systems Research Group
Computer Science Division
Department of Electrical Engineering and Computer Science
University of California, Berkeley
Berkeley, California  94720
.LP
As network or remote filesystems have been implemented for
.UX ,
several stylized interfaces between the filesystem implementation
and the rest of the kernel have been developed.
Notable among these are Sun Microsystems' virtual filesystem interface
using vnodes, Digital Equipment's Generic File System architecture,
and AT&T's File System Switch.
Each design attempts to isolate filesystem-dependent details
below the generic interface and to provide a framework within which
new filesystems may be incorporated.
However, each of these interfaces is different from
and incompatible with the others.
Each of them addresses somewhat different design goals.
Each was based upon a different starting version of
.UX ,
targetted a different set of filesystems with varying characteristics,
and uses a different set of primitive operations provided by the filesystem.
The current study compares the various filesystem interfaces.
Criteria for comparison include generality, completeness, robustness,
efficiency and esthetics.
As a result of this comparison, a proposal for a new filesystem interface
is advanced that includes the best features of the existing implementations.
The proposal adopts the calling convention for name lookup introduced
in 4.3BSD.
A prototype implementation is described.
This proposal and the rationale underlying its development
have been presented to major software vendors
as an early step toward convergence upon a compatible filesystem interface.
