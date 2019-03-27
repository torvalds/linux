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
.\"	@(#)mail5.nr	8.1 (Berkeley) 6/8/93
.\" $FreeBSD$
.\"
.bp
.sh 1 "Additional features"
.pp
This section describes some additional commands useful for
reading your mail, setting options, and handling lists of messages.
.sh 2 "Message lists"
.pp
Several
.i Mail
commands accept a list of messages as an argument.
Along with
.b type
and
.b delete ,
described in section 2,
there is the
.b from
command, which prints the message headers associated with the
message list passed to it.
The
.b from
command is particularly useful in conjunction with some of the
message list features described below.
.pp
A
.i "message list"
consists of a list of message numbers, ranges, and names,
separated by spaces or tabs.  Message numbers may be either
decimal numbers, which directly specify messages, or one of the
special characters
.q \(ua
.q "."
or
.q "$"
to specify the first relevant, current, or last
relevant message, respectively.
.i Relevant
here means, for most commands
.q "not deleted"
and
.q "deleted"
for the
.b undelete
command.
.pp
A range of messages consists of two message numbers (of the form
described in the previous paragraph) separated by a dash.
Thus, to print the first four messages, use
.(l
type 1\-4
.)l
and to print all the messages from the current message to the last
message, use
.(l
type .\-$
.)l
.pp
A
.i name
is a user name.  The user names given in the message list are
collected together and each message selected by other means
is checked to make sure it was sent by one of the named users.
If the message consists entirely of user names, then every
message sent by one of those users that is
.i relevant
(in the sense described earlier)
is selected.  Thus, to print every message sent to you by
.q root,
do
.(l
type root
.)l
.pp
As a shorthand notation, you can specify simply
.q *
to get every
.i relevant
(same sense)
message.  Thus,
.(l
type *
.)l
prints all undeleted messages,
.(l
delete *
.)l
deletes all undeleted messages, and
.(l
undelete *
.)l
undeletes all deleted messages.
.pp
You can search for the presence of a word in subject lines with
.b / .
For example, to print the headers of all messages that contain the
word
.q PASCAL,
do:
.(l
from /pascal
.)l
Note that subject searching ignores upper/lower case differences.
.sh 2 "List of commands"
.pp
This section describes all the
.i Mail
commands available when
receiving mail.
.ip "\fB\-\fP\ \ "
The
.rb \-
command goes to the previous message and prints it.  The
.rb \-
command may be given a decimal number
.i n
as an argument, in which case the
.i n th
previous message is gone to and printed.
.ip "\fB?\fP\ \ "
Prints a brief summary of commands.
.ip "\fB!\fP\ \ "
Used to preface a command to be executed by the shell.
.ip "\fBPrint\fP\ \ "
Like
.b print ,
but also print out ignored header fields.  See also
\fBprint\fP, \fBignore\fP and \fBretain\fP.
\fBPrint\fP can be abbreviated to \fBP\fP.
.ip "\fBReply\fP or \fBRespond\fP\ \ "
Note the capital \fBR\fP in the name.
Frame a reply to a one or more messages.
The reply (or replies if you are using this on multiple messages)
will be sent ONLY to the person who sent you the message
(respectively, the set of people who sent the messages you are
replying to).
You can
add people using the \fB~t\fP, \fB~c\fP and \fB~b\fP
tilde escapes.  The subject in your reply is formed by prefacing the
subject in the original message with
.q "Re:"
unless it already began thus.
If the original message included a
.q "reply-to"
header field, the reply will go
.i only
to the recipient named by
.q "reply-to."
You type in your message using the same conventions available to you
through the
.b mail
command.
The
.b Reply
command is especially useful for replying to messages that were sent
to enormous distribution groups when you really just want to
send a message to the originator.  Use it often.
\fBReply\fP (and \fBRespond\fP) can be abbreviated to \fBR\fP.
.ip "\fBType\fP\ \ "
Identical to the
.b Print
command.
\fBType\fP can be abbreviated to \fBT\fP.
.ip "\fBalias\fP\ \ "
Define a name to stand for a set of other names.
This is used when you want to send messages to a certain
group of people and want to avoid retyping their names.
For example
.(l
alias project john sue willie kathryn
.)l
creates an alias
.i project
which expands to the four people John, Sue, Willie, and Kathryn.
If no arguments are given, all currently-defined aliases are printed.
If one argument is given, that alias is printed (if it exists).
\fBAlias\fP can be abbreviated to \fBa\fP.
.ip "\fBalternates\fP\ \ "
If you have accounts on several machines, you may find it convenient
to use the /usr/lib/aliases on all the machines except one to direct
your mail to a single account.
The
.b alternates
command is used to inform
.i Mail
that each of these other addresses is really
.i you .
.i Alternates
takes a list of user names and remembers that they are all actually you.
When you
.b reply
to messages that were sent to one of these alternate names,
.i Mail
will not bother to send a copy of the message to this other address (which
would simply be directed back to you by the alias mechanism).
If
.i alternates
is given no argument, it lists the current set of alternate names.
.b Alternates
is usually used in the .mailrc file.
\fBAlternates\fP can be abbreviated to \fBalt\fP.
.ip "\fBchdir\fP\ \ "
The
.b chdir
command allows you to change your current directory.
.b Chdir
takes a single argument, which is taken to be the pathname of
the directory to change to.  If no argument is given,
.b chdir
changes to your home directory.
\fBChdir\fP can be abbreviated to \fBc\fP.
.ip "\fBcopy\fP\ \ "
The
.b copy
command does the same thing that
.b save
does, except that it does not mark the messages it is used on
for deletion when you quit.
\fBCopy\fP can be abbreviated to \fBco\fP.
.ip "\fBdelete\fP\ \ "
Deletes a list of messages.  Deleted messages can be reclaimed
with the
.b undelete
command.
\fBDelete\fP can be abbreviated to \fBd\fP.
.ip "\fBdp\fP or \fBdt\fP\ \ "
These
commands delete the current message and print the next message.
They are useful for quickly reading and disposing of mail.
If there is no next message, \fImail\fP says ``at EOF.''
.ip "\fBedit\fP\ \ "
To edit individual messages using the text editor, the
.b edit
command is provided.  The
.b edit
command takes a list of messages as described under the
.b type
command and processes each by writing it into the file
Message\c
.i x
where
.i x
is the message number being edited and executing the text editor on it.
When you have edited the message to your satisfaction, write the message
out and quit, upon which
.i Mail
will read the message back and remove the file.
.b Edit
can be abbreviated to
.b e .
.ip "\fBelse\fP\ \ "
Marks the end of the then-part of an
.b if
statement and the beginning of the
part to take effect if the condition of the
.b if
statement is false.
.ip "\fBendif\fP\ \ "
Marks the end of an
.b if
statement.
.ip "\fBexit\fP or \fBxit\fP\ \ "
Leave
.i Mail
without updating the system mailbox or the file your were reading.
Thus, if you accidentally delete several messages, you can use
.b exit
to avoid scrambling your mailbox.
\fBExit\fP can be abbreviated to \fBex\fP or \fBx\fP.
.ip "\fBfile\fP\ \ "
The same as
.b folder .
\fBFile\fP can be abbreviated to \fBfi\fP.
.ip "\fBfolders\fP\ \ "
List the names of the folders in your folder directory.
.ip "\fBfolder\fP\ \ "
The
.b folder
command switches to a new mail file or folder.  With no arguments, it
tells you which file you are currently reading.  If you give
it an argument, it will write out changes (such as deletions)
you have made in the current file and read the new file.
Some special conventions are recognized for the name:
.(b
.TS
center;
c c
l a.
Name	Meaning
_
#	Previous file read
%	Your system mailbox
%name	\fIName\fP's system mailbox
&	Your ~/mbox file
+folder	A file in your folder directory
.TE
.)b
\fBFolder\fP can be abbreviated to \fBfo\fP.
.ip "\fBfrom\fP\ \ "
The
.b from
command takes a list of messages and prints out the header lines for each one;
hence
.(l
from joe
.)l
is the easy way to display all the message headers from \*(lqjoe.\*(rq
\fBFrom\fP can be abbreviated to \fBf\fP.
.ip "\fBheaders\fP\ \ "
When you start up
.i Mail
to read your mail, it lists the message headers that you have.
These headers tell you who each message is from, when they were
received, how many lines and characters each message is, and the
.q "Subject:"
header field of each message, if present.  In addition,
.i Mail
tags the message header of each message that has been the object
of the
.b preserve
command with a
.q P.
Messages that have been
.b saved
or
.b written
are flagged with a
.q *.
Finally,
.b deleted
messages are not printed at all.  If you wish to reprint the current
list of message headers, you can do so with the
.b headers
command.  The
.b headers
command (and thus the initial header listing)
only lists the first so many message headers.
The number of headers listed depends on the speed of your
terminal.
This can be overridden by specifying the number of headers you
want with the
.i window
option.
.i Mail
maintains a notion of the current
.q window
into your messages for the purposes of printing headers.
Use the
.b z
command to move forward and back a window.
You can move
.i Mail's
notion of the current window directly to a particular message by
using, for example,
.(l
headers 40
.)l
to move
.i Mail's
attention to the messages around message 40.
If a ``+'' argument is given, then the next screenful of message headers is
printed, and if a ``\-'' argument is given, the previous screenful of message
headers is printed.
\fBHeaders\fP can be abbreviated to \fBh\fP.
.ip "\fBhelp\fP\ \ "
Print a brief and usually out of date help message about the commands
in
.i Mail .
The 
.i man
page for 
.i mail
is usually more up-to-date than either the help message or this manual.
It is also a synonym for \fB?\fP.
.ip "\fBhold\fP\ \ "
Arrange to hold a list of messages in the system mailbox, instead
of moving them to the file
.i mbox
in your home directory.  If you set the binary option
.i hold ,
this will happen by default.
It does not override the \fBdelete\fP command.
\fBHold\fP can be abbreviated to \fBho\fP.
.ip "\fBif\fP\ \ "
Commands in your
.q .mailrc
file can be executed conditionally depending on whether you are
sending or receiving mail with the
.b if
command.  For example, you can do:
.(l
if receive
	\fIcommands\fP...
endif
.)l
An
.b else
form is also available:
.(l
if send
	\fIcommands\fP...
else
	\fIcommands\fP...
endif
.)l
Note that the only allowed conditions are
.b receive
and
.b send .
.ip "\fBignore\fP \ \ "
.b N.B.:
.i Ignore
has been superseded by
.i retain.
.br
Add the list of header fields named to the
.i "ignore list" .
Header fields in the ignore list are not printed on your
terminal when you print a message.  This allows you to suppress
printing of certain machine-generated header fields, such as
.i Via
which are not usually of interest.  The
.b Type
and
.b Print
commands can be used to print a message in its entirety, including
ignored fields.
If
.b ignore
is executed with no arguments, it lists the current set of ignored fields.
.ip "\fBlist\fP\ \ "
List the valid
.i Mail
commands.
\fBList\fP can be abbreviated to \fBl\fP.
.\".ip \fBlocal\fP
.\"Define a list of local names for this host. This command is useful
.\"when the host is known by more than one name. Names in the list
.\"may be qualified be the domain of the host. The first name on the local
.\"list is the
.\".i distinguished
.\"name of the host.
.\"The names on the local list are used by
.\".i Mail
.\"to decide which addresses are local to the host.
.\"For example:
.\".(l
.\"local ucbarpa.BERKELEY.ARPA arpa.BERKELEY.ARPA \\
.\"	arpavax.BERKELEY.ARPA r.BERKELEY.ARPA \\
.\"	ucb-arpa.ARPA
.\".)l
.\"From this list we see that
.\".i "fred@ucbarpa.BERKELEY.ARPA",
.\".i "harold@arpa.BERKELEY",
.\"and
.\".i "larry@r"
.\"are all addresses of users on the local host.
.\"The
.\".b local
.\"command is usually not used be general users since it is designed for
.\"local configuration; it is usually found in the file /etc/mail.rc.
.ip "\fBmail\fP\ \ "
Send mail to one or more people.  If you have the
.i ask
option set,
.i Mail
will prompt you for a subject to your message.  Then you
can type in your message, using tilde escapes as described in
section 4 to edit, print, or modify your message.  To signal your
satisfaction with the message and send it, type control-d at the
beginning of a line, or a . alone on a line if you set the option
.i dot .
To abort the message, type two interrupt characters (\s-2RUBOUT\s0
by default) in a row or use the
.b ~q
escape.
The \fBmail\fP command can be abbreviated to \fBm\fP.
.ip "\fBmbox\fP\ \ "
Indicate that a list of messages be sent to
.i mbox
in your home directory when you quit.  This is the default
action for messages if you do
.i not
have the
.i hold
option set.
.ip "\fBnext\fP or \fB+\fP\ \ "
The
.b next
command goes to the next message and types it.  If given a message list,
.b next
goes to the first such message and types it.  Thus,
.(l
next root
.)l
goes to the next message sent by
.q root
and types it.  The
.b next
command can be abbreviated to simply a newline, which means that one
can go to and type a message by simply giving its message number or
one of the magic characters
.q "^"
.q "."
or
.q "$".
Thus,
.(l
\&.
.)l
prints the current message and
.(l
4
.)l
prints message 4, as described previously.
\fBNext\fP can be abbreviated to \fBn\fP.
.ip "\fBpreserve\fP\ \ "
Same as
.b hold .
Cause a list of messages to be held in your system mailbox when you quit.
\fBPreserve\fP can be abbreviated to \fBpre\fP.
.ip "\fBprint\fP\ \ "
Print the specified messages. If the
.b crt
variable is set, messages longer than the number of lines it indicates
are paged through the command specified by the \fBPAGER\fP variable.
The \fBprint\fP command can be abbreviated to \fBp\fP.
.ip "\fBquit\fP\ \ "
Terminates the session, saving all undeleted, unsaved and unwritten messages 
in the user's \fImbox\fP file in their login directory
(messages marked as having been read), preserving all
messages marked with \fBhold\fP or \fBpreserve\fP or never referenced
in their system mailbox.
Any messages that were deleted, saved, written or saved to \fImbox\fP are
removed from their system mailbox.
If new mail has arrived during the session, the message
``You have new mail'' is given.  If given while editing a mailbox file
with the \fB\-f\fP flag, then the edit file is rewritten.
A return to the Shell is effected, unless the rewrite of edit file fails,
in which case the user can escape with the \fBexit\fP command.
\fBQuit\fP can be abbreviated to \fBq\fP.
.ip "\fBreply\fP or \fBrespond\fP\ \ "
Frame a reply to a single message.
The reply will be sent to the
person who sent you the message (to which you are replying), plus all
the people who received the original message, except you.  You can
add people using the \fB~t\fP, \fB~c\fP and \fB~b\fP
tilde escapes.  The subject in your reply is formed by prefacing the
subject in the original message with
.q "Re:"
unless it already began thus.
If the original message included a
.q "reply-to"
header field, the reply will go
.i only
to the recipient named by
.q "reply-to."
You type in your message using the same conventions available to you
through the
.b mail
command.
The \fBreply\fP (and \fBrespond\fP) command can be abbreviated to \fBr\fP.
.ip "\fBretain\fP\ \ "
Add the list of header fields named to the \fIretained list\fP.
Only the header fields in the retain list
are shown on your terminal when you print a message.
All other header fields are suppressed.
The
.b Type
and
.b Print
commands can be used to print a message in its entirety.
If
.b retain
is executed with no arguments, it lists the current set of
retained fields.
.ip "\fBsave\fP\ \ "
It is often useful to be able to save messages on related topics
in a file.  The
.b save
command gives you the ability to do this.  The
.b save
command takes as an argument a list of message numbers, followed by
the name of the file in which to save the messages.  The messages
are appended to the named file, thus allowing one to keep several
messages in the file, stored in the order they were put there.
The filename in quotes, followed by the line
count and character count is echoed on the user's terminal.
An example of the
.b save
command relative to our running example is:
.(l
s 1 2 tuitionmail
.)l
.b Saved
messages are not automatically saved in
.i mbox
at quit time, nor are they selected by the
.b next
command described above, unless explicitly specified.
\fBSave\fP can be abbreviated to \fBs\fP.
.ip "\fBset\fP\ \ "
Set an option or give an option a value.  Used to customize
.i Mail .
Section 5.3 contains a list of the options.  Options can be
.i binary ,
in which case they are
.i on
or
.i off ,
or
.i valued .
To set a binary option
.i option
.i on ,
do
.(l
set option
.)l
To give the valued option
.i option
the value
.i value ,
do
.(l
set option=value
.)l
There must be no space before or after the ``='' sign.
If no arguments are given, all variable values are printed.
Several options can be specified in a single
.b set
command.
\fBSet\fP can be abbreviated to \fBse\fP.
.ip "\fBshell\fP\ \ "
The
.b shell
command allows you to
escape to the shell.
.b Shell
invokes an interactive shell and allows you to type commands to it.
When you leave the shell, you will return to
.i Mail .
The shell used is a default assumed by
.i Mail ;
you can override this default by setting the valued option
.q SHELL,
eg:
.(l
set SHELL=/bin/csh
.)l
\fBShell\fP can be abbreviated to \fBsh\fP.
.ip "\fBsize\fP\ \ "
Takes a message list and prints out the size in characters of each
message.
.ip "\fBsource\fP\ \ "
The
.b source
command reads
.i mail
commands from a file.  It is useful when you are trying to fix your
.q .mailrc
file and you need to re-read it.
\fBSource\fP can be abbreviated to \fBso\fP.
.ip "\fBtop\fP\ \ "
The
.b top
command takes a message list and prints the first five lines
of each addressed message.
If you wish, you can change the number of lines that
.b top
prints out by setting the valued option
.q "toplines."
On a CRT terminal,
.(l
set toplines=10
.)l
might be preferred.
\fBTop\fP can be abbreviated to \fBto\fP.
.ip "\fBtype\fP\ \ "
Same as \fBprint\fP.
Takes a message list and types out each message on the terminal.
The \fBtype\fP command can be abbreviated to \fBt\fP.
.ip "\fBundelete\fP \ \"
Takes a message list and marks each message as \fInot\fP
being deleted.
\fBUndelete\fP can be abbreviated to \fBu\fP.
.ip "\fBunread\fP\ \ "
Takes a message list and marks each message as
.i not
having been read.
\fBUnread\fP can be abbreviated to \fBU\fP.
.ip "\fBunset\fP\ \ "
Takes a list of option names and discards their remembered values;
the inverse of \fBset\fP .
.ip "\fBvisual\fP\ \ "
It is often useful to be able to invoke one of two editors,
based on the type of terminal one is using.  To invoke
a display oriented editor, you can use the
.b visual
command.  The operation of the
.b visual
command is otherwise identical to that of the
.b edit
command.
.ne 2v+\n(psu
.sp \n(psu
Both the
.b edit
and
.b visual
commands assume some default text editors.  These default editors
can be overridden by the valued options
.q EDITOR
and
.q VISUAL
for the standard and screen editors.  You might want to do:
.(l
set EDITOR=/usr/bin/ex VISUAL=/usr/bin/vi
.)l
\fBVisual\fP can be abbreviated to \fBv\fP.
.ip "\fBwrite\fP\ \ "
The
.b save
command always writes the entire message, including the headers,
into the file.  If you want to write just the message itself, you
can use the
.b write
command.  The
.b write
command has the same syntax as the
.b save
command, and can be abbreviated to simply
.b w .
Thus, we could write the second message by doing:
.(l
w 2 file.c
.)l
As suggested by this example, the
.b write
command is useful for such tasks as sending and receiving
source program text over the message system.
The filename in quotes, followed by the line
count and character count is echoed on the user's terminal.
.ip "\fBz\fP\ \ "
.i Mail
presents message headers in windowfuls as described under
the
.b headers
command.
You can move
.i Mail's
attention forward to the next window by giving the
.(l
z+
.)l
command.  Analogously, you can move to the previous window with:
.(l
z\-
.)l
.sh 2 "Custom options"
.pp
Throughout this manual, we have seen examples of binary and valued options.
This section describes each of the options in alphabetical order, including
some that you have not seen yet.
To avoid confusion, please note that the options are either
all lower case letters or all upper case letters.  When I start a sentence
such as:
.q "Ask"
causes
.i Mail
to prompt you for a subject header,
I am only capitalizing
.q ask
as a courtesy to English.
.ip "\fBEDITOR\fP\ \ "
The valued option
.q EDITOR
defines the pathname of the text editor to be used in the
.b edit
command and ~e.  If not defined, a standard editor is used.
.ip "\fBPAGER\fP\ \ "
Pathname of the program to use for paginating output when
it exceeds \fIcrt\fP lines.
A default paginator is used if this option is not defined.
.ip "\fBSHELL\fP\ \ "
The valued option
.q SHELL
gives the path name of your shell.  This shell is used for the
.b !
command and ~! escape.  In addition, this shell expands
file names with shell metacharacters like * and ? in them.
.ip "\fBVISUAL\fP\ \ "
The valued option
.q VISUAL
defines the pathname of the screen editor to be used in the
.b visual
command
and ~v escape.  A standard screen editor is used if you do not define one.
.ip "\fBappend\fP\ \ "
The
.q append
option is binary and
causes messages saved in
.i mbox
to be appended to the end rather than prepended.
Normally, \fIMail\fP will put messages in \fImbox\fP
in the same order that the system puts messages in your system mailbox.
By setting
.q append,
you are requesting that
.i mbox
be appended to regardless.  It is in any event quicker to append.
.ip "\fBask\fP\ \ "
.q "Ask"
is a binary option which
causes
.i Mail
to prompt you for the subject of each message you send.
If you respond with simply a newline, no subject field will be sent.
.ip "\fBaskcc\fP\ \ "
.q Askcc
is a binary option which
causes you to be prompted for additional carbon copy recipients at the
end of each message.  Responding with a newline shows your
satisfaction with the current list.
.ip "\fBautoprint\fP\ \ "
.q Autoprint
is a binary option which
causes the
.b delete
command to behave like
.b dp
\*- thus, after deleting a message, the next one will be typed
automatically.  This is useful when quickly scanning and deleting
messages in your mailbox.
.ip "\fBcrt\fP \ \ "
The valued option
.q crt
is used as a threshold to determine how long a message must
be before
.b PAGER
is used to read it.
.ip "\fBdebug\fP \ \ "
The binary option
.q debug
causes debugging information to be displayed. Use of this
option is the same as using the \fB\-d\fP command line flag.
.ip "\fBdot\fP\ \ "
.q Dot
is a binary option which, if set, causes
.i Mail
to interpret a period alone on a line as the terminator
of the message you are sending.
.ip "\fBescape\fP\ \ "
To allow you to change the escape character used when sending
mail, you can set the valued option
.q escape.
Only the first character of the
.q escape
option is used, and it must be doubled if it is to appear as
the first character of a line of your message.  If you change your escape
character, then ~ loses all its special meaning, and need no longer be doubled
at the beginning of a line.
.ip "\fBfolder\fP\ \ "
The name of the directory to use for storing folders of messages.
If this name begins with a `/'
.i Mail
considers it to be an absolute pathname; otherwise, the folder directory
is found relative to your home directory.
.ip "\fBhold\fP\ \ "
The binary option
.q hold
causes messages that have been read but not manually dealt with
to be held in the system mailbox. This prevents such messages from
being automatically swept into your \fImbox\fP file.
.ip "\fBignore\fP\ \ "
The binary option
.q ignore
causes \s-2RUBOUT\s0 characters from your terminal to be ignored and echoed
as @'s while you are sending mail.  \s-2RUBOUT\s0 characters retain their
original meaning in
.i Mail
command mode.
Setting the
.q ignore
option is equivalent to supplying the
.b \-i
flag on the command line as described in section 6.
.ip "\fBignoreeof\fP\ \ "
An option related to
.q dot
is
.q ignoreeof
which makes
.i Mail
refuse to accept a control\-d as the end of a message.
.q Ignoreeof
also applies to
.i Mail
command mode.
.ip "\fBkeep\fP\ \ "
The
.q keep
option causes
.i Mail
to truncate your system mailbox instead of deleting it when it
is empty.  This is useful if you elect to protect your mailbox, which
you would do with the shell command:
.(l
chmod 600 /var/mail/yourname
.)l
where
.i yourname
is your login name.  If you do not do this, anyone can probably read
your mail, although people usually don't.
.ip "\fBkeepsave\fP\ \ "
When you
.b save
a message,
.i Mail
usually discards it when you
.b quit .
To retain all saved messages, set the
.q keepsave
option.
.ip "\fBmetoo\fP\ \ "
When sending mail to an alias,
.i Mail
makes sure that if you are included in the alias, that mail will not
be sent to you.  This is useful if a single alias is being used by
all members of the group.  If however, you wish to receive a copy of
all the messages you send to the alias, you can set the binary option
.q metoo.
.ip "\fBnoheader\fP\ \ "
The binary option
.q noheader
suppresses the printing of the version and headers when
.i Mail
is first invoked. Setting this option is the same as using
.b \-N
on the command line.
.ip "\fBnosave\fP\ \ "
Normally,
when you abort a message with two \s-2RUBOUTs\s0,
.i Mail
copies the partial letter to the file
.q dead.letter
in your home directory.  Setting the binary option
.q nosave
prevents this.
.ip "\fBReplyall\fP\ \ "
Reverses the sense of
.i reply
and
.i Reply
commands.
.ip "\fBquiet\fP\ \ "
The binary option
.q quiet
suppresses the printing of the version when
.i Mail
is first invoked,
as well as printing the for example
.q "Message 4:"
from the
.b type
command.
.ip "\fBrecord\fP\ \ "
If you love to keep records, then the
valued option
.q record
can be set to the name of a file to save your outgoing mail.
Each new message you send is appended to the end of the file.
.ip "\fBscreen\fP\ \ "
When
.i Mail
initially prints the message headers, it determines the number to
print by looking at the speed of your terminal.  The faster your
terminal, the more it prints.
The valued option
.q screen
overrides this calculation and
specifies how many message headers you want printed.
This number is also used for scrolling with the
.b z
command.
.ip "\fBsendmail\fP\ \ "
To use an alternate mail delivery system, set the
.q sendmail
option to the full pathname of the program to use.  Note:  this is not
for everyone!  Most people should use the default delivery system.
.ip "\fBtoplines\fP\ \ "
The valued option
.q toplines
defines the number of lines that the
.q top
command will print out instead of the default five lines.
.ip "\fBverbose\fP\ \ "
The binary option "verbose" causes
.i Mail
to invoke sendmail with the 
.b \-v
flag, which causes it to go into verbose mode and announce expansion
of aliases, etc. Setting the "verbose" option is equivalent to
invoking
.i Mail
with the
.b \-v
flag as described in section 6.
