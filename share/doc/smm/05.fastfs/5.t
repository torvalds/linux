.\" Copyright (c) 1986, 1993
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
.\"	@(#)5.t	8.1 (Berkeley) 6/8/93
.\"
.ds RH Functional enhancements
.NH 
File system functional enhancements
.PP
The performance enhancements to the
UNIX file system did not require
any changes to the semantics or
data structures visible to application programs.
However, several changes had been generally desired for some 
time but had not been introduced because they would require users to 
dump and restore all their file systems.
Since the new file system already
required all existing file systems to
be dumped and restored, 
these functional enhancements were introduced at this time.
.NH 2
Long file names
.PP
File names can now be of nearly arbitrary length.
Only programs that read directories are affected by this change.
To promote portability to UNIX systems that
are not running the new file system, a set of directory
access routines have been introduced to provide a consistent
interface to directories on both old and new systems.
.PP
Directories are allocated in 512 byte units called chunks.
This size is chosen so that each allocation can be transferred
to disk in a single operation.
Chunks are broken up into variable length records termed
directory entries.  A directory entry
contains the information necessary to map the name of a
file to its associated inode.
No directory entry is allowed to span multiple chunks.
The first three fields of a directory entry are fixed length
and contain: an inode number, the size of the entry, and the length
of the file name contained in the entry.
The remainder of an entry is variable length and contains
a null terminated file name, padded to a 4 byte boundary.
The maximum length of a file name in a directory is
currently 255 characters.
.PP
Available space in a directory is recorded by having
one or more entries accumulate the free space in their
entry size fields.  This results in directory entries
that are larger than required to hold the
entry name plus fixed length fields.  Space allocated
to a directory should always be completely accounted for
by totaling up the sizes of its entries.
When an entry is deleted from a directory,
its space is returned to a previous entry
in the same directory chunk by increasing the size of the
previous entry by the size of the deleted entry.
If the first entry of a directory chunk is free, then 
the entry's inode number is set to zero to indicate
that it is unallocated.
.NH 2
File locking
.PP
The old file system had no provision for locking files.
Processes that needed to synchronize the updates of a
file had to use a separate ``lock'' file.
A process would try to create a ``lock'' file. 
If the creation succeeded, then the process
could proceed with its update;
if the creation failed, then the process would wait and try again.
This mechanism had three drawbacks.
Processes consumed CPU time by looping over attempts to create locks.
Locks left lying around because of system crashes had
to be manually removed (normally in a system startup command script).
Finally, processes running as system administrator
are always permitted to create files,
so were forced to use a different mechanism.
While it is possible to get around all these problems,
the solutions are not straight forward,
so a mechanism for locking files has been added.
.PP
The most general schemes allow multiple processes
to concurrently update a file.
Several of these techniques are discussed in [Peterson83].
A simpler technique is to serialize access to a file with locks.
To attain reasonable efficiency,
certain applications require the ability to lock pieces of a file.
Locking down to the byte level has been implemented in the
Onyx file system by [Bass81].
However, for the standard system applications,
a mechanism that locks at the granularity of a file is sufficient.
.PP
Locking schemes fall into two classes,
those using hard locks and those using advisory locks.
The primary difference between advisory locks and hard locks is the
extent of enforcement.
A hard lock is always enforced when a program tries to
access a file;
an advisory lock is only applied when it is requested by a program.
Thus advisory locks are only effective when all programs accessing
a file use the locking scheme.
With hard locks there must be some override
policy implemented in the kernel.
With advisory locks the policy is left to the user programs.
In the UNIX system, programs with system administrator
privilege are allowed override any protection scheme.
Because many of the programs that need to use locks must
also run as the system administrator,
we chose to implement advisory locks rather than 
create an additional protection scheme that was inconsistent
with the UNIX philosophy or could
not be used by system administration programs.
.PP
The file locking facilities allow cooperating programs to apply
advisory
.I shared
or
.I exclusive
locks on files.
Only one process may have an exclusive
lock on a file while multiple shared locks may be present.
Both shared and exclusive locks cannot be present on
a file at the same time.
If any lock is requested when
another process holds an exclusive lock,
or an exclusive lock is requested when another process holds any lock,
the lock request will block until the lock can be obtained.
Because shared and exclusive locks are advisory only,
even if a process has obtained a lock on a file,
another process may access the file.
.PP
Locks are applied or removed only on open files.
This means that locks can be manipulated without
needing to close and reopen a file.
This is useful, for example, when a process wishes
to apply a shared lock, read some information
and determine whether an update is required, then
apply an exclusive lock and update the file.
.PP
A request for a lock will cause a process to block if the lock
can not be immediately obtained.
In certain instances this is unsatisfactory.
For example, a process that
wants only to check if a lock is present would require a separate
mechanism to find out this information.
Consequently, a process may specify that its locking
request should return with an error if a lock can not be immediately
obtained.
Being able to conditionally request a lock
is useful to ``daemon'' processes
that wish to service a spooling area.
If the first instance of the
daemon locks the directory where spooling takes place,
later daemon processes can
easily check to see if an active daemon exists.
Since locks exist only while the locking processes exist,
lock files can never be left active after
the processes exit or if the system crashes.
.PP
Almost no deadlock detection is attempted.
The only deadlock detection done by the system is that the file
to which a lock is applied must not already have a
lock of the same type (i.e. the second of two successive calls
to apply a lock of the same type will fail).
.NH 2
Symbolic links
.PP
The traditional UNIX file system allows multiple
directory entries in the same file system
to reference a single file.  Each directory entry
``links'' a file's name to an inode and its contents.
The link concept is fundamental;
inodes do not reside in directories, but exist separately and
are referenced by links.
When all the links to an inode are removed,
the inode is deallocated.
This style of referencing an inode does
not allow references across physical file
systems, nor does it support inter-machine linkage. 
To avoid these limitations
.I "symbolic links"
similar to the scheme used by Multics [Feiertag71] have been added.
.PP
A symbolic link is implemented as a file that contains a pathname.
When the system encounters a symbolic link while
interpreting a component of a pathname,
the contents of the symbolic link is prepended to the rest
of the pathname, and this name is interpreted to yield the
resulting pathname.
In UNIX, pathnames are specified relative to the root
of the file system hierarchy, or relative to a process's
current working directory.  Pathnames specified relative
to the root are called absolute pathnames.  Pathnames
specified relative to the current working directory are
termed relative pathnames.
If a symbolic link contains an absolute pathname,
the absolute pathname is used,
otherwise the contents of the symbolic link is evaluated
relative to the location of the link in the file hierarchy.
.PP
Normally programs do not want to be aware that there is a
symbolic link in a pathname that they are using.
However certain system utilities
must be able to detect and manipulate symbolic links.
Three new system calls provide the ability to detect, read, and write
symbolic links; seven system utilities required changes
to use these calls.
.PP
In future Berkeley software distributions
it may be possible to reference file systems located on
remote machines using pathnames.  When this occurs,
it will be possible to create symbolic links that span machines.
.NH 2
Rename
.PP
Programs that create a new version of an existing
file typically create the
new version as a temporary file and then rename the temporary file
with the name of the target file.
In the old UNIX file system renaming required three calls to the system.
If a program were interrupted or the system crashed between these calls,
the target file could be left with only its temporary name.
To eliminate this possibility the \fIrename\fP system call
has been added.  The rename call does the rename operation
in a fashion that guarantees the existence of the target name.
.PP
Rename works both on data files and directories.
When renaming directories,
the system must do special validation checks to insure
that the directory tree structure is not corrupted by the creation
of loops or inaccessible directories.
Such corruption would occur if a parent directory were moved
into one of its descendants.
The validation check requires tracing the descendents of the target
directory to insure that it does not include the directory being moved.
.NH 2
Quotas
.PP
The UNIX system has traditionally attempted to share all available
resources to the greatest extent possible.
Thus any single user can allocate all the available space
in the file system.
In certain environments this is unacceptable.
Consequently, a quota mechanism has been added for restricting the
amount of file system resources that a user can obtain.
The quota mechanism sets limits on both the number of inodes
and the number of disk blocks that a user may allocate.
A separate quota can be set for each user on each file system.
Resources are given both a hard and a soft limit.
When a program exceeds a soft limit,
a warning is printed on the users terminal;
the offending program is not terminated
unless it exceeds its hard limit.
The idea is that users should stay below their soft limit between
login sessions,
but they may use more resources while they are actively working.
To encourage this behavior,
users are warned when logging in if they are over
any of their soft limits.
If users fails to correct the problem for too many login sessions,
they are eventually reprimanded by having their soft limit
enforced as their hard limit.
.ds RH Acknowledgements
.sp 2
.ne 1i
