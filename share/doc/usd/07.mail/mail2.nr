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
.\"	@(#)mail2.nr	8.1 (Berkeley) 6/8/93
.\"
.bp
.sh 1 "Common usage"
.pp
The
.i Mail
command has two distinct usages, according to whether one
wants to send or receive mail.  Sending mail is simple:  to send a
message to a user whose login name is, say,
\*(lqroot,\*(rq
use the shell
command:
.(l
% Mail root
.)l
then type your message.  When you reach the end of the message, type
an EOT (control\-d) at the beginning of a line, which will cause
.i Mail
to echo \*(lqEOT\*(rq and return you to the Shell.  When the user you sent mail
to next logs in, he will receive the message:
.(l
You have mail.
.)l
to alert him to the existence of your message.
.pp
If, while you are composing the message
you decide that you do not wish to send it after all, you can
abort the letter with a \s-2RUBOUT\s0.  Typing a single \s-2RUBOUT\s0
causes
.i Mail
to print
.(l
(Interrupt -- one more to kill letter)
.)l
Typing a second
\s-2RUBOUT\s0 causes
.i Mail
to save your partial letter on the file
.q dead.letter
in your home directory and abort the letter.
Once you have
sent mail to someone, there is no way to undo the act, so be
careful.
.pp
The message your recipient reads will consist of the message you
typed, preceded by a line telling who sent the message (your login name)
and the date and time it
was sent.
.pp
If you want to send the same message to several other people, you can list
their login names on the command line.
Thus,
.(l
% Mail sam bob john
Tuition fees are due next Friday.  Don't forget!!
<Control\-d>
EOT
%
.)l
will send the reminder to sam, bob, and john.
.pp
If, when you log in, you see the message,
.(l
You have mail.
.)l
you can read the mail by typing simply:
.(l
% Mail
.)l
.i Mail
will respond by typing its version number and date and then listing
the messages you have waiting.  Then it will type a prompt and await
your command.  The messages are assigned numbers starting with 1 \*- you
refer to the messages with these numbers.
.i Mail
keeps track of which messages are
.i new
(have been sent since you last read your mail) and
.i read
(have been read by you).  New messages have an
.b N
next to them in the header listing and old, but unread messages have
a
.b U
next to them.
.i Mail
keeps track of new/old and read/unread messages by putting a
header field called
.q Status
into your messages.
.pp
To look at a specific message, use the
.b type
command, which may be abbreviated to simply
.b t .
For example, if you had the following messages:
.(l
N 1 root     Wed Sep 21 09:21  "Tuition fees"
N 2 sam      Tue Sep 20 22:55
.)l
you could examine the first message by giving the command:
.(l
type 1
.)l
which might cause
.i Mail
to respond with, for example:
.(l
Message  1:
From root  Wed Sep 21 09:21:45 1978
Subject: Tuition fees
Status: R

Tuition fees are due next Wednesday.  Don't forget!!

.)l
Many
.i Mail
commands that operate on messages take a message number as an
argument like the
.b type
command.  For these commands, there is a notion of a current
message.  When you enter the
.i Mail
program, the current message is initially the first one.  Thus,
you can often omit the message number and use, for example,
.(l
t
.)l
to type the current message.  As a further shorthand, you can type a message
by simply giving its message number.  Hence,
.(l
1
.)l
would type the first message.
.pp
Frequently, it is useful to read the messages in your mailbox in order,
one after another.  You can read the next message in
.i Mail
by simply typing a newline.  As a special case, you can type a newline
as your first command to
.i Mail
to type the first message.
.pp
If, after typing a message, you wish to immediately send a reply,
you can do so with the
.b reply
command.
.b Reply ,
like
.b type ,
takes a message number as an argument.
.i Mail
then begins a message addressed to the user who sent you the message.
You may then type in your letter in reply, followed by a <control-d>
at the beginning of a line, as before.
.i Mail
will type EOT, then type the ampersand prompt to indicate its readiness
to accept another command.  In our example, if, after typing the
first message, you wished to reply to it, you might give the command:
.(l
reply
.)l
.i Mail
responds by typing:
.(l
To: root
Subject: Re: Tuition fees
.)l
and waiting for you to enter your letter.
You are now in the message collection mode described at the beginning
of this section and
.i Mail
will gather up your message up to a control\-d.
Note that it copies the subject
header from the original message.  This is useful in that correspondence
about a particular matter will tend to retain the same subject heading,
making it easy to recognize.  If there are other header fields in
the message, the information found will also be used.
For example, if the letter had a
.q "To:"
header listing several recipients,
.i Mail
would arrange to send your replay to the same people as well.
Similarly, if the original message contained a
.q "Cc:"
(carbon copies to) field,
.i Mail
would send your reply to
.i those
users, too.
.i Mail
is careful, though, not too send the message to
.i you ,
even if you appear in the
.q "To:"
or
.q "Cc:"
field, unless you ask to be included explicitly.  See section 4 for more
details.
.pp
After typing in your letter, the dialog with
.i Mail
might look like the following:
.(l
reply
To: root
Subject: Tuition fees

Thanks for the reminder
EOT
&
.)l
.pp
The
.b reply
command is especially useful for sustaining extended conversations
over the message system, with other
.q listening
users receiving copies of the conversation.  The
.b reply
command can be abbreviated to
.b r .
.pp
Sometimes you will receive a message that has been sent to
several people and wish to reply
.i only
to the person who sent it.
.b Reply
with a capital
.b R
replies to a message, but sends a copy to the sender only.
.pp
If you wish, while reading your mail, to send a message to someone,
but not as a reply to one of your messages, you can send the message
directly with the
.b mail
command, which takes as arguments the names of the recipients you wish
to send to.  For example, to send a message to
.q frank,
you would do:
.(l
mail frank
This is to confirm our meeting next Friday at 4.
EOT
&
.)l
The
.b mail
command can be abbreviated to
.b m .
.pp
Normally, each message you receive is saved in the file
.i mbox
in your login directory at the time you leave
.i Mail .
Often,
however, you will not want to save a particular message you
have received because it is only of passing interest.  To avoid
saving a message in
.i mbox
you can delete it using the
.b delete
command.  In our example,
.(l
delete 1
.)l
will prevent
.i Mail
from saving message 1 (from root) in
.i mbox .
In addition to not saving deleted messages,
.i Mail
will not let
you type them, either.  The effect is to make the message disappear
altogether, along with its number.  The
.b delete
command can be abbreviated to simply
.b d .
.pp
Many features of
.i Mail
can be tailored to your liking with the
.b set
command.  The
.b set
command has two forms, depending on whether you are setting
a
.i binary
option or a
.i valued
option.
Binary options are either on or off.  For example, the
.q ask
option informs
.i Mail
that each time you send a message, you want it to prompt you for
a subject header, to be included in the message.
To set the
.q ask
option, you would type
.(l
set ask
.)l
.pp
Another useful
.i Mail
option is
.q hold.
Unless told otherwise,
.i Mail
moves the messages from your system mailbox to the file
.i mbox
in your home directory when you leave
.i Mail .
If you want
.i Mail
to keep your letters in the system mailbox instead, you can set the
.q hold
option.
.pp
Valued options are values which
.i Mail
uses to adapt to your tastes.  For example, the
.q SHELL
option tells
.i Mail
which shell you like to use, and is specified by
.(l
set SHELL=/bin/csh
.)l
for example.  Note that no spaces are allowed in
.q "SHELL=/bin/csh."
A complete list of the
.i Mail
options appears in section 5.
.pp
Another important valued option is
.q crt.
If you use a fast video terminal, you will find that when you
print long messages, they fly by too quickly for you to read them.
With the
.q crt
option, you can make
.i Mail
print any message larger than a given number of lines by sending
it through a paging program. This program is specified by the
valued option \fBPAGER\fP.
If \fBPAGER\fP is not set, a default paginator is used.
For example, most CRT users with 24-line screens should do:
.(l
set crt=24
.)l
to paginate messages that will not fit on their screens.
In the default state, \fImore\fP (default paginator) prints a screenful of
information, then types --More--.  Type a space to see the next screenful.
.pp
Another adaptation to user needs that
.i Mail
provides is that of
.i aliases .
An alias is simply a name which stands for one or more
real user names.
.i Mail
sent to an alias is really sent to the list of real users
associated with it.  For example, an alias can be defined for the
members of a project, so that you can send mail to the whole project
by sending mail to just a single name.  The
.b alias
command in
.i Mail
defines an alias.  Suppose that the users in a project are
named Sam, Sally, Steve, and Susan.  To define an alias called
.q project
for them, you would use the
.i Mail
command:
.(l
alias project sam sally steve susan
.)l
The
.b alias
command can also be used to provide a convenient name for someone
whose user name is inconvenient.  For example, if a user named
.q "Bob Anderson"
had the login name
.q anderson,"
you might want to use:
.(l
alias bob anderson
.)l
so that you could send mail to the shorter name,
.q bob.
.pp
While the
.b alias
and
.b set
commands allow you to customize
.i Mail ,
they have the drawback that they must be retyped each time you enter
.i Mail .
To make them more convenient to use,
.i Mail
always looks for two files when it is invoked.  It first reads
a system wide file
.q /etc/mail.rc,
then a user specific file,
.q .mailrc,
which is found in the user's home directory.
The system wide file
is maintained by the system administrator and
contains
.b set
commands that are applicable to all users of the system.
The
.q .mailrc
file is usually used by each user to set options the way he likes
and define individual aliases.
For example, my .mailrc file looks like this:
.(l
set ask nosave SHELL=/bin/csh
.)l
As you can see, it is possible to set many options in the
same
.b set
command.  The
.q nosave
option is described in section 5.
.pp
Mail aliasing is implemented
at the system-wide level
by the mail delivery
system
.i sendmail .
These aliases are stored in the file /usr/lib/aliases and are
accessible to all users of the system.
The lines in /usr/lib/aliases are of
the form:
.(l
alias: name\*<1\*>, name\*<2\*>, name\*<3\*>
.)l
where
.i alias
is the mailing list name and the
.i name\*<i\*>
are the members of the list.  Long lists can be continued onto the next
line by starting the next line with a space or tab.  Remember that you
must execute the shell command
.i newaliases
after editing /usr/lib/aliases since the delivery system
uses an indexed file created by
.i newaliases .
.pp
We have seen that
.i Mail
can be invoked with command line arguments which are people
to send the message to, or with no arguments to read mail.
Specifying the
.rb \-f
flag on the command line causes
.i Mail
to read messages from a file other than your system mailbox.
For example, if you have a collection of messages in
the file
.q letters
you can use
.i Mail
to read them with:
.(l
% Mail \-f letters
.)l
You can use all
the
.i Mail
commands described in this document to examine, modify, or delete
messages from your
.q letters
file, which will be rewritten when you leave
.i Mail
with the
.b quit
command described below.
.pp
Since mail that you read is saved in the file
.i mbox
in your home directory by default, you can read
.i mbox
in your home directory by using simply
.(l
% Mail \-f
.)l
.pp
Normally, messages that you examine using the
.b type
command are saved in the file
.q mbox
in your home directory if you leave
.i Mail
with the
.b quit
command described below.
If you wish to retain a message in your system mailbox
you can use the
.b preserve
command to tell
.i Mail
to leave it there.
The
.b preserve
command accepts a list of message numbers, just like
.b type
and may be abbreviated to
.b pre .
.pp
Messages in your system mailbox that you do not examine are
normally retained in your system mailbox automatically.
If you wish to have such a message saved in
.i mbox
without reading it, you may use the
.b mbox
command to have them so saved.  For example,
.(l
mbox 2
.)l
in our example would cause the second message (from sam)
to be saved in
.i mbox
when the
.b quit
command is executed.
.b Mbox
is also the way to direct messages to your
.i mbox
file if you have set the
.q hold
option described above.
.b Mbox
can be abbreviated to
.b mb .
.pp
When you have perused all the messages of interest, you can leave
.i Mail
with the
.b quit
command, which saves the messages you have typed but not
deleted in the file
.i mbox
in your login directory.  Deleted messages are discarded irretrievably,
and messages left untouched are preserved in your system mailbox so
that you will see them the next time you type:
.(l
% Mail
.)l
The
.b quit
command can be abbreviated to simply
.b q .
.pp
If you wish for some reason to leave
.i Mail
quickly without altering either your system mailbox or
.i mbox ,
you can type the
.b x
command (short for
.b exit ),
which will immediately return you to the Shell without changing anything.
.pp
If, instead, you want to execute a Shell command without leaving
.i Mail ,
you
can type the command preceded by an exclamation point, just as in the
text editor.  Thus, for instance:
.(l
!date
.)l
will print the current date without leaving
.i Mail .
.pp
Finally, the
.b help
command is available to print out a brief summary of the
.i Mail
commands, using only the single character command abbreviations.
