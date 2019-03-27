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
.\"	@(#)3.t	8.1 (Berkeley) 6/8/93
.\"
.\".ds RH "System Building Process
.ne 2i
.NH
SYSTEM BUILDING PROCESS
.PP
In this section we consider the steps necessary to build a bootable system
image.  We assume the system source is located in the ``/sys'' directory
and that, initially, the system is being configured from source code.
.PP
Under normal circumstances there are 5 steps in building a system.
.IP 1) 3
Create a configuration file for the system.
.IP 2) 3
Make a directory for the system to be constructed in.
.IP 3) 3
Run
.I config
on the configuration file to generate the files required
to compile and load the system image.
.IP 4)
Construct the source code interdependency rules for the
configured system with
.I make depend
using
.IR make (1).
.IP 5)
Compile and load the system with 
.IR make .
.PP
Steps 1 and 2 are usually done only once.  When a system configuration
changes it usually suffices to just run
.I config
on the modified configuration file, rebuild the source code dependencies,
and remake the system.  Sometimes,
however, configuration dependencies may not be noticed in which case
it is necessary to clean out the relocatable object files saved
in the system's directory; this will be discussed later.
.NH 2
Creating a configuration file
.PP
Configuration files normally reside in the directory ``/sys/conf''.
A configuration file is most easily constructed by copying an
existing configuration file and modifying it.  The 4.4BSD distribution
contains a number of configuration files for machines at Berkeley;
one may be suitable or, in worst case, a copy
of the generic configuration file may be edited.
.PP
The configuration file must have the same name as the directory in
which the configured system is to be built.  
Further,
.I config
assumes this directory is located in the parent directory of
the directory in which it
is run.  For example, the generic
system has a configuration file ``/sys/conf/GENERIC'' and an accompanying
directory named ``/sys/GENERIC''.
Although it is not required that the system sources and configuration
files reside in ``/sys,'' the configuration and compilation procedure
depends on the relative locations of directories within that hierarchy,
as most of the system code and the files created by
.I config
use pathnames of the form ``../''.
If the system files are not located in ``/sys,''
it is desirable to make a symbolic link there for use in installation
of other parts of the system that share files with the kernel.
.PP
When building the configuration file, be sure to include the items
described in section 2.  In particular, the machine type,
cpu type, timezone, system identifier, maximum users, and root device
must be specified.  The specification of the hardware present may take
a bit of work; particularly if your hardware is configured at non-standard
places (e.g. device registers located at funny places or devices not
supported by the system).  Section 4 of this document
gives a detailed description of the configuration file syntax,
section 5 explains some sample configuration files, and
section 6 discusses how to add new devices to
the system.  If the devices to be configured are not already
described in one of the existing configuration files you should check
the manual pages in section 4 of the UNIX Programmers Manual.  For each
supported device, the manual page synopsis entry gives a
sample configuration line.
.PP
Once the configuration file is complete, run it through
.I config
and look for any errors.  Never try and use a system which
.I config
has complained about; the results are unpredictable.
For the most part,
.IR config 's
error diagnostics are self explanatory.  It may be the case that
the line numbers given with the error messages are off by one.
.PP
A successful run of
.I config
on your configuration file will generate a number of files in
the configuration directory.  These files are:
.IP \(bu 3
A file to be used by \fImake\fP\|(1)
in compiling and loading the system,
.IR Makefile .
.IP \(bu 3
One file for each possible system image for this machine,
.IR swapxxx.c ,
where
.I xxx
is the name of the system image,
which describes where swapping, the root file system, and other
miscellaneous system devices are located.
.IP \(bu 3
A collection of header files, one per possible device the
system supports, which define the hardware configured.
.IP \(bu 3
A file containing the I/O configuration tables used by the system
during its 
.I autoconfiguration
phase,
.IR ioconf.c .
.IP \(bu 3
An assembly language file of interrupt vectors which
connect interrupts from the machine's external buses to the main
system path for handling interrupts,
and a file that contains counters and names for the interrupt vectors.
.PP
Unless you have reason to doubt 
.IR config ,
or are curious how the system's autoconfiguration scheme
works, you should never have to look at any of these files.
.NH 2
Constructing source code dependencies
.PP
When 
.I config
is done generating the files needed to compile and link your system it
will terminate with a message of the form ``Don't forget to run make depend''.
This is a reminder that you should change over to the configuration
directory for the system just configured and type ``make depend''
to build the rules used by 
.I make
to recognize interdependencies in the system source code.
This will insure that any changes to a piece of the system
source code will result in the proper modules being recompiled
the next time
.I make
is run.
.PP
This step is particularly important if your site makes changes
to the system include files.  The rules generated specify which source code
files are dependent on which include files.  Without these rules,
.I make
will not recognize when it must rebuild modules
due to the modification of a system header file.
The dependency rules are generated by a pass of the C preprocessor
and reflect the global system options.
This step must be repeated when the configuration file is changed
and
.I config
is used to regenerate the system makefile.
.NH 2
Building the system
.PP
The makefile constructed by
.I config
should allow a new system to be rebuilt by simply typing ``make image-name''.
For example, if you have named your bootable system image ``kernel'',
then ``make kernel''
will generate a bootable image named ``kernel''.  Alternate system image names
are used when the root file system location and/or swapping configuration
is done in more than one way.  The makefile which
.I config
creates has entry points for each system image defined in
the configuration file.
Thus, if you have configured ``kernel'' to be a system with the root file
system on an ``hp'' device and ``hkkernel'' to be a system with the root
file system on an ``hk'' device, then ``make kernel hkkernel'' will generate
binary images for each.
As the system will generally use the disk from which it is loaded
as the root filesystem, separate system images are only required
to support different swap configurations.
.PP
Note that the name of a bootable image is different from the system
identifier.  All bootable images are configured for the same system;
only the information about the root file system and paging devices differ.
(This is described in more detail in section 4.)
.PP
The last step in the system building process is to rearrange certain commonly
used symbols in the symbol table of the system image;  the makefile
generated by 
.I config
does this automatically for you.
This is advantageous for programs such as
\fInetstat\fP\|(1) and \fIvmstat\fP\|(1),
which run much faster when the symbols they need are located at
the front of the symbol table.  
Remember also that many programs expect
the currently executing system to be named ``/kernel''.  If you install
a new system and name it something other than ``/kernel'', many programs
are likely to give strange results.
.NH 2
Sharing object modules
.PP
If you have many systems which are all built on a single machine
there are at least two approaches to saving time in building system
images.  The best way is to have a single system image which is run on
all machines.  This is attractive since it minimizes disk space used
and time required to rebuild systems after making changes.  However,
it is often the case that one or more systems will require a separately
configured system image.  This may be due to limited memory (building
a system with many unused device drivers can be expensive), or to
configuration requirements (one machine may be a development machine
where disk quotas are not needed, while another is a production machine
where they are), etc.  In these cases it is possible
for common systems to share relocatable object modules which are not
configuration dependent; most of the modules in the directory ``/sys/sys''
are of this sort.
.PP
To share object modules, a generic system should be built.  Then, for
each system configure the system as before, but before recompiling and
linking the system, type ``make links'' in the system compilation directory.
This will cause the system
to be searched for source modules which are safe to share between systems
and generate symbolic links in the current directory to the appropriate
object modules in the directory ``../GENERIC''.  A shell script,
``makelinks'' is generated with this request and may be checked for
correctness.  The file ``/sys/conf/defines'' contains a list of symbols
which we believe are safe to ignore when checking the source code
for modules which may be shared.  Note that this list includes the definitions
used to conditionally compile in the virtual memory tracing facilities, and
the trace point support used only rarely (even at Berkeley). 
It may be necessary
to modify this file to reflect local needs.  Note further that
interdependencies which are not directly visible
in the source code are not caught.  This means that if you place
per-system dependencies in an include file, they will not be recognized
and the shared code may be selected in an unexpected fashion.
.NH 2
Building profiled systems
.PP
It is simple to configure a system which will automatically
collect profiling information as it operates.  The profiling data
may be collected with \fIkgmon\fP\|(8) and processed with
\fIgprof\fP\|(1)
to obtain information regarding the system's operation.  Profiled
systems maintain histograms of the program counter as well as the
number of invocations of each routine.  The \fIgprof\fP
command will also generate a dynamic call graph of the executing
system and propagate time spent in each routine along the arcs
of the call graph (consult the \fIgprof\fP documentation for elaboration).
The program counter sampling can be driven by the system clock, or
if you have an alternate real time clock, this can be used.  The 
latter is highly recommended, as use of the system clock will result
in statistical anomalies, and time spent in the clock routine will
not be accurately attributed.
.PP
To configure a profiled system, the
.B \-p
option should be supplied to \fIconfig\fP.
A profiled system is about 5-10% larger in its text space due to
the calls to count the subroutine invocations.  When the system
executes, the profiling data is stored in a buffer which is 1.2
times the size of the text space.  The overhead for running a
profiled system varies; under normal load we see anywhere from 5-25%
of the system time spent in the profiling code.
.PP
Note that systems configured for profiling should not be shared as
described above unless all the other shared systems are also to be
profiled.
