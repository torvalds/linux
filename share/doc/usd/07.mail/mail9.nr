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
.\"	@(#)mail9.nr	8.1 (Berkeley) 6/8/93
.\"
.bp
.sh 1 "Summary of commands, options, and escapes"
.pp
This section gives a quick summary of the
.i Mail
commands, binary and valued options, and tilde escapes.
.pp
The following table describes the commands:
.TS
center ;
c ci
lb l.
Command	Description
_
+	Same as \fBnext\fP
-	Back up to previous message
?	Print brief summary of \fIMail\fP commands
!	Single command escape to shell
Print	Type message with ignored fields
Reply	Reply to author of message only
Respond	Same as \fBReply\fP
Type	Type message with ignored fields
alias	Define an alias as a set of user names
alternates	List other names you are known by
chdir	Change working directory, home by default
copy	Copy a message to a file or folder
delete	Delete a list of messages
dp	Same as \fBdt\fP
dt	Delete current message, type next message
edit	Edit a list of messages
else	Start of else part of conditional; see \fBif\fP
endif	End of conditional statement; see \fBif\fP
exit	Leave mail without changing anything
file	Interrogate/change current mail file
folder	Same as \fBfile\fP
folders	List the folders in your folder directory
from	List headers of a list of messages
headers	List current window of messages
help	Same as \fB?\fP
hold	Same as \fBpreserve\fP
if	Conditional execution of \fIMail\fP commands
ignore	Set/examine list of ignored header fields
list	List valid \fIMail\fP commands
local	List other names for the local host
mail	Send mail to specified names
mbox	Arrange to save a list of messages in \fImbox\fP
next	Go to next message and type it
preserve	Arrange to leave list of messages in system mailbox
print	Print messages
quit	Leave \fIMail\fP; update system mailbox, \fImbox\fP as appropriate
reply	Compose a reply to a message
respond	Same as \fBreply\fP
retain	Supersedes \fBignore\fP
save	Append messages, headers included, on a file
set	Set binary or valued options
shell	Invoke an interactive shell
size	Prints out size of message list
source	Read \fImail\fP commands from a file
top	Print first so many (5 by default) lines of list of messages
type	Same as \fBprint\fP
undelete	Undelete list of messages
unread	Marks list of messages as not been read
unset	Undo the operation of a \fBset\fP
visual	Invoke visual editor on a list of messages
write	Append messages to a file, don't include headers
xit	Same as \fBexit\fP
z	Scroll to next/previous screenful of headers
.TE
.bp
.(b
.pp
The following table describes the options.  Each option is
shown as being either a binary or valued option.
.TS
center;
c ci ci
l ci l.
Option	Type	Description
_
EDITOR	valued	Pathname of editor for ~e and \fBedit\fP
PAGER	valued	Pathname of paginator for \fBPrint\fP, \fBprint\fP, \fBType\fP and \fBtype\fP
SHELL	valued	Pathname of shell for \fBshell\fP, ~! and \fB!\fP
VISUAL	valued	Pathname of screen editor for ~v, \fBvisual\fP
append	binary	Always append messages to end of \fImbox\fP
ask	binary	Prompt user for Subject: field when sending
askcc	binary	Prompt user for additional Cc's at end of message
autoprint	binary	Print next message after \fBdelete\fP
crt	valued	Minimum number of lines before using \fBPAGER\fP
debug	binary	Print out debugging information
dot	binary	Accept . alone on line to terminate message input
escape	valued	Escape character to be used instead of\ \ ~
folder	valued	Directory to store folders in
hold	binary	Hold messages in system mailbox by default
ignore	binary	Ignore \s-2RUBOUT\s0 while sending mail
ignoreeof	binary	Don't terminate letters/command input with \fB\(uaD\fP
keep	binary	Don't unlink system mailbox when empty
keepsave	binary	Don't delete \fBsave\fPd messages by default
metoo	binary	Include sending user in aliases
noheader	binary	Suppress initial printing of version and headers
nosave	binary	Don't save partial letter in \fIdead.letter\fP
quiet	binary	Suppress printing of \fIMail\fP version and message numbers
record	valued	File to save all outgoing mail in
screen	valued	Size of window of message headers for \fBz\fP, etc.
sendmail	valued	Choose alternate mail delivery system
toplines	valued	Number of lines to print in \fBtop\fP
verbose	binary	Invoke sendmail with the \fB\-v\fP flag
.TE
.)b
.(b
.pp
The following table summarizes the tilde escapes available
while sending mail.
.TS
center;
c ci ci
l li l.
Escape	Arguments	Description
_
~!	command	Execute shell command
~b	name ...	Add names to "blind" Cc: list
~c	name ...	Add names to Cc: field
~d		Read \fIdead.letter\fP into message
~e		Invoke text editor on partial message
~f	messages	Read named messages
~h		Edit the header fields
~m	messages	Read named messages, right shift by tab
~p		Print message entered so far
~q		Abort entry of letter; like \s-2RUBOUT\s0
~r	filename	Read file into message
~s	string	Set Subject: field to \fIstring\fP
~t	name ...	Add names to To: field
~v		Invoke screen editor on message
~w	filename	Write message on file
~|	command	Pipe message through \fIcommand\fP
~:	Mail command	Execute a \fIMail\fP command
~~	string	Quote a ~ in front of \fIstring\fP
.TE
.)b
.(b
.pp
The following table shows the command line flags that
.i Mail
accepts:
.TS
center;
c c
l a.
Flag	Description
_
\-N	Suppress the initial printing of headers
\-T \fIfile\fP	Article-id's of read/deleted messages to \fIfile\fP
\-d	Turn on debugging
\-f \fIfile\fP	Show messages in \fIfile\fP or \fI~/mbox\fP
\-h \fInumber\fP	Pass on hop count for mail forwarding
\-i	Ignore tty interrupt signals
\-n	Inhibit reading of /etc/mail.rc
\-r \fIname\fP	Pass on \fIname\fP for mail forwarding
\-s \fIstring\fP	Use \fIstring\fP as subject in outgoing mail
\-u \fIname\fP	Read \fIname's\fP mail instead of your own
\-v 	Invoke sendmail with the \fB\-v\fP flag
.TE
.)b
.lp
Notes:
.b \-T ,
.b \-d ,
.b \-h ,
and
.b \-r
are not for human use.
