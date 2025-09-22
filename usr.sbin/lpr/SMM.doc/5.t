.\" $OpenBSD: 5.t,v 1.5 2004/04/14 20:52:20 millert Exp $
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
.\"	@(#)5.t	8.1 (Berkeley) 6/8/93
.\"
.NH 1
Output filter specifications
.PP
The filters supplied with OpenBSD
handle printing and accounting for most common
line printers, the Benson-Varian, the wide (36") and
narrow (11") Versatec printer/plotters. For other devices or accounting
methods, it may be necessary to create a new filter.
.PP
Filters are spawned by \fIlpd\fP
with their standard input the data to be printed, and standard output
the printer.  The standard error is attached to the
.B lf
file for logging errors or \fIsyslogd\fP may be used for logging errors.
A filter must return a 0 exit
code if there were no errors, 1 if the job should be reprinted,
and 2 if the job should be thrown away.
When \fIlprm\fP
sends a kill signal to the \fIlpd\fP process controlling
printing, it sends a SIGINT signal 
to all filters and descendents of filters.
This signal can be trapped by filters that need
to do cleanup operations such as
deleting temporary files.
.PP
Arguments passed to a filter depend on its type.
The
.B of
filter is called with the following arguments.
.DS
\fIfilter\fP \fB\-w\fPwidth \fB\-l\fPlength
.DE
The \fIwidth\fP and \fIlength\fP values come from the
.B pw
and
.B pl
entries in the printcap database.
The
.B if
filter is passed the following parameters.
.DS
\fIfilter\fP [\|\fB\-c\fP\|] \fB\-w\fPwidth \fB\-l\fPlength \fB\-i\fPindent \fB\-n\fP login \fB\-j\fP jobname \fB\-h\fP host accounting_file
.DE
The
.B \-c
flag is optional, and only supplied when control characters
are to be passed uninterpreted to the printer (when using the
.B \-l
option of
.I lpr
to print the file).
The
.B \-w
and
.B \-l
parameters are the same as for the
.B of
filter.
The
.B \-n
and
.B \-h
parameters specify the login name and host name of the job owner.
The last argument is the name of the accounting file from
.IR printcap .
The
.B \-j
parameter is optional and specifies the name of the print job if available.
.PP
All other filters are called with the following arguments:
.DS
\fIfilter\fP \fB\-x\fPwidth \fB\-y\fPlength \fB\-n\fP login \fB\-j\fP jobname \fB\-h\fP host accounting_file
.DE
The
.B \-x
and
.B \-y
options specify the horizontal and vertical page
size in pixels (from the
.B px
and
.B py
entries in the printcap file).
The rest of the arguments are the same as for the
.B if
filter.
