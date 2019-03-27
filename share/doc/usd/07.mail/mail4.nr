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
.\"	@(#)mail4.nr	8.1 (Berkeley) 6/8/93
.\"
.bp
.sh 1 "More about sending mail"
.sh 2 "Tilde escapes"
.pp
While typing in a message to be sent to others, it is often
useful to be able to invoke the text editor on the partial message,
print the message, execute a shell command, or do some other
auxiliary function. 
.i Mail
provides these capabilities through
.i "tilde escapes" ,
which consist of a tilde (~) at the beginning of a line, followed by
a single character which indicates the function to be performed.  For
example, to print the text of the message so far, use:
.(l
~p
.)l
which will print a line of dashes, the recipients of your message, and
the text of the message so far.
Since
.i Mail
requires two consecutive \s-2RUBOUT\s0's to abort a letter, you
can use a single \s-2RUBOUT\s0 to abort the output of ~p or any other
~ escape without killing your letter.
.pp
If you are dissatisfied with the message as
it stands, you can invoke the text editor on it using the escape
.(l
~e
.)l
which causes the message to be copied into a temporary file and an
instance of the editor to be spawned.  After modifying the message to
your satisfaction, write it out and quit the editor. 
.i Mail
will respond
by typing
.(l
(continue)
.)l
after which you may continue typing text which will be appended to your
message, or type <control-d> to end the message.
A standard text editor is provided by
.i Mail .
You can override this default by setting the valued option
.q EDITOR
to something else.  For example, you might prefer:
.(l
set EDITOR=/usr/bin/ex
.)l
.pp
Many systems offer a screen editor as an alternative to the standard
text editor, such as the
.i vi
editor from UC Berkeley.
To use the screen, or
.i visual
editor, on your current message, you can use the escape,
.(l
~v
.)l
~v works like ~e, except that the screen editor is invoked instead.
A default screen editor is defined by
.i Mail .
If it does not suit you, you can set the valued option
.q VISUAL
to the path name of a different editor.
.pp
It is often useful to be able to include the contents of some
file in your message; the escape
.(l
~r filename
.)l
is provided for this purpose, and causes the named file to be appended
to your current message. 
.i Mail
complains if the file doesn't exist
or can't be read.  If the read is successful, the number of lines and
characters appended to your message is printed, after which you may continue
appending text.  The filename may contain shell metacharacters like * and ?
which are expanded according to the conventions of your shell.
.pp
As a special case of ~r, the escape
.(l
~d
.)l
reads in the file
.q dead.letter
in your home directory.  This is often useful since
.i Mail
copies the text
of your message there when you abort a message with \s-2RUBOUT\s0.
.pp
To save the current text of your message on a file you may use the
.(l
~w filename
.)l
escape. 
.i Mail
will print out the number of lines and characters written
to the file, after which you may continue appending text to your message.
Shell metacharacters may be used in the filename, as in ~r and are expanded
with the conventions of your shell.
.pp
If you are sending mail from within
.i Mail's
command mode
you can read a message sent to you into the message
you are constructing with the escape:
.(l
~m 4
.)l
which will read message 4 into the current message, shifted right by
one tab stop.  You can name any non-deleted message, or list of messages.
Messages can also be forwarded without shifting by a tab stop with ~f.
This is the usual way to forward a message.
.pp
If, in the process of composing a message, you decide to add additional
people to the list of message recipients, you can do so with the escape
.(l
~t name1 name2 ...
.)l
You may name as few or many additional recipients as you wish.  Note
that the users originally on the recipient list will still receive
the message; you cannot remove someone from the recipient
list with ~t.
.pp
If you wish, you can associate a subject with your message by using the
escape
.(l
~s Arbitrary string of text
.)l
which replaces any previous subject with
.q "Arbitrary string of text."
The subject, if given, is sent near the
top of the message prefixed with
.q "Subject:"
You can see what the message will look like by using ~p.
.pp
For political reasons, one occasionally prefers to list certain
people as recipients of carbon copies of a message rather than
direct recipients.  The escape
.(l
~c name1 name2 ...
.)l
adds the named people to the
.q "Cc:"
list, similar to ~t.
Again, you can execute ~p to see what the message will look like.
.pp
The escape
.(l
~b name1 name2 ...
.)l
adds the named people to the
.q "Cc:"
list, but does not make the names visible in the
.q "Cc:"
line ("blind" carbon copy).
.pp
The recipients of the message together constitute the
.q "To:"
field, the subject the
.q "Subject:"
field, and the carbon copies the
.q "Cc:"
field.  If you wish to edit these in ways impossible with the ~t, ~s, ~c
and ~b escapes, you can use the escape
.(l
~h
.)l
which prints
.q "To:"
followed by the current list of recipients and leaves the cursor
(or printhead) at the end of the line.  If you type in ordinary
characters, they are appended to the end of the current list of
recipients.  You can also use your erase character to erase back into
the list of recipients, or your kill character to erase them altogether.
Thus, for example, if your erase and kill characters are the standard
(on printing terminals) # and @ symbols,
.(l
~h
To: root kurt####bill
.)l
would change the initial recipients
.q "root kurt"
to
.q "root bill."
When you type a newline,
.i Mail
advances to the
.q "Subject:"
field, where the same rules apply.  Another newline brings you to
the
.q "Cc:"
field, which may be edited in the same fashion.  Another newline
brings you to the
.q "Bcc:"
("blind" carbon copy) field, which follows the same rules as the "Cc:"
field.  Another newline
leaves you appending text to the end of your message.  You can use
~p to print the current text of the header fields and the body
of the message.
.pp
To effect a temporary escape to the shell, the escape
.(l
~!command
.)l
is used, which executes
.i command
and returns you to mailing mode without altering the text of
your message.  If you wish, instead, to filter the body of your
message through a shell command, then you can use
.(l
~|command
.)l
which pipes your message through the command and uses the output
as the new text of your message.  If the command produces no output,
.i Mail
assumes that something is amiss and retains the old version
of your message.  A frequently-used filter is the command
.i fmt ,
designed to format outgoing mail.
.pp
To effect a temporary escape to
.i Mail
command mode instead, you can use the
.(l
~:\fIMail command\fP
.)l
escape.  This is especially useful for retyping the message you are
replying to, using, for example:
.(l
~:t
.)l
It is also useful for setting options and modifying aliases.
.pp
If you wish abort the current message, you can use the escape
.(l
~q
.)l
This will terminate the current message and return you to the
shell (or \fIMail\fP if you were using the \fBmail\fP command).
If the \fBsave\fP option is set, the message will be copied
to the file
.q dead.letter
in your home directory.
.pp
If you wish (for some reason) to send a message that contains
a line beginning with a tilde, you must double it.  Thus, for example,
.(l
~~This line begins with a tilde.
.)l
sends the line
.(l
~This line begins with a tilde.
.)l
.pp
Finally, the escape
.(l
~?
.)l
prints out a brief summary of the available tilde escapes.
.pp
On some terminals (particularly ones with no lower case)
tilde's are difficult to type.
.i Mail
allows you to change the escape character with the
.q escape
option.  For example, I set
.(l
set escape=]
.)l
and use a right bracket instead of a tilde.  If I ever need to
send a line beginning with right bracket, I double it, just as for ~.
Changing the escape character removes the special meaning of ~.
.sh 2 "Network access"
.pp
This section describes how to send mail to people on other machines.
Recall that sending to a plain login name sends mail to that person
on your machine.  If your machine is directly (or sometimes, even,
indirectly) connected to the Arpanet, you can send messages to people
on the Arpanet using a name of the form
.(l
name@host.domain
.)l
where
.i name
is the login name of the person you're trying to reach,
.i host
is the name of the machine on the Arpanet,
and 
.i domain
is the higher-level scope within which the hostname is known, e.g. EDU (for educational
institutions), COM (for commercial entities), GOV (for governmental agencies),
ARPA for many other things, BITNET or CSNET for those networks.
.pp
If your recipient logs in on a machine connected to yours by
UUCP (the Bell Laboratories supplied network that communicates
over telephone lines), sending mail can be a bit more complicated.
You must know the list of machines through which your message must
travel to arrive at his site.  So, if his machine is directly connected
to yours, you can send mail to him using the syntax:
.(l
host!name
.)l
where, again,
.i host
is the name of the machine and
.i name
is the login name.
If your message must go through an intermediary machine first, you
must use the syntax:
.(l
intermediary!host!name
.)l
and so on.  It is actually a feature of UUCP that the map of all
the systems in the network is not known anywhere (except where people
decide to write it down for convenience).  Talk to your system administrator
about good ways to get places; the
.i uuname
command will tell you systems whose names are recognized, but not which
ones are frequently called or well-connected.
.pp
When you use the
.b reply
command to respond to a letter, there is a problem of figuring out the
names of the users in the
.q "To:"
and
.q "Cc:"
lists
.i "relative to the current machine" .
If the original letter was sent to you by someone on the local machine,
then this problem does not exist, but if the message came from a remote
machine, the problem must be dealt with.
.i Mail
uses a heuristic to build the correct name for each user relative
to the local machine.  So, when you
.b reply
to remote mail, the names in the
.q "To:"
and
.q "Cc:"
lists may change somewhat.
.sh 2 "Special recipients"
.pp
As described previously, you can send mail to either user names or
.b alias
names.  It is also possible to send messages directly to files or to
programs, using special conventions.  If a recipient name has a
`/' in it or begins with a `+', it is assumed to be the
path name of a file into which
to send the message.  If the file already exists, the message is
appended to the end of the file.  If you want to name a file in
your current directory (ie, one for which a `/' would not usually
be needed) you can precede the name with `./'
So, to send mail to the file
.q memo
in the current directory, you can give the command:
.(l
% Mail ./memo
.)l
If the name begins with a `+,' it is expanded into the full path name
of the folder name in your folder directory.
This ability to send mail to files can be used for a variety of
purposes, such as maintaining a journal and keeping a record of
mail sent to a certain group of users.  The second example can be
done automatically by including the full pathname of the record
file in the
.b alias
command for the group.  Using our previous
.b alias
example, you might give the command:
.(l
alias project sam sally steve susan /usr/project/mail_record
.)l
Then, all mail sent to "project" would be saved on the file
.q /usr/project/mail_record
as well as being sent to the members of the project.  This file
can be examined using
.i "Mail \-f" .
.pp
It is sometimes useful to send mail directly to a program, for
example one might write a project billboard program and want to access
it using
.i Mail .
To send messages to the billboard program, one can send mail
to the special name `|billboard' for example.
.i Mail
treats recipient names that begin with a `|' as a program to send
the mail to.  An
.b alias
can be set up to reference a `|' prefaced name if desired.
.i Caveats :
the shell treats `|' specially, so it must be quoted on the command
line.  Also, the `| program' must be presented as a single argument to
mail.  The safest course is to surround the entire name with double
quotes.  This also applies to usage in the
.b alias
command.  For example, if we wanted to alias `rmsgs' to `rmsgs \-s'
we would need to say:
.(l
alias rmsgs "| rmsgs -s"
.)l
