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
.\"	@(#)a.t	8.1 (Berkeley) 6/8/93
.\"
.ds RH Summary of facilities
.bp
.SH
\s+2I.  Summary of facilities\s0
.PP
.de h
.br
.if n .ne 8
\fB\\$1 \\$2\fP
.br
..
.nr H1 0
.NH
Kernel primitives
.LP
.h 1.1. "Process naming and protection
.in +5
.TS
lw(1.6i) aw(3i).
sethostid	set UNIX host id
gethostid	get UNIX host id
sethostname	set UNIX host name
gethostname	get UNIX host name
getpid	get process id
fork	create new process
exit	terminate a process
execve	execute a different process
getuid	get user id
geteuid	get effective user id
setreuid	set real and effective user id's
getgid	get accounting group id
getegid	get effective accounting group id
getgroups	get access group set
setregid	set real and effective group id's
setgroups	set access group set
getpgrp	get process group
setpgrp	set process group
.TE
.in -5
.h 1.2 "Memory management
.in +5
.TS
lw(1.6i) aw(3i).
<sys/mman.h>	memory management definitions
sbrk	change data section size
sstk\(dg	change stack section size
getpagesize	get memory page size
mmap\(dg	map pages of memory
msync\(dg	flush modified mapped pages to filesystem
munmap\(dg	unmap memory
mprotect\(dg	change protection of pages
madvise\(dg	give memory management advice
mincore\(dg	determine core residency of pages
msleep\(dg	sleep on a lock
mwakeup\(dg	wakeup process sleeping on a lock
.TE
.FS
\(dg Not supported in 4.3BSD.
.FE
.in -5
.h 1.3 "Signals
.in +5
.TS
lw(1.6i) aw(3i).
<signal.h>	signal definitions
sigvec	set handler for signal
kill	send signal to process
killpgrp	send signal to process group
sigblock	block set of signals
sigsetmask	restore set of blocked signals
sigpause	wait for signals
sigstack	set software stack for signals
.TE
.in -5
.h 1.4 "Timing and statistics
.in +5
.TS
lw(1.6i) aw(3i).
<sys/time.h>	time-related definitions
gettimeofday	get current time and timezone
settimeofday	set current time and timezone
getitimer	read an interval timer
setitimer	get and set an interval timer
profil	profile process
.TE
.in -5
.h 1.5 "Descriptors
.in +5
.TS
lw(1.6i) aw(3i).
getdtablesize	descriptor reference table size
dup	duplicate descriptor
dup2	duplicate to specified index
close	close descriptor
select	multiplex input/output
fcntl	control descriptor options
wrap\(dg	wrap descriptor with protocol
.TE
.FS
\(dg Not supported in 4.3BSD.
.FE
.in -5
.h 1.6 "Resource controls
.in +5
.TS
lw(1.6i) aw(3i).
<sys/resource.h>	resource-related definitions
getpriority	get process priority
setpriority	set process priority
getrusage	get resource usage
getrlimit	get resource limitations
setrlimit	set resource limitations
.TE
.in -5
.h 1.7 "System operation support
.in +5
.TS
lw(1.6i) aw(3i).
mount	mount a device file system
swapon	add a swap device
umount	umount a file system
sync	flush system caches
reboot	reboot a machine
acct	specify accounting file
.TE
.in -5
.NH
System facilities
.LP
.h 2.1 "Generic operations
.in +5
.TS
lw(1.6i) aw(3i).
read	read data
write	write data
<sys/uio.h>	scatter-gather related definitions
readv	scattered data input
writev	gathered data output
<sys/ioctl.h>	standard control operations
ioctl	device control operation
.TE
.in -5
.h 2.2 "File system
.PP
Operations marked with a * exist in two forms: as shown,
operating on a file name, and operating on a file descriptor,
when the name is preceded with a ``f''.
.in +5
.TS
lw(1.6i) aw(3i).
<sys/file.h>	file system definitions
chdir	change directory
chroot	change root directory
mkdir	make a directory
rmdir	remove a directory
open	open a new or existing file
mknod	make a special file
portal\(dg	make a portal entry
unlink	remove a link
stat*	return status for a file	
lstat	returned status of link
chown*	change owner
chmod*	change mode
utimes	change access/modify times
link	make a hard link
symlink	make a symbolic link
readlink	read contents of symbolic link
rename	change name of file
lseek	reposition within file
truncate*	truncate file
access	determine accessibility
flock	lock a file
.TE
.in -5
.h 2.3 "Communications
.in +5
.TS
lw(1.6i) aw(3i).
<sys/socket.h>	standard definitions
socket	create socket
bind	bind socket to name
getsockname	get socket name
listen	allow queuing of connections
accept	accept a connection
connect	connect to peer socket
socketpair	create pair of connected sockets
sendto	send data to named socket
send	send data to connected socket
recvfrom	receive data on unconnected socket
recv	receive data on connected socket
sendmsg	send gathered data and/or rights
recvmsg	receive scattered data and/or rights
shutdown	partially close full-duplex connection
getsockopt	get socket option
setsockopt	set socket option
.TE
.in -5
.h 2.4 "Terminals, block and character devices
.in +5
.in -5
.h 2.5 "Processes and kernel hooks
.in +5
