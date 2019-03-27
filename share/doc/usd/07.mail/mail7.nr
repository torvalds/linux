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
.\"	@(#)mail7.nr	8.1 (Berkeley) 6/8/93
.\"
.sh 1 "Format of messages"
.pp
This section describes the format of messages.
Messages begin with a
.i from
line, which consists of the word
.q From
followed by a user name, followed by anything, followed by
a date in the format returned by the
.i ctime
library routine described in section 3 of the Unix Programmer's
Manual.  A possible
.i ctime
format date is:
.(l
Tue Dec  1 10:58:23 1981
.)l
The
.i ctime
date may be optionally followed by a single space and a
time zone indication, which
should be three capital letters, such as PDT.
.pp
Following the
.i from
line are zero or more
.i "header field"
lines.
Each header field line is of the form:
.(l
name: information
.)l
.i Name
can be anything, but only certain header fields are recognized as
having any meaning.  The recognized header fields are:
.i article-id ,
.i bcc ,
.i cc ,
.i from ,
.i reply-to ,
.i sender ,
.i subject ,
and
.i to .
Other header fields are also significant to other systems; see,
for example, the current Arpanet message standard for much more
information on this topic.
A header field can be continued onto following lines by making the
first character on the following line a space or tab character.
.pp
If any headers are present, they must be followed by a blank line.
The part that follows is called the
.i body
of the message, and must be ASCII text, not containing null characters.
Each line in the message body must be no longer than 512 characters and
terminated with an ASCII newline character.
If binary data must be passed through the mail system, it is suggested
that this data be encoded in a system which encodes six bits into
a printable character (i.e.: uuencode).
For example, one could use the upper and lower case letters, the digits,
and the characters comma and period to make up the 64 characters.
Then, one can send a 16-bit binary number
as three characters.  These characters should be packed into lines,
preferably lines about 70 characters long as long lines are transmitted
more efficiently.
.pp
The message delivery system always adds a blank line to the end of
each message.  This blank line must not be deleted.
.pp
The UUCP message delivery system sometimes adds a blank line to
the end of a message each time it is forwarded through a machine.
.pp
It should be noted that some network transport protocols enforce
limits to the lengths of messages.
