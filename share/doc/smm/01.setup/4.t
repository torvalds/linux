.\" Copyright (c) 1980, 1986, 1988 The Regents of the University of California.
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
.\"	@(#)4.t	8.1 (Berkeley) 7/29/93
.\" $FreeBSD$
.\"
.ds LH "Installing/Operating \*(4B
.ds CF \*(Dy
.ds RH "System setup
.Sh 1 "System setup"
.PP
This section describes procedures used to set up a \*(4B UNIX system.
These procedures are used when a system is first installed
or when the system configuration changes.  Procedures for normal
system operation are described in the next section.
.Sh 2 "Kernel configuration"
.PP
This section briefly describes the layout of the kernel code and
how files for devices are made.
For a full discussion of configuring
and building system images, consult the document ``Building
4.3BSD UNIX Systems with Config'' (SMM:2).
.Sh 3 "Kernel organization"
.PP
As distributed, the kernel source is in a
separate tar image.  The source may be physically
located anywhere within any filesystem so long as
a symbolic link to the location is created for the file
.Pn /sys
(many files in
.Pn /usr/include
are normally symbolic links relative to
.Pn /sys ).
In further discussions of the system source all path names
will be given relative to
.Pn /sys .
.LP
The kernel is made up of several large generic parts:
.TS
l l l.
sys		main kernel header files
kern		kernel functions broken down as follows
	init	system startup, syscall dispatching, entry points
	kern	scheduling, descriptor handling and generic I/O
	sys	process management, signals
	tty	terminal handling and job control
	vfs	filesystem management
	uipc	interprocess communication (sockets)
	subr	miscellaneous support routines
vm		virtual memory management
ufs		local filesystems broken down as follows
	ufs	common local filesystem routines
	ffs	fast filesystem
	lfs	log-based filesystem
	mfs	memory based filesystem
nfs		Sun-compatible network filesystem
miscfs		miscellaneous filesystems broken down as follows
	deadfs	where rejected vnodes go to die
	fdesc	access to per-process file descriptors
	fifofs	IEEE Std1003.1 FIFOs
	kernfs	filesystem access to kernel data structures
	lofs	loopback filesystem
	nullfs	another loopback filesystem
	specfs	device special files
	umapfs	provide alternate uid/gid mappings
dev		generic device drivers (SCSI, vnode, concatenated disk)
.TE
.LP
The networking code is organized by protocol
.TS
l l.
net	routing and generic interface drivers
netinet	Internet protocols (TCP, UDP, IP, etc)
netiso	ISO protocols (TP-4, CLNP, CLTP, etc)
netns	Xerox network systems protocols (IDP, SPP, etc)
netx25	CCITT X.25 protocols (X.25 Packet Level, HDLC/LAPB)
.TE
.LP
A separate subdirectory is provided for each machine architecture
.TS
l l.
hp300	HP 9000/300 series of Motorola 68000-based machines
hp	code common to both HP 68k and (non-existent) PA-RISC ports
i386	Intel 386/486-based PC machines
luna68k	Omron 68000-based workstations
news3400	Sony News MIPS-based workstations
pmax	Digital 3100/5000 MIPS-based workstations
sparc	Sun Microsystems SPARCstation 1, 1+, and 2
tahoe	(deprecated) CCI Power 6-series machines
vax	(deprecated) Digital VAX machines
.TE
.LP
Each machine directory is subdivided by function;
for example the hp300 directory contains
.TS
l l.
include	exported machine-dependent header files
hp300	machine-dependent support code and private header files
dev	device drivers
conf	configuration files
stand	machine-dependent standalone code
.TE
.LP
Other kernel related directories
.TS
l l.
compile	area to compile kernels
conf	machine-independent configuration files
stand	machine-independent standalone code
.TE
.Sh 3 "Devices and device drivers"
.PP
Devices supported by UNIX are implemented in the kernel
by drivers whose source is kept in
.Pn /sys/<architecture>/dev .
These drivers are loaded
into the system when included in a cpu specific configuration file
kept in the conf directory.  Devices are accessed through special
files in the filesystem, made by the
.Xr mknod (8)
program and normally kept in the
.Pn /dev
directory.
For all the devices supported by the distribution system, the
files in
.Pn /dev
are created by devfs.
.PP
Determine the set of devices that you have and create a new
.Pn /dev
directory by mounting devfs.
.Sh 3 "Building new system images"
.PP
The kernel configuration of each UNIX system is described by
a single configuration file, stored in the
.Pn /sys/<architecture>/conf
directory.
To learn about the format of this file and the procedure used
to build system images,
start by reading ``Building 4.3BSD UNIX Systems with Config'' (SMM:2),
look at the manual pages in section 4
of the UNIX manual for the devices you have,
and look at the sample configuration files in the
.Pn /sys/<architecture>/conf
directory.
.PP
The configured system image
.Pn kernel
should be copied to the root, and then booted to try it out.
It is best to name it
.Pn /newkernel
so as not to destroy the working system until you are sure it does work:
.DS
\fB#\fP \fIcp kernel /newkernel\fP
\fB#\fP \fIsync\fP
.DE
It is also a good idea to keep the previous system around under some other
name.  In particular, we recommend that you save the generic distribution
version of the system permanently as
.Pn /genkernel
for use in emergencies.
To boot the new version of the system you should follow the
bootstrap procedures outlined in section 6.1.
After having booted and tested the new system, it should be installed as
.Pn /kernel
before going into multiuser operation.
A systematic scheme for numbering and saving old versions
of the system may be useful.
.Sh 2 "Configuring terminals"
.PP
If UNIX is to support simultaneous
access from directly-connected terminals other than the console,
the file
.Pn /etc/ttys
(see
.Xr ttys (5))
must be edited.
.PP
To add a new terminal device, be sure the device is configured into the system
and that the special files for the device exist in
.Pn /dev .
Then, enable the appropriate lines of
.Pn /etc/ttys
by setting the ``status''
field to \fBon\fP (or add new lines).
Note that lines in
.Pn /etc/ttys
are one-for-one with entries in the file of current users
(see
.Pn /var/run/utmp ),
and therefore it is best to make changes
while running in single-user mode
and to add all the entries for a new device at once.
.PP
Each line in the
.Pn /etc/ttys
file is broken into four tab separated
fields (comments are shown by a `#' character and extend to
the end of the line).  For each terminal line the four fields
are:
the device (without a leading
.Pn /dev ),
the program
.Pn /sbin/init
should startup to service the line
(or \fBnone\fP if the line is to be left alone),
the terminal type (found in
.Pn /usr/share/misc/termcap ),
and optional status information describing if the terminal is
enabled or not and if it is ``secure'' (i.e. the super user should
be allowed to login on the line).
If the console is marked as ``insecure'',
then the root password is required to bring the machine up single-user.
All fields are character strings
with entries requiring embedded white space enclosed in double
quotes.
Thus a newly added terminal
.Pn /dev/tty00
could be added as
.DS
tty00 	"/usr/libexec/getty std.9600"	vt100	on secure	# mike's office
.DE
The std.9600 parameter provided to
.Pn /usr/libexec/getty
is used in searching the file
.Pn /etc/gettytab ;
it specifies a terminal's characteristics (such as baud rate).
To make custom terminal types, consult
.Xr gettytab (5)
before modifying
.Pn /etc/gettytab .
.PP
Dialup terminals should be wired so that carrier is asserted only when the
phone line is dialed up.
For non-dialup terminals, from which modem control is not available,
you must wire back the signals so that
the carrier appears to always be present.  For further details,
find your terminal driver in section 4 of the manual.
.PP
For network terminals (i.e. pseudo terminals), no program should
be started up on the lines.  Thus, the normal entry in
.Pn /etc/ttys
would look like
.DS
ttyp0 	none	network
.DE
(Note, the fourth field is not needed here.)
.PP
When the system is running multi-user, all terminals that are listed in
.Pn /etc/ttys
as \fBon\fP have their line enabled.
If, during normal operations, you wish
to disable a terminal line, you can edit the file
.Pn /etc/ttys
to change the terminal's status to \fBoff\fP and
then send a hangup signal to the
.Xr init
process, by doing
.DS
\fB#\fP \fIkill \-1 1\fP
.DE
Terminals can similarly be enabled by changing the status field
from \fBoff\fP to \fBon\fP and sending a hangup signal to
.Xr init .
.PP
Note that if a special file is inaccessible when
.Xr init
tries to create a process for it,
.Xr init
will log a message to the
system error logging process (see
.Xr syslogd (8))
and try to reopen the terminal every minute, reprinting the warning
message every 10 minutes.  Messages of this sort are normally
printed on the console, though other actions may occur depending
on the configuration information found in
.Pn /etc/syslog.conf .
.PP
Finally note that you should change the names of any dialup
terminals to ttyd?
where ? is in [0-9a-zA-Z], as some programs use this property of the
names to determine if a terminal is a dialup.
.PP
While it is possible to use truly arbitrary strings for terminal names,
the accounting and noticeably the
.Xr ps (1)
command make good use of the convention that tty names
(by default, and also after dialups are named as suggested above)
are distinct in the last 2 characters.
Change this and you may be sorry later, as the heuristic
.Xr ps (1)
uses based on these conventions will then break down and
.Xr ps
will run MUCH slower.
.Sh 2 "Adding users"
.PP
The procedure for adding a new user is described in
.Xr adduser (8).
You should add accounts for the initial user community, giving
each a directory and a password, and putting users who will wish
to share software in the same groups.
.PP
Several guest accounts have been provided on the distribution
system; these accounts are for people at Berkeley,
Bell Laboratories, and others
who have done major work on UNIX in the past.  You can delete these accounts,
or leave them on the system if you expect that these people would have
occasion to login as guests on your system.
.Sh 2 "Site tailoring"
.PP
All programs that require the site's name, or some similar
characteristic, obtain the information through system calls
or from files located in
.Pn /etc .
Aside from parts of the
system related to the network, to tailor the system to your
site you must simply select a site name, then edit the file
.DS
/etc/netstart
.DE
The first lines in
.Pn /etc/netstart
use a variable to set the hostname,
.DS
hostname=\fImysitename\fP
/bin/hostname $hostname
.DE
to define the value returned by the
.Xr gethostname (2)
system call.  If you are running the name server, your site
name should be your fully qualified domain name.  Programs such as
.Xr getty (8),
.Xr mail (1),
.Xr wall (1),
and
.Xr uucp (1)
use this system call so that the binary images are site
independent.
.PP
You will also need to edit
.Pn /etc/netstart
to do the network interface initialization using
.Xr ifconfig (8).
If you are not sure how to do this, see sections 5.1, 5.2, and 5.3.
If you are not running a routing daemon and have
more than one Ethernet in your environment
you will need to set up a default route;
see section 5.4 for details.
Before bringing your system up multiuser,
you should ensure that the networking is properly configured.
The network is started by running
.Pn /etc/netstart .
Once started, you should test connectivity using
.Xr ping (8).
You should first test connectivity to yourself, 
then another host on your Ethernet,
and finally a host on another Ethernet.
The
.Xr netstat (8)
program can be used to inspect and debug
your routes; see section 5.4.
.Sh 2 "Setting up the line printer system"
.PP
The line printer system consists of at least
the following files and commands:
.DS
.TS
l l.
/usr/bin/lpq	spooling queue examination program
/usr/bin/lprm	program to delete jobs from a queue
/usr/bin/lpr	program to enter a job in a printer queue
/etc/printcap	printer configuration and capability database
/usr/sbin/lpd	line printer daemon, scans spooling queues
/usr/sbin/lpc	line printer control program
/etc/hosts.lpd	list of host allowed to use the printers
.TE
.DE
.PP
The file
.Pn /etc/printcap
is a master database describing line
printers directly attached to a machine and, also, printers
accessible across a network.  The manual page
.Xr printcap (5)
describes the format of this database and also
shows the default values for such things as the directory
in which spooling is performed.  The line printer system handles
multiple printers, multiple spooling queues, local and remote
printers, and also printers attached via serial lines that require
line initialization such as the baud rate.  Raster output devices
such as a Varian or Versatec, and laser printers such as an Imagen,
are also supported by the line printer system.
.PP
Remote spooling via the network is handled with two spooling
queues, one on the local machine and one on the remote machine.
When a remote printer job is started with
.Xr lpr ,
the job is queued locally and a daemon process created to oversee the
transfer of the job to the remote machine.  If the destination
machine is unreachable, the job will remain queued until it is
possible to transfer the files to the spooling queue on the
remote machine.  The
.Xr lpq
program shows the contents of spool
queues on both the local and remote machines.
.PP
To configure your line printers, consult the printcap manual page
and the accompanying document, ``4.3BSD Line Printer Spooler Manual'' (SMM:7).
A call to the
.Xr lpd
program should be present in
.Pn /etc/rc .
.Sh 2 "Setting up the mail system"
.PP
The mail system consists of the following commands:
.DS
.TS
l l.
/usr/bin/mail	UCB mail program, described in \fImail\fP\|(1)
/usr/sbin/sendmail	mail routing program
/var/spool/mail	mail spooling directory
/var/spool/secretmail	secure mail directory
/usr/bin/xsend	secure mail sender
/usr/bin/xget	secure mail receiver
/etc/aliases	mail forwarding information
/usr/bin/newaliases	command to rebuild binary forwarding database
/usr/bin/biff	mail notification enabler
/usr/libexec/comsat	mail notification daemon
.TE
.DE
Mail is normally sent and received using the
.Xr mail (1)
command (found in
.Pn /usr/bin/mail ),
which provides a front-end to edit the messages sent
and received, and passes the messages to
.Xr sendmail (8)
for routing.
The routing algorithm uses knowledge of the network name syntax,
aliasing and forwarding information, and network topology, as
defined in the configuration file
.Pn /usr/lib/sendmail.cf ,
to process each piece of mail.
Local mail is delivered by giving it to the program
.Pn /usr/libexec/mail.local
that adds it to the mailboxes in the directory
.Pn /var/spool/mail/<username> ,
using a locking protocol to avoid problems with simultaneous updates.
After the mail is delivered, the local mail delivery daemon
.Pn /usr/libexec/comsat
is notified, which in turn notifies users who have issued a
``\fIbiff\fP y'' command that mail has arrived.
.PP
Mail queued in the directory
.Pn /var/spool/mail
is normally readable only by the recipient.
To send mail that is secure against perusal
(except by a code-breaker) you should use the secret mail facility,
which encrypts the mail.
.PP
To set up the mail facility you should read the instructions in the
file READ_ME in the directory
.Pn /usr/src/usr.sbin/sendmail
and then adjust the necessary configuration files.
You should also set up the file
.Pn /etc/aliases
for your installation, creating mail groups as appropriate.
For more informations see
``Sendmail Installation and Operation Guide'' (SMM:8) and
``Sendmail \- An Internetwork Mail Router'' (SMM:9).
.Sh 3 "Setting up a UUCP connection"
.LP
The version of
.Xr uucp
included in \*(4B has the following features:
.IP \(bu 3
support for many auto call units and dialers
in addition to the DEC DN11,
.IP \(bu 3
breakup of the spooling area into multiple subdirectories,
.IP \(bu 3
addition of an
.Pn L.cmds
file to control the set
of commands that may be executed by a remote site,
.IP \(bu 3
enhanced ``expect-send'' sequence capabilities when
logging in to a remote site,
.IP \(bu 3
new commands to be used in polling sites and
obtaining snap shots of
.Xr uucp
activity,
.IP \(bu 3
additional protocols for different communication media.
.LP
This section gives a brief overview of
.Xr uucp
and points out the most important steps in its installation.
.PP
To connect two UNIX machines with a
.Xr uucp
network link using modems,
one site must have an automatic call unit
and the other must have a dialup port.
It is better if both sites have both.
.PP
You should first read the paper in the UNIX System Manager's Manual:
``Uucp Implementation Description'' (SMM:14).
It describes in detail the file formats and conventions,
and will give you a little context.
In addition,
the document ``setup.tblms'',
located in the directory
.Pn /usr/src/usr.bin/uucp/UUAIDS ,
may be of use in tailoring the software to your needs.
.PP
The
.Xr uucp
support is located in three major directories:
.Pn /usr/bin,
.Pn /usr/lib/uucp,
and
.Pn /var/spool/uucp .
User commands are kept in
.Pn /usr/bin,
operational commands in
.Pn /usr/lib/uucp ,
and
.Pn /var/spool/uucp
is used as a spooling area.
The commands in
.Pn /usr/bin
are:
.DS
.TS
l l.
/usr/bin/uucp	file-copy command
/usr/bin/uux	remote execution command
/usr/bin/uusend	binary file transfer using mail
/usr/bin/uuencode	binary file encoder (for \fIuusend\fP)
/usr/bin/uudecode	binary file decoder (for \fIuusend\fP)
/usr/bin/uulog	scans session log files
/usr/bin/uusnap	gives a snap-shot of \fIuucp\fP activity
/usr/bin/uupoll	polls remote system until an answer is received
/usr/bin/uuname	prints a list of known uucp hosts
/usr/bin/uuq	gives information about the queue
.TE
.DE
The important files and commands in
.Pn /usr/lib/uucp
are:
.DS
.TS
l l.
/usr/lib/uucp/L-devices	list of dialers and hard-wired lines
/usr/lib/uucp/L-dialcodes	dialcode abbreviations
/usr/lib/uucp/L.aliases	hostname aliases
/usr/lib/uucp/L.cmds	commands remote sites may execute
/usr/lib/uucp/L.sys	systems to communicate with, how to connect, and when
/usr/lib/uucp/SEQF	sequence numbering control file
/usr/lib/uucp/USERFILE	remote site pathname access specifications
/usr/lib/uucp/uucico	\fIuucp\fP protocol daemon
/usr/lib/uucp/uuclean	cleans up garbage files in spool area
/usr/lib/uucp/uuxqt	\fIuucp\fP remote execution server
.TE
.DE
while the spooling area contains the following important files and directories:
.DS
.TS
l l.
/var/spool/uucp/C.	directory for command, ``C.'' files
/var/spool/uucp/D.	directory for data, ``D.'', files
/var/spool/uucp/X.	directory for command execution, ``X.'', files
/var/spool/uucp/D.\fImachine\fP	directory for local ``D.'' files
/var/spool/uucp/D.\fImachine\fPX	directory for local ``X.'' files
/var/spool/uucp/TM.	directory for temporary, ``TM.'', files
/var/spool/uucp/LOGFILE	log file of \fIuucp\fP activity
/var/spool/uucp/SYSLOG	log file of \fIuucp\fP file transfers
.TE
.DE
.PP
To install
.Xr uucp
on your system,
start by selecting a site name
(shorter than 14 characters). 
A
.Xr uucp
account must be created in the password file and a password set up.
Then,
create the appropriate spooling directories with mode 755
and owned by user
.Xr uucp ,
group \fIdaemon\fP.
.PP
If you have an auto-call unit,
the L.sys, L-dialcodes, and L-devices files should be created.
The L.sys file should contain
the phone numbers and login sequences
required to establish a connection with a
.Xr uucp
daemon on another machine.
For example, our L.sys file looks something like:
.DS
adiron Any ACU 1200 out0123456789- ogin-EOT-ogin uucp
cbosg Never Slave 300
cbosgd Never Slave 300
chico Never Slave 1200 out2010123456
.DE
The first field is the name of a site,
the second shows when the machine may be called,
the third field specifies how the host is connected
(through an ACU, a hard-wired line, etc.),
then comes the phone number to use in connecting through an auto-call unit,
and finally a login sequence.
The phone number
may contain common abbreviations that are defined in the L-dialcodes file.
The device specification should refer to devices
specified in the L-devices file.
Listing only ACU causes the
.Xr uucp
daemon,
.Xr uucico ,
to search for any available auto-call unit in L-devices.
Our L-dialcodes file is of the form:
.DS
ucb 2
out 9%
.DE
while our L-devices file is:
.DS
ACU cul0 unused 1200 ventel
.DE
Refer to the README file in the
.Xr uucp
source directory for more information about installation.
.PP
As
.Xr uucp
operates it creates (and removes) many small
files in the directories underneath
.Pn /var/spool/uucp .
Sometimes files are left undeleted;
these are most easily purged with the
.Xr uuclean
program.
The log files can grow without bound unless trimmed back;
.Xr uulog
maintains these files.
Many useful aids in maintaining your
.Xr uucp
installation are included in a subdirectory UUAIDS beneath
.Pn /usr/src/usr.bin/uucp .
Peruse this directory and read the ``setup'' instructions also located there.
