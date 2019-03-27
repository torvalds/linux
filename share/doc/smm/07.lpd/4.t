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
.\"	@(#)4.t	8.1 (Berkeley) 6/8/93
.\"
.NH 1
Setting up
.PP
The 4.3BSD release comes with the necessary programs 
installed and with the default line printer queue
created.  If the system must be modified, the
makefile in the directory /usr/src/usr.lib/lpr
should be used in recompiling and reinstalling
the necessary programs.
.PP
The real work in setting up is to create the
.I printcap
file and any printer filters for printers not supported
in the distribution system.
.NH 2
Creating a printcap file
.PP
The 
.I printcap
database contains one or more entries per printer.
A printer should have a separate spooling directory;
otherwise, jobs will be printed on
different printers depending on which printer daemon starts first.
This section describes how to create entries for printers that do not
conform to the default printer description (an LP-11 style interface to a
standard, band printer).
.NH 3
Printers on serial lines
.PP
When a printer is connected via a serial communication line
it must have the proper baud rate and terminal modes set.
The following example is for a DecWriter III printer connected
locally via a 1200 baud serial line.
.DS
.DT
lp|LA-180 DecWriter III:\e
	:lp=/dev/lp:br#1200:fs#06320:\e
	:tr=\ef:of=/usr/lib/lpf:lf=/usr/adm/lpd-errs:
.DE
The
.B lp
entry specifies the file name to open for output. Here it could
be left out since ``/dev/lp'' is the default.
The
.B br
entry sets the baud rate for the tty line and the
.B fs
entry sets CRMOD, no parity, and XTABS (see \fItty\fP\|(4)).
The
.B tr
entry indicates that a form-feed should be printed when the queue
empties so the paper can be torn off without turning the printer off-line and
pressing form feed.
The
.B of
entry specifies the filter program
.I lpf
should be used for printing the files;
more will be said about filters later.
The last entry causes errors
to be written to the file ``/usr/adm/lpd-errs''
instead of the console.  Most errors from \fIlpd\fP are logged using
\fIsyslogd\fP\|(8) and will not be logged in the specified file.  The
filters should use \fIsyslogd\fP to report errors; only those that
write to standard error output will end up with errors in the \fBlf\fP file.
(Occasionally errors sent to standard error output have not appeared
in the log file; the use of \fIsyslogd\fP is highly recommended.)
.NH 3
Remote printers
.PP
Printers that reside on remote hosts should have an empty
.B lp
entry.
For example, the following printcap entry would send output to the printer
named ``lp'' on the machine ``ucbvax''.
.DS
.DT
lp|default line printer:\e
	:lp=:rm=ucbvax:rp=lp:sd=/usr/spool/vaxlpd:
.DE
The
.B rm
entry is the name of the remote machine to connect to; this name must
be a known host name for a machine on the network.
The
.B rp
capability indicates
the name of the printer on the remote machine is ``lp'';
here it could be left out since this is the default value.
The
.B sd
entry specifies ``/usr/spool/vaxlpd''
as the spooling directory instead of the
default value of ``/usr/spool/lpd''.
.NH 2
Output filters
.PP
Filters are used to handle device dependencies and to
do accounting functions.  The output filtering of
.B of
is used when accounting is
not being done or when all text data must be passed through a filter.
It is not intended to do accounting since it is started only once,
all text files are filtered through it, and no provision is made for passing
owners' login name, identifying the beginning and ending of jobs, etc.
The other filters (if specified) are started for each file
printed and do accounting if there is an
.B af
entry.
If entries for both
.B of
and other filters are specified,
the output filter is used only to print the banner page;
it is then stopped to allow other filters access to the printer.
An example of a printer that requires output filters
is the Benson-Varian.
.DS
.DT
va|varian|Benson-Varian:\e
	:lp=/dev/va0:sd=/usr/spool/vad:of=/usr/lib/vpf:\e
	:tf=/usr/lib/rvcat:mx#2000:pl#58:px=2112:py=1700:tr=\ef:
.DE
The
.B tf
entry specifies ``/usr/lib/rvcat'' as the filter to be
used in printing \fItroff\fP\|(1) output.
This filter is needed to set the device into print mode
for text, and plot mode for printing
.I troff
files and raster images (see \fIva\fP\|(4V)).
Note that the page length is set to 58 lines by the
.B pl
entry for 8.5" by 11" fan-fold paper.
To enable accounting, the varian entry would be
augmented with an
.B af
filter as shown below.
.DS
.DT
va|varian|Benson-Varian:\e
	:lp=/dev/va0:sd=/usr/spool/vad:of=/usr/lib/vpf:\e
	:if=/usr/lib/vpf:tf=/usr/lib/rvcat:af=/usr/adm/vaacct:\e
	:mx#2000:pl#58:px=2112:py=1700:tr=\ef:
.DE
.NH 2
Access Control
.PP
Local access to printer queues is controlled with the
.B rg
printcap entry.
.DS
	:rg=lprgroup:
.DE
Users must be in the group
.I lprgroup
to submit jobs to the specified printer.
The default is to allow all users access.
Note that once the files are in the local queue, they can be printed
locally or forwarded to another host depending on the configuration.
.PP
Remote access is controlled by listing the hosts in either the file
/etc/hosts.equiv or /etc/hosts.lpd, one host per line. Note that
.IR rsh (1)
and
.IR rlogin (1)
use /etc/hosts.equiv to determine which hosts are equivalent for allowing logins
without passwords. The file /etc/hosts.lpd is only used to control
which hosts have line printer access.
Remote access can be further restricted to only allow remote users with accounts
on the local host to print jobs by using the \fBrs\fP printcap entry.
.DS
	:rs:
.DE
