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
.\"	@(#)0.t	8.1 (Berkeley) 6/8/93
.\"
.EQ
delim $$
.EN
.if n .ND
.TL
A Fast File System for UNIX*
.EH 'SMM:05-%''A Fast File System for \s-2UNIX\s+2'
.OH 'A Fast File System for \s-2UNIX\s+2''SMM:05-%'
.AU
Marshall Kirk McKusick, William N. Joy\(dg,
Samuel J. Leffler\(dd, Robert S. Fabry
.AI
Computer Systems Research Group
Computer Science Division
Department of Electrical Engineering and Computer Science
University of California, Berkeley
Berkeley, CA  94720
.AB
.FS
* UNIX is a trademark of Bell Laboratories.
.FE
.FS
\(dg William N. Joy is currently employed by:
Sun Microsystems, Inc, 2550 Garcia Avenue, Mountain View, CA 94043
.FE
.FS
\(dd Samuel J. Leffler is currently employed by:
Lucasfilm Ltd., PO Box 2009, San Rafael, CA 94912
.FE
.FS
This work was done under grants from
the National Science Foundation under grant MCS80-05144,
and the Defense Advance Research Projects Agency (DoD) under
ARPA Order No. 4031 monitored by Naval Electronic System Command under
Contract No. N00039-82-C-0235.
.FE
A reimplementation of the UNIX file system is described.
The reimplementation provides substantially higher throughput
rates by using more flexible allocation policies
that allow better locality of reference and can
be adapted to a wide range of peripheral and processor characteristics.
The new file system clusters data that is sequentially accessed
and provides two block sizes to allow fast access to large files
while not wasting large amounts of space for small files.
File access rates of up to ten times faster than the traditional
UNIX file system are experienced.
Long needed enhancements to the programmers'
interface are discussed.
These include a mechanism to place advisory locks on files, 
extensions of the name space across file systems,
the ability to use long file names,
and provisions for administrative control of resource usage.
.sp
.LP
Revised February 18, 1984
.AE
.LP
.sp 2
CR Categories and Subject Descriptors:
D.4.3
.B "[Operating Systems]":
File Systems Management \-
.I "file organization, directory structures, access methods";
D.4.2
.B "[Operating Systems]":
Storage Management \-
.I "allocation/deallocation strategies, secondary storage devices";
D.4.8
.B "[Operating Systems]":
Performance \-
.I "measurements, operational analysis";
H.3.2
.B "[Information Systems]":
Information Storage \-
.I "file organization"
.sp
Additional Keywords and Phrases:
UNIX,
file system organization,
file system performance,
file system design,
application program interface.
.sp
General Terms:
file system,
measurement,
performance.
.bp 
.ce
.B "TABLE OF CONTENTS"
.LP
.sp 1
.nf
.B "1.  Introduction"
.LP
.sp .5v
.nf
.B "2.  Old file system
.LP
.sp .5v
.nf
.B "3.  New file system organization
3.1.    Optimizing storage utilization
3.2.    File system parameterization
3.3.    Layout policies
.LP
.sp .5v
.nf
.B "4.  Performance
.LP
.sp .5v
.nf
.B "5.  File system functional enhancements
5.1.     Long file names
5.2.     File locking
5.3.     Symbolic links
5.4.     Rename
5.5.     Quotas
.LP
.sp .5v
.nf
.B Acknowledgements
.LP
.sp .5v
.nf
.B References
