.\"
.\" $FreeBSD$
.\"
.NH
Implementation jail in the FreeBSD kernel.
.NH 2
The jail(2) system call, allocation, refcounting and deallocation of
\fCstruct prison\fP.
.PP
The jail(2) system call is implemented as a non-optional system call
in FreeBSD.  Other system calls are controlled by compile time options
in the kernel configuration file, but due to the minute footprint of
the jail implementation, it was decided to make it a standard
facility in FreeBSD.
.PP
The implementation of the system call is straightforward:  a data structure
is allocated and populated with the arguments provided.  The data structure
is attached to the current process' \fCstruct proc\fP, its reference count
set to one and a call to the
chroot(2) syscall implementation completes the task.
.PP
Hooks in the code implementing process creation and destruction maintains
the reference count on the data structure and free it when the last reference
is lost.
Any new process created by a process in a jail will inherit a reference
to the jail, which effectively puts the new process in the same jail.
.PP
There is no way to modify the contents of the data structure describing
the jail after its creation, and no way to attach a process to an existing
jail if it was not created from the inside that jail.
.NH 2
Fortification of the chroot(2) facility for filesystem name scoping.
.PP
A number of ways to escape the confines of a chroot(2)-created subscope
of the filesystem view have been identified over the years.
chroot(2) was never intended to be security mechanism as such, but even
then the ftp daemon largely depended on the security provided by
chroot(2) to provide the ``anonymous ftp'' access method.
.PP
Three classes of escape routes existed: recursive chroot(2) escapes,
``..'' based escapes and fchdir(2) based escapes.
All of these exploited the fact that chroot(2) didn't try sufficiently
hard to enforce the new root directory.
.PP
New code were added to detect and thwart these escapes, amongst
other things by tracking the directory of the first level of chroot(2)
experienced by a process and refusing backwards traversal across
this directory, as well as additional code to refuse chroot(2) if
file-descriptors were open referencing directories.
.NH 2
Restriction of process visibility and interaction.
.PP
A macro was already in available in the kernel to determine if one process
could affect another process.  This macro did the rather complex checking
of uid and gid values.  It was felt that the complexity of the macro were
approaching the lower edge of IOCCC entrance criteria, and it was therefore
converted to a proper function named \fCp_trespass(p1, p2)\fP which does
all the previous checks and additionally checks the jail aspect of the access.
The check is implemented such that access fails if the origin process is jailed
but the target process is not in the same jail.
.PP
Process visibility is provided through two mechanisms in FreeBSD,
the \fCprocfs\fP file system and a sub-tree of the \fCsysctl\fP tree.
Both of these were modified to report only the processes in the same
jail to a jailed process.
.NH 2
Restriction to one IP number.
.PP
Restricting TCP and UDP access to just one IP number was done almost
entirely in the code which manages ``protocol control blocks''.
When a jailed process binds to a socket, the IP number provided by
the process will not be used, instead the pre-configured IP number of
the jail is used.
.PP
BSD based TCP/IP network stacks sport a special interface, the loop-back
interface, which has the ``magic'' IP number 127.0.0.1.
This is often used by processes to contact servers on the local machine,
and consequently special handling for jails were needed.
To handle this case it was necessary to also intercept and modify the
behaviour of connection establishment, and when the 127.0.0.1 address
were seen from a jailed process, substitute the jails configured IP number.
.PP
Finally the APIs through which the network configuration and connection
state may be queried were modified to report only information relevant
to the configured IP number of a jailed process.
.NH 2
Adding jail awareness to selected device drivers.
.PP
A couple of device drivers needed to be taught about jails, the ``pty''
driver is one of them.  The pty driver provides ``virtual terminals'' to
services like telnet, ssh, rlogin and X11 terminal window programs.
Therefore jails need access to the pty driver, and code had to be added
to enforce that a particular virtual terminal were not accessed from more
than one jail at the same time.
.NH 2
General restriction of super-users powers for jailed super-users.
.PP
This item proved to be the simplest but most tedious to implement.
Tedious because a manual review of all places where the kernel allowed
the super user special powers were called for,
simple because very few places were required to let a jailed root through.
Of the approximately 260 checks in the FreeBSD 4.0 kernel, only
about 35 will let a jailed root through.
.PP
Since the default is for jailed roots to not receive privilege, new
code or drivers in the FreeBSD kernel are automatically jail-aware: they
will refuse jailed roots privilege.
The other part of this protection comes from the fact that a jailed
root cannot create new device nodes with the mknod(2) systemcall, so
unless the machine administrator creates device nodes for a particular
device inside the jails filesystem tree, the driver in effect does
not exist in the jail.
.PP
As a side-effect of this work the suser(9) API were cleaned up and
extended to cater for not only the jail facility, but also to make room
for future partitioning facilities.
.NH 2
Implementation statistics
.PP
The change of the suser(9) API modified approx 350 source lines
distributed over approx. 100 source files.  The vast majority of 
these changes were generated automatically with a script.
.PP
The implementation of the jail facility added approx 200 lines of
code in total, distributed over approx. 50 files.  and about 200 lines
in two new kernel files.
