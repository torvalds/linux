.\" Copyright (c) 1980, 1993
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
.\"	@(#)mail6.nr	8.1 (Berkeley) 6/8/93
.\"
.bp
.sh 1 "Command line options"
.pp
This section describes command line options for
.i Mail
and what they are used for.
.ip \-N
Suppress the initial printing of headers.
.ip \-d
Turn on debugging information.  Not of general interest.
.ip "\-f file\ \ "
Show the messages in
.i file
instead of your system mailbox.  If
.i file
is omitted,
.i Mail
reads
.i mbox
in your home directory.
.ip \-i
Ignore tty interrupt signals.  Useful on noisy phone lines, which
generate spurious RUBOUT or DELETE characters.  It's usually
more effective to change your interrupt character to control\-c,
for which see the
.i stty
shell command.
.ip \-n
Inhibit reading of /etc/mail.rc.  Not generally useful, since
/etc/mail.rc is usually empty.
.ip "\-s string"
Used for sending mail.
.i String
is used as the subject of the message being composed.  If
.i string
contains blanks, you must surround it with quote marks.
.ip "\-u name"
Read
.i names's
mail instead of your own.  Unwitting others often neglect to protect
their mailboxes, but discretion is advised. Essentially,
.b "\-u user"
is a shorthand way of doing
.b "\-f /var/mail/user".
.ip "\-v"
Use the
.b \-v
flag when invoking sendmail. This feature may also be enabled
by setting the option "verbose".
.pp
The following command line flags are also recognized, but are
intended for use by programs invoking
.i Mail
and not for people.
.ip "\-T file"
Arrange to print on
.i file
the contents of the
.i article-id
fields of all messages that were either read or deleted.
.b \-T
is for the
.i readnews
program and should NOT be used for reading your mail.
.ip "\-h number"
Pass on hop count information.
.i Mail
will take the number, increment it, and pass it with
.b \-h
to the mail delivery system.
.b \-h
only has effect when sending mail and is used for network mail
forwarding.
.ip "\-r name"
Used for network mail forwarding:  interpret
.i name
as the sender of the message.  The
.i name
and
.b \-r
are simply sent along to the mail delivery system.  Also,
.i Mail
will wait for the message to be sent and return the exit status.
Also restricts formatting of message.
.pp
Note that
.b \-h
and
.b \-r ,
which are for network mail forwarding, are not used in practice
since mail forwarding is now handled separately.  They may
disappear soon.
