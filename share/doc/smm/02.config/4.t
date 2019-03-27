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
.\"	@(#)4.t	8.1 (Berkeley) 6/8/93
.\"
.\".ds RH "Configuration File Syntax
.ne 2i
.NH
CONFIGURATION FILE SYNTAX
.PP
In this section we consider the specific rules used in writing
a configuration file.  A complete grammar for the input language
can be found in Appendix A and may be of use if you should have
problems with syntax errors.
.PP
A configuration file is broken up into three logical pieces:
.IP \(bu 3
configuration parameters global to all system images 
specified in the configuration file,
.IP \(bu 3
parameters specific to each
system image to be generated, and
.IP \(bu 3
device specifications.
.NH 2
Global configuration parameters
.PP
The global configuration parameters are the type of machine,
cpu types, options, timezone, system identifier, and maximum users.
Each is specified with a separate line in the configuration file.
.IP "\fBmachine\fP \fItype\fP"
.br
The system is to run on the machine type specified.  No more than
one machine type can appear in the configuration file.  Legal values
are
.B vax
and
\fBsun\fP.
.IP "\fBcpu\fP ``\fItype\fP''"
.br
This system is to run on the cpu type specified.
More than one cpu type specification
can appear in a configuration file.
Legal types for a
.B vax
machine are
\fBVAX8600\fP, \fBVAX780\fP, \fBVAX750\fP,
\fBVAX730\fP
and
\fBVAX630\fP (MicroVAX II).
The 8650 is listed as an 8600, the 785 as a 780, and a 725 as a 730.
.IP "\fBoptions\fP \fIoptionlist\fP"
.br
Compile the listed optional code into the system. 
Options in this list are separated by commas.
Possible options are listed at the top of the generic makefile.
A line of the form ``options FUNNY,HAHA'' generates global ``#define''s
\-DFUNNY \-DHAHA in the resultant makefile.
An option may be given a value by following its name with ``\fB=\fP'',
then the value enclosed in (double) quotes.
The following are major options are currently in use:
COMPAT (include code for compatibility with 4.1BSD binaries),
INET (Internet communication protocols),
NS (Xerox NS communication protocols),
and
QUOTA (enable disk quotas).
Other kernel options controlling system sizes and limits
are listed in Appendix D;
options for the network are found in Appendix E.
There are additional options which are associated with certain
peripheral devices; those are listed in the Synopsis section
of the manual page for the device.
.IP "\fBmakeoptions\fP \fIoptionlist\fP"
.br
Options that are used within the system makefile
and evaluated by
.I make
are listed as
.IR makeoptions .
Options are listed with their values with the form
``makeoptions name=value,name2=value2.''
The values must be enclosed in double quotes if they include numerals
or begin with a dash.
.IP "\fBtimezone\fP \fInumber\fP [ \fBdst\fP [ \fInumber\fP ] ]"
.br
Specifies the timezone used by the system.  This is measured in the
number of hours your timezone is west of GMT.  
EST is 5 hours west of GMT, PST is 8.  Negative numbers
indicate hours east of GMT. If you specify
\fBdst\fP, the system will operate under daylight savings time.
An optional integer or floating point number may be included
to specify a particular daylight saving time correction algorithm;
the default value is 1, indicating the United States.
Other values are: 2 (Australian style), 3 (Western European),
4 (Middle European), and 5 (Eastern European).  See
\fIgettimeofday\fP\|(2) and \fIctime\fP\|(3) for more information.
.IP "\fBident\fP \fIname\fP"
.br
This system is to be known as
.IR name .
This is usually a cute name like ERNIE (short for Ernie Co-Vax) or
VAXWELL (for Vaxwell Smart).
This value is defined for use in conditional compilation,
and is also used to locate an optional list of source files specific
to this system.
.IP "\fBmaxusers\fP \fInumber\fP"
.br
The maximum expected number of simultaneously active user on this system is
.IR number .
This number is used to size several system data structures.
.NH 2
System image parameters
.PP
Multiple bootable images may be specified in a single configuration
file.  The systems will have the same global configuration parameters
and devices, but the location of the root file system and other
system specific devices may be different.  A system image is specified
with a ``config'' line:
.IP
\fBconfig\fP\ \fIsysname\fP\ \fIconfig-clauses\fP
.LP
The
.I sysname
field is the name given to the loaded system image; almost everyone
names their standard system image ``kernel''.  The configuration clauses
are one or more specifications indicating where the root file system
is located and the number and location of paging devices.
The device used by the system to process argument lists during
.IR execve (2)
calls may also be specified, though in practice this is almost
always selected by
.I config
using one of its rules for selecting default locations for
system devices.
.PP
A configuration clause is one of the following
.IP
.nf
\fBroot\fP [ \fBon\fP ] \fIroot-device\fP
\fBswap\fP [ \fBon\fP ] \fIswap-device\fP [ \fBand\fP \fIswap-device\fP ] ...
\fBdumps\fP [ \fBon\fP ] \fIdump-device\fP
\fBargs\fP [ \fBon\fP ] \fIarg-device\fP
.LP
(the ``on'' is optional.)  Multiple configuration clauses
are separated by white space; 
.I config
allows specifications to be continued across multiple lines
by beginning the continuation line with a tab character.
The ``root'' clause specifies where the root file system
is located, the ``swap'' clause indicates swapping and paging
area(s), the ``dumps'' clause can be used to force system dumps
to be taken on a particular device, and the ``args'' clause
can be used to specify that argument list processing for
.I execve
should be done on a particular device.
.PP
The device names supplied in the clauses may be fully specified
as a device, unit, and file system partition; or underspecified
in which case
.I config
will use builtin rules to select default unit numbers and file
system partitions.  The defaulting rules are a bit complicated
as they are dependent on the overall system configuration.
For example, the swap area need not be specified at all if 
the root device is specified; in this case the swap area is
placed in the ``b'' partition of the same disk where the root
file system is located.  Appendix B contains a complete list
of the defaulting rules used in selecting system configuration
devices.
.PP
The device names are translated to the
appropriate major and minor device
numbers on a per-machine basis.  A file,
``/sys/conf/devices.machine'' (where ``machine''
is the machine type specified in the configuration file),
is used to map a device name to its major block device number.
The minor device number is calculated using the standard 
disk partitioning rules: on unit 0, partition ``a'' is minor device
0, partition ``b'' is minor device 1, and so on; for units
other than 0, add 8 times the unit number to get the minor
device.
.PP
If the default mapping of device name to major/minor device
number is incorrect for your configuration, it can be replaced
by an explicit specification of the major/minor device.
This is done by substituting
.IP
\fBmajor\fP \fIx\fP \fBminor\fP \fIy\fP
.LP
where the device name would normally be found.  For example,
.IP
.nf
\fBconfig\fP kernel \fBroot\fP \fBon\fP \fBmajor\fP 99 \fBminor\fP 1
.fi
.PP
Normally, the areas configured for swap space are sized by the system
at boot time.  If a non-standard size is to be used for one
or more swap areas (less than the full partition),
this can also be specified.  To do this, the
device name specified for a swap area should have a ``size''
specification appended.  For example,
.IP
.nf
\fBconfig\fP kernel \fBroot\fP \fBon\fP hp0 \fBswap\fP \fBon\fP hp0b \fBsize\fP 1200
.fi
.LP
would force swapping to be done in partition ``b'' of ``hp0'' and
the swap partition size would be set to 1200 sectors.  A swap area
sized larger than the associated disk partition is trimmed to the
partition size.
.PP
To create a generic configuration, only the clause ``swap generic''
should be specified; any extra clauses will cause an error.
.NH 2
Device specifications
.PP
Each device attached to a machine must be specified
to
.I config
so that the system generated will know to probe for it during
the autoconfiguration process carried out at boot time.  Hardware
specified in the configuration need not actually be present on
the machine where the generated system is to be run.  Only the
hardware actually found at boot time will be used by the system.
.PP
The specification of hardware devices in the configuration file
parallels the interconnection hierarchy of the machine to be
configured.  On the VAX, this means that a configuration file must
indicate what MASSBUS and UNIBUS adapters are present, and to
which \fInexi\fP they might be connected.* 
.FS
* While VAX-11/750's and VAX-11/730 do not actually have 
nexi, the system treats them as having 
.I "simulated nexi"
to simplify device configuration.
.FE
Similarly, devices
and controllers must be indicated as possibly being connected
to one or more adapters.  A device description may provide a
complete definition of the possible configuration parameters
or it may leave certain parameters undefined and make the system
probe for all the possible values.  The latter allows a single
device configuration list to match many possible physical
configurations.  For example, a disk may be indicated as present
at UNIBUS adapter 0, or at any UNIBUS adapter which the system
locates at boot time.  The latter scheme, termed 
.IR wildcarding ,
allows more flexibility in the physical configuration of a system;
if a disk must be moved around for some reason, the system will
still locate it at the alternate location.
.PP
A device specification takes one of the following forms:
.IP
.nf
\fBmaster\fP \fIdevice-name\fP \fIdevice-info\fP
\fBcontroller\fP \fIdevice-name\fP \fIdevice-info\fP [ \fIinterrupt-spec\fP ]
\fBdevice\fP \fIdevice-name\fP \fIdevice-info\fP \fIinterrupt-spec\fP
\fBdisk\fP \fIdevice-name\fP \fIdevice-info\fP
\fBtape\fP \fIdevice-name\fP \fIdevice-info\fP
.fi
.LP
A ``master'' is a MASSBUS tape controller; a ``controller'' is a
disk controller, a UNIBUS tape controller, a MASSBUS adapter, or
a UNIBUS adapter.  A ``device'' is an autonomous device which
connects directly to a UNIBUS adapter (as opposed to something
like a disk which connects through a disk controller).  ``Disk''
and ``tape'' identify disk drives and tape drives connected to
a ``controller'' or ``master.''
.PP
The
.I device-name
is one of the standard device names, as
indicated in section 4 of the UNIX Programmers Manual,
concatenated with the
.I logical
unit number to be assigned the device (the 
.I logical
unit number may be different than the
.I physical
unit number indicated on the front of something
like a disk; the
.I logical
unit number is used to refer to the UNIX device, not
the physical unit number).  For example, ``hp0'' is logical
unit 0 of a MASSBUS storage device, even though it might
be physical unit 3 on MASSBUS adapter 1.
.PP
The
.I device-info
clause specifies how the hardware is
connected in the interconnection hierarchy.  On the VAX,
UNIBUS and MASSBUS adapters are connected to the internal
system bus through
a \fInexus\fP.
Thus, one of the following
specifications would be used:
.IP
.ta 1.5i 2.5i 4.0i
.nf
\fBcontroller\fP	mba0	\fBat\fP \fBnexus\fP \fIx\fP
\fBcontroller\fP	uba0	\fBat\fP \fBnexus\fP \fIx\fP
.fi
.LP
To tie a controller to a specific nexus, ``x'' would be supplied
as the number of that nexus; otherwise ``x'' may be specified as
``?'', in which
case the system will probe all nexi present looking
for the specified controller.
.PP
The remaining interconnections on the VAX are:
.IP \(bu 3
a controller
may be connected to another controller (e.g. a disk controller attached
to a UNIBUS adapter),
.IP \(bu 3
a master is always attached to a controller (a MASSBUS adapter),
.IP \(bu 3
a tape is always attached to a master (for MASSBUS
tape drives),
.IP \(bu 3
a disk is always attached to a controller, and
.IP \(bu 3
devices
are always attached to controllers (e.g. UNIBUS controllers attached
to UNIBUS adapters).
.LP
The following lines give an example of each of these interconnections:
.IP
.ta 1.5i 2.5i 4.0i
.nf
\fBcontroller\fP	hk0	\fBat\fP uba0 ...
\fBmaster\fP	ht0	\fBat\fP mba0 ...
\fBdisk\fP	hp0	\fBat\fP mba0 ...
\fBtape\fP	tu0	\fBat\fP ht0 ...
\fBdisk\fP	rk1	\fBat\fP hk0 ...
\fBdevice\fP	dz0	\fBat\fP uba0 ...
.fi
.LP
Any piece of hardware which may be connected to a specific
controller may also be wildcarded across multiple controllers.
.PP
The final piece of information needed by the system to configure
devices is some indication of where or how a device will interrupt.
For tapes and disks, simply specifying the \fIslave\fP or \fIdrive\fP
number is sufficient to locate the control status register for the
device.
\fIDrive\fP numbers may be wildcarded
on MASSBUS devices, but not on disks on a UNIBUS controller.
For controllers, the control status register must be
given explicitly, as well the number of interrupt vectors used and
the names of the routines to which they should be bound. 
Thus the example lines given above might be completed as:
.IP
.ta 1.5i 2.5i 4.0i
.nf
\fBcontroller\fP	hk0	\fBat\fP uba0 \fBcsr\fP 0177440	\fBvector\fP rkintr
\fBmaster\fP	ht0	\fBat\fP mba0 \fBdrive\fP 0
\fBdisk\fP	hp0	\fBat\fP mba0 \fBdrive\fP ?
\fBtape\fP	tu0	\fBat\fP ht0 \fBslave\fP 0
\fBdisk\fP	rk1	\fBat\fP hk0 \fBdrive\fP 1
\fBdevice\fP	dz0	\fBat\fP uba0 \fBcsr\fP 0160100	\fBvector\fP dzrint dzxint
.fi
.PP
Certain device drivers require extra information passed to them
at boot time to tailor their operation to the actual hardware present.
The line printer driver, for example, needs to know how many columns
are present on each non-standard line printer (i.e. a line printer
with other than 80 columns).  The drivers for the terminal multiplexors
need to know which lines are attached to modem lines so that no one will
be allowed to use them unless a connection is present.  For this reason,
one last parameter may be specified to a
.IR device ,
a 
.I flags
field.  It has the syntax
.IP
\fBflags\fP \fInumber\fP
.LP
and is usually placed after the
.I csr
specification.  The
.I number
is passed directly to the associated driver.  The manual pages
in section 4 should be consulted to determine how each driver
uses this value (if at all).
Communications interface drivers commonly use the flags
to indicate whether modem control signals are in use.
.PP
The exact syntax for each specific device is given in the Synopsis
section of its manual page in section 4 of the manual.
.NH 2
Pseudo-devices
.PP
A number of drivers and software subsystems
are treated like device drivers without any associated hardware.
To include any of these pieces, a ``pseudo-device'' specification
must be used.  A specification for a pseudo device takes the form
.IP
.DT
.nf
\fBpseudo-device\fP	\fIdevice-name\fP [ \fIhowmany\fP ]
.fi
.PP
Examples of pseudo devices are
\fBpty\fP, the pseudo terminal driver (where the optional
.I howmany
value indicates the number of pseudo terminals to configure, 32 default),
and \fBloop\fP, the software loopback network pseudo-interface.
Other pseudo devices for the network include
\fBimp\fP (required when a CSS or ACC imp is configured)
and \fBether\fP (used by the Address Resolution Protocol
on 10 Mb/sec Ethernets).
More information on configuring each of these can also be found
in section 4 of the manual.
