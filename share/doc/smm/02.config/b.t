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
.\"	@(#)b.t	8.1 (Berkeley) 6/8/93
.\"
.\".ds RH "Device Defaulting Rules
.bp
.LG
.B
.ce
APPENDIX B. RULES FOR DEFAULTING SYSTEM DEVICES
.sp
.R
.NL
.PP
When \fIconfig\fP processes a ``config'' rule which does
not fully specify the location of the root file system,
paging area(s), device for system dumps, and device for
argument list processing it applies a set of rules to
define those values left unspecified.  The following list
of rules are used in defaulting system devices.
.IP 1) 3
If a root device is not specified, the swap
specification must indicate a ``generic'' system is to be built.
.IP 2) 3
If the root device does not specify a unit number, it
defaults to unit 0.
.IP 3) 3
If the root device does not include a partition specification,
it defaults to the ``a'' partition.
.IP 4) 3
If no swap area is specified, it defaults to the ``b''
partition of the root device.
.IP 5) 3
If no device is specified for processing argument lists, the
first swap partition is selected.
.IP 6) 3
If no device is chosen for system dumps, the first swap
partition is selected (see below to find out where dumps are
placed within the partition).
.PP
The following table summarizes the default partitions selected
when a device specification is incomplete, e.g. ``hp0''.
.DS
.TS
l l.
Type	Partition
_
root	``a''
swap	``b''
args	``b''
dumps	``b''
.TE
.DE
.SH
Multiple swap/paging areas
.PP
When multiple swap partitions are specified, the system treats the
first specified as a ``primary'' swap area which is always used.
The remaining partitions are then interleaved into the paging
system at the time a
.IR swapon (2)
system call is made.  This is normally done at boot time with
a call to
.IR swapon (8)
from the /etc/rc file.
.SH
System dumps
.PP
System dumps are automatically taken after a system crash,
provided the device driver for the ``dumps'' device supports
this.  The dump contains the contents of memory, but not
the swap areas.  Normally the dump device is a disk in
which case the information is copied to a location at the
back of the partition.  The dump is placed in the back of the
partition because the primary swap and dump device are commonly
the same device and this allows the system to be rebooted without
immediately overwriting the saved information.  When a dump has
occurred, the system variable \fIdumpsize\fP 
is set to a non-zero value indicating the size (in bytes) of
the dump.  The \fIsavecore\fP\|(8)
program then copies the information from the dump partition to
a file in a ``crash'' directory and also makes a copy of the
system which was running at the time of the crash (usually
``/kernel'').  The offset to the system dump is defined in the
system variable \fIdumplo\fP (a sector offset from
the front of the dump partition). The 
.I savecore
program operates by reading the contents of \fIdumplo\fP, \fIdumpdev\fP,
and \fIdumpmagic\fP from /dev/kmem, then comparing the value
of \fIdumpmagic\fP read from /dev/kmem to that located in
corresponding location in the dump area of the dump partition.
If a match is found, 
.I savecore
assumes a crash occurred and reads \fIdumpsize\fP from the dump area
of the dump partition.  This value is then used in copying the
system dump.  Refer to 
\fIsavecore\fP\|(8)
for more information about its operation.
.PP
The value \fIdumplo\fP is calculated to be 
.DS
\fIdumpdev-size\fP \- \fImemsize\fP
.DE
where \fIdumpdev-size\fP is the size of the disk partition
where system dumps are to be placed, and
\fImemsize\fP is the size of physical memory.
If the disk partition is not large enough to hold a full
dump, \fIdumplo\fP is set to 0 (the start of the partition).
