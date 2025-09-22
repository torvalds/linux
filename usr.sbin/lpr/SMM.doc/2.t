.\" $OpenBSD: 2.t,v 1.5 2003/06/02 23:36:53 millert Exp $
.\"
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
.\"	@(#)2.t	8.1 (Berkeley) 6/8/93
.\"
.NH 1
Commands
.NH 2
lpd \- line printer daemon
.PP
The program
.IR lpd (8),
usually invoked at boot time from the /etc/rc file, acts as
a master server for coordinating and controlling
the spooling queues configured in the printcap file.
When
.I lpd
is started it makes a single pass through the
.I printcap
database restarting any printers that have jobs.
In normal operation
.I lpd
listens for service requests on multiple sockets,
one in the LOCAL domain (named ``/var/run/printer'') for
local requests, and one in the Internet domain
(under the ``printer'' service specification)
for requests for printer access from off machine;
see \fIsocket\fP\|(2) and \fIservices\fP\|(5)
for more information on sockets and service
specifications, respectively.
.I Lpd
spawns a copy of itself to process the request; the master daemon
continues to listen for new requests.
.PP
Clients communicate with 
.I lpd
using a simple transaction oriented protocol.
Authentication of remote clients is done based
on the ``privilege port'' scheme employed by
\fIrshd\fP\|(8) and \fIrcmd\fP\|(3).
The following table shows the requests 
understood by
.IR lpd .
In each request the first byte indicates the
``meaning'' of the request, followed by the name
of the printer to which it should be applied.  Additional
qualifiers may follow, depending on the request.
.DS
.TS
l l.
Request	Interpretation
_
^Aprinter\en	check the queue for jobs and print any found
^Bprinter\en	receive and queue a job from another machine
^Cprinter [users ...] [jobs ...]\en	return short list of current queue state
^Dprinter [users ...] [jobs ...]\en	return long list of current queue state
^Eprinter person [users ...] [jobs ...]\en	remove jobs from a queue
.TE
.DE
.PP
The \fIlpr\fP\|(1) command
is used by users to enter a print job in a local queue and to notify
the local
.I lpd
that there are new jobs in the spooling area.
.I Lpd
either schedules the job to be printed locally, or if
printing remotely, attempts to forward
the job to the appropriate machine.
If the printer cannot be opened or the destination
machine is unreachable, the job will remain queued until it is
possible to complete the work.
.NH 2
lpq \- show line printer queue
.PP
The \fIlpq\fP\|(1)
program works recursively backwards displaying the queue of the machine with
the printer and then the queue(s) of the machine(s) that lead to it.
.I Lpq
has two forms of output: in the default, short, format it
gives a single line of output per queued job; in the long 
format it shows the list of files, and their sizes, that
comprise a job.
.NH 2
lprm \- remove jobs from a queue
.PP
The \fIlprm\fP\|(1) command deletes jobs from a spooling
queue.  If necessary, \fIlprm\fP will first kill off a
running daemon that is servicing the queue and restart
it after the required files are removed.  When removing
jobs destined for a remote printer, \fIlprm\fP acts
similarly to \fIlpq\fP except it first checks locally
for jobs to remove and then
tries to remove files in queues off-machine.
.NH 2
lpc \- line printer control program
.PP
The
.IR lpc (8)
program is used by the system administrator to control the
operation of the line printer system.  
For each line printer configured in /etc/printcap,
.I lpc
may be used to:
.IP \(bu
disable or enable a printer,
.IP \(bu
disable or enable a printer's spooling queue,
.IP \(bu
rearrange the order of jobs in a spooling queue,
.IP \(bu
find the status of printers, and their associated
spooling queues and printer daemons.
