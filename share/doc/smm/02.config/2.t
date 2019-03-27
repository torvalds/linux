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
.\"	@(#)2.t	8.1 (Berkeley) 6/8/93
.\"
.\".ds RH "Configuration File Contents
.ne 2i
.NH
CONFIGURATION FILE CONTENTS
.PP
A system configuration must include at least the following
pieces of information:
.IP \(bu 3
machine type
.IP \(bu 3
cpu type
.IP \(bu 3
system identification
.IP \(bu 3
timezone
.IP \(bu 3
maximum number of users
.IP \(bu 3
location of the root file system
.IP \(bu 3
available hardware
.PP
.I Config
allows multiple system images to be generated from a single
configuration description.  Each system image is configured
for identical hardware, but may have different locations for the root
file system and, possibly, other system devices.
.NH 2
Machine type
.PP
The 
.I "machine type"
indicates if the system is going to operate on a DEC VAX-11\(dg computer,
.FS
\(dg DEC, VAX, UNIBUS, MASSBUS and MicroVAX are trademarks of Digital
Equipment Corporation.
.FE
or some other machine on which 4.4BSD operates.  The machine type
is used to locate certain data files which are machine specific, and
also to select rules used in constructing the resultant
configuration files.
.NH 2
Cpu type
.PP
The
.I "cpu type"
indicates which, of possibly many, cpu's the system is to operate on.
For example, if the system is being configured for a VAX-11, it could
be running on a VAX 8600, VAX-11/780, VAX-11/750, VAX-11/730 or MicroVAX II.
(Other VAX cpu types, including the 8650, 785 and 725, are configured using
the cpu designation for compatible machines introduced earlier.)
Specifying
more than one cpu type implies that the system should be configured to run
on any of the cpu's specified.  For some types of machines this is not
possible and 
.I config
will print a diagnostic indicating such.
.NH 2
System identification
.PP
The
.I "system identification"
is a moniker attached to the system, and often the machine on which the
system is to run.  For example, at Berkeley we have machines named Ernie
(Co-VAX), Kim (No-VAX), and so on.  The system identifier selected is used to
create a global C ``#define'' which may be used to isolate system dependent
pieces of code in the kernel.  For example, Ernie's Varian driver used
to be special cased because its interrupt vectors were wired together.  The
code in the driver which understood how to handle this non-standard hardware
configuration was conditionally compiled in only if the system
was for Ernie.  
.PP
The system identifier ``GENERIC'' is given to a system which
will run on any cpu of a particular machine type; it should not
otherwise be used for a system identifier.
.NH 2
Timezone
.PP
The timezone in which the system is to run is used to define the
information returned by the \fIgettimeofday\fP\|(2)
system call.  This value is specified as the number of hours east
or west of GMT.  Negative numbers indicate a value east of GMT.
The timezone specification may also indicate the
type of daylight savings time rules to be applied.
.NH 2
Maximum number of users
.PP
The system allocates many system data structures at boot time
based on the maximum number of users the system will support.
This number is normally between 8 and 40, depending
on the hardware and expected job mix.  The rules
used to calculate system data structures are discussed in
Appendix D.
.NH 2
Root file system location
.PP
When the system boots it must know the location of
the root of the file system
tree.  This location and the part(s) of the disk(s) to be used
for paging and swapping must be specified in order to create
a complete configuration description.  
.I Config
uses many rules to calculate default locations for these items;
these are described in Appendix B.
.PP
When a generic system is configured, the root file system is left
undefined until the system is booted.  In this case, the root file
system need not be specified, only that the system is a generic system.
.NH 2
Hardware devices
.PP
When the system boots it goes through an
.I autoconfiguration
phase.  During this period, the system searches for all
those hardware devices
which the system builder has indicated might be present.  This probing
sequence requires certain pieces of information such as register
addresses, bus interconnects, etc.  A system's hardware may be configured
in a very flexible manner or be specified without any flexibility
whatsoever.  Most people do not configure hardware devices into the
system unless they are currently present on the machine, expect
them to be present in the near future, or are simply guarding
against a hardware
failure somewhere else at the site (it is often wise to configure in
extra disks in case an emergency requires moving one off a machine which
has hardware problems).
.PP
The specification of hardware devices usually occupies the majority of
the configuration file.  As such, a large portion of this document will
be spent understanding it.  Section 6.3 contains a description of
the autoconfiguration process, as it applies to those planning to
write, or modify existing, device drivers.
.NH 2
Pseudo devices
.PP
Several system facilities are configured in a manner like that used
for hardware devices although they are not associated with specific hardware.
These system options are configured as
.IR pseudo-devices .
Some pseudo devices allow an optional parameter that sets the limit
on the number of instances of the device that are active simultaneously.
.NH 2
System options
.PP
Other than the mandatory pieces of information described above, it
is also possible to include various optional system facilities
or to modify system behavior and/or limits.
For example, 4.4BSD can be configured to support binary compatibility for
programs built under 4.3BSD.  Also, optional support is provided
for disk quotas and tracing the performance of the virtual memory
subsystem.  Any optional facilities to be configured into
the system are specified in the configuration file.  The resultant
files generated by
.I config
will automatically include the necessary pieces of the system.
