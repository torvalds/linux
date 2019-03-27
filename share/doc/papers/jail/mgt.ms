.\"
.\" $FreeBSD$
.\"
.NH
Managing Jails and the Jail File System Environment
.NH 2
Creating a Jail Environment
.PP
While the jail(2) call could be used in a number of ways, the expected
configuration creates a complete FreeBSD installation for each jail.
This includes copies of all relevant system binaries, data files, and its
own \fC/etc\fP directory.
Such a configuration maximises the independence of various jails,
and reduces the chances of interference between jails being possible,
especially when it is desirable to provide root access within a jail to
a less trusted user.
.PP
On a box making use of the jail facility, we refer to two types of
environment: the host environment, and the jail environment.
The host environment is the real operating system environment, which is
used to configure interfaces, and start up the jails.
There are then one or more jail environments, effectively virtual
FreeBSD machines.
When configuring Jail for use, it is necessary to configure both the
host and jail environments to prevent overlap.
.PP
As jailed virtual machines are generally bound to an IP address configured
using the normal IP alias mechanism, those jail IP addresses are also
accessible to host environment applications to use.
If the accessibility of some host applications in the jail environment is
not desirable, it is necessary to configure those applications to only
listen on appropriate addresses.
.PP
In most of the production environments where jail is currently in use,
one IP address is allocated to the host environment, and then a number
are allocated to jail boxes, with each jail box receiving a unique IP.
In this situation, it is sufficient to configure the networking applications
on the host to listen only on the host IP.
Generally, this consists of specifying the appropriate IP address to be
used by inetd and SSH, and disabling applications that are not capable
of limiting their address scope, such as sendmail, the port mapper, and
syslogd.
Other third party applications that have been installed on the host must also be
configured in this manner, or users connecting to the jailbox will
discover the host environment service, unless the jailbox has
specifically bound a service to that port.
In some situations, this can actually be the desirable behaviour.
.PP
The jail environments must also be custom-configured.
This consists of building and installing a miniature version of the
FreeBSD file system tree off of a subdirectory in the host environment,
usually \fC/usr/jail\fP, or \fC/data/jail\fP, with a subdirectory per jail.
Appropriate instructions for generating this tree are included in the
jail(8) man page, but generally this process may be automated using the
FreeBSD build environment.
.PP
One notable difference from the default FreeBSD install is that only
a limited set of device nodes should be created.
.PP
To improve storage efficiency, a fair number of the binaries in the system tree
may be deleted, as they are not relevant in a jail environment.
This includes the kernel, boot loader, and related files, as well as
hardware and network configuration tools.
.PP
After the creation of the jail tree, the easiest way to configure it is
to start up the jail in single-user mode.
The sysinstall admin tool may be used to help with the task, although
it is not installed by default as part of the system tree.
These tools should be run in the jail environment, or they will affect
the host environment's configuration.
.DS
.ft C
.ps -2
# mkdir /data/jail/192.168.11.100/stand
# cp /stand/sysinstall /data/jail/192.168.11.100/stand
# jail /data/jail/192.168.11.100 testhostname 192.168.11.100 \e
	/bin/sh
