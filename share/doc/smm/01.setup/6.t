.\" Copyright (c) 1980, 1986, 1988, 1993 The Regents of the University of California.
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
.\"	@(#)6.t	8.1 (Berkeley) 7/27/93
.\"
.ds LH "Installing/Operating \*(4B
.ds CF \*(Dy
.Sh 1 "System operation"
.PP
This section describes procedures used to operate a \*(4B UNIX system.
Procedures described here are used periodically, to reboot the system,
analyze error messages from devices, do disk backups, monitor
system performance, recompile system software and control local changes.
.Sh 2 "Bootstrap and shutdown procedures"
.PP
In a normal reboot, the system checks the disks and comes up multi-user
without intervention at the console.
Such a reboot
can be stopped (after it prints the date) with a ^C (interrupt).
This will leave the system in single-user mode, with only the console
terminal active.
(If the console has been marked ``insecure'' in
.Pn /etc/ttys
you must enter the root password to bring the machine to single-user mode.)
It is also possible to allow the filesystem checks to complete
and then to return to single-user mode by signaling
.Xr fsck (8)
with a QUIT signal (^\|\e).
.PP
To bring the system up to a multi-user configuration from the single-user
status,
all you have to do is hit ^D on the console.  The system
will then execute
.Pn /etc/rc ,
a multi-user restart script (and
.Pn /etc/rc.local ),
and come up on the terminals listed as
active in the file
.Pn /etc/ttys .
See
.Xr init (8)
and
.Xr ttys (5) for more details.
Note, however, that this does not cause a filesystem check to be done.
Unless the system was taken down cleanly, you should run
``fsck \-p'' or force a reboot with
.Xr reboot (8)
to have the disks checked.
.PP
To take the system down to a single user state you can use
.DS
\fB#\fP \fIkill 1\fP
.DE
or use the
.Xr shutdown (8)
command (which is much more polite, if there are other users logged in)
when you are running multi-user.
Either command will kill all processes and give you a shell on the console,
as if you had just booted.  Filesystems remain mounted after the
system is taken single-user.  If you wish to come up multi-user again, you
should do this by:
.DS
\fB#\fP \fIcd /\fP
\fB#\fP \fI/sbin/umount -a\fP
\fB#\fP \fI^D\fP
.DE
.PP
Each system shutdown, crash, processor halt and reboot
is recorded in the system log
with its cause.
.Sh 2 "Device errors and diagnostics"
.PP
When serious errors occur on peripherals or in the system, the system
prints a warning diagnostic on the console.
These messages are collected
by the system error logging process
.Xr syslogd (8)
and written into a system error log file
.Pn /var/log/messages .
Less serious errors are sent directly to
.Xr syslogd ,
which may log them on the console.
The error priorities that are logged and the locations to which they are logged
are controlled by
.Pn /etc/syslog.conf .
See
.Xr syslogd (8)
for further details.
.PP
Error messages printed by the devices in the system are described with the
drivers for the devices in section 4 of the programmer's manual.
If errors occur suggesting hardware problems, you should contact
your hardware support group or field service.  It is a good idea to
examine the error log file regularly
(e.g. with the command \fItail \-r /var/log/messages\fP).
.Sh 2 "Filesystem checks, backups, and disaster recovery"
.PP
Periodically (say every week or so in the absence of any problems)
and always (usually automatically) after a crash,
all the filesystems should be checked for consistency
by
.Xr fsck (1).
The procedures of
.Xr reboot (8)
should be used to get the system to a state where a filesystem
check can be done manually or automatically.
.PP
Dumping of the filesystems should be done regularly,
since once the system is going it is easy to
become complacent.
Complete and incremental dumps are easily done with
.Xr dump (8).
You should arrange to do a towers-of-hanoi dump sequence; we tune
ours so that almost all files are dumped on two tapes and kept for at
least a week in most every case.  We take full dumps every month (and keep
these indefinitely).
Operators can execute ``dump w'' at login that will tell them what needs
to be dumped
(based on the
.Pn /etc/fstab
information).
Be sure to create a group
.B operator
in the file
.Pn /etc/group
so that dump can notify logged-in operators when it needs help.
.PP
More precisely, we have three sets of dump tapes: 10 daily tapes,
5 weekly sets of 2 tapes, and fresh sets of three tapes monthly.
We do daily dumps circularly on the daily tapes with sequence
`3 2 5 4 7 6 9 8 9 9 9 ...'.
Each weekly is a level 1 and the daily dump sequence level
restarts after each weekly dump.
Full dumps are level 0 and the daily sequence restarts after each full dump
also.
.PP
Thus a typical dump sequence would be:
.br
.ne 6
.TS
center;
c c c c c
n n n l l.
tape name	level number	date	opr	size
_
FULL	0	Nov 24, 1992	operator	137K
D1	3	Nov 28, 1992	operator	29K
D2	2	Nov 29, 1992	operator	34K
D3	5	Nov 30, 1992	operator	19K
D4	4	Dec 1, 1992	operator	22K
W1	1	Dec 2, 1992	operator	40K
D5	3	Dec 4, 1992	operator	15K
D6	2	Dec 5, 1992	operator	25K
D7	5	Dec 6, 1992	operator	15K
D8	4	Dec 7, 1992	operator	19K
W2	1	Dec 9, 1992	operator	118K
D9	3	Dec 11, 1992	operator	15K
D10	2	Dec 12, 1992	operator	26K
D1	5	Dec 15, 1992	operator	14K
W3	1	Dec 17, 1992	operator	71K
D2	3	Dec 18, 1992	operator	13K
FULL	0	Dec 22, 1992	operator	135K
.TE
We do weekly dumps often enough that daily dumps always fit on one tape.
.PP
Dumping of files by name is best done by
.Xr tar (1)
but the amount of data that can be moved in this way is limited
to a single tape.
Finally if there are enough drives entire
disks can be copied with
.Xr dd (1)
using the raw special files and an appropriate
blocking factor; the number of sectors per track is usually
a good value to use, consult
.Pn /etc/disktab .
.PP
It is desirable that full dumps of the root filesystem be
made regularly.
This is especially true when only one disk is available.
Then, if the
root filesystem is damaged by a hardware or software failure, you
can rebuild a workable disk doing a restore in the
same way that the initial root filesystem was created.
.PP
Exhaustion of user-file space is certain to occur
now and then; disk quotas may be imposed, or if you
prefer a less fascist approach, try using the programs
.Xr du (1),
.Xr df (1),
and
.Xr quot (8),
combined with threatening
messages of the day, and personal letters.
.Sh 2 "Moving filesystem data"
.PP
If you have the resources,
the best way to move a filesystem
is to dump it to a spare disk partition, or magtape, using
.Xr dump (8),
use
.Xr newfs (8)
to create the new filesystem,
and restore the filesystem using
.Xr restore (8).
Filesystems may also be moved by piping the output of
.Xr dump
to
.Xr restore .
The
.Xr restore
program uses an ``in-place'' algorithm that
allows filesystem dumps to be restored without concern for the
original size of the filesystem.  Further, portions of a
filesystem may be selectively restored using a method similar
to the tape archive program.
.PP
If you have to merge a filesystem into another, existing one,
the best bet is to use
.Xr tar (1).
If you must shrink a filesystem, the best bet is to dump
the original and restore it onto the new filesystem.
If you
are playing with the root filesystem and only have one drive,
the procedure is more complicated.
If the only drive is a Winchester disk, this procedure may not be used
without overwriting the existing root or another partition.
What you do is the following:
.IP 1.
GET A SECOND PACK, OR USE ANOTHER DISK DRIVE!!!!
.IP 2.
Dump the root filesystem to tape using
.Xr dump (8).
.IP 3.
Bring the system down.
.IP 4.
Mount the new pack in the correct disk drive, if
using removable media.
.IP 5.
Load the distribution tape and install the new
root filesystem as you did when first installing the system.
Boot normally
using the newly created disk filesystem.
.PP
Note that if you change the disk partition tables or add new disk
drivers they should also be added to the standalone system in
.Pn /sys/<architecture>/stand ,
and the default disk partition tables in
.Pn /etc/disktab
should be modified.
.Sh 2 "Monitoring system performance"
.PP
The
.Xr systat
program provided with the system is designed to be an aid to monitoring
systemwide activity.  The default ``pigs'' mode shows a dynamic ``ps''.
By running in the ``vmstat'' mode
when the system is active you can judge the system activity in several
dimensions: job distribution, virtual memory load, paging and swapping
activity, device interrupts, and disk and cpu utilization.
Ideally, there should be few blocked (b) jobs,
there should be little paging or swapping activity, there should
be available bandwidth on the disk devices (most single arms peak
out at 20-30 tps in practice), and the user cpu utilization (us) should
be high (above 50%).
.PP
If the system is busy, then the count of active jobs may be large,
and several of these jobs may often be blocked (b).  If the virtual
memory is active, then the paging demon will be running (sr will
be non-zero).  It is healthy for the paging demon to free pages when
the virtual memory gets active; it is triggered by the amount of free
memory dropping below a threshold and increases its pace as free memory
goes to zero.
.PP
If you run in the ``vmstat'' mode
when the system is busy, you can find
imbalances by noting abnormal job distributions.  If many
processes are blocked (b), then the disk subsystem
is overloaded or imbalanced.  If you have several non-dma
devices or open teletype lines that are ``ringing'', or user programs
that are doing high-speed non-buffered input/output, then the system
time may go high (60-70% or higher).
It is often possible to pin down the cause of high system time by
looking to see if there is excessive context switching (cs), interrupt
activity (in) and per-device interrupt counts,
or system call activity (sy).  Cumulatively on one of
our large machines we average about 60-200 context switches and interrupts
per second and about 50-500 system calls per second.
.PP
If the system is heavily loaded, or if you have little memory
for your load (2M is little in most any case), then the system
may be forced to swap.  This is likely to be accompanied by a noticeable
reduction in system performance and pregnant pauses when interactive
jobs such as editors swap out.
If you expect to be in a memory-poor environment
for an extended period you might consider administratively
limiting system load.
.Sh 2 "Recompiling and reinstalling system software"
.PP
It is easy to regenerate either the entire system or a single utility,
and it is a good idea to try rebuilding pieces of the system to build
confidence in the procedures.
.LP
In general, there are six well-known targets supported by
all the makefiles on the system:
.IP all 9
This entry is the default target, the same as if no target is specified.
This target builds the kernel, binary or library, as well as its
associated manual pages.
This target \fBdoes not\fP build the dependency files.
Some of the utilities require that a \fImake depend\fP be done before
a \fImake all\fP can succeed.
.IP depend
Build the include file dependency file, ``.depend'', which is
read by
.Xr make .
See
.Xr mkdep (1)
for further details.
.IP install
Install the kernel, binary or library, as well as its associated
manual pages.
See
.Xr install (1)
for further details.
.IP clean
Remove the kernel, binary or library, as well as any object files
created when building it.
.IP cleandir
The same as clean, except that the dependency files and formatted
manual pages are removed as well.
.IP obj
Build a shadow directory structure in the area referenced by
.Pn /usr/obj
and create a symbolic link in the current source directory to
referenced it, named ``obj''.
Once this shadow structure has been created, all the files created by
.Xr make
will live in the shadow structure, and
.Pn /usr/src
may be mounted read-only by multiple machines.
Doing a \fImake obj\fP in
.Pn /usr/src
will build the shadow directory structure for everything on the
system except for the contributed, old, and kernel software.
.PP
The system consists of three major parts:
the kernel itself, found in
.Pn /usr/src/sys ,
the libraries , found in
.Pn /usr/src/lib ,
and the user programs (the rest of
.Pn /usr/src ).
.PP
Deprecated software, found in
.Pn /usr/src/old ,
often has old style makefiles;
some of it does not compile under \*(4B at all.
.PP
Contributed software, found in
.Pn /usr/src/contrib ,
usually does not support the ``cleandir'', ``depend'', or ``obj'' targets.
.PP
The kernel does not support the ``obj'' shadow structure.
All kernels are compiled in subdirectories of
.Pn /usr/src/sys/compile
which is usually abbreviated as
.Pn /sys/compile .
If you want to mount your source tree read-only,
.Pn /usr/src/sys/compile
will have to be on a separate filesystem from
.Pn /usr/src .
Separation from
.Pn /usr/src
can be done by making
.Pn /usr/src/sys/compile
a symbolic link that references
.Pn /usr/obj/sys/compile .
If it is a symbolic link, the \fIS\fP variable in the kernel
Makefile must be changed from
.Pn \&../..
to the absolute pathname needed to locate the kernel sources, usually
.Pn /usr/src/sys .
The symbolic link created by
.Xr config (8)
for
.Pn machine
must also be manually changed to an absolute pathname.
Finally, the
.Pn /usr/src/sys/libkern/obj
directory must be located in
.Pn /usr/obj/sys/libkern .
.PP
Each of the standard utilities and libraries may be built and
installed by changing directories into the correct location and
doing:
.DS
\fB#\fP \fImake\fP
\fB#\fP \fImake install\fP
.DE
Note, if system include files have changed between compiles,
.Xr make
will not do the correct dependency checks if the dependency
files have not been built using the ``depend'' target.
.PP
The entire library and utility suite for the system may be recompiled
from scratch by changing directory to
.Pn /usr/src
and doing:
.DS
\fB#\fP \fImake build\fP
.DE
This target installs the system include files, cleans the source
tree, builds and installs the libraries, and builds and installs
the system utilities.
.PP
To recompile a specific program, first determine where the binary
resides with the
.Xr whereis (1)
command, then change to the corresponding source directory and build
it with the Makefile in the directory.
For instance, to recompile ``passwd'',
all one has to do is:
.DS
\fB#\fP \fIwhereis passwd\fP
\fB/usr/bin/passwd\fP
\fB#\fP \fIcd /usr/src/usr.bin/passwd\fP
\fB#\fP \fImake\fP
\fB#\fP \fImake install\fP
.DE
this will compile and install the
.Xr passwd
utility.
.PP
If you wish to recompile and install all programs into a particular
target area you can override the default path prefix by doing:
.DS
\fB#\fP \fImake\fP
\fB#\fP \fImake DESTDIR=\fPpathname \fIinstall\fP
.DE
Similarly, the mode, owner, group, and other characteristics of
the installed object can be modified by changing other default
make variables.
See
.Xr make (1),
.Pn /usr/src/share/mk/bsd.README ,
and the ``.mk'' scripts in the
.Pn /usr/share/mk
directory for more information.
.PP
If you modify the C library or system include files, to change a
system call for example, and want to rebuild and install everything,
you have to be a little careful.
You must ensure that the include files are installed before anything
is compiled, and that the libraries are installed before the remainder
of the source, otherwise the loaded images will not contain the new
routine from the library.
If include files have been modified, the following commands should
be done first:
.DS
\fB#\fP \fIcd /usr/src/include\fP
\fB#\fP \fImake install\fP
.DE
Then, if, for example, C library files have been modified, the
following commands should be executed:
.DS
\fB#\fP \fIcd /usr/src/lib/libc\fP
\fB#\fP \fImake depend\fP
\fB#\fP \fImake\fP
\fB#\fP \fImake install\fP
\fB#\fP \fIcd /usr/src\fP
\fB#\fP \fImake depend\fP
\fB#\fP \fImake\fP
\fB#\fP \fImake install\fP
.DE
Alternatively, the \fImake build\fP command described above will
accomplish the same tasks.
This takes several hours on a reasonably configured machine.
.Sh 2 "Making local modifications"
.PP
The source for locally written commands is normally stored in
.Pn /usr/src/local ,
and their binaries are kept in
.Pn /usr/local/bin .
This isolation of local binaries allows
.Pn /usr/bin ,
and
.Pn /bin
to correspond to the distribution tape (and to the manuals that
people can buy).
People using local commands should be made aware that they are not
in the base manual.
Manual pages for local commands should be installed in
.Pn /usr/local/man/cat[1-8].
The
.Xr man (1)
command automatically finds manual pages placed in
/usr/local/man/cat[1-8] to encourage this practice (see
.Xr man.conf (5)).
.Sh 2 "Accounting"
.PP
UNIX optionally records two kinds of accounting information:
connect time accounting and process resource accounting.  The connect
time accounting information is stored in the file
.Pn /var/log/wtmp ,
which is summarized by the program
.Xr ac (8).
The process time accounting information is stored in the file
.Pn /var/account/acct
after it is enabled by
.Xr accton (8),
and is analyzed and summarized by the program
.Xr sa (8).
.PP
If you need to recharge for computing time, you can develop
procedures based on the information provided by these commands.
A convenient way to do this is to give commands to the clock daemon
.Pn /usr/sbin/cron
to be executed every day at a specified time.
This is done by adding lines to
.Pn /etc/crontab.local ;
see
.Xr cron (8)
for details.
.Sh 2 "Resource control"
.PP
Resource control in the current version of UNIX is more
elaborate than in most UNIX systems.  The disk quota
facilities developed at the University of Melbourne have
been incorporated in the system and allow control over the
number of files and amount of disk space each user and/or group may use
on each filesystem.  In addition, the resources consumed
by any single process can be limited by the mechanisms of
.Xr setrlimit (2).
As distributed, the latter mechanism
is voluntary, though sites may choose to modify the login
mechanism to impose limits not covered with disk quotas.
.PP
To use the disk quota facilities, the system must be
configured with ``options QUOTA''.  Filesystems may then
be placed under the quota mechanism by creating a null file
.Pn quota.user
and/or
.Pn quota.group
at the root of the filesystem, running
.Xr quotacheck (8),
and modifying
.Pn /etc/fstab
to show that the filesystem is to run
with disk quotas (options userquota and/or groupquota).
The
.Xr quotaon (8)
program may then be run to enable quotas.
.PP
Individual quotas are applied by using the quota editor
.Xr edquota (8).
Users may view their quotas (but not those of other users) with the
.Xr quota (1)
program. The
.Xr repquota (8)
program may be used to summarize the quotas and current
space usage on a particular filesystem or filesystems.
.PP
Quotas are enforced with \fIsoft\fP and \fIhard\fP limits.
When a user and/or group first reaches a soft limit on a resource, a
message is generated on their terminal.  If the user and/or group fails to
lower the resource usage below the soft limit
for longer than the time limit established for that filesystem
(default seven days) the system then treats the soft limit as a
\fIhard\fP limit and disallows any allocations until enough space is
reclaimed to bring the user and/or group back below the soft limit.
Hard limits are enforced strictly resulting in errors when a user
and/or group tries to create or write a file.  Each time a hard limit is
exceeded the system will generate a message on the user's terminal.
.PP
Consult the auxiliary document, ``Disc Quotas in a UNIX Environment'' (SMM:4)
and the appropriate manual entries for more information.
.Sh 2 "Network troubleshooting"
.PP
If you have anything more than a trivial network configuration,
from time to time you are bound to run into problems.  Before
blaming the software, first check your network connections.  On
networks such as the Ethernet a
loose cable tap or misplaced power cable can result in severely
deteriorated service.  The
.Xr netstat (1)
program may be of aid in tracking down hardware malfunctions.
In particular, look at the \fB\-i\fP and \fB\-s\fP options in the manual page.
.PP
Should you believe a communication protocol problem exists,
consult the protocol specifications and attempt to isolate the
problem in a packet trace.  The SO_DEBUG option may be supplied
before establishing a connection on a socket, in which case the
system will trace all traffic and internal actions (such as timers
expiring) in a circular trace buffer.
This buffer may then be printed out with the
.Xr trpt (8)
program.
Most of the servers distributed with the system
accept a \fB\-d\fP option forcing
all sockets to be created with debugging turned on.
Consult the appropriate manual pages for more information.
.Sh 2 "Files that need periodic attention"
.PP
We conclude the discussion of system operations by listing
the files that require periodic attention or are system specific:
.TS
center;
lfC l.
/etc/fstab	how disk partitions are used
/etc/disktab	default disk partition sizes/labels
/etc/printcap	printer database
/etc/gettytab	terminal type definitions
/etc/remote	names and phone numbers of remote machines for \fItip\fP(1)
/etc/group	group memberships
/etc/motd	message of the day
/etc/master.passwd	password file; each account has a line
/etc/rc.local	local system restart script; runs reboot; starts daemons
/etc/inetd.conf	local internet servers
/etc/hosts	local host name database
/etc/networks	network name database
/etc/services	network services database
/etc/hosts.equiv	hosts under same administrative control
/etc/syslog.conf	error log configuration for \fIsyslogd\fP\|(8)
/etc/ttys	enables/disables ports
/etc/crontab	commands that are run periodically
/etc/crontab.local	local commands that are run periodically
/etc/aliases	mail forwarding and distribution groups
/var/account/acct	raw process account data
/var/log/messages	system error log
/var/log/wtmp	login session accounting
.TE
.pn 2
.bp
.PX
