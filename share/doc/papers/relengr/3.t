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
.\"	@(#)3.t	5.1 (Berkeley) 4/17/91
.\"
.NH
System Release
.PP
Once the decision has been made to halt development
and begin release engineering,
all currently unfinished projects are evaluated.
This evaluation involves computing the time required to complete
the project as opposed to how important the project is to the
upcoming release.
Projects that are not selected for completion are
removed from the distribution branch of the source code control system
and saved on branch deltas so they can be retrieved,
completed, and merged into a future release;
the remaining unfinished projects are brought to orderly completion.
.PP
Developments from
.SM CSRG
are released in three steps: alpha, beta, and final.
Alpha and beta releases are not true distributions\(emthey
are test systems.
Alpha releases are normally available to only a few sites,
usually those working closely with
.SM CSRG .
More sites are given beta releases,
as the system is closer to completion,
and needs wider testing to find more obscure problems.
For example, \*(b3 alpha was distributed to about fifteen
sites, while \*(b3 beta ran at more than a hundred.
.NH 2
Alpha Distribution Development
.PP
The first step in creating an alpha distribution is to evaluate the
existing state of the system and to decide what software should be
included in the release.
This decision process includes not only deciding what software should
be added, but also what obsolete software ought to be retired from the
distribution.
The new software includes the successful projects that have been
completed at
.SM CSRG
and elsewhere, as well as some portion of the vast quantity of
contributed software that has been offered during the development
period.
.PP
Once an initial list has been created,
a prototype filesystem corresponding to the distribution
is constructed, typically named
.PN /nbsd .
This prototype will eventually turn into the master source tree for the
final distribution.
During the period that the alpha distribution is being created,
.PN /nbsd
is mounted read-write, and is highly fluid.
Programs are created and deleted,
old versions of programs are completely replaced,
and the correspondence between the sources and binaries
is only loosely tracked.
People outside
.SM CSRG
who are helping with the distribution are free to
change their parts of the distribution at will.
.PP
During this period the newly forming distribution is
checked for interoperability.
For example,
in \*(b3 the output of context differences from
.PN diff
was changed to merge overlapping sections.
Unfortunately, this change broke the
.PN patch
program which could no longer interpret the output of
.PN diff .
Since the change to
.PN diff
and the
.PN patch
program had originated outside Berkeley,
.SM CSRG
had to coordinate the efforts of the respective authors
to make the programs work together harmoniously.
.PP
Once the sources have stabilized,
an attempt is made to compile the entire source tree.
Often this exposes errors caused by changed header files,
or use of obsoleted C library interfaces.
If the incompatibilities affect too many programs,
or require excessive amounts of change in the programs
that are affected,
the incompatibility is backed out or some backward-compatible
interface is provided.
The incompatibilities that are found and left in are noted
in a list that is later incorporated into the release notes.
Thus, users upgrading to the new system can anticipate problems
in their own software that will require change.
.PP
Once the source tree compiles completely,
it is installed and becomes the running system that
.SM CSRG
uses on its main development machine.
Once in day-to-day use,
other interoperability problems become apparent
and are resolved.
When all known problems have been resolved, and the system has been
stable for some period of time, an alpha distribution tape is made
from the contents of
.PN /nbsd .
.PP
The alpha distribution is sent out to a small set of test sites.
These test sites are selected as having a
sophisticated user population, not only capable of finding bugs,
but also of determining their cause and developing a fix for the problem.
These sites are usually composed of groups that are contributing
software to the distribution or groups that have a particular expertise
with some portion of the system.
.NH 2
Beta Distribution Development
.PP
After the alpha tape is created,
the distribution filesystem is mounted read-only.
Further changes are requested in a change log rather than
being made directly to the distribution.
The change requests are inspected and implemented by a
.SM CSRG
staff person, followed by a compilation of the affected
programs to ensure that they still build correctly.
Once the alpha tape has been cut,
changes to the distribution are no longer made by people outside
.SM CSRG .
.PP
As the alpha sites install and begin running the alpha distribution,
they monitor the problems that they encounter.
For minor bugs, they typically report back the bug along with
a suggested fix.
Since many of the alpha sites are selected from among the people
working closely with
.SM CSRG ,
they often have accounts on, and access to, the primary
.SM CSRG
development machine.
Thus, they are able to directly install the fix themselves,
and simply notify
.SM CSRG
when they have fixed the problem.
After verifying the fix, the affected files are added to
the list to be updated on
.PN /nbsd .
.PP
The more important task of the alpha sites is to test out the
new facilities that have been added to the system.
The alpha sites often find major design flaws
or operational shortcomings of the facilities.
When such problems are found,
the person in charge of that facility is responsible
for resolving the problem.
Occasionally this requires redesigning and reimplementing
parts of the affected facility.
For example,
in 4.2\s-1BSD\s+1,
the alpha release of the networking system did not have connection queueing.
This shortcoming prevented the network from handling many
connections to a single server.
The result was that the networking interface had to be
redesigned to provide this functionality.
.PP
The alpha sites are also responsible for ferreting out interoperability
problems between different utilities.
The user populations of the test sites differ from the user population at
.SM CSRG ,
and, as a result, the utilities are exercised in ways that differ
from the ways that they are used at
.SM CSRG .
These differences in usage patterns turn up problems that
do not occur in our initial test environment.
.PP
The alpha sites frequently redistribute the alpha tape to several
of their own alpha sites that are particularly interested
in parts of the new system.
These additional sites are responsible for reporting
problems back to the site from which they received the distribution,
not to
.SM CSRG .
Often these redistribution sites are less sophisticated than the
direct alpha sites, so their reports need to be filtered
to avoid spurious, or site dependent, bug reports.
The direct alpha sites sift through the reports to find those that
are relevant, and usually verify the suggested fix if one is given,
or develop a fix if none is provided.
This hierarchical testing process forces
bug reports, fixes, and new software
to be collected, evaluated, and checked for inaccuracies
by first-level sites before being forwarded to
.SM CSRG ,
allowing the developers at
.SM CSRG
to concentrate on tracking the changes being made to the system
rather than sifting through information (often voluminous) from every
alpha-test site.
.PP
Once the major problems have been attended to,
the focus turns to getting the documentation synchronized
with the code that is being shipped.
The manual pages need to be checked to be sure that
they accurately reflect any changes to the programs that
they describe.
Usually the manual pages are kept up to date as
the program they describe evolves.
However, the supporting documents frequently do not get changed,
and must be edited to bring them up to date.
During this review, the need for other documents becomes evident.
For example, it was
during this phase of \*(b3 that it was decided
to add a tutorial document on how to use the socket
interprocess communication primitives.
.PP
Another task during this period is to contact the people that
have contributed complete software packages
(such as
.PN RCS
or
.PN MH )
in previous releases to see if they wish to
make any revisions to their software.
For those who do,
the new software has to be obtained,
and tested to verify that it compiles and runs
correctly on the system to be released.
Again, this integration and testing can often be done by the
contributors themselves by logging directly into the master machine.
.PP
After the stream of bug reports has slowed down
to a reasonable level,
.SM CSRG
begins a careful review of all the changes to the
system since the previous release.
The review is done by running a recursive
.PN diff
of the entire source tree\(emhere, of
.PN /nbsd
with 4.2\s-1BSD\s+1.
All the changes are checked to ensure that they are reasonable,
and have been properly documented.
The process often turns up questionable changes.
When such a questionable change is found,
the source code control system log is examined to find
out who made the change and what their explanation was
for the change.
If the log does not resolve the problem,
the person responsible for the change is asked for an explanation
of what they were trying to accomplish.
If the reason is not compelling,
the change is backed out.
Facilities deemed inappropriate in \*(b3 included new options to
the directory-listing command and a changed return value for the
.RN fseek
library routine;
the changes were removed from the source before final distribution.
Although this process is long and tedious,
it forces the developers to obtain a coherent picture of the entire set of
changes to the system.
This exercise often turns up inconsistencies that would
otherwise never be found.
.PP
The outcome of the comparison results in
a pair of documents detailing
changes to every user-level command
.[
Bug Fixes and Changes
.]
and to every kernel source file.
.[
Changes to the Kernel
.]
These documents are delivered with the final distribution.
A user can look up any command by name and see immediately
what has changed,
and a developer can similarly look up any kernel
file by name and get a summary of the changes to that file.
.PP
Having completed the review of the entire system,
the preparation of the beta distribution is started.
Unlike the alpha distribution, where pieces of the system
may be unfinished and the documentation incomplete,
the beta distribution is put together as if it were
going to be the final distribution.
All known problems are fixed, and any remaining development
is completed.
Once the beta tape has been prepared,
no further changes are permitted to
.PN /nbsd
without careful review,
as spurious changes made after the system has been
.PN diff ed
are unlikely to be caught.
.NH 2
Final Distribution Development
.PP
The beta distribution goes to more sites than the
alpha distribution for three main reasons.
First, as it is closer to the final release, more sites are willing
to run it in a production environment without fear of catastrophic failures.
Second, more commercial sites delivering
.SM BSD -\c
derived systems are interested in getting a preview of the
upcoming changes in preparation for merging them into their
own systems.
Finally, because the beta tape has fewer problems,
it is beneficial to offer it to more sites in hopes of
finding as many of the remaining problems as possible.
Also, by handing the system out to less sophisticated sites,
issues that would be ignored by the users of the alpha sites
become apparent.
.PP
The anticipation is that the beta tape will not require
extensive changes to either the programs or the documentation.
Most of the work involves sifting through the reported bugs
to find those that are relevant and devising the minimal
reasonable set of changes to fix them.
After throughly testing the fix, it is listed in the update log for
.PN /nbsd .
One person at
.SM CSRG
is responsible for doing the update of
.PN /nbsd
and ensuring that everything affected by the change is rebuilt and tested.
Thus, a change to a C library routine requires that the entire
system be rebuilt.
.PP
During this period, the documentation is all printed and proofread.
As minor changes are made to the manual pages and documentation,
the affected pages must be reprinted.
.PP
The final step in the release process is to check the distribution tree
to ensure that it is in a consistent state.
This step includes verification that every file and directory
on the distribution has the proper owner, group, and modes.
All source files must be checked to be sure that they have
appropriate copyright notices and source code control system headers.
Any extraneous files must be removed.
Finally, the installed binaries must be checked to ensure that they correspond
exactly to the sources and libraries that are on the distribution.
.PP
This checking is a formidable task given that there are over 20,000 files on
a typical distribution.
Much of the checking can be done by a set of programs set to scan
over the distribution tree.
Unfortunately, the exception list is long, and requires
hours of tedious hand checking; this has caused
.SM CSRG
to develop even
more comprehensive validation programs for use in our next release.
.PP
Once the final set of checks has been run,
the master tape can be made, and the official distribution started.
As for the staff of
.SM CSRG ,
we usually take a brief vacation before plunging back into
a new development phase.
