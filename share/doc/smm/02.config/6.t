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
.\"	@(#)6.t	8.1 (Berkeley) 6/8/93
.\" $FreeBSD$
.\"
.\".ds RH "Adding New Devices
.ne 2i
.NH
ADDING NEW SYSTEM SOFTWARE
.PP
This section is not for the novice, it describes
some of the inner workings of the configuration process as
well as the pertinent parts of the system autoconfiguration process.
It is intended to give
those people who intend to install new device drivers and/or
other system facilities sufficient information to do so in the
manner which will allow others to easily share the changes.
.PP
This section is broken into four parts:
.IP \(bu 3
general guidelines to be followed in modifying system code,
.IP \(bu 3
how to add non-standard system facilities to 4.4BSD,
.IP \(bu 3
how to add a device driver to 4.4BSD, and
.NH 2
Modifying system code
.PP
If you wish to make site-specific modifications to the system
it is best to bracket them with
.DS
#ifdef SITENAME
\&...
#endif
.DE
to allow your source to be easily distributed to others, and
also to simplify \fIdiff\fP\|(1) listings.  If you choose not
to use a source code control system (e.g. SCCS, RCS), and
perhaps even if you do, it is
recommended that you save the old code with something
of the form:
.DS
#ifndef SITENAME
\&...
#endif
.DE
We try to isolate our site-dependent code in individual files
which may be configured with pseudo-device specifications.
.PP
Indicate machine-specific code with ``#ifdef vax'' (or other machine,
as appropriate).
4.4BSD underwent extensive work to make it extremely portable to
machines with similar architectures\- you may someday find
yourself trying to use a single copy of the source code on
multiple machines.
.NH 2
Adding non-standard system facilities
.PP
This section considers the work needed to augment 
.IR config 's
data base files for non-standard system facilities.
.I Config
uses a set of files that list the source modules that may be required
when building a system.
The data bases are taken from the directory in which
.I config
is run, normally /sys/conf.
Three such files may be used:
.IR files ,
.IR files .machine,
and
.IR files .ident.
The first is common to all systems,
the second contains files unique to a single machine type,
and the third is an optional list of modules for use on a specific machine.
This last file may override specifications in the first two.
The format of the 
.I files
file has grown somewhat complex over time.  Entries are normally of
the form
.IP
.nf
.DT
\fIdir/source.c\fP	\fItype\fP	 \fIoption-list\fP \fImodifiers\fP
.LP
for example,
.IP
.nf
.DT
\fIvaxuba/foo.c\fP	\fBoptional\fP	foo	\fBdevice-driver\fP
.LP
The
.I type
is one of
.B standard
or
.BR optional .
Files marked as standard are included in all system configurations.
Optional file specifications include a list of one or more system
options that together require the inclusion of this module.
The options in the list may be either names of devices that may
be in the configuration file,
or the names of system options that may be defined.
An optional file may be listed multiple times with different options;
if all of the options for any of the entries are satisfied,
the module is included.
.PP
If a file is specified as a
.IR device-driver ,
any special compilation options for device drivers will be invoked.
On the VAX this results in the use of the
.B \-i
option for the C optimizer.  This is required when pointer references
are made to memory locations in the VAX I/O address space.
.PP
Two other optional keywords modify the usage of the file.
.I Config
understands that certain files are used especially for
kernel profiling.  These files are indicated in the
.I files
files with a 
.I profiling-routine
keyword.  For example, the current profiling subroutines
are sequestered off in a separate file with the following
entry:
.IP
.nf
.DT
\fIsys/subr_mcount.c\fP	\fBoptional\fP	\fBprofiling-routine\fP
.fi
.LP
The 
.I profiling-routine
keyword forces
.I config
not to compile the source file with the 
.B \-pg
option.
.PP
The second keyword which can be of use is the
.I config-dependent
keyword.  This causes
.I config
to compile the indicated module with the global configuration
parameters.  This allows certain modules, such as
.I machdep.c
to size system data structures based on the maximum number
of users configured for the system.
.NH 2
Adding device drivers to 4.4BSD
.PP
The I/O system and
.I config
have been designed to easily allow new device support to be added.
The system source directories are organized as follows:
.DS
.TS
lw(1.0i) l.
/sys/h	machine independent include files
/sys/sys	machine-independent system source files
/sys/conf	site configuration files and basic templates
/sys/net	network-protocol-independent, but network-related code
/sys/netinet	DARPA Internet code
/sys/netimp	IMP support code
/sys/netns	Xerox NS code
/sys/vax	VAX-specific mainline code
/sys/vaxif	VAX network interface code
/sys/vaxmba	VAX MASSBUS device drivers and related code
/sys/vaxuba	VAX UNIBUS device drivers and related code
.TE
.DE
.PP
Existing block and character device drivers for the VAX 
reside in ``/sys/vax'', ``/sys/vaxmba'', and ``/sys/vaxuba''.  Network
interface drivers reside in ``/sys/vaxif''.  Any new device
drivers should be placed in the appropriate source code directory
and named so as not to conflict with existing devices.
Normally, definitions for things like device registers are placed in
a separate file in the same directory.  For example, the ``dh''
device driver is named ``dh.c'' and its associated include file is
named ``dhreg.h''.
.PP
Once the source for the device driver has been placed in a directory,
the file ``/sys/conf/files.machine'', and possibly
``/sys/conf/devices.machine'' should be modified.  The 
.I files
files in the conf directory contain a line for each C source or binary-only
file in the system.  Those files which are machine independent are
located in ``/sys/conf/files,'' while machine specific files
are in ``/sys/conf/files.machine.''  The ``devices.machine'' file
is used to map device names to major block device numbers.  If the device
driver being added provides support for a new disk
you will want to modify this file (the format is obvious).
.PP
In addition to including the driver in the
.I files
file, it must also be added to the device configuration tables.  These
are located in ``/sys/vax/conf.c'', or similar for machines other than
the VAX.  If you don't understand what to add to this file, you should
study an entry for an existing driver. 
Remember that the position in the
device table specifies the major device number.
The block major number is needed in the ``devices.machine'' file
if the device is a disk.
