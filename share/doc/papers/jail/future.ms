.\"
.\" $FreeBSD$
.\"
.NH
Future Directions
.PP
The jail facility has already been deployed in numerous capacities and
a few opportunities for improvement have manifested themselves.
.NH 2
Improved Virtualisation
.PP
As it stands, the jail code provides a strict subset of system resources
to the jail environment, based on access to processes, files, network
resources, and privileged services.
Virtualisation, or making the jail environments appear to be fully
functional FreeBSD systems, allows maximum application support and the
ability to offer a wide range of services within a jail environment.
However, there are a number of limitations on the degree of virtualisation
in the current code, and removing these limitations will enhance the
ability to offer services in a jail environment.
Two areas that deserve greater attention are the virtualisation of
network resources, and management of scheduling resources.
.PP
Currently, a single IP address may be allocated to each jail, and all
communication from the jail is limited to that IP address.
In particular, these addresses are IPv4 addresses.
There has been substantial interest in improving interface virtualisation,
allowing one or more addresses to be assigned to an interface, and
removing the requirement that the address be an IPv4 address, allowing
the use of IPv6.
Also, access to raw sockets is currently prohibited, as the current
implementation of raw sockets allows access to raw IP packets associated
with all interfaces.
Limiting the scope of the raw socket would allow its safe use within
a jail, re-enabling support for ping, and other network debugging and
evaluation tools.
.PP
Another area of great interest to the current consumers of the jail code
is the ability to limit the impact of one jail on the CPU resources
available for other jails.
Specifically, this would require that the jail of a process play a rule in
its scheduling parameters.
Prior work in the area of lottery scheduling, currently available as
patches on FreeBSD 2.2.x, might be leveraged to allow some degree of
partitioning between jail environments \s-2[LOTTERY1] [LOTTERY2]\s+2.
However, as the current scheduling mechanism is targeted at time
sharing, and FreeBSD does not currently support real time preemption
of processes in kernel, complete partitioning is not possible within the
current framework.
.NH 2
Improved Management
.PP
Management of jail environments is currently somewhat ad hoc--creating
and starting jails is a well-documented procedure, but day-to-day
management of jails, as well as special case procedures such as shutdown,
are not well analysed and documented.
The current kernel process management infrastructure does not have the
ability to manage pools of processes in a jail-centric way.
For example, it is possible to, within a jail, deliver a signal to all
processes in a jail, but it is not possibly to atomically target all
processes within a jail from outside of the jail.
If the jail code is to effectively limit the behaviour of a jail, the
ability to shut it down cleanly is paramount.
Similarly, shutting down a jail cleanly from within is also not well
defined, the traditional shutdown utilities having been written with
a host environment in mind.
This suggests a number of improvements, both in the kernel and in the
user-land utility set.
.PP
First, the ability to address kernel-centric management mechanisms at
jails is important.
One way in which this might be done is to assign a unique jail id, not
unlike a process id or process group id, at jail creation time.
A new jailkill() syscall would permit the direction of signals to
specific jailids, allowing for the effective termination of all processes
in the jail.
A unique jailid could also supplant the hostname as the unique
identifier for a jail, allowing the hostname to be changed by the
processes in the jail without interfering with jail management.
.PP
More carefully defining the user-land semantics of a jail during startup
and shutdown is also important.
The traditional FreeBSD environment makes use of an init process to
bring the system up during the boot process, and to assist in shutdown.
A similar technique might be used for jail, in effect a jailinit,
formulated to handle the clean startup and shutdown, including calling
out to jail-local /etc/rc.shutdown, and other useful shutdown functions.
A jailinit would also present a central location for delivering
management requests to within a jail from the host environment, allowing
the host environment to request the shutdown of the jail cleanly, before
resorting to terminating processes, in the same style as the host
environment shutting down before killing all processes and halting the
kernel.
.PP
Improvements in the host environment would also assist in improving
jail management, possibly including automated runtime jail management tools,
tools to more easily construct the per-jail file system area, and
include jail shutdown as part of normal system shutdown.
.PP
These improvements in the jail framework would improve both raw
functionality and usability from a management perspective.
The jail code has raised significant interest in the FreeBSD community,
and it is hoped that this type of improved functionality will be
available in upcoming releases of FreeBSD.
