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
.\"	@(#)2.2.t	8.1 (Berkeley) 6/8/93
.\"
.sh "File system
.NH 3
Overview
.PP
The file system abstraction provides access to a hierarchical
file system structure.
The file system contains directories (each of which may contain
other sub-directories) as well as files and references to other
objects such as devices and inter-process communications sockets.
.PP
Each file is organized as a linear array of bytes.  No record
boundaries or system related information is present in
a file.
Files may be read and written in a random-access fashion.
The user may read the data in a directory as though
it were an ordinary file to determine the names of the contained files,
but only the system may write into the directories.
The file system stores only a small amount of ownership, protection and usage
information with a file.
.NH 3
Naming
.PP
The file system calls take \fIpath name\fP arguments.
These consist of a zero or more component \fIfile names\fP
separated by ``/\^'' characters, where each file name
is up to 255 ASCII characters excluding null and ``/\^''.
.PP
Each process always has two naming contexts: one for the
root directory of the file system and one for the
current working directory.  These are used
by the system in the filename translation process.
If a path name begins with a ``/\^'', it is called
a full path name and interpreted relative to the root directory context.
If the path name does not begin with a ``/\^'' it is called
a relative path name and interpreted relative to the current directory
context.
.PP
The system limits
the total length of a path name to 1024 characters.
.PP
The file name ``..'' in each directory refers to
the parent directory of that directory.
The parent directory of the root of the file system is always that directory.
.PP
The calls
.DS
chdir(path);
char *path;