.ps +2
.R
.DE
.PP
After running the jail command, the shell is now within the jail environment,
and all further commands
will be limited to the scope of the jail until the shell exits.
If the network alias has not yet been configured, then the jail will be
unable to access the network.
.PP
The startup configuration of the jail environment may be configured so
as to quell warnings from services that cannot run in the jail.
Also, any per-system configuration required for a normal FreeBSD system
is also required for each jailbox.
Typically, this includes:
.IP "" 5n
\(bu Create empty /etc/fstab
.IP
\(bu Disable portmapper
.IP
\(bu Run newaliases
.IP
\(bu Disabling interface configuration
.IP
\(bu Configure the resolver
.IP
\(bu Set root password
.IP
\(bu Set timezone
.IP
\(bu Add any local accounts
.IP
\(bu Install any packets
.NH 2
Starting Jails
.PP
Jails are typically started by executing their /etc/rc script in much
the same manner a shell was started in the previous section.
Before starting the jail, any relevant networking configuration
should also be performed.
Typically, this involves adding an additional IP address to the
appropriate network interface, setting network properties for the
IP address using IP filtering, forwarding, and bandwidth shaping,
and mounting a process file system for the jail, if the ability to
debug processes from within the jail is desired.
.DS
.ft C
.ps -2
# ifconfig ed0 inet add 192.168.11.100 netmask 255.255.255.255
# mount -t procfs proc /data/jail/192.168.11.100/proc
# jail /data/jail/192.168.11.100 testhostname 192.168.11.100 \e
	/bin/sh /etc/rc
.ps +2
.ft P
.DE
.PP
A few warnings are generated for sysctl's that are not permitted
to be set within the jail, but the end result is a set of processes
in an isolated process environment, bound to a single IP address.
Normal procedures for accessing a FreeBSD machine apply: telneting in
through the network reveals a telnet prompt, login, and shell.
.DS
.ft C
.ps -2
% ps ax
  PID  TT  STAT      TIME COMMAND
  228  ??  SsJ    0:18.73 syslogd
  247  ??  IsJ    0:00.05 inetd -wW
  249  ??  IsJ    0:28.43 cron
  252  ??  SsJ    0:30.46 sendmail: accepting connections on port 25
  291  ??  IsJ    0:38.53 /usr/local/sbin/sshd
93694  ??  SJ     0:01.01 sshd: rwatson@ttyp0 (sshd)
93695  p0  SsJ    0:00.06 -csh (csh)
93700  p0  R+J    0:00.00 ps ax
.ps +2
.ft P
.DE
.PP
It is immediately obvious that the environment is within a jailbox: there
is no init process, no kernel daemons, and a J flag is present beside all
processes indicating the presence of a jail.
.PP
As with any FreeBSD system, accounts may be created and deleted,
mail is delivered, logs are generated, packages may be added, and the
system may be hacked into if configured incorrectly, or running a buggy
version of a piece of software.
However, all of this happens strictly within the scope of the jail.
.NH 2
Jail Management
.PP
Jail management is an interesting prospect, as there are two perspectives
from which a jail environment may be administered: from within the jail,
and from the host environment.
From within the jail, as described above, the process is remarkably similar
to any regular FreeBSD install, although certain actions are prohibited,
such as mounting file systems, modifying system kernel properties, etc.
The only area that really differs are that of shutting
the system down: the processes within the jail may deliver signals 
between them, allowing all processes to be killed, but bringing the
system back up requires intervention from outside of the jailbox.
.PP
From outside of the jail, there are a range of capabilities, as well
as limitations.
The jail environment is, in effect, a subset of the host environment:
the jail file system appears as part of the host file system, and may
be directly modified by processes in the host environment.
Processes within the jail appear in the process listing of the host,
and may likewise be signalled or debugged.
The host process file system makes the hostname of the jail environment
accessible in /proc/procnum/status, allowing utilities in the host
environment to manage processes based on jailname.
However, the default configuration allows privileged processes within
jails to set the hostname of the jail, which makes the status file less
useful from a management perspective if the contents of the jail are
malicious.
To prevent a jail from changing its hostname, the
"security.jail.set_hostname_allowed" sysctl may be set to 0 prior to 
starting any jails.
.PP
One aspect immediately observable in an environment with multiple jails
is that uids and gids are local to each jail environment: the uid associated
with a process in one jail may be for a different user than in another
jail.
This collision of identifiers is only visible in the host environment,
as normally processes from one jail are never visible in an environment
with another scope for user/uid and group/gid mapping.
Managers in the host environment should understand these scoping issues,
or confusion and unintended consequences may result.
.PP
Jailed processes are subject to the normal restrictions present for
any processes, including resource limits, and limits placed by the network
code, including firewall rules.
By specifying firewall rules for the IP address bound to a jail, it is
possible to place connectivity and bandwidth limitations on individual
jails, restricting services that may be consumed or offered.
.PP
Management of jails is an area that will see further improvement in
future versions of FreeBSD.  Some of these potential improvements are
discussed later in this paper.
