.\"
.\" $FreeBSD$
.\"
.if n .ftr C R
.ig TL
.ds CH "
.nr PI 2n
.nr PS 12
.nr LL 15c
.nr PO 3c
.nr FM 3.5c
.po 3c
.TL
Jails: Confining the omnipotent root.
.FS
This paper was presented at the 2nd International System Administration and Networking Conference "SANE 2000" May 22-25, 2000 in Maastricht, The Netherlands and is published in the proceedings.
.FE
.AU
Poul-Henning Kamp <phk@FreeBSD.org>
.AU
Robert N. M. Watson <rwatson@FreeBSD.org>
.AI
The FreeBSD Project
.FS
This work was sponsored by \fChttp://www.servetheweb.com/\fP and
donated to the FreeBSD Project for inclusion in the FreeBSD
OS.  FreeBSD 4.0-RELEASE was the first release including this
code.
Follow-on work was sponsored by Safeport Network Services,
\fChttp://www.safeport.com/\fP
.FE
.AB
The traditional UNIX security model is simple but inexpressive.
Adding fine-grained access control improves the expressiveness,
but often dramatically increases both the cost of system management
and implementation complexity.
In environments with a more complex management model, with delegation
of some management functions to parties under varying degrees of trust,
the base UNIX model and most natural
extensions are inappropriate at best.
Where multiple mutually un-trusting parties are introduced,
``inappropriate'' rapidly transitions to ``nightmarish'', especially
with regards to data integrity and privacy protection.
.PP
The FreeBSD ``Jail'' facility provides the ability to partition
the operating system environment, while maintaining the simplicity
of the UNIX ``root'' model.
In Jail, users with privilege find that the scope of their requests
is limited to the jail, allowing system administrators to delegate
management capabilities for each virtual machine
environment.
Creating virtual machines in this manner has many potential uses; the
most popular thus far has been for providing virtual machine services
in Internet Service Provider environments.
.AE
.NH
Introduction
.PP
The UNIX access control mechanism is designed for an environment with two
types of users: those with, and without administrative privilege.
Within this framework, every attempt is made to provide an open
system, allowing easy sharing of files and inter-process communication.
As a member of the UNIX family, FreeBSD inherits these
security properties.
Users of FreeBSD in non-traditional UNIX environments must balance
their need for strong application support, high network performance
and functionality, and low total cost of ownership with the need
for alternative security models that are difficult or impossible to
implement with the UNIX security mechanisms.
.PP
One such consideration is the desire to delegate some (but not all)
administrative functions to untrusted or less trusted parties, and
simultaneously impose system-wide mandatory policies on process
interaction and sharing.
Attempting to create such an environment in the current-day FreeBSD
security environment is both difficult and costly: in many cases,
the burden of implementing these policies falls on user
applications, which means an increase in the size and complexity
of the code base, in turn translating to higher development 
and maintenance cost, as well as less overall flexibility.
.PP
This abstract risk becomes more clear when applied to a practical,
real-world example:
many web service providers turn to the FreeBSD
operating system to host customer web sites, as it provides a
high-performance, network-centric server environment.
However, these providers have a number of concerns on their plate, both in
terms of protecting the integrity and confidentiality of their own
files and services from their customers, as well as protecting the files
and services of one customer from (accidental or
intentional) access by any other customer.
At the same time, a provider would like to provide
substantial autonomy to customers, allowing them to install and
maintain their own software, and to manage their own services,
such as web servers and other content-related daemon programs.
.PP
This problem space points strongly in the direction of a partitioning
solution, in which customer processes and storage are isolated from those of
other customers, both in terms of accidental disclosure of data or process
information, but also in terms of the ability to modify files or processes 
outside of a compartment.
Delegation of management functions within the system must
be possible, but not at the cost of system-wide requirements, including
integrity and privacy protection between partitions.
.PP
However, UNIX-style access control makes it notoriously difficult to
compartmentalise functionality.
While mechanisms such as chroot(2) provide a modest
level compartmentalisation, it is well known
that these mechanisms have serious shortcomings, both in terms of the
scope of their functionality, and effectiveness at what they provide \s-2[CHROOT]\s+2.
.PP
In the case of the chroot(2) call, a process's visibility of
the file system name-space is limited to a single subtree.
However, the compartmentalisation does not extend to the process
or networking spaces and therefore both observation of and interference
with processes outside their compartment is possible.
.PP
To this end, we describe the new FreeBSD ``Jail'' facility, which 
provides a strong partitioning solution, leveraging existing
mechanisms, such as chroot(2), to what effectively amounts to a
virtual machine environment.  Processes in a jail are provided
full access to the files that they may manipulate, processes they
may influence, and network services they can make use of, and neither
access nor visibility of files, processes or network services outside
their partition.
.PP
Unlike other fine-grained security solutions, Jail does not
substantially increase the policy management requirements for the
system administrator, as each Jail is a virtual FreeBSD environment
permitting local policy to be independently managed, with much the
same properties as the main system itself, making Jail easy to use
for the administrator, and far more compatible with applications.
.NH
Traditional UNIX Security, or, ``God, root, what difference?" \s-2[UF]\s+2.
.PP
The traditional UNIX access model assigns numeric uids to each user of the
system. In turn, each process ``owned'' by a user will be tagged with that
user's uid in an unforgeable manner.  The uids serve two purposes: first,
they determine how discretionary access control mechanisms will be applied, and
second, they are used to determine whether special privileges are accorded.
.PP
In the case of discretionary access controls, the primary object protected is
a file.  The uid (and related gids indicating group membership) are mapped to
a set of rights for each object, courtesy the UNIX file mode, in effect acting
as a limited form of access control list.  Jail is, in general, not concerned
with modifying the semantics of discretionary access control mechanisms,
although there are important implications from a management perspective.
.PP
For the purposes of determining whether special privileges are accorded to a
process, the check is simple: ``is the numeric uid equal to 0 ?''.
If so, the
process is acting with ``super-user privileges'', and all access checks are
granted, in effect allowing the process the ability to do whatever it wants
to \**.
.FS
\&... no matter how patently stupid it may be.
.FE
.PP
For the purposes of human convenience, uid 0 is canonically allocated
to the ``root'' user \s-2[ROOT]\s+2.
For the purposes of jail, this behaviour is extremely relevant: many of
these privileged operations can be used to manage system hardware and
configuration, file system name-space, and special network operations.
.PP
Many limitations to this model are immediately clear: the root user is a
single, concentrated source of privilege that is exposed to many pieces of
software, and as such an immediate target for attacks.  In the event of a
compromise of the root capability set, the attacker has complete control over
the system.  Even without an attacker, the risks of a single administrative
account are serious: delegating a narrow scope of capability to an
inexperienced administrator is difficult, as the granularity of delegation is
that of all system management abilities.  These features make the omnipotent
root account a sharp, efficient and extremely dangerous tool.
.PP
The BSD family of operating systems have implemented the ``securelevel''
mechanism which allows the administrator to block certain configuration
and management functions from being performed by root,
until the system is restarted and brought up into single-user mode.
While this does provide some amount of protection in the case of a root
compromise of the machine, it does nothing to address the need for
delegation of certain root abilities.
.NH
Other Solutions to the Root Problem
.PP
Many operating systems attempt to address these limitations by providing
fine-grained access controls for system resources \s-2[BIBA]\s+2.
These efforts vary in
degrees of success, but almost all suffer from at least three serious
limitations:
.PP
First, increasing the granularity of security controls increases the
complexity of the administration process, in turn increasing both the
opportunity for incorrect configuration, as well as the demand on
administrator time and resources.  In many cases, the increased complexity
results in significant frustration for the administrator, which may result
in two
disastrous types of policy: ``all doors open as it's too much trouble'', and
``trust that the system is secure, when in fact it isn't''.
.PP
The extent of the trouble is best illustrated by the fact that an entire
niche industry has emerged providing tools to manage fine grained security
controls \s-2[UAS]\s+2.
.PP
Second, usefully segregating capabilities and assigning them to running code
and users is very difficult.  Many privileged operations in UNIX seem
independent, but are in fact closely related, and the handing out of one
privilege may, in effect, be transitive to the many others.  For example, in
some trusted operating systems, a system capability may be assigned to a
running process to allow it to read any file, for the purposes of backup.
However, this capability is, in effect, equivalent to the ability to switch to
any other account, as the ability to access any file provides access to system
keying material, which in turn provides the ability to authenticate as any
user.  Similarly, many operating systems attempt to segregate management
capabilities from auditing capabilities.  In a number of these operating
systems, however, ``management capabilities'' permit the administrator to
assign ``auditing capabilities'' to itself, or another account, circumventing
the segregation of capability.
.PP
Finally, introducing new security features often involves introducing new
security management APIs.  When fine-grained capabilities are introduced to
replace the setuid mechanism in UNIX-like operating systems, applications that
previously did an ``appropriateness check'' to see if they were running as
root before executing must now be changed to know that they need not run as
root.  In the case of applications running with privilege and executing other
programs, there is now a new set of privileges that must be voluntarily given
up before executing another program.  These change can introduce significant
incompatibility for existing applications, and make life more difficult for
application developers who may not be aware of differing security semantics on
different systems \s-2[POSIX1e]\s+2.
.NH
The Jail Partitioning Solution
.PP
Jail neatly side-steps the majority of these problems through partitioning.
Rather
than introduce additional fine-grained access control mechanism, we partition
a FreeBSD environment (processes, file system, network resources) into a
management environment, and optionally subset Jail environments.  In doing so,
we simultaneously maintain the existing UNIX security model, allowing
multiple users and a privileged root user in each jail, while
limiting the scope of root's activities to his jail.
Consequently the administrator of a
FreeBSD machine can partition the machine into separate jails, and provide
access to the super-user account in each of these without losing control of
the over-all environment.
.PP
A process in a partition is referred to as ``in jail''.  When a FreeBSD
system is booted up after a fresh install, no processes will be in jail.
When
a process is placed in a jail, it, and any descendents of the process created
after the jail creation, will be in that jail.  A process may be in only one
jail, and after creation, it can not leave the jail.
Jails are created when a
privileged process calls the jail(2) syscall, with a description of the jail as an
argument to the call.  Each call to jail(2) creates a new jail; the only way
for a new process to enter the jail is by inheriting access to the jail from
another process already in that jail.
Processes may never
leave the jail they created, or were created in.
.KF
.if t .PSPIC jail01.eps 4i
.ce 1
Fig. 1 \(em Schematic diagram of machine with two configured jails
.sp
.KE
.PP
Membership in a jail involves a number of restrictions: access to the file
name-space is restricted in the style of chroot(2), the ability to bind network
resources is limited to a specific IP address, the ability to manipulate
system resources and perform privileged operations is sharply curtailed, and
the ability to interact with other processes is limited to only processes
inside the same jail.
.PP
Jail takes advantage of the existing chroot(2) behaviour to limit access to the
file system name-space for jailed processes.  When a jail is created, it is
bound to a particular file system root.
Processes are unable to manipulate files that they cannot address,
and as such the integrity and confidentiality of files outside of the jail
file system root are protected.  Traditional mechanisms for breaking out of
chroot(2) have been blocked.
In the expected and documented configuration, each jail is provided
with its exclusive file system root, and standard FreeBSD directory layout,
but this is not mandated by the implementation.
.PP
Each jail is bound to a single IP address: processes within the jail may not
make use of any other IP address for outgoing or incoming connections; this
includes the ability to restrict what network services a particular jail may
offer.  As FreeBSD distinguishes attempts to bind all IP addresses from
attempts to bind a particular address, bind requests for all IP addresses are
redirected to the individual Jail address.  Some network functionality
associated with privileged calls are wholesale disabled due to the nature of the
functionality offered, in particular facilities which would allow ``spoofing''
of IP numbers or disruptive traffic to be generated have been disabled.
.PP
Processes running without root privileges will notice few, if any differences
between a jailed environment or un-jailed environment.  Processes running with
root privileges will find that many restrictions apply to the privileged calls
they may make.  Some calls will now return an access error \(em for example, an
attempt to create a device node will now fail.  Others will have a more
limited scope than normal \(em attempts to bind a reserved port number on all
available addresses will result in binding only the address associated with
the jail.  Other calls will succeed as normal: root may read a file owned by
any uid, as long as it is accessible through the jail file system name-space.
.PP
Processes within the jail will find that they are unable to interact or
even verify the existence of 
processes outside the jail \(em  processes within the jail are
prevented from delivering signals to processes outside the jail, as well as
connecting to those processes with debuggers, or even see them in the
sysctl or process file system monitoring mechanisms.  Jail does not prevent,
nor is it intended to prevent, the use of covert channels or communications
mechanisms via accepted interfaces \(em for example, two processes may communicate
via sockets over the IP network interface.  Nor does it attempt to provide
scheduling services based on the partition; however, it does prevent calls
that interfere with normal process operation.
.PP
As a result of these attempts to retain the standard FreeBSD API and
framework, almost all applications will run unaffected.  Standard system
services such as Telnet, FTP, and SSH all behave normally, as do most third
party applications, including the popular Apache web server.
.NH
Jail Implementation
.PP
Processes running with root privileges in the jail find that there are serious
restrictions on what it is capable of doing \(em in particular, activities that
would extend outside of the jail:
.IP "" 5n
\(bu Modifying the running kernel by direct access and loading kernel
modules is prohibited.
.IP
\(bu Modifying any of the network configuration, interfaces, addresses, and
routing table is prohibited.
.IP
\(bu Mounting and unmounting file systems is prohibited.
.IP
\(bu Creating device nodes is prohibited.
.IP
\(bu Accessing raw, divert, or routing sockets is prohibited.
.IP
\(bu Modifying kernel runtime parameters, such as most sysctl settings, is
prohibited.
.IP
\(bu Changing securelevel-related file flags is prohibited.
.IP
\(bu Accessing network resources not associated with the jail is prohibited.
.PP
Other privileged activities are permitted as long as they are limited to the
scope of the jail:
.IP "" 5n
\(bu Signalling any process within the jail is permitted.
.IP
\(bu Changing the ownership and mode of any file within the jail is permitted, as
long as the file flags permit this.
.IP
\(bu Deleting any file within the jail is permitted, as long as the file flags
permit this.
.IP
\(bu Binding reserved TCP and UDP port numbers on the jails IP address is
permitted.  (Attempts to bind TCP and UDP ports using INADDR_ANY will be
redirected to the jails IP address.)
.IP
\(bu Functions which operate on the uid/gid space are all permitted since they
act as labels for filesystem objects of proceses
which are partitioned off by other mechanisms.
.PP
These restrictions on root access limit the scope of root processes, enabling
most applications to run un-hindered, but preventing calls that might allow an
application to reach beyond the jail and influence other processes or
system-wide configuration.
.PP
.so implementation.ms
.so mgt.ms
.so future.ms
.NH
Conclusion
.PP
The jail facility provides FreeBSD with a conceptually simple security
partitioning mechanism, allowing the delegation of administrative rights
within virtual machine partitions.
.PP
The implementation relies on
restricting access within the jail environment to a well-defined subset
of the overall host environment.  This includes limiting interaction
between processes, and to files, network resources, and privileged
operations.  Administrative overhead is reduced through avoiding
fine-grained access control mechanisms, and maintaining a consistent
administrative interface across partitions and the host environment. 
.PP
The jail facility has already seen widespread deployment in particular as
a vehicle for delivering "virtual private server" services.
.PP
The jail code is included in the base system as part of FreeBSD 4.0-RELEASE,
and fully documented in the jail(2) and jail(8) man-pages.
.bp
.SH
Notes & References
.IP \s-2[BIBA]\s+2 .5i
K. J. Biba, Integrity Considerations for Secure
Computer Systems, USAF Electronic Systems Division, 1977
.IP \s-2[CHROOT]\s+2 .5i
Dr. Marshall Kirk Mckusick, private communication:
``According to the SCCS logs, the chroot call was added by Bill Joy
on March 18, 1982 approximately 1.5 years before 4.2BSD was released.
That was well before we had ftp servers of any sort (ftp did not
show up in the source tree until January 1983).  My best guess as
to its purpose was to allow Bill to chroot into the /4.2BSD build
directory and build a system using only the files, include files,
etc contained in that tree.  That was the only use of chroot that
I remember from the early days.'' 
.IP \s-2[LOTTERY1]\s+2 .5i
David Petrou and John Milford. Proportional-Share Scheduling:
Implementation and Evaluation in a Widely-Deployed Operating System,
December 1997.
.nf
\s-2\fChttp://www.cs.cmu.edu/~dpetrou/papers/freebsd_lottery_writeup98.ps\fP\s+2
\s-2\fChttp://www.cs.cmu.edu/~dpetrou/code/freebsd_lottery_code.tar.gz\fP\s+2
.IP \s-2[LOTTERY2]\s+2 .5i
Carl A. Waldspurger and William E. Weihl. Lottery Scheduling: Flexible Proportional-Share Resource Management, Proceedings of the First Symposium on Operating Systems Design and Implementation (OSDI '94), pages 1-11, Monterey, California, November 1994.
.nf
\s-2\fChttp://www.research.digital.com/SRC/personal/caw/papers.html\fP\s+2
.IP \s-2[POSIX1e]\s+2 .5i
Draft Standard for Information Technology \(em
Portable Operating System Interface (POSIX) \(em
Part 1: System Application Program Interface (API) \(em Amendment:
Protection, Audit and Control Interfaces [C Language]
IEEE Std 1003.1e Draft 17 Editor Casey Schaufler
.IP \s-2[ROOT]\s+2 .5i
Historically other names have been used at times, Zilog for instance
called the super-user account ``zeus''.
.IP \s-2[UAS]\s+2 .5i
One such niche product is the ``UAS'' system to maintain and audit
RACF configurations on MVS systems.
.nf
\s-2\fChttp://www.entactinfo.com/products/uas/\fP\s+2
.IP \s-2[UF]\s+2 .5i
Quote from the User-Friendly cartoon by Illiad.
.nf
\s-2\fChttp://www.userfriendly.org/cartoons/archives/98nov/19981111.html\fP\s+2
