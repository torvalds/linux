.\" $OpenBSD: 3.t,v 1.5 2003/06/02 23:36:53 millert Exp $
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
.\"	@(#)3.t	8.1 (Berkeley) 6/8/93
.\"
.NH 1
Access control
.PP
The printer system maintains protected spooling areas so that
users cannot circumvent printer accounting or
remove files other than their own.
The strategy used to maintain protected
spooling areas is as follows:
.IP \(bu 3
The spooling area is writable only by \fIroot\fP and
and the \fIdaemon\fP group.
.IP \(bu 3
The \fIlpr\fP and \fIlprm\fP programs run set-user-id to user \fIdaemon\fP and
set-group-id to group \fIdaemon\fP.
.IP \(bu 3
The \fIlpc\fP and \fIlpq\fP programs run set-group-id to group \fIdaemon\fP
to access spool files.
.IP \(bu 3
Control and data files in a spooling area are made with \fIdaemon\fP
ownership and group ownership \fIdaemon\fP.  Their mode is 0660.
This ensures control files are not modified by a user
and that no user can remove files except through \fIlprm\fP.
.IP \(bu 3
The printer server, \fIlpd\fP, runs as \fIroot\fP but spends most
of its time with the effective user-id set to \fIdaemon\fP and the
effective group-id set to \fIdaemon\fP.  As a result, spool files
it creates belong to user and group \fIdaemon\fP.  \fILpd\fP uses
the same verification procedures as \fIrshd\fP\|(8) in authenticating
remote clients.  The host on which a client resides must be present
in the file /etc/hosts.equiv or /etc/hosts.lpd and the request
message must come from a reserved port number.
