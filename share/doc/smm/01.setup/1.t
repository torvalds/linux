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
.\"	@(#)1.t	8.1 (Berkeley) 7/27/93
.\"
.ds lq ``
.ds rq ''
.ds LH "Installing/Operating \*(4B
.ds RH Introduction
.ds CF \*(Dy
.LP
.bp
.Sh 1 "Introduction"
.PP
This document explains how to install the \*(4B Berkeley
version of UNIX on your system.
The filesystem format is compatible with \*(Ps
and it will only be necessary for you to do a full bootstrap
procedure if you are installing the release on a new machine.
The object file formats are completely different from the System
V release, so the most straightforward procedure for upgrading
a System V system is to do a full bootstrap.
.PP
The full bootstrap procedure
is outlined in section 2; the process starts with copying a filesystem
image onto a new disk.
This filesystem is then booted and used to extract the remainder of the
system binaries and sources from the archives on the tape(s).
.PP
The technique for upgrading a \*(Ps system is described
in section 3 of this document.
The upgrade procedure involves extracting system binaries
onto new root and
.Pn /usr
filesystems and merging local
configuration files into the new system.
User filesystems may be upgraded in place.
Most \*(Ps binaries may be used with \*(4B in the course
of the conversion.
It is desirable to recompile local sources after the conversion,
as the new compiler (GCC) provides superior code optimization.
Consult section 3.5 for a description of some of the differences
between \*(Ps and \*(4B.
.Sh 2 "Distribution format"
.PP
The distribution comes in two formats:
.DS
(3)\0\0 6250bpi 2400' 9-track magnetic tapes, or
(1)\0\0 8mm Exabyte tape
.DE
.PP
If you have the facilities, we \fBstrongly\fP recommend copying the
magnetic tape(s) in the distribution kit to guard against disaster.
The tapes contain \*(Bb-byte records.
There are interspersed tape marks;
end-of-tape is signaled by a double end-of-file.
The first file on the tape is architecture dependent.
Additional files on the tape(s)
contain tape archive images of the system binaries and sources (see
.Xr tar (1)\**).
.FS
References of the form \fIX\fP(Y) mean the entry named
\fIX\fP in section Y of the ``UNIX Programmer's Manual''.
.FE
See the tape label for a description of the contents
and format of each individual tape.
.Sh 2 "UNIX device naming"
.PP
Device names have a different syntax depending on whether you are talking
to the standalone system or a running UNIX kernel.
The standalone system syntax is currently architecture dependent and is
described in the various architecture specific sections as applicable.
When not running standalone, devices are available via files in the
.Pn /dev/
directory.
The file name typically encodes the device type, its logical unit and
a partition within that unit.
For example,
.Pn /dev/sd2b
refers to the second partition (``b'') of
SCSI (``sd'') drive number ``2'', while
.Pn /dev/rmt0
refers to the raw (``r'') interface of 9-track tape (``mt'') unit ``0''.
.PP
The mapping of physical addressing information (e.g. controller, target)
to a logical unit number is dependent on the system configuration.
In all simple cases, where only a single controller is present, a drive
with physical unit number 0 (e.g., as determined by its unit
specification, either unit plug or other selection mechanism)
will be called unit 0 in its UNIX file name.
This is not, however, strictly
necessary, since the system has a level of indirection in this naming.
If there are multiple controllers, the disk unit numbers will normally
be counted sequentially across controllers.  This can be taken
advantage of to make the system less dependent on the interconnect
topology, and to make reconfiguration after hardware failure easier.
.PP
Each UNIX physical disk is divided into at most 8 logical disk partitions,
each of which may occupy any consecutive cylinder range on the physical
device.  The cylinders occupied by the 8 partitions for each drive type
are specified initially in the disk description file
.Pn /etc/disktab
(c.f.
.Xr disktab (5)).
The partition information and description of the
drive geometry are written in one of the first sectors of each disk with the
.Xr disklabel (8)
program.  Each partition may be used for either a
raw data area such as a paging area or to store a UNIX filesystem.
It is conventional for the first partition on a disk to be used
to store a root filesystem, from which UNIX may be bootstrapped.
The second partition is traditionally used as a paging area, and the
rest of the disk is divided into spaces for additional ``mounted
filesystems'' by use of one or more additional partitions.
.Sh 2 "UNIX devices: block and raw"
.PP
UNIX makes a distinction between ``block'' and ``raw'' (character)
devices.  Each disk has a block device interface where
the system makes the device byte addressable and you can write
a single byte in the middle of the disk.  The system will read
out the data from the disk sector, insert the byte you gave it
and put the modified data back.  The disks with the names
.Pn /dev/xx0[a-h] ,
etc., are block devices.
There are also raw devices available.
These have names like
.Pn /dev/rxx0[a-h] ,
the ``r'' here standing for ``raw''.
Raw devices bypass the buffer cache and use DMA directly to/from
the program's I/O buffers;
they are normally restricted to full-sector transfers.
In the bootstrap procedures we
will often suggest using the raw devices, because these tend
to work faster.
Raw devices are used when making new filesystems,
when checking unmounted filesystems,
or for copying quiescent filesystems.
The block devices are used to mount filesystems.
.PP
You should be aware that it is sometimes important whether to use
the character device (for efficiency) or not (because it would not
work, e.g. to write a single byte in the middle of a sector).
Do not change the instructions by using the wrong type of device
indiscriminately.
