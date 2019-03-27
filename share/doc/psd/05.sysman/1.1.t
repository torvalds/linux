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
.\"	@(#)1.1.t	8.1 (Berkeley) 6/8/93
.\"	$FreeBSD$
.\"
.sh "Processes and protection
.NH 3
Host and process identifiers
.PP
Each UNIX host has associated with it a 32-bit host id, and a host
name of up to 256 characters (as defined by MAXHOSTNAMELEN in
\fI<sys/param.h>\fP).
These are set (by a privileged user)
and returned by the calls:
.DS
sethostid(hostid)
long hostid;

hostid = gethostid();
result long hostid;

sethostname(name, len)
char *name; int len;

len = gethostname(buf, buflen)
result int len; result char *buf; int buflen;
.DE
On each host runs a set of \fIprocesses\fP.
Each process is largely independent of other processes,
having its own protection domain, address space, timers, and
an independent set of references to system or user implemented objects.
.PP
Each process in a host is named by an integer
called the \fIprocess id\fP.  This number is
in the range 1-30000
and is returned by
the \fIgetpid\fP routine:
.DS
pid = getpid();
result int pid;
.DE
On each UNIX host this identifier is guaranteed to be unique;
in a multi-host environment, the (hostid, process id) pairs are
guaranteed unique.
.NH 3
Process creation and termination
.PP
A new process is created by making a logical duplicate of an
existing process:
.DS
pid = fork();
result int pid;
.DE
The \fIfork\fP call returns twice, once in the parent process, where
\fIpid\fP is the process identifier of the child,
and once in the child process where \fIpid\fP is 0.
The parent-child relationship induces a hierarchical structure on
the set of processes in the system.
.PP
A process may terminate by executing an \fIexit\fP call:
.DS
exit(status)
int status;
.DE
returning 8 bits of exit status to its parent.
.PP
When a child process exits or
terminates abnormally, the parent process receives
information about any
event which caused termination of the child process.  A
second call provides a non-blocking interface and may also be used
to retrieve information about resources consumed by the process during its
lifetime.
.DS
#include <sys/wait.h>

pid = wait(astatus);
result int pid; result union wait *astatus;

pid = wait3(astatus, options, arusage);
result int pid; result union waitstatus *astatus;
int options; result struct rusage *arusage;
.DE
.PP
A process can overlay itself with the memory image of another process,
passing the newly created process a set of parameters, using the call:
.DS
execve(name, argv, envp)
char *name, **argv, **envp;
.DE
The specified \fIname\fP must be a file which is in a format recognized
by the system, either a binary executable file or a file which causes
the execution of a specified interpreter program to process its contents.
.NH 3
User and group ids
.PP
Each process in the system has associated with it two user-id's:
a \fIreal user id\fP and a \fIeffective user id\fP, both 16 bit
unsigned integers (type \fBuid_t\fP).
Each process has an \fIreal accounting group id\fP and an \fIeffective
accounting group id\fP and a set of
\fIaccess group id's\fP.  The group id's are 16 bit unsigned integers
(type \fBgid_t\fP).
Each process may be in several different access groups, with the maximum
concurrent number of access groups a system compilation parameter,
the constant NGROUPS in the file \fI<sys/param.h>\fP,
guaranteed to be at least 8.
.PP
The real and effective user ids associated with a process are returned by:
.DS
ruid = getuid();
result uid_t ruid;

euid = geteuid();
result uid_t euid;
.DE
the real and effective accounting group ids by:
.DS
rgid = getgid();
result gid_t rgid;

egid = getegid();
result gid_t egid;
.DE
The access group id set is returned by a \fIgetgroups\fP call*:
.DS
ngroups = getgroups(gidsetsize, gidset);
result int ngroups; int gidsetsize; result int gidset[gidsetsize];
.DE
.FS
* The type of the gidset array in getgroups and setgroups
remains integer for compatibility with 4.2BSD.
It may change to \fBgid_t\fP in future releases.
.FE
.PP
The user and group id's
are assigned at login time using the \fIsetreuid\fP, \fIsetregid\fP,
and \fIsetgroups\fP calls:
.DS
setreuid(ruid, euid);
int ruid, euid;

setregid(rgid, egid);
int rgid, egid;

setgroups(gidsetsize, gidset)
int gidsetsize; int gidset[gidsetsize];
.DE
The \fIsetreuid\fP call sets both the real and effective user-id's,
while the \fIsetregid\fP call sets both the real
and effective accounting group id's.
Unless the caller is the super-user, \fIruid\fP
must be equal to either the current real or effective user-id,
and \fIrgid\fP equal to either the current real or effective
accounting group id.  The \fIsetgroups\fP call is restricted
to the super-user.
.NH 3
Process groups
.PP
Each process in the system is also normally associated with a \fIprocess
group\fP.  The group of processes in a process group is sometimes
referred to as a \fIjob\fP and manipulated by high-level system
software (such as the shell).
The current process group of a process is returned by the
\fIgetpgrp\fP call:
.DS
pgrp = getpgrp(pid);
result int pgrp; int pid;
.DE
When a process is in a specific process group it may receive
software interrupts affecting the group, causing the group to
suspend or resume execution or to be interrupted or terminated.
In particular, a system terminal has a process group and only processes
which are in the process group of the terminal may read from the
terminal, allowing arbitration of terminals among several different jobs.
.PP
The process group associated with a process may be changed by
the \fIsetpgrp\fP call:
.DS
setpgrp(pid, pgrp);
int pid, pgrp;
.DE
Newly created processes are assigned process id's distinct from all
processes and process groups, and the same process group as their
parent.  A normal (unprivileged) process may set its process group equal
to its process id.  A privileged process may set the process group of any
process to any value.
