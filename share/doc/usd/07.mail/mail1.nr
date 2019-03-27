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
.\"	@(#)mail1.nr	8.1 (Berkeley) 6/8/93
.\"
.sh 1 Introduction
.pp
.i Mail
provides a simple and friendly environment for sending and receiving mail.
It divides incoming mail into
its constituent messages and allows the user to deal with them
in any order.  In addition, it provides a set of
.i ed -\c
like commands for manipulating messages and sending mail.
.i Mail
offers the user simple editing capabilities to ease the composition
of outgoing messages, as well as providing the ability to define and send
to names which address groups of users.  Finally,
.i Mail
is able to send and receive messages across such networks as the
ARPANET, UUCP, and Berkeley network.
.pp
This document describes how to use the
.i Mail
program to send and receive messages.  The reader is not assumed to
be familiar with other message handling systems, but should be
familiar with the \s-2UNIX\s0\**
.(f
\** \s-1UNIX\s0 is a trademark of Bell Laboratories.
.)f
shell, the text editor, and some of the common \s-2UNIX\s0 commands.
.q "The \s-2UNIX\s0 Programmer's Manual,"
.q "An Introduction to Csh,"
and
.q "Text Editing with Ex and Vi"
can be consulted for more information on these topics.
.pp
Here is how messages are handled:
the mail system accepts incoming
.i messages
for you from other people
and collects them in a file, called your
.i "system mailbox" .
When you login, the system notifies you if there are any messages
waiting in your system mailbox.  If you are a
.i csh
user, you will be notified when new mail arrives if you inform
the shell of the location of your mailbox.  On version 7 systems,
your system mailbox is located in the directory /var/mail
in a file with your login name.  If your login name is
.q sam,
then you can make
.i csh
notify you of new mail by including the following line in your .cshrc
file:
.(l
set mail=/var/mail/sam
.)l
When you read your mail using
.i Mail ,
it reads your system mailbox and separates that file into the
individual messages that have been sent to you.  You can then
read, reply to, delete, or save these messages.
Each message is marked with its author and the date they sent it.