chroot(path)
char *path;
.DE
change the current working directory and root directory context of a process.
Only the super-user can change the root directory context of a process.
.NH 3
Creation and removal
.PP
The file system allows directories, files, special devices,
and ``portals'' to be created and removed from the file system.
.NH 4
Directory creation and removal
.PP
A directory is created with the \fImkdir\fP system call:
.DS
mkdir(path, mode);
char *path; int mode;
.DE
where the mode is defined as for files (see below).
Directories are removed with the \fIrmdir\fP system call:
.DS
rmdir(path);
char *path;
.DE
A directory must be empty if it is to be deleted.
.NH 4
File creation
.PP
Files are created with the \fIopen\fP system call,
.DS
fd = open(path, oflag, mode);
result int fd; char *path; int oflag, mode;
.DE
The \fIpath\fP parameter specifies the name of the
file to be created.  The \fIoflag\fP parameter must
include O_CREAT from below to cause the file to be created.
Bits for \fIoflag\fP are
defined in \fI<sys/file.h>\fP:
.DS
._d
#define	O_RDONLY	000	/* open for reading */
#define	O_WRONLY	001	/* open for writing */
#define	O_RDWR	002	/* open for read & write */
#define	O_NDELAY	004 	/* non-blocking open */
#define	O_APPEND	010	/* append on each write */
#define	O_CREAT	01000	/* open with file create */
#define	O_TRUNC	02000	/* open with truncation */
#define	O_EXCL	04000	/* error on create if file exists */
.DE
.PP
One of O_RDONLY, O_WRONLY and O_RDWR should be specified,
indicating what types of operations are desired to be performed
on the open file.  The operations will be checked against the user's
access rights to the file before allowing the \fIopen\fP to succeed.
Specifying O_APPEND causes writes to automatically append to the
file.
The flag O_CREAT causes the file to be created if it does not
exist, owned by the current user
and the group of the containing directory.
The protection for the new file is specified in \fImode\fP.
The file mode is used as a three digit octal number.
Each digit encodes read access as 4, write access as 2 and execute
access as 1, or'ed together.  The 0700 bits describe owner
access, the 070 bits describe the access rights for processes in the same
group as the file, and the 07 bits describe the access rights
for other processes.
.PP
If the open specifies to create the file with O_EXCL
and the file already exists, then the \fIopen\fP will fail
without affecting the file in any way.  This provides a
simple exclusive access facility.
If the file exists but is a symbolic link, the open will fail
regardless of the existence of the file specified by the link.
.NH 4
Creating references to devices
.PP
The file system allows entries which reference peripheral devices.
Peripherals are distinguished as \fIblock\fP or \fIcharacter\fP
devices according by their ability to support block-oriented
operations.
Devices are identified by their ``major'' and ``minor''
device numbers.  The major device number determines the kind
of peripheral it is, while the minor device number indicates
one of possibly many peripherals of that kind.
Structured devices have all operations performed internally
in ``block'' quantities while
unstructured devices often have a number of
special \fIioctl\fP operations, and may have input and output
performed in varying units.
The \fImknod\fP call creates special entries:
.DS
mknod(path, mode, dev);
char *path; int mode, dev;
.DE
where \fImode\fP is formed from the object type
and access permissions.  The parameter \fIdev\fP is a configuration
dependent parameter used to identify specific character or
block I/O devices.
.NH 4
Portal creation\(dg
.PP
.FS
\(dg The \fIportal\fP call is not implemented in 4.3BSD.
.FE
The call
.DS
fd = portal(name, server, param, dtype, protocol, domain, socktype)
result int fd; char *name, *server, *param; int dtype, protocol;
int domain, socktype;
.DE
places a \fIname\fP in the file system name space that causes connection to a
server process when the name is used.
The portal call returns an active portal in \fIfd\fP as though an
access had occurred to activate an inactive portal, as now described.
.PP
When an inactive portal is accessed, the system sets up a socket
of the specified \fIsocktype\fP in the specified communications
\fIdomain\fP (see section 2.3), and creates the \fIserver\fP process,
giving it the specified \fIparam\fP as argument to help it identify
the portal, and also giving it the newly created socket as descriptor
number 0.  The accessor of the portal will create a socket in the same
\fIdomain\fP and \fIconnect\fP to the server.  The user will then
\fIwrap\fP the socket in the specified \fIprotocol\fP to create an object of
the required descriptor type \fIdtype\fP and proceed with the
operation which was in progress before the portal was encountered.
.PP
While the server process holds the socket (which it received as \fIfd\fP
from the \fIportal\fP call on descriptor 0 at activation) further references
will result in connections being made to the same socket.
.NH 4
File, device, and portal removal
.PP
A reference to a file, special device or portal may be removed with the
\fIunlink\fP call,
.DS
unlink(path);
char *path;
.DE
The caller must have write access to the directory in which
the file is located for this call to be successful.
.NH 3
Reading and modifying file attributes
.PP
Detailed information about the attributes of a file
may be obtained with the calls:
.DS
#include <sys/stat.h>

stat(path, stb);
char *path; result struct stat *stb;

fstat(fd, stb);
int fd; result struct stat *stb;
.DE
The \fIstat\fP structure includes the file
type, protection, ownership, access times,
size, and a count of hard links.
If the file is a symbolic link, then the status of the link
itself (rather than the file the link references)
may be found using the \fIlstat\fP call:
.DS
lstat(path, stb);
char *path; result struct stat *stb;
.DE
.PP
Newly created files are assigned the user id of the
process that created it and the group id of the directory
in which it was created.  The ownership of a file may
be changed by either of the calls
.DS
chown(path, owner, group);
char *path; int owner, group;

fchown(fd, owner, group);
int fd, owner, group;
.DE
.PP
In addition to ownership, each file has three levels of access
protection associated with it.  These levels are owner relative,
group relative, and global (all users and groups).  Each level
of access has separate indicators for read permission, write
permission, and execute permission.
The protection bits associated with a file may be set by either
of the calls:
.DS
chmod(path, mode);
char *path; int mode;

fchmod(fd, mode);
int fd, mode;
.DE
where \fImode\fP is a value indicating the new protection
of the file, as listed in section 2.2.3.2.
.PP
Finally, the access and modify times on a file may be set by the call:
.DS
utimes(path, tvp)
char *path; struct timeval *tvp[2];
.DE
This is particularly useful when moving files between media, to
preserve relationships between the times the file was modified.
.NH 3
Links and renaming
.PP
Links allow multiple names for a file
to exist.  Links exist independently of the file linked to.
.PP
Two types of links exist, \fIhard\fP links and \fIsymbolic\fP
links.  A hard link is a reference counting mechanism that
allows a file to have multiple names within the same file
system.  Symbolic links cause string substitution
during the pathname interpretation process.
.PP
Hard links and symbolic links have different
properties.  A hard link insures the target
file will always be accessible, even after its original
directory entry is removed; no such guarantee exists for a symbolic link.
Symbolic links can span file systems boundaries.
.PP
The following calls create a new link, named \fIpath2\fP,
to \fIpath1\fP:
.DS
link(path1, path2);
char *path1, *path2;

symlink(path1, path2);
char *path1, *path2;
.DE
The \fIunlink\fP primitive may be used to remove
either type of link. 
.PP
If a file is a symbolic link, the ``value'' of the
link may be read with the \fIreadlink\fP call,
.DS
len = readlink(path, buf, bufsize);
result int len; result char *path, *buf; int bufsize;
.DE
This call returns, in \fIbuf\fP, the null-terminated string
substituted into pathnames passing through \fIpath\fP\|.
.PP
Atomic renaming of file system resident objects is possible
with the \fIrename\fP call:
.DS
rename(oldname, newname);
char *oldname, *newname;
.DE
where both \fIoldname\fP and \fInewname\fP must be
in the same file system.
If \fInewname\fP exists and is a directory, then it must be empty.
.NH 3
Extension and truncation
.PP
Files are created with zero length and may be extended
simply by writing or appending to them.  While a file is
open the system maintains a pointer into the file
indicating the current location in the file associated with
the descriptor.  This pointer may be moved about in the
file in a random access fashion.
To set the current offset into a file, the \fIlseek\fP
call may be used,
.DS
oldoffset = lseek(fd, offset, type);
result off_t oldoffset; int fd; off_t offset; int type;
.DE
where \fItype\fP is given in \fI<sys/file.h>\fP as one of:
.DS
._d
#define	L_SET	0	/* set absolute file offset */
#define	L_INCR	1	/* set file offset relative to current position */
#define	L_XTND	2	/* set offset relative to end-of-file */
.DE
The call ``lseek(fd, 0, L_INCR)''
returns the current offset into the file.
.PP
Files may have ``holes'' in them.  Holes are void areas in the
linear extent of the file where data has never been
written.  These may be created by seeking to
a location in a file past the current end-of-file and writing.
Holes are treated by the system as zero valued bytes.
.PP
A file may be truncated with either of the calls:
.DS
truncate(path, length);
char *path; int length;

ftruncate(fd, length);
int fd, length;
.DE
reducing the size of the specified file to \fIlength\fP bytes.
.NH 3
Checking accessibility
.PP
A process running with
different real and effective user ids
may interrogate the accessibility of a file to the
real user by using
the \fIaccess\fP call:
.DS
accessible = access(path, how);
result int accessible; char *path; int how;
.DE
Here \fIhow\fP is constructed by or'ing the following bits, defined
in \fI<sys/file.h>\fP:
.DS
._d
#define	F_OK	0	/* file exists */
#define	X_OK	1	/* file is executable */
#define	W_OK	2	/* file is writable */
#define	R_OK	4	/* file is readable */
.DE
The presence or absence of advisory locks does not affect the
result of \fIaccess\fP\|.
.NH 3
Locking
.PP
The file system provides basic facilities that allow cooperating processes
to synchronize their access to shared files.  A process may
place an advisory \fIread\fP or \fIwrite\fP lock on a file,
so that other cooperating processes may avoid interfering
with the process' access.  This simple mechanism
provides locking with file granularity.  More granular
locking can be built using the IPC facilities to provide a lock
manager.
The system does not force processes to obey the locks;
they are of an advisory nature only.
.PP
Locking is performed after an \fIopen\fP call by applying the
\fIflock\fP primitive,
.DS
flock(fd, how);
int fd, how;
.DE
where the \fIhow\fP parameter is formed from bits defined in \fI<sys/file.h>\fP:
.DS
._d
#define	LOCK_SH	1	/* shared lock */
#define	LOCK_EX	2	/* exclusive lock */
#define	LOCK_NB	4	/* don't block when locking */
#define	LOCK_UN	8	/* unlock */
.DE
Successive lock calls may be used to increase or
decrease the level of locking.  If an object is currently
locked by another process when a \fIflock\fP call is made,
the caller will be blocked until the current lock owner
releases the lock; this may be avoided by including LOCK_NB
in the \fIhow\fP parameter.
Specifying LOCK_UN removes all locks associated with the descriptor.
Advisory locks held by a process are automatically deleted when
the process terminates.
.NH 3
Disk quotas
.PP
As an optional facility, each file system may be requested to
impose limits on a user's disk usage.
Two quantities are limited: the total amount of disk space which
a user may allocate in a file system and the total number of files
a user may create in a file system.  Quotas are expressed as
\fIhard\fP limits and \fIsoft\fP limits.  A hard limit is
always imposed; if a user would exceed a hard limit, the operation
which caused the resource request will fail.  A soft limit results
in the user receiving a warning message, but with allocation succeeding.
Facilities are provided to turn soft limits into hard limits if a
user has exceeded a soft limit for an unreasonable period of time.
.PP
To enable disk quotas on a file system the \fIsetquota\fP call
is used:
.DS
setquota(special, file)
char *special, *file;
.DE
where \fIspecial\fP refers to a structured device file where
a mounted file system exists, and
\fIfile\fP refers to a disk quota file (residing on the file
system associated with \fIspecial\fP) from which user quotas
should be obtained.  The format of the disk quota file is 
implementation dependent.
.PP
To manipulate disk quotas the \fIquota\fP call is provided:
.DS
#include <sys/quota.h>

quota(cmd, uid, arg, addr)
int cmd, uid, arg; caddr_t addr;
.DE
The indicated \fIcmd\fP is applied to the user ID \fIuid\fP.
The parameters \fIarg\fP and \fIaddr\fP are command specific.
The file \fI<sys/quota.h>\fP contains definitions pertinent to the
use of this call.
