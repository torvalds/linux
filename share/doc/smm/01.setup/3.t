.\" Copyright (c) 1980, 1986, 1988, 1993
.\"	 The Regents of the University of California.  All rights reserved.
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
.\" $FreeBSD$
.\"	@(#)3.t	8.1 (Berkeley) 7/27/93
.\"
.ds lq ``
.ds rq ''
.ds RH "Upgrading a \*(Ps System
.ds CF \*(Dy
.Sh 1 "Upgrading a \*(Ps system"
.PP
This section describes the procedure for upgrading a \*(Ps
system to \*(4B.  This procedure may vary according to the version of
the system running before conversion.
If you are converting from a
System V system, some of this section will still apply (in particular,
the filesystem conversion).  However, many of the system configuration
files are different, and the executable file formats are completely
incompatible.
.PP
In particular be wary when using this information to upgrade
a \*(Ps HP300 system.
There are at least four different versions of ``\*(Ps'' out there:
.IP 1)
HPBSD 1.x from Utah.
.br
This was the original version of \*(Ps for HP300s from which the
other variants (and \*(4B) are derived.
It is largely a \*(Ps system with Sun's NFS 3.0 filesystem code and
some \*(Ps-Tahoe features (e.g. networking code).
Since the filesystem code is 4.2/4.3 vintage and the filesystem
hierarchy is largely \*(Ps, most of this section should apply.
.IP 2)
MORE/bsd from Mt. Xinu.
.br
This is a \*(Ps-Tahoe vintage system with Sun's NFS 4.0 filesystem code
upgraded with Tahoe UFS features.
The instructions for \*(Ps-Tahoe should largely apply.
.IP 3)
\*(Ps-Reno from CSRG.
.br
At least one site bootstrapped HP300 support from the Reno distribution.
The Reno filesystem code was somewhere between \*(Ps and \*(4B: the VFS switch
had been added but many of the UFS features (e.g. ``inline'' symlinks)
were missing.
The filesystem hierarchy reorganization first appeared in this release.
Be extremely careful following these instructions if you are
upgrading from the Reno distribution.
.IP 4)
HPBSD 2.0 from Utah.
.br
As if things were not bad enough already,
this release has the \*(4B filesystem and networking code
as well as some utilities, but still has a \*(Ps hierarchy.
No filesystem conversions are necessary for this upgrade,
but files will still need to be moved around.
.Sh 2 "Installation overview"
.PP
If you are running \*(Ps, upgrading your system
involves replacing your kernel and system utilities.
In general, there are three possible ways to install a new \*(Bs distribution:
(1) boot directly from the distribution tape, use it to load new binaries
onto empty disks, and then merge or restore any existing configuration files
and filesystems;
(2) use an existing \*(Ps or later system to extract the root and
.Pn /usr
filesystems from the distribution tape,
boot from the new system, then merge or restore existing
configuration files and filesystems; or
(3) extract the sources from the distribution tape onto an existing system,
and use that system to cross-compile and install \*(4B.
For this release, the second alternative is strongly advised,
with the third alternative reserved as a last resort.
In general, older binaries will continue to run under \*(4B,
but there are many exceptions that are on the critical path
for getting the system running.
Ideally, the new system binaries (root and
.Pn /usr
filesystems) should be installed on spare disk partitions,
then site-specific files should be merged into them.
Once the new system is up and fully merged, the previous root and
.Pn /usr
filesystems can be reused.
Other existing filesystems can be retained and used,
except that (as usual) the new
.Xr fsck
should be run before they are mounted.
.PP
It is \fBSTRONGLY\fP advised that you make full dumps of each filesystem
before beginning, especially any that you intend to modify in place
during the merge.
It is also desirable to run filesystem checks
of all filesystems to be converted to \*(4B before shutting down.
This is an excellent time to review your disk configuration
for possible tuning of the layout.
Most systems will need to provide a new filesystem for system use
mounted on
.Pn /var
(see below).
However, the
.Pn /tmp
filesystem can be an MFS virtual-memory-resident filesystem,
potentially freeing an existing disk partition.
(Additional swap space may be desirable as a consequence.)
See
.Xr mount_mfs (8).
.PP
The recommended installation procedure includes the following steps.
The order of these steps will probably vary according to local needs.
.IP \(bu
Extract root and
.Pn /usr
filesystems from the distribution tapes.
.IP \(bu
Extract kernel and/or user-level sources from the distribution tape
if space permits.
This can serve as the backup documentation as needed.
.IP \(bu
Configure and boot a kernel for the local system.
This can be delayed if the generic kernel from the distribution
supports enough hardware to proceed.
.IP \(bu
Build a skeletal
.Pn /var
filesystem (see
.Xr mtree (8)).
.IP \(bu
Merge site-dependent configuration files from
.Pn /etc
and
.Pn /usr/lib
into the new
.Pn /etc
directory.
Note that many file formats and contents have changed; see section 3.4
of this document.
.IP \(bu
Copy or merge files from
.Pn /usr/adm ,
.Pn /usr/spool ,
.Pn /usr/preserve ,
.Pn /usr/lib ,
and other locations into
.Pn /var .
.IP \(bu
Merge local macros, dictionaries, etc. into
.Pn /usr/share .
.IP \(bu
Merge and update local software to reflect the system changes.
.IP \(bu
Take off the rest of the morning, you've earned it!
.PP
Section 3.2 lists the files to be saved as part of the conversion process.
Section 3.3 describes the bootstrap process.
Section 3.4 discusses the merger of the saved files back into the new system.
Section 3.5 gives an overview of the major
bug fixes and changes between \*(Ps and \*(4B.
Section 3.6 provides general hints on possible problems to be
aware of when converting from \*(Ps to \*(4B.
.Sh 2 "Files to save"
.PP
The following list enumerates the standard set of files you will want to
save and suggests directories in which site-specific files should be present.
This list will likely be augmented with non-standard files you
have added to your system.
If you do not have enough space to create parallel
filesystems, you should create a
.Xr tar
image of the following files before the new filesystems are created.
The rest of this subsection describes where theses files
have moved and how they have changed.
.TS
lfC c l.
/.cshrc	\(dg	root csh startup script (moves to \f(CW/root/.cshrc\fP)
/.login	\(dg	root csh login script (moves to \f(CW/root/.login\fP)
/.profile	\(dg	root sh startup script (moves to \f(CW/root/.profile\fP)
/.rhosts	\(dg	for trusted machines and users (moves to \f(CW/root/.rhosts\fP)
/etc/disktab	\(dd	in case you changed disk partition sizes
/etc/fstab	*	disk configuration data
/etc/ftpusers	\(dg	for local additions
/etc/gettytab	\(dd	getty database
/etc/group	*	group data base
/etc/hosts	\(dg	for local host information
/etc/hosts.equiv	\(dg	for local host equivalence information
/etc/hosts.lpd	\(dg	printer access file
/etc/inetd.conf	*	Internet services configuration data
/etc/named*	\(dg	named configuration files
/etc/netstart	\(dg	network initialization
/etc/networks	\(dg	for local network information
/etc/passwd	*	user data base
/etc/printcap	*	line printer database
/etc/protocols	\(dd	in case you added any local protocols
/etc/rc	*	for any local additions
/etc/rc.local	*	site specific system startup commands
/etc/remote	\(dg	auto-dialer configuration
/etc/services	\(dd	for local additions
/etc/shells	\(dd	list of valid shells
/etc/syslog.conf	*	system logger configuration
/etc/securettys	*	merged into ttys
/etc/ttys	*	terminal line configuration data
/etc/ttytype	*	merged into ttys
/etc/termcap	\(dd	for any local entries that may have been added
/lib	\(dd	for any locally developed language processors
/usr/dict/*	\(dd	for local additions to words and papers
/usr/include/*	\(dd	for local additions
/usr/lib/aliases	*	mail forwarding data base (moves to \f(CW/etc/aliases\fP)
/usr/lib/crontab	*	cron daemon data base (moves to \f(CW/etc/crontab\fP)
/usr/lib/crontab.local	*	local cron daemon data base (moves to \f(CW/etc/crontab.local\fP)
/usr/lib/lib*.a	\(dg	for local libraries
/usr/lib/mail.rc	\(dg	system-wide mail(1) initialization (moves to \f(CW/etc/mail.rc\fP)
/usr/lib/sendmail.cf	*	sendmail configuration (moves to \f(CW/etc/sendmail.cf\fP)
/usr/lib/tmac/*	\(dd	for locally developed troff/nroff macros (moves to \f(CW/usr/share/tmac/*\fP)
/usr/lib/uucp/*	\(dg	for local uucp configuration files
/usr/man/manl	*	for manual pages for locally developed programs (moves to \f(CW/usr/local/man\fP)
/usr/spool/*	\(dg	for current mail, news, uucp files, etc. (moves to \f(CW/var/spool\fP)
/usr/src/local	\(dg	for source for locally developed programs
/sys/conf/HOST	\(dg	configuration file for your machine (moves to \f(CW/sys/<arch>/conf\fP)
/sys/conf/files.HOST	\(dg	list of special files in your kernel (moves to \f(CW/sys/<arch>/conf\fP)
/*/quotas	*	filesystem quota files (moves to \f(CW/*/quotas.user\fP)
.TE
.DS
\(dg\|Files that can be used from \*(Ps without change.
\(dd\|Files that need local changes merged into \*(4B files.
*\|Files that require special work to merge and are discussed in section 3.4.
.DE
.Sh 2 "Installing \*(4B"
.PP
The next step is to build a working \*(4B system.
This can be done by following the steps in section 2 of
this document for extracting the root and
.Pn /usr
filesystems from the distribution tape onto unused disk partitions.
For the SPARC, the root filesystem dump on the tape could also be
extracted directly.
For the HP300 and DECstation, the raw disk image can be copied
into an unused partition and this partition can then be dumped
to create an image that can be restored.
The exact procedure chosen will depend on the disk configuration
and the number of suitable disk partitions that may be used.
It is also desirable to run filesystem checks
of all filesystems to be converted to \*(4B before shutting down.
In any case, this is an excellent time to review your disk configuration
for possible tuning of the layout.
Section 2.5 and
.Xr config (8)
are required reading.
.LP
The filesystem in \*(4B has been reorganized in an effort to
meet several goals:
.IP 1)
The root filesystem should be small.
.IP 2)
There should be a per-architecture centrally-shareable read-only
.Pn /usr
filesystem.
.IP 3)
Variable per-machine directories should be concentrated below
a single mount point named
.Pn /var .
.IP 4)
Site-wide machine independent shareable text files should be separated
from architecture specific binary files and should be concentrated below
a single mount point named
.Pn /usr/share .
.LP
These goals are realized with the following general layouts.
The reorganized root filesystem has the following directories:
.TS
lfC l.
/etc	(config files)
/bin	(user binaries needed when single-user)
/sbin	(root binaries needed when single-user)
/local	(locally added binaries used only by this machine)
/tmp	(mount point for memory based filesystem)
/dev	(local devices)
/home	(mount point for AMD)
/var	(mount point for per-machine variable directories)
/usr	(mount point for multiuser binaries and files)
.TE
.LP
The reorganized
.Pn /usr
filesystem has the following directories:
.TS
lfC l.
/usr/bin	(user binaries)
/usr/contrib	(software contributed to \*(4B)
/usr/games	(binaries for games, score files in \f(CW/var\fP)
/usr/include	(standard include files)
/usr/lib	(lib*.a from old \f(CW/usr/lib\fP)
/usr/libdata	(databases from old \f(CW/usr/lib\fP)
/usr/libexec	(executables from old \f(CW/usr/lib\fP)
/usr/local	(locally added binaries used site-wide)
/usr/old	(deprecated binaries)
/usr/sbin	(root binaries)
/usr/share	(mount point for site-wide shared text)
/usr/src	(mount point for sources)
.TE
.LP
The reorganized
.Pn /usr/share
filesystem has the following directories:
.TS
lfC l.
/usr/share/calendar	(various useful calendar files)
/usr/share/dict	(dictionaries)
/usr/share/doc	(\*(4B manual sources)
/usr/share/games	(games text files)
/usr/share/groff_font	(groff font information)
/usr/share/man	(typeset manual pages)
/usr/share/misc	(dumping ground for random text files)
/usr/share/mk	(templates for \*(4B makefiles)
/usr/share/skel	(template user home directory files)
/usr/share/tmac	(various groff macro packages)
/usr/share/zoneinfo	(information on time zones)
.TE
.LP
The reorganized
.Pn /var
filesystem has the following directories:
.TS
lfC l.
/var/account	(accounting files, formerly \f(CW/usr/adm\fP)
/var/at	(\fIat\fP\|(1) spooling area)
/var/backups	(backups of system files)
/var/crash	(crash dumps)
/var/db	(system-wide databases, e.g. tags)
/var/games	(score files)
/var/log	(log files)
/var/mail	(users mail)
/var/obj	(hierarchy to build \f(CW/usr/src\fP)
/var/preserve	(preserve area for vi)
/var/quotas	(directory to store quota files)
/var/run	(directory to store *.pid files)
/var/rwho	(rwho databases)
/var/spool/ftp	(home directory for anonymous ftp)
/var/spool/mqueue	(sendmail spooling directory)
/var/spool/news	(news spooling area)
/var/spool/output	(printer spooling area)
/var/spool/uucp	(uucp spooling area)
/var/tmp	(disk-based temporary directory)
/var/users	(root of per-machine user home directories)
.TE
.PP
The \*(4B bootstrap routines pass the identity of the boot device
through to the kernel.
The kernel then uses that device as its root filesystem.
Thus, for example, if you boot from
.Pn /dev/\*(Dk1a ,
the kernel will use
.Pn \*(Dk1a
as its root filesystem. If
.Pn /dev/\*(Dk1b
is configured as a swap partition,
it will be used as the initial swap area,
otherwise the normal primary swap area (\c
.Pn /dev/\*(Dk0b )
will be used.
The \*(4B bootstrap is backward compatible with \*(Ps,
so you can replace your old bootstrap if you use it
to boot your first \*(4B kernel.
However, the \*(Ps bootstrap cannot access \*(4B filesystems,
so if you plan to convert your filesystems to \*(4B,
you must install a new bootstrap \fIbefore\fP doing the conversion.
Note that SPARC users cannot build a \*(4B compatible version
of the bootstrap, so must \fInot\fP convert their root filesystem
to the new \*(4B format.
.PP
Once you have extracted the \*(4B system and booted from it,
you will have to build a kernel customized for your configuration.
If you have any local device drivers,
they will have to be incorporated into the new kernel.
See section 4.1.3 and ``Building 4.3BSD UNIX Systems with Config'' (SMM:2).
.PP
If converting from \*(Ps, your old filesystems should be converted.
If you've modified the partition
sizes from the original \*(Ps ones, and are not already using the
\*(4B disk labels, you will have to modify the default disk partition
tables in the kernel.  Make the necessary table changes and boot
your custom kernel \fBBEFORE\fP trying to access any of your old
filesystems!  After doing this, if necessary, the remaining filesystems
may be converted in place by running the \*(4B version of
.Xr fsck (8)
on each filesystem and allowing it to make the necessary corrections.
The new version of
.Xr fsck
is more strict about the size of directories than
the version supplied with \*(Ps.
Thus the first time that it is run on a \*(Ps filesystem,
it will produce messages of the form:
.DS
\fBDIRECTORY ...: LENGTH\fP xx \fBNOT MULTIPLE OF 512 (ADJUSTED)\fP
.DE
Length ``xx'' will be the size of the directory;
it will be expanded to the next multiple of 512 bytes.
The new
.Xr fsck
will also set default \fIinterleave\fP and
\fInpsect\fP (number of physical sectors per track) values on older
filesystems, in which these fields were unused spares; this correction
will produce messages of the form:
.DS
\fBIMPOSSIBLE INTERLEAVE=0 IN SUPERBLOCK (SET TO DEFAULT)\fP\**
\fBIMPOSSIBLE NPSECT=0 IN SUPERBLOCK (SET TO DEFAULT)\fP
.DE
.FS
The defaults are to set \fIinterleave\fP to 1 and
\fInpsect\fP to \fInsect\fP.
This is correct on most drives;
it affects only performance (usually virtually unmeasurably).
.FE
Filesystems that have had their interleave and npsect values
set will be diagnosed by the old
.Xr fsck
as having a bad superblock; the old
.Xr fsck
will run only if given an alternate superblock
(\fIfsck \-b32\fP),
in which case it will re-zero these fields.
The \*(4B kernel will internally set these fields to their defaults
if fsck has not done so; again, the \fI\-b32\fP option may be
necessary for running the old
.Xr fsck .
.PP
In addition, \*(4B removes several limits on filesystem sizes
that were present in \*(Ps.
The limited filesystems
continue to work in \*(4B, but should be converted
as soon as it is convenient
by running
.Xr fsck
with the \fI\-c 2\fP option.
The sequence \fIfsck \-p \-c 2\fP will update them all,
fix the interleave and npsect fields,
fix any incorrect directory lengths,
expand maximum uid's and gid's to 32-bits,
place symbolic links less than 60 bytes into their inode,
and fill in directory type fields all at once.
The new filesystem formats are incompatible with older systems.
If you wish to continue using these filesystems with the older
systems you should make only the compatible changes using
\fIfsck \-c 1\fP.
.Sh 2 "Merging your files from \*(Ps into \*(4B"
.PP
When your system is booting reliably and you have the \*(4B root and
.Pn /usr
filesystems fully installed you will be ready
to continue with the next step in the conversion process,
merging your old files into the new system.
.PP
If you saved the files on a
.Xr tar
tape, extract them into a scratch directory, say
.Pn /usr/convert :
.DS
\fB#\fP \fImkdir /usr/convert\fP
\fB#\fP \fIcd /usr/convert\fP
\fB#\fP \fItar xp\fP
.DE
.PP
The data files marked in the previous table with a dagger (\(dg)
may be used without change from the previous system.
Those data files marked with a double dagger (\(dd) have syntax
changes or substantial enhancements.
You should start with the \*(4B version and carefully
integrate any local changes into the new file.
Usually these local changes can be incorporated
without conflict into the new file;
some exceptions are noted below.
The files marked with an asterisk (*) require
particular attention and are discussed below.
.PP
As described in section 3.3,
the most immediately obvious change in \*(4B is the reorganization
of the system filesystems.
Users of certain recent vendor releases have seen this general organization,
although \*(4B takes the reorganization a bit further.
The directories most affected are
.Pn /etc ,
that now contains only system configuration files;
.Pn /var ,
a new filesystem containing per-system spool and log files; and
.Pn /usr/share,
that contains most of the text files shareable across architectures
such as documentation and macros.
System administration programs formerly in
.Pn /etc
are now found in
.Pn /sbin
and
.Pn /usr/sbin .
Various programs and data files formerly in
.Pn /usr/lib
are now found in
.Pn /usr/libexec
and
.Pn /usr/libdata ,
respectively.
Administrative files formerly in
.Pn /usr/adm
are in
.Pn /var/account
and, similarly, log files are now in
.Pn /var/log .
The directory
.Pn /usr/ucb
has been merged into
.Pn /usr/bin ,
and the sources for programs in
.Pn /usr/bin
are in
.Pn /usr/src/usr.bin .
Other source directories parallel the destination directories;
.Pn /usr/src/etc
has been greatly expanded, and
.Pn /usr/src/share
is new.
The source for the manual pages, in general, are with the source
code for the applications they document.
Manual pages not closely corresponding to an application program
are found in
.Pn /usr/src/share/man .
The locations of all man pages is listed in
.Pn /usr/src/share/man/man0/man[1-8] .
The manual page
.Xr hier (7)
has been updated and made more detailed;
it is included in the printed documentation.
You should review it to familiarize yourself with the new layout.
.PP
A new utility,
.Xr mtree (8),
is provided to build and check filesystem hierarchies
with the proper contents, owners and permissions.
Scripts are provided in
.Pn /etc/mtree
(and
.Pn /usr/src/etc/mtree )
for the root,
.Pn /usr
and
.Pn /var
filesystems.
Once a filesystem has been made for
.Pn /var ,
.Xr mtree
can be used to create a directory hierarchy there
or you can simply use tar to extract the prototype from
the second file of the distribution tape.
.Sh 3 "Changes in the \f(CW/etc\fP directory"
.PP
The
.Pn /etc
directory now contains nearly all the host-specific configuration
files.
Note that some file formats have changed,
and those configuration files containing pathnames are nearly all affected
by the reorganization.
See the examples provided in
.Pn /etc
(installed from
.Pn /usr/src/etc )
as a guide.
The following table lists some of the local configuration files
whose locations and/or contents have changed.
.TS
l l l
lfC lfC l.
\*(Ps and Earlier	\*(4B	Comments
_	_	_
/etc/fstab	/etc/fstab	new format; see below
/etc/inetd.conf	/etc/inetd.conf	pathnames of executables changed
/etc/printcap	/etc/printcap	pathnames changed
/etc/syslog.conf	/etc/syslog.conf	pathnames of log files changed
/etc/ttys	/etc/ttys	pathnames of executables changed
/etc/passwd	/etc/master.passwd	new format; see below
/usr/lib/sendmail.cf	/etc/sendmail.cf	changed pathnames
/usr/lib/aliases	/etc/aliases	may contain changed pathnames
/etc/*.pid	/var/run/*.pid

.T&
l l l
lfC lfC l.
New in \*(Ps-Tahoe	\*(4B	Comments
_	_	_
/usr/games/dm.config	/etc/dm.conf	configuration for games (see \fIdm\fP\|(8))
/etc/zoneinfo/localtime	/etc/localtime	timezone configuration
/etc/zoneinfo	/usr/share/zoneinfo	timezone configuration
.TE
.ne 1.5i
.TS
l l l
lfC lfC l.
	New in \*(4B	Comments
_	_	_
	/etc/aliases.db	database version of the aliases file
	/etc/amd-home	location database of home directories
	/etc/amd-vol	location database of exported filesystems
	/etc/changelist	\f(CW/etc/security\fP files to back up
	/etc/csh.cshrc	system-wide csh(1) initialization file
	/etc/csh.login	system-wide csh(1) login file
	/etc/csh.logout	system-wide csh(1) logout file
	/etc/disklabels	directory for saving disklabels
	/etc/exports	NFS list of export permissions
	/etc/ftpwelcome	message displayed for ftp users; see ftpd(8)
	/etc/man.conf	lists directories searched by \fIman\fP\|(1)
	/etc/mtree	directory for local mtree files; see mtree(8)
	/etc/netgroup	NFS group list used in \f(CW/etc/exports\fP
	/etc/pwd.db	non-secure hashed user data base file
	/etc/spwd.db	secure hashed user data base file
	/etc/security	daily system security checker
.TE
.PP
System security changes require adding several new ``well-known'' groups to
.Pn /etc/group .
The groups that are needed by the system as distributed are:
.TS
l n l.
name	number	purpose
_
wheel	0	users allowed superuser privilege
daemon	1	processes that need less than wheel privilege
kmem	2	read access to kernel memory
sys	3	access to kernel sources
tty	4	access to terminals
operator	5	read access to raw disks
bin	7	group for system binaries
news	8	group for news
wsrc	9	write access to sources
games	13	access to games
staff	20	system staff
guest	31	system guests
nobody	39	the least privileged group
utmp	45	access to utmp files
dialer	117	access to remote ports and dialers
.TE
Only users in the ``wheel'' group are permitted to
.Xr su
to ``root''.
Most programs that manage directories in
.Pn /var/spool
now run set-group-id to ``daemon'' so that users cannot
directly access the files in the spool directories.
The special files that access kernel memory,
.Pn /dev/kmem
and
.Pn /dev/mem ,
are made readable only by group ``kmem''.
Standard system programs that require this access are
made set-group-id to that group.
The group ``sys'' is intended to control access to kernel sources,
and other sources belong to group ``wsrc.''
Rather than make user terminals writable by all users,
they are now placed in group ``tty'' and made only group writable.
Programs that should legitimately have access to write on user terminals
such as
.Xr talkd
and
.Xr write
now run set-group-id to ``tty''.
The ``operator'' group controls access to disks.
By default, disks are readable by group ``operator'',
so that programs such as
.Xr dump
can access the filesystem information without being set-user-id to ``root''.
The
.Xr shutdown (8)
program is executable only by group operator
and is setuid to root so that members of group operator may shut down
the system without root access.
.PP
The ownership and modes of some directories have changed.
The
.Xr at
programs now run set-user-id ``root'' instead of ``daemon.''
Also, the uucp directory no longer needs to be publicly writable,
as
.Xr tip
reverts to privileged status to remove its lock files.
After copying your version of
.Pn /var/spool ,
you should do:
.DS
\fB#\fP \fIchown \-R root /var/spool/at\fP
\fB#\fP \fIchown \-R uucp:daemon /var/spool/uucp\fP
\fB#\fP \fIchmod \-R o\-w /var/spool/uucp\fP
.DE
.PP
The format of the cron table,
.Pn /etc/crontab ,
has been changed to specify the user-id that should be used to run a process.
The userid ``nobody'' is frequently useful for non-privileged programs.
Local changes are now put in a separate file,
.Pn /etc/crontab.local .
.PP
Some of the commands previously in
.Pn /etc/rc.local
have been moved to
.Pn /etc/rc ;
several new functions are now handled by
.Pn /etc/rc ,
.Pn /etc/netstart
and
.Pn /etc/rc.local .
You should look closely at the prototype version of these files
and read the manual pages for the commands contained in it
before trying to merge your local copy.
Note in particular that
.Xr ifconfig
has had many changes,
and that host names are now fully specified as domain-style names
(e.g., vangogh.CS.Berkeley.EDU) for the benefit of the name server.
.PP
Some of the commands previously in
.Pn /etc/daily
have been moved to
.Pn /etc/security ,
and several new functions have been added to
.Pn /etc/security
to do nightly security checks on the system.
The script
.Pn /etc/daily
runs
.Pn /etc/security
each night, and mails the output to the super-user.
Some of the checks done by
.Pn /etc/security
are:
.DS
\(bu Syntax errors in the password and group files.
\(bu Duplicate user and group names and id's.
\(bu Dangerous search paths and umask values for the superuser.
\(bu Dangerous values in various initialization files.
\(bu Dangerous .rhosts files.
\(bu Dangerous directory and file ownership or permissions.
\(bu Globally exported filesystems.
\(bu Dangerous owners or permissions for special devices.
.DE
In addition, it reports any changes to setuid and setgid files, special
devices, or the files in
.Pn /etc/changelist
since the last run of
.Pn /etc/security .
Backup copies of the files are saved in
.Pn /var/backups .
Finally, the system binaries are checksummed and their permissions
validated against the
.Xr mtree (8)
specifications in
.Pn /etc/mtree .
.PP
The C-library and system binaries on the distribution tape
are compiled with new versions of
.Xr gethostbyname
and
.Xr gethostbyaddr
that use the name server,
.Xr named (8).
If you have only a small network and are not connected
to a large network, you can use the distributed library routines without
any problems; they use a linear scan of the host table
.Pn /etc/hosts
if the name server is not running.
If you are on the Internet or have a large local network,
it is recommend that you set up
and use the name server.
For instructions on how to set up the necessary configuration files,
refer to ``Name Server Operations Guide for BIND'' (SMM:10).
Several programs rely on the host name returned by
.Xr gethostname
to determine the local domain name.
.PP
If you are using the name server, your
.Xr sendmail
configuration file will need some updates to accommodate it.
See the ``Sendmail Installation and Operation Guide'' (SMM:8) and
the sample
.Xr sendmail
configuration files in
.Pn /usr/src/usr.sbin/sendmail/cf .
The aliases file,
.Pn /etc/aliases
has also been changed to add certain well-known addresses.
.Sh 3 "Shadow password files"
.PP
The password file format adds change and expiration fields
and its location has changed to protect
the encrypted passwords stored there.
The actual password file is now stored in
.Pn /etc/master.passwd .
The hashed dbm password files do not contain encrypted passwords,
but contain the file offset to the entry with the password in
.Pn /etc/master.passwd
(that is readable only by root).
Thus, the
.Fn getpwnam
and
.Fn getpwuid
functions will no longer return an encrypted password string to non-root
callers.
An old-style passwd file is created in
.Pn /etc/passwd
by the
.Xr vipw (8)
and
.Xr pwd_mkdb (8)
programs.
See also
.Xr passwd (5).
.PP
Several new users have also been added to the group of ``well-known'' users in
.Pn /etc/passwd .
The current list is:
.DS
.TS
l c.
name	number
_
root	0
daemon	1
operator	2
bin	3
games	7
uucp	66
nobody	32767
.TE
.DE
The ``daemon'' user is used for daemon processes that
do not need root privileges.
The ``operator'' user-id is used as an account for dumpers
so that they can log in without having the root password.
By placing them in the ``operator'' group,
they can get read access to the disks.
The ``uucp'' login has existed long before \*(4B,
and is noted here just to provide a common user-id.
The password entry ``nobody'' has been added to specify
the user with least privilege.  The ``games'' user is a pseudo-user
that controls access to game programs.
.PP
After installing your updated password file, you must run
.Xr pwd_mkdb (8)
to create the password database.
Note that
.Xr pwd_mkdb (8)
is run whenever
.Xr vipw (8)
is run.
.Sh 3 "The \f(CW/var\fP filesystem"
.PP
The spooling directories saved on tape may be restored in their
eventual resting places without too much concern.  Be sure to
use the `\-p' option to
.Xr tar (1)
so that files are recreated with the same file modes.
The following commands provide a guide for copying spool and log files from
an existing system into a new
.Pn /var
filesystem.
At least the following directories should already exist on
.Pn /var :
.Pn output ,
.Pn log ,
.Pn backups
and
.Pn db .
.LP
.DS
.ft CW
SRC=/oldroot/usr

cd $SRC; tar cf - msgs preserve | (cd /var && tar xpf -)
.DE
.DS
.ft CW
# copy $SRC/spool to /var
cd $SRC/spool
tar cf - at mail rwho | (cd /var && tar xpf -)
tar cf - ftp mqueue news secretmail uucp uucppublic | \e
	(cd /var/spool && tar xpf -)
.DE
.DS
.ft CW
# everything else in spool is probably a printer area
mkdir .save
mv at ftp mail mqueue rwho secretmail uucp uucppublic .save
tar cf - * | (cd /var/spool/output && tar xpf -)
mv .save/* .
rmdir .save
.DE
.DS
.ft CW
cd /var/spool/mqueue
mv syslog.7 /var/log/maillog.7
mv syslog.6 /var/log/maillog.6
mv syslog.5 /var/log/maillog.5
mv syslog.4 /var/log/maillog.4
mv syslog.3 /var/log/maillog.3
mv syslog.2 /var/log/maillog.2
mv syslog.1 /var/log/maillog.1
mv syslog.0 /var/log/maillog.0
mv syslog /var/log/maillog
.DE
.DS
.ft CW
# move $SRC/adm to /var
cd $SRC/adm
tar cf - . | (cd /var/account && tar  xpf -)
cd /var/account
rm -f msgbuf
mv messages messages.[0-9] ../log
mv wtmp wtmp.[0-9] ../log
mv lastlog ../log
.DE
.Sh 2 "Bug fixes and changes between \*(Ps and \*(4B"
.PP
The major new facilities available in the \*(4B release are
a new virtual memory system,
the addition of ISO/OSI networking support,
a new virtual filesystem interface supporting filesystem stacking,
a freely redistributable implementation of NFS,
a log-structured filesystem,
enhancement of the local filesystems to support
files and filesystems that are up to 2^63 bytes in size,
enhanced security and system management support,
and the conversion to and addition of the IEEE Std1003.1 (``POSIX'')
facilities and many of the IEEE Std1003.2 facilities.
In addition, many new utilities and additions to the C
library are present as well.
The kernel sources have been reorganized to collect all machine-dependent
files for each architecture under one directory,
and most of the machine-independent code is now free of code
conditional on specific machines.
The user structure and process structure have been reorganized
to eliminate the statically-mapped user structure and to make most
of the process resources shareable by multiple processes.
The system and include files have been converted to be compatible
with ANSI C, including function prototypes for most of the exported
functions.
There are numerous other changes throughout the system.
.Sh 3 "Changes to the kernel"
.PP
This release includes several important structural kernel changes.
The kernel uses a new internal system call convention;
the use of global (``u-dot'') variables for parameters and error returns
has been eliminated,
and interrupted system calls no longer abort using non-local goto's (longjmp's).
A new sleep interface separates signal handling from scheduling priority,
returning characteristic errors to abort or restart the current system call.
This sleep call also passes a string describing the process state,
that is used by the ps(1) program.
The old sleep interface can be used only for non-interruptible sleeps.
The sleep interface (\fItsleep\fP) can be used at any priority,
but is only interruptible if the PCATCH flag is set.
When interrupted, \fItsleep\fP returns EINTR or ERESTART.
.PP
Many data structures that were previously statically allocated
are now allocated dynamically.
These structures include mount entries, file entries,
user open file descriptors, the process entries, the vnode table,
the name cache, and the quota structures.
.PP
To protect against indiscriminate reading or writing of kernel
memory, all writing and most reading of kernel data structures
must be done using a new ``sysctl'' interface.
The information to be accessed is described through an extensible
``Management Information Base'' (MIB) style name,
described as a dotted set of components.
A new utility,
.Xr sysctl (8),
retrieves kernel state and allows processes with appropriate
privilege to set kernel state.
.Sh 3 "Security"
.PP
The kernel runs with four different levels of security.
Any superuser process can raise the security level, but only
.Fn init (8)
can lower it.
Security levels are defined as follows:
.IP \-1
Permanently insecure mode \- always run system in level 0 mode.
.IP "  0"
Insecure mode \- immutable and append-only flags may be turned off.
All devices may be read or written subject to their permissions.
.IP "  1"
Secure mode \- immutable and append-only flags may not be cleared;
disks for mounted filesystems,
.Pn /dev/mem ,
and
.Pn /dev/kmem
are read-only.
.IP "  2"
Highly secure mode \- same as secure mode, plus disks are always
read-only whether mounted or not.
This level precludes tampering with filesystems by unmounting them,
but also inhibits running
.Xr newfs (8)
while the system is multi-user.
See
.Xr chflags (1)
and the \-\fBo\fP option to
.Xr ls (1)
for information on setting and displaying the immutable and append-only
flags.
.PP
Normally, the system runs in level 0 mode while single user
and in level 1 mode while multiuser.
If the level 2 mode is desired while running multiuser,
it can be set in the startup script
.Pn /etc/rc
using
.Xr sysctl (1).
If it is desired to run the system in level 0 mode while multiuser,
the administrator must build a kernel with the variable
.Li securelevel
in the kernel source file
.Pn /sys/kern/kern_sysctl.c
initialized to \-1.
.Sh 4 "Virtual memory changes"
.PP
The new virtual memory implementation is derived from the Mach
operating system developed at Carnegie-Mellon,
and was ported to the BSD kernel at the University of Utah.
It is based on the 2.0 release of Mach
(with some bug fixes from the 2.5 and 3.0 releases)
and retains many of its essential features such as
the separation of the machine dependent and independent layers
(the ``pmap'' interface),
efficient memory utilization using copy-on-write
and other lazy-evaluation techniques,
and support for large, sparse address spaces.
It does not include the ``external pager'' interface instead using
a primitive internal pager interface.
The Mach virtual memory system call interface has been replaced with the
``mmap''-based interface described in the ``Berkeley Software
Architecture Manual'' (see UNIX Programmer's Manual,
Supplementary Documents, PSD:5).
The interface is similar to the interfaces shipped
by several commercial vendors such as Sun, USL, and Convex Computer Corp.
The integration of the new virtual memory is functionally complete,
but still has serious performance problems under heavy memory load.
The internal kernel interfaces have not yet been completed
and the memory pool and buffer cache have not been merged.
Some additional caveats:
.IP \(bu
Since the code is based on the 2.0 release of Mach,
bugs and misfeatures of the BSD version should not be considered
short-comings of the current Mach virtual memory system.
.IP \(bu
Because of the disjoint virtual memory (page) and IO (buffer) caches,
it is possible to see inconsistencies if using both the mmap and
read/write interfaces on the same file simultaneously.
.IP \(bu
Swap space is allocated on-demand rather than up front and no
allocation checks are performed so it is possible to over-commit
memory and eventually deadlock.
.IP \(bu
The semantics of the
.Xr vfork (2)
system call are slightly different.
The synchronization between parent and child is preserved,
but the memory sharing aspect is not.
In practice this has been enough for backward compatibility,
but newer code should just use
.Xr fork (2).
.Sh 4 "Networking additions and changes"
.PP
The ISO/OSI Networking consists of a kernel implementation of
transport class 4 (TP-4),
connectionless networking protocol (CLNP),
and 802.3-based link-level support (hardware-compatible with Ethernet\**).
.FS
Ethernet is a trademark of the Xerox Corporation.
.FE
We also include support for ISO Connection-Oriented Network Service,
X.25, TP-0.
The session and presentation layers are provided outside
the kernel using the ISO Development Environment by Marshall Rose,
that is available via anonymous FTP
(but is not included on the distribution tape).
Included in this development environment are file
transfer and management (FTAM), virtual terminals (VT),
a directory services implementation (X.500),
and miscellaneous other utilities.
.PP
Kernel support for the ISO OSI protocols is enabled with the ISO option
in the kernel configuration file.
The
.Xr iso (4)
manual page describes the protocols and addressing;
see also
.Xr clnp (4),
.Xr tp (4)
and
.Xr cltp (4).
The OSI equivalent to ARP is ESIS (End System to Intermediate System Routing
Protocol); running this protocol is mandatory, however one can manually add
translations for machines that do not participate by use of the
.Xr route (8)
command.
Additional information is provided in the manual page describing
.Xr esis (4).
.PP
The command
.Xr route (8)
has a new syntax and several new capabilities:
it can install routes with a specified destination and mask,
and can change route characteristics such as hop count, packet size
and window size.
.PP
Several important enhancements have been added to the TCP/IP
protocols including TCP header prediction and
serial line IP (SLIP) with header compression.
The routing implementation has been completely rewritten
to use a hierarchical routing tree with a mask per route
to support the arbitrary levels of routing found in the ISO protocols.
The routing table also stores and caches route characteristics
to speed the adaptation of the throughput and congestion avoidance
algorithms.
.PP
The format of the
.I sockaddr
structure (the structure used to describe a generic network address with an
address family and family-specific data)
has changed from previous releases,
as have the address family-specific versions of this structure.
The
.I sa_family
family field has been split into a length,
.Pn sa_len ,
and a family,
.Pn sa_family .
System calls that pass a
.I sockaddr
structure into the kernel (e.g.
.Fn sendto
and
.Fn connect )
have a separate parameter that specifies the
.I sockaddr
length, and thus it is not necessary to fill in the
.I sa_len
field for those system calls.
System calls that pass a
.I sockaddr
structure back from the kernel (e.g.
.Fn recvfrom
and
.Fn accept )
receive a completely filled-in
.I sockaddr
structure, thus the length field is valid.
Because this would not work for old binaries,
the new library uses a different system call number.
Thus, most networking programs compiled under \*(4B are incompatible
with older systems.
.PP
Although this change is mostly source and binary compatible
with old programs, there are three exceptions.
Programs with statically initialized
.I sockaddr
structures
(usually the Internet form, a
.I sockaddr_in )
are not compatible.
Generally, such programs should be changed to fill in the structure
at run time, as C allows no way to initialize a structure without
assuming the order and number of fields.
Also, programs with use structures to describe a network packet format
that contain embedded
.I sockaddr
structures also require change; a definition of an
.I osockaddr
structure is provided for this purpose.
Finally, programs that use the
.Sm SIOCGIFCONF
ioctl to get a complete list of interface addresses
need to check the
.I sa_len
field when iterating through the array of addresses returned,
as not all the structures returned have the same length
(this variance in length is nearly guaranteed by the presence of link-layer
address structures).
.Sh 4 "Additions and changes to filesystems"
.PP
The \*(4B distribution contains most of the interfaces
specified in the IEEE Std1003.1 system interface standard.
Filesystem additions include IEEE Std1003.1 FIFOs,
byte-range file locking, and saved user and group identifiers.
.PP
A new virtual filesystem interface has been added to the
kernel to support multiple filesystems.
In comparison with other interfaces,
the Berkeley interface has been structured for more efficient support
of filesystems that maintain state (such as the local filesystem).
The interface has been extended with support for stackable
filesystems done at UCLA.
These extensions allow for filesystems to be layered on top of each
other and allow new vnode operations to be added without requiring
changes to existing filesystem implementations.
For example,
the umap filesystem (see
.Xr mount_umap (8))
is used to mount a sub-tree of an existing filesystem
that uses a different set of uids and gids than the local system.
Such a filesystem could be mounted from a remote site via NFS or it
could be a filesystem on removable media brought from some foreign
location that uses a different password file.
.PP
Other new filesystems that may be stacked include the loopback filesystem
.Xr mount_lofs (8),
and the kernel filesystem
.Xr mount_kernfs (8).
.PP
The buffer cache in the kernel is now organized as a file block cache
rather than a device block cache.
As a consequence, cached blocks from a file
and from the corresponding block device would no longer be kept consistent.
The block device thus has little remaining value.
Three changes have been made for these reasons:
.IP 1)
block devices may not be opened while they are mounted,
and may not be mounted while open, so that the two versions of cached
file blocks cannot be created,
.IP 2)
filesystem checks of the root now use the raw device
to access the root filesystem, and
.IP 3)
the root filesystem is initially mounted read-only
so that nothing can be written back to disk during or after change to
the raw filesystem by
.Xr fsck .
.LP
The root filesystem may be made writable while in single-user mode
with the command:
.DS
.ft CW
mount \-uw /
.DE
The mount command has an option to update the flags on a mounted filesystem,
including the ability to upgrade a filesystem from read-only to read-write
or downgrade it from read-write to read-only.
.PP
In addition to the local ``fast filesystem'',
we have added an implementation of the network filesystem (NFS)
that fully interoperates with the NFS shipped by Sun and its licensees.
Because our NFS implementation was implemented
by Rick Macklem of the University of Guelph
using only the publicly available NFS specification,
it does not require a license from Sun to use in source or binary form.
By default it runs over UDP to be compatible with Sun's implementation.
However, it can be configured on a per-mount basis to run over TCP.
Using TCP allows it to be used quickly and efficiently through
gateways and over long-haul networks.
Using an extended protocol, it supports Leases to allow a limited
callback mechanism that greatly reduces the network traffic necessary
to maintain cache consistency between the server and its clients.
Its use will be familiar to users of other implementations of NFS.
See the manual pages
.Xr mount (8),
.Xr mountd (8),
.Xr fstab (5),
.Xr exports (5),
.Xr netgroup (5),
.Xr nfsd (8),
.Xr nfsiod (8),
and
.Xr nfssvc (8).
and the document ``The 4.4BSD NFS Implementation'' (SMM:6)
for further information.
The format of
.Pn /etc/fstab
has changed from previous \*(Bs releases
to a blank-separated format to allow colons in pathnames.
.PP
A new local filesystem, the log-structured filesystem (LFS),
has been added to the system.
It provides near disk-speed output and fast crash recovery.
This work is based, in part, on the LFS filesystem created
for the Sprite operating system at Berkeley.
While the kernel implementation is almost complete,
only some of the utilities to support the
filesystem have been written,
so we do not recommend it for production use.
See
.Xr newlfs (8),
.Xr mount_lfs (8)
and
.Xr lfs_cleanerd (8)
for more information.
For an in-depth description of the implementation and performance
characteristics of log-structured filesystems in general,
and this one in particular, see Dr. Margo Seltzer's doctoral thesis,
available from the University of California Computer Science Department.
.PP
We have also added a memory-based filesystem that runs in
pageable memory, allowing large temporary filesystems without
requiring dedicated physical memory.
.PP
The local ``fast filesystem'' has been enhanced to do
clustering that allows large pieces of files to be
allocated contiguously resulting in near doubling
of filesystem throughput.
The filesystem interface has been extended to allow
files and filesystems to grow to 2^63 bytes in size.
The quota system has been rewritten to support both
user and group quotas (simultaneously if desired).
Quota expiration is based on time rather than
the previous metric of number of logins over quota.
This change makes quotas more useful on fileservers
onto which users seldom login.
.PP
The system security has been greatly enhanced by the
addition of additional file flags that permit a file to be
marked as immutable or append only.
Once set, these flags can only be cleared by the super-user
when the system is running in insecure mode (normally, single-user).
In addition to the immutable and append-only flags,
the filesystem supports a new user-settable flag ``nodump''.
(File flags are set using the
.Xr chflags (1)
utility.)
When set on a file,
.Xr dump (8)
will omit the file from incremental backups
but retain them on full backups.
See the ``-h'' flag to
.Xr dump (8)
for details on how to change this default.
The ``nodump'' flag is usually set on core dumps,
system crash dumps, and object files generated by the compiler.
Note that the flag is not preserved when files are copied
so that installing an object file will cause it to be preserved.
.PP
The filesystem format used in \*(4B has several additions.
Directory entries have an additional field,
.Pn d_type ,
that identifies the type of the entry
(normally found in the
.Pn st_mode
field of the
.Pn stat
structure).
This field is particularly useful for identifying
directories without the need to use
.Xr stat (2).
.PP
Short (less than sixty byte) symbolic links are now stored
in the inode itself rather than in a separate data block.
This saves disk space and makes access of symbolic links faster.
Short symbolic links are not given a special type,
so a user-level application is unaware of their special treatment.
Unlike pre-\*(4B systems, symbolic links do
not have an owner, group, access mode, times, etc.
Instead, these attributes are taken from the directory that contains the link.
The only attributes returned from an
.Xr lstat (2)
that refer to the symbolic link itself are the file type (S_IFLNK),
size, blocks, and link count (always 1).
.PP
An implementation of an auto-mounter daemon,
.Xr amd ,
was contributed by Jan-Simon Pendry of the
Imperial College of Science, Technology & Medicine.
See the document ``AMD \- The 4.4BSD Automounter'' (SMM:13)
for further information.
.PP
The directory
.Pn /dev/fd
contains special files
.Pn 0
through
.Pn 63
that, when opened, duplicate the corresponding file descriptor.
The names
.Pn /dev/stdin ,
.Pn /dev/stdout
and
.Pn /dev/stderr
refer to file descriptors 0, 1 and 2.
See
.Xr fd (4)
and
.Xr mount_fdesc (8)
for more information.
.Sh 4 "POSIX terminal driver changes"
.PP
The \*(4B system uses the IEEE P1003.1 (POSIX.1) terminal interface
rather than the previous \*(Bs terminal interface.
The terminal driver is similar to the System V terminal driver
with the addition of the necessary extensions to get the
functionality previously available in the \*(Ps terminal driver.
Both the old
.Xr ioctl
calls and old options to
.Xr stty (1)
are emulated.
This emulation is expected to be unavailable in many vendors releases,
so conversion to the new interface is encouraged.
.PP
\*(4B also adds the IEEE Std1003.1 job control interface,
that is similar to the \*(Ps job control interface,
but adds a security model that was missing in the
\*(Ps job control implementation.
A new system call,
.Fn setsid ,
creates a job-control session consisting of a single process
group with one member, the caller, that becomes a session leader.
Only a session leader may acquire a controlling terminal.
This is done explicitly via a
.Sm TIOCSCTTY
.Fn ioctl
call, not implicitly by an
.Fn open
call.
The call fails if the terminal is in use.
Programs that allocate controlling terminals (or pseudo-terminals)
require change to work in this environment.
The versions of
.Xr xterm
provided in the X11R5 release includes the necessary changes.
New library routines are available for allocating and initializing
pseudo-terminals and other terminals as controlling terminal; see
.Pn /usr/src/lib/libutil/pty.c
and
.Pn /usr/src/lib/libutil/login_tty.c .
.PP
The POSIX job control model formalizes the previous conventions
used in setting up a process group.
Unfortunately, this requires that changes be made in a defined order
and with some synchronization that were not necessary in the past.
Older job control shells (csh, ksh) will generally not operate correctly
with the new system.
.PP
Most of the other kernel interfaces have been changed to correspond
with the POSIX.1 interface, although that work is not complete.
See the relevant manual pages and the IEEE POSIX standard.
.Sh 4 "Native operating system compatibility"
.PP
Both the HP300 and SPARC ports feature the ability to run binaries
built for the native operating system (HP-UX or SunOS) by emulating
their system calls.
Building an HP300 kernel with the HPUXCOMPAT and COMPAT_OHPUX options
or a SPARC kernel with the COMPAT_SUNOS option will enable this feature
(on by default in the generic kernel provided in the root filesystem image).
Though this native operating system compatibility was provided by the
developers as needed for their purposes and is by no means complete,
it is complete enough to run several non-trivial applications including
those that require HP-UX or SunOS shared libraries.
For example, the vendor supplied X11 server and windowing environment
can be used on both the HP300 and SPARC.
.PP
It is important to remember that merely copying over a native binary
and executing it (or executing it directly across NFS) does not imply
that it will run.
All but the most trivial of applications are likely to require access
to auxiliary files that do not exist under \*(4B (e.g.
.Pn /etc/ld.so.cache )
or have a slightly different format (e.g.
.Pn /etc/passwd ).
However, by using system call tracing and
through creative use of symlinks,
many problems can be tracked down and corrected.
.PP
The DECstation port also has code for ULTRIX emulation
(kernel option ULTRIXCOMPAT, not compiled into the generic kernel)
but it was used primarily for initially bootstrapping the port and
has not been used since.
Hence, some work may be required to make it generally useful.
.Sh 3 "Changes to the utilities"
.PP
We have been tracking the IEEE Std1003.2 shell and utility work
and have included prototypes of many of the proposed utilities
based on draft 12 of the POSIX.2 Shell and Utilities document.
Because most of the traditional utilities have been replaced
with implementations conformant to the POSIX standards,
you should realize that the utility software may not be as stable,
reliable or well documented as in traditional Berkeley releases.
In particular, almost the entire manual suite has been rewritten to
reflect the POSIX defined interfaces, and in some instances
it does not correctly reflect the current state of the software.
It is also worth noting that, in rewriting this software, we have generally
been rewarded with significant performance improvements.
Most of the libraries and header files have been converted
to be compliant with ANSI C.
The shipped compiler (gcc) is a superset of ANSI C,
but supports traditional C as a command-line option.
The system libraries and utilities all compile
with either ANSI or traditional C.
.Sh 4 "Make and Makefiles"
.PP
This release uses a completely new version of the
.Xr make
program derived from the
.Xr pmake
program developed by the Sprite project at Berkeley.
It supports existing makefiles, although certain incorrect makefiles
may fail.
The makefiles for the \*(4B sources make extensive use of the new
facilities, especially conditionals and file inclusion, and are thus
completely incompatible with older versions of
.Xr make
(but nearly all the makefiles are now trivial!).
The standard include files for
.Xr make
are in
.Pn /usr/share/mk .
There is a
.Pn bsd.README
file in
.Pn /usr/src/share/mk .
.PP
Another global change supported by the new
.Xr make
is designed to allow multiple architectures to share a copy of the sources.
If a subdirectory named
.Pn obj
is present in the current directory,
.Xr make
descends into that directory and creates all object and other files there.
We use this by building a directory hierarchy in
.Pn /var/obj
that parallels
.Pn /usr/src .
We then create the
.Pn obj
subdirectories in
.Pn /usr/src
as symbolic links to the corresponding directories in
.Pn /var/obj .
(This step is automated.
The command ``make obj'' in
.Pn /usr/src
builds both the local symlink and the shadow directory,
using
.Pn /usr/obj ,
that may be a symbolic link, as the root of the shadow tree.
The use of
.Pn /usr/obj
is for historic reasons only, and the system make configuration files in
.Pn /usr/share/mk
can trivially be modified to use
.Pn /var/obj
instead.)
We have one
.Pn /var/obj
hierarchy on the local system, and another on each
system that shares the source filesystem.
All the sources in
.Pn /usr/src
except for
.Pn /usr/src/contrib
and portions of
.Pn /usr/src/old
have been converted to use the new make and
.Pn obj
subdirectories;
this change allows compilation for multiple
architectures from the same source tree
(that may be mounted read-only).
.Sh 4 "Kerberos"
.PP
The Kerberos authentication system designed by MIT (version 5)
is included in this release.
See
.Xr kerberos (8)
for a general introduction.
Pluggable Authentication Modules (PAM) can use Kerberos
at the system administrator's discretion.
If it is configured,
apps such as 
.Xr login (1),
.Xr passwd (1),
.Xr ftp (1)
and
.Xr ssh (1)
can use it automatically.
The file
Each system needs the file
.Pn /etc/krb5.conf
to set its realm and local servers,
and a private key stored in
.Pn /etc/krb5.keytab
(see
.Xr ktutil (8)).
The Kerberos server should be set up on a single,
physically secure,
server machine.
Users and hosts may be added and modified with
.Xr kadmin (8).
.PP
Note that the password-changing program
.Xr passwd (1)
can change the Kerberos password,
if configured by the administrator using PAM.
The
.Li \-l
option to
.Xr passwd (1)
changes the ``local'' password if one exists.
.Sh 4 "Timezone support"
.PP
The timezone conversion code in the C library uses data files installed in
.Pn /usr/share/zoneinfo
to convert from ``GMT'' to various timezones.  The data file for the default
timezone for the system should be copied to
.Pn /etc/localtime .
Other timezones can be selected by setting the TZ environment variable.
.PP
The data files initially installed in
.Pn /usr/share/zoneinfo
include corrections for leap seconds since the beginning of 1970.
Thus, they assume that the
kernel will increment the time at a constant rate during a leap second;
that is, time just keeps on ticking.  The conversion routines will then
name a leap second 23:59:60.  For purists, this effectively means that
the kernel maintains TAI (International Atomic Time) rather than UTC
(Coordinated Universal Time, aka GMT).
.PP
For systems that run current NTP (Network Time Protocol) implementations
or that wish to conform to the letter of the POSIX.1 law, it is possible
to rebuild the timezone data files so that leap seconds are not counted.
(NTP causes the time to jump over a leap second, and POSIX effectively
requires the clock to be reset by hand when a leap second occurs.
In this mode, the kernel effectively runs UTC rather than TAI.)
.PP
The data files without leap second information
are constructed from the source directory,
.Pn /usr/src/share/zoneinfo .
Change the variable REDO in Makefile
from ``right'' to ``posix'', and then do
.DS
make obj	(if necessary)
make
make install
.DE
.PP
You will then need to copy the correct default zone file to
.Pn /etc/localtime ,
as the old one would still have used leap seconds, and because the Makefile
installs a default
.Pn /etc/localtime
each time ``make install'' is done.
.PP
It is possible to install both sets of timezone data files.  This results
in subdirectories
.Pn /usr/share/zoneinfo/right
and
.Pn /usr/share/zoneinfo/posix .
Each contain a complete set of zone files.
See
.Pn /usr/src/share/zoneinfo/Makefile
for details.
.Sh 4 "Additions and changes to the libraries"
.PP
Notable additions to the libraries include functions to traverse a
filesystem hierarchy, database interfaces to btree and hashing functions,
a new, faster implementation of stdio and a radix and merge sort
functions.
.PP
The
.Xr fts (3)
functions will do either physical or logical traversal of
a file hierarchy as well as handle essentially infinite depth
filesystems and filesystems with cycles.
All the utilities in \*(4B which traverse file hierarchies
have been converted to use
.Xr fts (3).
The conversion has always resulted in a significant performance
gain, often of four or five to one in system time.
.PP
The
.Xr dbopen (3)
functions are intended to be a family of database access methods.
Currently, they consist of
.Xr hash (3),
an extensible, dynamic hashing scheme,
.Xr btree (3),
a sorted, balanced tree structure (B+tree's), and
.Xr recno (3),
a flat-file interface for fixed or variable length records
referenced by logical record number.
Each of the access methods stores associated key/data pairs and
uses the same record oriented interface for access.
.PP
The
.Xr qsort (3)
function has been rewritten for additional performance.
In addition, three new types of sorting functions,
.Xr heapsort (3),
.Xr mergesort (3)
and
.Xr radixsort (3)
have been added to the system.
The
.Xr mergesort
function is optimized for data with pre-existing order,
in which case it usually significantly outperforms
.Xr qsort .
The
.Xr radixsort (3)
functions are variants of most-significant-byte radix sorting.
They take time linear to the number of bytes to be
sorted, usually significantly outperforming
.Xr qsort
on data that can be sorted in this fashion.
An implementation of the POSIX 1003.2 standard
.Xr sort (1),
based on
.Xr radixsort ,
is included in
.Pn /usr/src/contrib/sort .
.PP
Some additional comments about the \*(4B C library:
.IP \(bu
The floating point support in the C library has been replaced
and is now accurate.
.IP \(bu
The C functions specified by both ANSI C, POSIX 1003.1 and
1003.2 are now part of the C library.
This includes support for file name matching, shell globbing
and both basic and extended regular expressions.
.IP \(bu
ANSI C multibyte and wide character support has been integrated.
The rune functionality from the Bell Labs' Plan 9 system is provided
as well.
.IP \(bu
The
.Xr termcap (3)
functions have been generalized and replaced with a general
purpose interface named
.Xr getcap (3).
.IP \(bu
The
.Xr stdio (3)
routines have been replaced, and are usually much faster.
In addition, the
.Xr funopen (3)
interface permits applications to provide their own I/O stream
function support.
.PP
The
.Xr curses (3)
library has been largely rewritten.
Important additional features include support for scrolling and
.Xr termios (3).
.PP
An application front-end editing library, named libedit, has been
added to the system.
.PP
A superset implementation of the SunOS kernel memory interface library,
libkvm, has been integrated into the system.
.PP
.Sh 4 "Additions and changes to other utilities"
.PP
There are many new utilities, offering many new capabilities,
in \*(4B.
Skimming through the section 1 and section 8 manual pages is sure
to be useful.
The additions to the utility suite include greatly enhanced versions of
programs that display system status information, implementations of
various traditional tools described in the IEEE Std1003.2 standard,
new tools not previous available on Berkeley UNIX systems,
and many others.
Also, with only a very few exceptions, all the utilities from
\*(Ps that included proprietary source code have been replaced,
and their \*(4B counterparts are freely redistributable.
Normally, this replacement resulted in significant performance
improvements and the increase of the limits imposed on data by
the utility as well.
.PP
A summary of specific additions and changes are as follows:
.TS
lfC l.
amd	An auto-mounter implementation.
ar	Replacement of the historic archive format with a new one.
awk	Replaced by gawk; see /usr/src/old/awk for the historic version.
bdes	Utility implementing DES modes of operation described in FIPS PUB 81.
calendar	Addition of an interface for system calendars.
cap_mkdb	Utility for building hashed versions of termcap style databases.
cc	Replacement of pcc with gcc suite.
chflags	A utility for setting the per-file user and system flags.
chfn	An editor based replacement for changing user information.
chpass	An editor based replacement for changing user information.
chsh	An editor based replacement for changing user information.
cksum	The POSIX 1003.2 checksum utility; compatible with sum.
column	A columnar text formatting utility.
cp	POSIX 1003.2 compatible, able to copy special files.
csh	Freely redistributable and 8-bit clean.
date	User specified formats added.
dd	New EBCDIC conversion tables, major performance improvements.
dev_mkdb	Hashed interface to devices.
dm	Dungeon master.
find	Several new options and primaries, major performance improvements.
fstat	Utility displaying information on files open on the system.
ftpd	Connection logging added.
hexdump	A binary dump utility, superseding od.
id	The POSIX 1003.2 user identification utility.
inetd	Tcpmux added.
jot	A text formatting utility.
kdump	A system-call tracing facility.
ktrace	A system-call tracing facility.
kvm_mkdb	Hashed interface to the kernel name list.
lam	A text formatting utility.
lex	A new, freely redistributable, significantly faster version.
locate	A database of the system files, by name, constructed weekly.
logname	The POSIX 1003.2 user identification utility.
mail.local	New local mail delivery agent, replacing mail.
make	Replaced with a new, more powerful make, supporting include files.
man	Added support for man page location configuration.
mkdep	A new utility for generating make dependency lists.
mkfifo	The POSIX 1003.2 FIFO creation utility.
mtree	A new utility for mapping file hierarchies to a file.
nfsstat	An NFS statistics utility.
nvi	A freely redistributable replacement for the ex/vi editors.
pax	The POSIX 1003.2 replacement for cpio and tar.
printf	The POSIX 1003.2 replacement for echo.
roff	Replaced by groff; see /usr/src/old/roff for the historic versions.
rs	New utility for text formatting.
shar	An archive building utility.
sysctl	MIB-style interface to system state.
tcopy	Fast tape-to-tape copying and verification.
touch	Time and file reference specifications.
tput	The POSIX 1003.2 terminal display utility.
tr	Addition of character classes.
uname	The POSIX 1003.2 system identification utility.
vis	A filter for converting and displaying non-printable characters.
xargs	The POSIX 1003.2 argument list constructor utility.
yacc	A new, freely redistributable, significantly faster version.
.TE
.PP
The new versions of
.Xr lex (1)
(``flex'') and
.Xr yacc (1)
(``zoo'') should be installed early on if attempting to
cross-compile \*(4B on another system.
Note that the new
.Xr lex
program is not completely backward compatible with historic versions of
.Xr lex ,
although it is believed that all documented features are supported.
.PP
The
.Xr find
utility has two new options that are important to be aware of if you
intend to use NFS.
The ``fstype'' and ``prune'' options can be used together to prevent
find from crossing NFS mount points.
See
.Pn /etc/daily
for an example of their use.
.Sh 2 "Hints on converting from \*(Ps to \*(4B"
.PP
This section summarizes changes between
\*(Ps and \*(4B that are likely to
cause difficulty in doing the conversion.
It does not include changes in the network;
see section 5 for information on setting up the network.
.PP
Since the stat st_size field is now 64-bits instead of 32,
doing something like:
.DS
.ft CW
foo(st.st_size);
.DE
and then (improperly) defining foo with an ``int'' or ``long'' parameter:
.DS
.ft CW
foo(size)
	int size;
{
	...
}
.DE
will fail miserably (well, it might work on a little endian machine).
This problem showed up in
.Xr emacs (1)
as well as several other programs.
A related problem is improperly casting (or failing to cast)
the second argument to
.Xr lseek (2),
.Xr truncate (2),
or
.Xr ftruncate (2)
ala:
.DS
.ft CW
lseek(fd, (long)off, 0);
.DE
or
.DS
.ft CW
lseek(fd, 0, 0);
.DE
The best solution is to include
.Pn <unistd.h>
which has prototypes that catch these types of errors.
.PP
Determining the ``namelen'' parameter for a
.Xr connect (2)
call on a unix domain socket should use the ``SUN_LEN'' macro from
.Pn <sys/un.h> .
One old way that was used:
.DS
.ft CW
addrlen = strlen(unaddr.sun_path) + sizeof(unaddr.sun_family);
.DE
no longer works as there is an additional
.Pn sun_len
field.
.PP
The kernel's limit on the number of open files has been
increased from 20 to 64.
It is now possible to change this limit almost arbitrarily.
The standard I/O library
autoconfigures to the kernel limit.
Note that file (``_iob'') entries may be allocated by
.Xr malloc
from
.Xr fopen ;
this allocation has been known to cause problems with programs
that use their own memory allocators.
Memory allocation does not occur until after 20 files have been opened
by the standard I/O library.
.PP
.Xr Select
can be used with more than 32 descriptors
by using arrays of \fBint\fPs for the bit fields rather than single \fBint\fPs.
Programs that used
.Xr getdtablesize
as their first argument to
.Xr select
will no longer work correctly.
Usually the program can be modified to correctly specify the number
of bits in an \fBint\fP.
Alternatively the program can be modified to use an array of \fBint\fPs.
There are a set of macros available in
.Pn <sys/types.h>
to simplify this.
See
.Xr select (2).
.PP
Old core files will not be intelligible by the current debuggers
because of numerous changes to the user structure
and because the kernel stack has been enlarged.
The
.Xr a.out
header that was in the user structure is no longer present.
Locally-written debuggers that try to check the magic number
will need to be changed.
.PP
Files may not be deleted from directories having the ``sticky'' (ISVTX) bit
set in their modes
except by the owner of the file or of the directory, or by the superuser.
This is primarily to protect users' files in publicly-writable directories
such as
.Pn /tmp
and
.Pn /var/tmp .
All publicly-writable directories should have their ``sticky'' bits set
with ``chmod +t.''
.PP
The following two sections contain additional notes about
changes in \*(4B that affect the installation of local files;
be sure to read them as well.
