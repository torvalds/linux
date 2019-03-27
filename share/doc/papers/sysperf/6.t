.\" Copyright (c) 1985 The Regents of the University of California.
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
.\"	@(#)6.t	5.1 (Berkeley) 4/17/91
.\"
.ds RH Security Tightening
.NH
Security Tightening
.PP
Since we do not wish to encourage rampant system cracking,
we describe only briefly the changes made to enhance security.
.NH 2
Generic Kernel
.PP
Several loopholes in the process tracing facility have been corrected.
Programs being traced may not be executed;
executing programs may not be traced.
Programs may not provide input to terminals to which they do not
have read permission.
The handling of process groups has been tightened to eliminate
some problems.
When a program attempts to change its process group,
the system checks to see if the process with the pid of the process 
group was started by the same user.
If it exists and was started by a different user the process group
number change is denied.
.NH 2
Security Problems in Utilities
.PP
Setuid utilities no longer use the \fIpopen\fP or \fIsystem\fP library routines.
Access to the kernel's data structures through the kmem device
is now restricted to programs that are set group id ``kmem''.
Thus many programs that used to run with root privileges
no longer need to do so.
Access to disk devices is now controlled by an ``operator'' group id;
this permission allows operators to function without being the super-user.
Only users in group wheel can do ``su root''; this restriction
allows administrators to define a super-user access list.
Numerous holes have been closed in the shell to prevent
users from gaining privileges from set user id shell scripts,
although use of such scripts is still highly discouraged on systems
that are concerned about security.
