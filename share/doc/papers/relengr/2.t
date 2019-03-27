.\" Copyright (c) 1989 The Regents of the University of California.
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
.\"	@(#)2.t	5.1 (Berkeley) 4/17/91
.\"
.NH
System Development
.PP
The first phase of each Berkeley system is its development.
.SM CSRG
maintains a continuously evolving list of projects that are candidates
for integration into the system.
Some of these are prompted by emerging ideas from the research world,
such as the availability of a new technology, while other additions
are suggested by the commercial world, such as the introduction of
new standards like
.SM POSIX ,
and still other projects are emergency responses to situations like
the Internet Worm.
.PP
These projects are ordered based on the perceived benefit of the
project as opposed to its difficulty;
the most important are selected for inclusion in each new release.
Often there is a prototype available from a group outside
.SM CSRG .
Because of the limited staff at
.SM CSRG ,
this prototype is obtained to use as a starting base
for integration into the
.SM BSD
system.
Only if no prototype is available is the project begun in-house.
In either case, the design of the facility is forced to conform to the
.SM CSRG
style.
.PP
Unlike other development groups, the staff of
.SM CSRG
specializes by projects rather than by particular parts
of the system;
a staff person will be responsible for all aspects of a project.
This responsibility starts at the associated kernel device drivers;
it proceeds up through the rest of the kernel,
through the C library and system utility programs,
ending at the user application layer.
This staff person is also responsible for related documentation,
including manual pages.
Many projects proceed in parallel,
interacting with other projects as their paths cross.
.PP
All source code, documentation, and auxiliary files are kept
under a source code control system.
During development,
this control system is critical for notifying people
when they are colliding with other ongoing projects.
Even more important, however,
is the audit trail maintained by the control system that
is critical to the release engineering phase of the project
described in the next section.
.PP
Much of the development of
.SM BSD
is done by personnel that are located at other institutions.
Many of these people not only have interim copies of the release
running on their own machines,
but also have user accounts on the main development
machine at Berkeley.
Such users are commonly found logged in at Berkeley over the
Internet, or sometimes via telephone dialup, from places as far away
as Massachusetts or Maryland, as well as from closer places, such as
Stanford.
For the \*(b3 release,
certain users had permission to modify the master copy of the
system source directly.
People given access to the master sources
are carefully screened beforehand,
but are not closely supervised.
Their work is checked at the end of the beta-test period by
.SM CSRG
personnel who back out inappropriate changes.
Several facilities, including the
Fortran and C compilers,
as well as important system programs, for example,
.PN telnet
and
.PN ftp ,
include significant contributions from people who did not work
directly for
.SM CSRG .
One important exception to this approach is that changes to the kernel
are made only by
.SM CSRG
personnel, although the changes are often suggested by the larger community.
.PP
The development phase continues until
.SM CSRG
decides that it is appropriate to make a release.
The decision to halt development and transition to release mode
is driven by several factors.
The most important is that enough projects have been completed
to make the system significantly superior to the previously released
version of the system.
For example,
\*(b3 was released primarily because of the need for
the improved networking capabilities and the markedly
improved system performance.
Of secondary importance is the issue of timing.
If the releases are too infrequent, then
.SM CSRG
will be inundated with requests for interim releases.
Conversely,
if systems are released too frequently,
the integration cost for many vendors will be too high,
causing them to ignore the releases.
Finally,
the process of release engineering is long and tedious.
Frequent releases slow the rate of development and
cause undue tedium to the staff.
