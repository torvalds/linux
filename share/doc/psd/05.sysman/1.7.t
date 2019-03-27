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
.\"	@(#)1.7.t	8.1 (Berkeley) 6/8/93
.\"
.sh "System operation support
.PP
Unless noted otherwise,
the calls in this section are permitted only to a privileged user.
.NH 3
Bootstrap operations
.PP
The call
.DS
mount(blkdev, dir, ronly);
char *blkdev, *dir; int ronly;
.DE
extends the UNIX name space.  The \fImount\fP call specifies
a block device \fIblkdev\fP containing a UNIX file system
to be made available starting at \fIdir\fP.  If \fIronly\fP is
set then the file system is read-only; writes to the file system
will not be permitted and access times will not be updated
when files are referenced.
\fIDir\fP is normally a name in the root directory.
.PP
The call
.DS
swapon(blkdev, size);
char *blkdev; int size;
.DE
specifies a device to be made available for paging and swapping.
.PP
.NH 3
Shutdown operations
.PP
The call
.DS
unmount(dir);
char *dir;
.DE
unmounts the file system mounted on \fIdir\fP.
This call will succeed only if the file system is
not currently being used.
.PP
The call
.DS
sync();
.DE
schedules input/output to clean all system buffer caches.
(This call does not require privileged status.)
.PP
The call
.DS
reboot(how)
int how;
.DE
causes a machine halt or reboot.  The call may request a reboot
by specifying \fIhow\fP as RB_AUTOBOOT, or that the machine be halted
with RB_HALT.  These constants are defined in \fI<sys/reboot.h>\fP.
.NH 3
Accounting
.PP
The system optionally keeps an accounting record in a file
for each process that exits on the system.
The format of this record is beyond the scope of this document.
The accounting may be enabled to a file \fIname\fP by doing
.DS
acct(path);
char *path;
.DE
If \fIpath\fP is null, then accounting is disabled.  Otherwise,
the named file becomes the accounting file.
