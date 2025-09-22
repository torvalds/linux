.\" $OpenBSD: 6.t,v 1.4 2003/06/02 23:36:53 millert Exp $
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
.\"	@(#)6.t	8.1 (Berkeley) 6/8/93
.\"
.NH 1
Line printer Administration
.PP
The
.I lpc
program provides local control over line printer activity.
The major commands and their intended use will be described.
The command format and remaining commands are described in
.IR lpc (8).
.LP
\fBabort\fP and \fBstart\fP
.IP
.I Abort
terminates an active spooling daemon on the local host immediately and
then disables printing (preventing new daemons from being started by
.IR lpr ).
This is normally used to forcibly restart a hung line printer daemon
(i.e., \fIlpq\fP reports that there is a daemon present but nothing is
happening).  It does not remove any jobs from the queue
(use the \fIlprm\fP command instead).
.I Start
enables printing and requests \fIlpd\fP to start printing jobs.
.LP
\fBenable\fP and \fBdisable\fP
.IP
\fIEnable\fP and \fIdisable\fP allow spooling in the local queue to be
turned on/off.
This will allow/prevent
.I lpr
from putting new jobs in the spool queue.  It is frequently convenient
to turn spooling off while testing new line printer filters since the
.I root
user can still use
.I lpr
to put jobs in the queue but no one else can.
The other main use is to prevent users from putting jobs in the queue
when the printer is expected to be unavailable for a long time.
.LP
\fBrestart\fP
.IP
.I Restart
allows ordinary users to restart printer daemons when
.I lpq
reports that there is no daemon present.
.LP
\fBstop\fP
.IP
.I Stop
halts a spooling daemon after the current job completes;
this also disables printing.  This is a clean way to shutdown a
printer to do maintenance, etc.  Note that users can still enter jobs in a
spool queue while a printer is
.IR stopped .
.LP
\fBtopq\fP
.IP
.I Topq
places jobs at the top of a printer queue.  This can be used
to reorder high priority jobs since
.I lpr
only provides first-come-first-serve ordering of jobs.
.LP
\fBup\fP and \fBdown\fP
.IP
\fIUp\fP and \fIdown\fP combine the functionality of \fIenable\fP
and \fIstart\fP with \fIstart\fP and \fIstop\fP.  \fIUp\fP is
equivalent to issuing the \fIstart\fP and \fIenable\fP commands,
whereas \fIdown\fP is equivalent to issuing the \fIstop\fP and
\fIdisable\fP commands.  \fIDown\fP also takes an optional message
that will be written to the printer's status file.  This allows the
administrator to indicate to users why the printer is out of service.
