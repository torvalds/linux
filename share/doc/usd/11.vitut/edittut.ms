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
.\" 3. All advertising materials mentioning features or use of this software
.\"    must display the following acknowledgement:
.\"	This product includes software developed by the University of
.\"	California, Berkeley and its contributors.
.\" 4. Neither the name of the University nor the names of its contributors
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
.\"	@(#)edittut.ms	8.3 (Berkeley) 8/18/96
.\"
.ll 6.5i
.nr LL 6.5i
.EH 'USD:11-%''Edit:  A Tutorial'
.OH 'Edit:  A Tutorial''USD:11-%'
.LP
.ds u \s-2UNIX\s0
.ND
.sp 4
.ce
\f3\s+2Edit:  A Tutorial\s0\f1
.sp
.ce 3
.I
Ricki Blau
.sp
James Joyce
.R
.sp
.ce 3
Computing Services
University of California
Berkeley, California 94720
.sp 3
.ce
.I
ABSTRACT
.R
.sp
.LP
This narrative introduction to the use of the text editor
.I edit
assumes no prior familiarity with computers or with text editing.
Its aim is to lead the beginning \s-2UNIX\(dg\s+2 user through the
.FS
\(dgUNIX is a trademark of Bell Laboratories.
.FE
fundamental steps of writing and revising a file of text.
Edit,
a version of the text editor
.I ex,
was designed to provide an informative environment
for new and casual users.
.PP
We welcome comments and suggestions about this tutorial
and the \s-2UNIX\s+2 documentation in general.
.sp .5v
September 1981
.bp
.ll 6.5i
.nr LL 6.5i
.nr LT 6.5i
.ds u \s-2UNIX\s0
.ce
\s+2\f3Contents\f1\s0
.LP
.nf
Introduction\ \ \ 3
.sp
Session 1\ \ 4
.in +.5i
Making contact with \s-2UNIX\s+2\ \ \ 4
Logging in\ \ 4
Asking for \fIedit\fR\ \ \ 4
The ``Command not found'' message\ \ \ 5
A summary\ \ 5
Entering text\ \ \ 5
Messages from \fIedit\fR\ \ \ 5
Text input mode\ \ \ 6
Making corrections\ \ \ 6
Writing text to disk\ \ \ 7
Signing off\ \ 7
.in -.5i
.sp
Session 2\ \ \ 8
.in +.5i
Adding more text to the file\ \ \ 8
Interrupt\ \ \ 8
Making corrections\ \ \ 8
Listing what's in the buffer (p)\ \ \ 9
Finding things in the buffer\ \ \ 9
The current line\ \ \ 10
Numbering lines (nu)\ \ \ 10
Substitute command (s)\ \ \ 10
Another way to list what's in the buffer (z)\ \ \ 11
Saving the modified text\ \ \ 12
.in -.5i
.sp
Session 3\ \ \ 13
.in +.5i
Bringing text into the buffer (e)\ \ \ 13
Moving text in the buffer (m)\ \ \ 13
Copying lines (copy)\ \ \ 14
Deleting lines (d)\ \ \ 14
A word or two of caution\ \ \ 15
Undo (u) to the rescue\ \ \ 15
More about the dot (.) and buffer end ($)\ \ \ 16
Moving around in the buffer (+ and \-)\ \ \ 16
Changing lines (c)\ \ \ 17
.in -.5i
.sp
Session 4\ \ \ 18
.in +.5i
Making commands global (g)\ \ \ 18
More about searching and substituting\ \ \ 19
Special characters\ \ \ 19
Issuing \s-2UNIX\s+2 commands from the editor\ \ \ 20
Filenames and file manipulation\ \ \ 20
The file (f) command\ \ \ 20
Reading additional files (r)\ \ \ 21
Writing parts of the buffer\ \ \ 21
Recovering files\ \ \ 21
Other recovery techniques\ \ \ 21
Further reading and other information\ \ \ 22
Using \fIex\fR\ \ \ 22
.in -.5i
.sp
Index\ \ \ 23
.bp
.SH
.ce
\s+2Introduction\s0
.PP
Text editing using a terminal connected to a computer
allows you to create, modify, and print text
easily.
A
.I
text editor
.R
is a program
that assists you
as you create and modify text.
The text editor you will learn here is named
.I edit.
Creating text using edit is as easy as typing it
on an electric typewriter.
Modifying text involves telling the text editor 
what you want to add, change, or delete.
You can review your text
by typing a command
to print the file contents
as they are currently.
Another program (which we do not discuss in this
document), a text formatter,
rearranges your text
for you into ``finished form.''
.PP
These lessons assume no prior familiarity with computers
or with text editing.
They consist of a series of text editing sessions
which lead you through the fundamental steps
of creating and revising text.
After scanning each lesson and before beginning the next,
you should try the examples at a terminal to get a feeling
for the actual process of text editing.
If you set aside some time for experimentation,
you will soon become familiar with using the
computer to write and modify text.
In addition to the actual use of the text editor,
other features of \s-2UNIX\s0 will be very important to your work.
You can begin to
learn about these other features by
reading one of the other tutorials
that provide a general introduction to the system.
You will be ready to proceed with this lesson as soon as
you are familiar with (1) your terminal and its special keys,
(2) how to login,
(3) and the ways of correcting typing errors.
Let's first define some terms:
.sp .5
.IP program 12
A set of instructions, given to the computer,
describing the sequence of steps the computer performs
in order to accomplish a specific task.
The task must be specific,
such as balancing your checkbook
or editing your text.
A general task,
such as working for world peace,
is something we can all do,
but not something we can currently write programs to do.
.IP UNIX
\s-2UNIX\s0 is a special type of program,
called an operating system, that supervises the machinery
and all other programs comprising the total
computer system.
.IP edit
.I edit
is the name of the \s-2UNIX\s0 text editor you will be learning to use,
and is a program that aids you in writing or revising text.
Edit was designed for beginning users,
and is a simplified version of an editor named
.I ex.
.IP file
Each \s-2UNIX\s0 account is allotted
space for the permanent storage of information,
such as programs, data or text.
A file is a logical unit of data,
for example, an essay, a program,
or a chapter from a book,
which is stored on a computer system.
Once you create a file,
it is kept until you instruct the system to remove it.
You may create a file during one \s-2UNIX\s0 session,
end the session,
and return to use it at a later time.
Files contain anything you choose to write and store in them.
The sizes of files vary to suit your needs;
one file might hold only a single number,
yet another might contain
a very long document or program.
The only way to save
information from one session to the next is to store it in a file,
which you will learn in Session 1.
.IP filename
Filenames are used to distinguish one file from another,
serving the same purpose as the labels of manila
folders in a file cabinet.
In order to write or access information in a file,
you use the name of that file in a \s-2UNIX\s0 command,
and the system will automatically locate the file.
.IP disk
Files are stored on an input/output device called a disk,
which looks something like a stack of phonograph records.
Each surface is coated with a material similar to that
on magnetic recording tape,
and information is recorded on it.
.IP buffer
A temporary work space, made available to the user
for the duration of a session of text editing
and used for creating and modifying
the text file.
We can think of the buffer as a blackboard that is
erased after each class, where each session with the editor
is a class.
.bp
.SH
.ce 1
\s+2Session 1\s0
.sp 1
.SH
Making contact with \s-1UNIX\s0
.PP
To use the editor you must first make contact with the computer
by logging in to \s-2UNIX\s0.
We'll quickly review the standard \s-2UNIX\s0 login procedure
for the two ways you can make contact:
on a terminal that is directly linked to the computer,
or over a telephone line where the computer answers your call.
.SH
Directly-linked terminals
.PP
Turn on your terminal and press the \s-1RETURN\s0 key.
You are now ready to login.
.SH
Dial-up terminals
.PP
If your terminal connects with the computer over a telephone line,
turn on the terminal, dial the system access number,
and, when you hear a high-pitched tone, place the 
telephone handset in the acoustic coupler, if you are using one.
You are now ready to login.
.SH
Logging in
.PP
The message inviting you to login is:
.DS I 1i
login:
.DE
.LP
Type your login name, which identifies you to \s-2UNIX\s0,
on the same line as the login message,
and press \s-2RETURN\s+2.
If the terminal you are using
has both upper and lower case,
.B
be sure you enter your login name in lower case;
.R
otherwise \s-2UNIX\s0 assumes your terminal
has only upper case and will not recognize lower case
letters you may type.
\s-2UNIX\s0 types ``login:'' and you reply
with your login name, for example ``susan'':
.DS I 1i
login: \fBsusan\fR \fI(and press the \s-2RETURN\s0 key)\fR
.DE
(In the examples, input you would type appears in
.B "bold face"
to distinguish it from the responses from \s-2UNIX\s0.)
.PP
\s-2UNIX\s0 will next respond with a request for a password
as an additional precaution to prevent
unauthorized people from using your account.
The password will not appear when you type it,
to prevent others from seeing it.
The message is:
.DS I 1i
Password:    \fI(type your password and press \s-2RETURN\s+2)\fR
.DE
If any of the information you gave during the login
sequence was mistyped or incorrect,
\s-2UNIX\s0 will respond with
.DS I 1i
Login incorrect.
.if t .sp .2v
.if n .sp 1
login:
.DE
in which case you should start the login process anew.
Assuming that you have successfully
logged in, \s-2UNIX\s0
will print the message of the day and eventually will present
you with a % at the beginning of a fresh line.
The % is the \s-2UNIX\s0 prompt symbol
which tells you that \s-2UNIX\s0 is ready to accept a command.
.bd I 3
.SH
Asking for \fIedit\fP
.fl
.bd I
.PP
You are ready to tell \s-2UNIX\s0 that you
want to work with edit, the text editor.
Now is a convenient time to choose
a name for the file of text you are about to create.
To begin your editing session,
type
.B edit
followed by a space and then the filename
you have selected; for example, ``text''.
After that,
press the \s-2RETURN\s0 key and wait for edit's response:
.DS I 1i
% \fBedit text\fP    \fI(followed by a \s-2RETURN\s+2)\fR
"text" No such file or directory
:
.DE
If you typed the command correctly,
you will now be in communication with edit.
Edit has set aside a buffer for use as
a temporary working space during your current editing session.
Since ``text'' is a new file we are about to create
the editor was unable to find that file, which it
confirms by saying:
.DS I 1i
"text" No such file or directory
.DE
On the next line appears edit's prompt ``:'',
announcing that you are in \f2command mode\f1 and
edit expects a command from you.
You may now begin to create the new file.
.SH
The ``Command not found'' message
.PP
If you misspelled edit by typing, say, ``editor'',
this might appear:
.DS I 1i
% \fBeditor\fP
editor: Command not found
%
.DE
Your mistake in calling edit ``editor'' was
treated by \s-2UNIX\s0 as a request
for a program named ``editor''.
Since there is no program
named ``editor'',
\s-2UNIX\s0 reported that the program was ``not found''.
A new % indicates that \s-2UNIX\s0 is ready for another command,
and you may then enter the correct command.
.SH
A summary
.PP
Your exchange with \s-2UNIX\s0 as you logged in and made contact with edit
should look something like this:
.DS I 1i
login: \fBsusan\fP
Password:
\&... A Message of General Interest ...
% \fBedit text\fP
"text" No such file or directory
:
.DE
.SH
Entering text
.PP
You may now begin entering text into the buffer.
This is done by \fIappending\fP (or adding) text to whatever
is currently in the buffer.
Since there is nothing in the buffer at the moment,
you are appending text to nothing;
in effect, 
since you are adding text to nothing
you are creating text.
Most edit commands have two equivalent forms:
a word that suggests what the command does,
and a shorter abbreviation of that word.
Many beginners find the full command names
easier to remember at first,
but once you are familiar with editing you may
prefer to type the shorter abbreviations.
The command to input text is ``append''.
(It may be abbreviated ``a''.)
Type
.B append
and press the \s-2RETURN\s0 key.
.DS I 1i
% \fBedit text
\fR:\|\fBappend
.R
.DE
.SH
.bd I 3
Messages from
.I edit
.fl
.bd I
.PP
If you make a mistake in entering a command and
type something that edit does not recognize,
edit will respond with a message
intended to help you diagnose your error.
For example, if you misspell the command to input text by typing,
perhaps, ``add'' instead of ``append'' or ``a'',
you will receive this message:
.DS I 1i
:\|\fBadd\fR
add: Not an editor command
:
.DE
When you receive a diagnostic message,
check what you typed in order to determine what
part of your command confused edit.
The message above means that edit
was unable to recognize your mistyped command
and, therefore, did not execute it.
Instead, a new ``:''
appeared to let you know that
edit is again ready to execute a command.
.SH
Text input mode
.PP
By giving the command ``append'' (or using the abbreviation ``a''),
you entered
.I
text input mode,
.R
also known as
.I
append mode.
.R
When you enter text input mode,
edit stops sending you a prompt.
You will not receive any prompts
or error messages
while in text input mode.
You can enter
pretty much anything you want on the lines.
The lines are transmitted one by one to the buffer
and held there during the editing session.
You may append as much text as you want, and
.I
when you wish to stop entering text lines you should
type a period as the only character on the line
and press the \s-2RETURN\s0 key.
.R
When you type the period and press \s-2RETURN\s0,
you signal that you want to stop appending text,
and edit responds by allowing
you to exit text input mode and reenter command mode.
Edit will again
prompt you for a command by printing ``:''.
.PP
Leaving append mode does not destroy the text in
the buffer.
You have to leave append
mode to do any of the other kinds of editing,
such as changing, adding, or printing text.
If you type a period as the first character and
type any other character on the same line,
edit will believe you want to remain in append mode
and will not let you out.
As this can be very frustrating, 
be sure to type
.B only
the period and the \s-2RETURN\s0 key.
.PP
This is a good place to learn an important
lesson about computers and text:  a blank space is
a character as far as a computer is concerned.  
If you so much as type a period followed by a blank
(that is, type a period and then the space bar on the keyboard),
you will remain in append mode with the last line of text
being:
.DS I 1i
.B
.ps +2
\&.
.ps -2
.R
.DE
Let's say that you enter the lines 
(try to type
.B exactly
what you see, including ``thiss''):
.DS I 1i
.B
This is some sample text.
And thiss is some more text.
Text editing is strange, but nice.
\&.
.R
.DE
The last line is the period followed by a \s-2RETURN\s0
that gets you out of append mode.  
.SH
Making corrections
.PP
If you have read a general introduction to \s-2UNIX\s0,
you will recall that it is possible to erase individual
letters that you have typed.
This is done by typing the designated erase character
as many times as there are characters
you want to erase.
.PP
The usual erase character varies from place to place and 
user to user.  Often it
is the backspace (control-H),
so you can correct typing errors
in the line you are typing
by holding down the \s-1CTRL\s+1 key
and typing the ``H'' key.  (Sometimes it is the DEL key.)
If you type the erase character
you will notice
that the terminal backspaces in the line you are on.
You can backspace over your error,
and then type what you want to be the rest of the line.
.PP
If you make a bad start
in a line
and would like to begin again,
you can either backspace to the beginning of the line
or you can use the at-sign ``@'' to erase everything on the line:
.DS I 1i
.B
Text edtiing is strange, but@
Text editing is strange, but nice.
.R
.fl
.bd S
.DE
When you type the at-sign (@), you erase
the entire line typed so far
and are given a fresh line to type on.
You may immediately begin to retype the line.
This, unfortunately, does not work after you type the
line and press \s-2RETURN\s+2.  
To make corrections in lines that have been completed,
it is necessary to use the editing commands
covered in the next sessions.
.SH
Writing text to disk
.PP
You are now ready to edit the text.  One common operation
is to write the text to disk as a file for safekeeping
after the session is over.
This is the only way to save information from one session to the next,
since the editor's buffer is temporary and will last only until the
end of the editing session.
Learning how to write a file to disk is second in
importance only to entering the text.
To write the contents of the buffer to a disk
file, use the command ``write''
(or its abbreviation ``w''):
.DS I 1i
:\|\fBwrite
.R
.DE
Edit will copy the contents of the buffer to a disk file.
If the file does not yet exist,
a new file will be created automatically
and the presence of a ``[New file]'' will be noted.
The newly-created file will be given the name specified when
you entered the editor, in this case ``text''.
To confirm that the disk file has been successfully written,
edit will repeat the filename and give
the number of lines and the total
number of characters in the file.
The buffer remains unchanged by the ``write'' command.
All of the lines that were written to disk will still be
in the buffer,
should you want to modify or add to them.
.PP
Edit must have a name for the file to be written.
If you forgot to indicate the name of the file
when you began to edit,
edit will print in response to your write command:
.DS I 1i
No current filename
.DE
If this happens, you can specify the filename in a new write command:
.DS I 1i
:\|\fBwrite text
.R
.DE
After the ``write'' (or ``w''), type a space and then the name of the file.
.SH
Signing off
.PP
We have done enough for this first lesson on using the
\s-2UNIX\s0 text editor, and are ready to quit the session with edit.
To do this we type ``quit'' (or ``q'') and press \s-2RETURN\s+2:
.DS I 1i
:\|\fBwrite
.R
"text" [New file]  3 lines, 90 characters
:\|\fBquit\fR
%
.DE
The % is from \s-2UNIX\s0 to tell you that your session with edit is
over and you may command \s-2UNIX\s0 further.
Since we want
to end the entire session at the terminal, we also need to
exit from \s-2UNIX\s0.
In response to the \s-2UNIX\s0 prompt of ``\|%\|''
type the command
.DS I 1i
%\|\fBlogout\fR
.DE
This will end your session with \s-2UNIX\s0, and will ready the
terminal for the next user.
It is always important to type \fBlogout\fR at the end of a session
to make absolutely sure no one
could accidentally stumble into your abandoned 
session and thus gain access to your files,
tempting even the most honest of souls.
.sp 1
.PP
This is the end of the first session on \s-2UNIX\s0 text editing.
.bp
.TL
Session 2
.sp
.PP
Login with \s-2UNIX\s0 as in the first session:
.DS I 1i
login: \fBsusan\fP  \fI(carriage return)\fR
Password:       \fI(give password and carriage return)\fR
.if t .sp .2v
.if n .sp 1
\&... A Message of General Interest ...
% 
.DE
When you indicate you want to edit,
you can specify the name of the file you worked on last time.
This will
start edit working, and it will fetch the contents of the
file into the buffer, so that you can resume editing the same file.
When edit has copied the file into the buffer, it
will repeat its name and tell
you the number of lines and characters it contains.
Thus,
.DS I 1i
.B
% edit text
.R
"text" 3 lines, 90 characters
:
.DE
means you asked edit to fetch
the file named ``text'' for editing,
causing it to copy the
90 characters of text into the buffer.
Edit awaits
your further instructions,
and indicates this by its prompt character, the colon (:).
In this session, we will append more text to our file,
print the contents of the buffer, and learn to change the text of a line.
.SH
Adding more text to the file
.PP
If you want to add more to the end of your
text you may do so by using the append command to enter text input mode.
When ``append'' is the first command
of your editing session,
the lines you enter
are placed at the end of the buffer.
Here we'll use the abbreviation for the append command, ``a'':
.DS I 1i
:\|\fBa
This is text added in Session 2.
It doesn't mean much here, but
it does illustrate the editor.
\|\fB\s+2\&.\s-2
.R
.DE
You may recall that once you enter append mode
using the ``a'' (or ``append'') command,
you need to type a line containing only a period (.)
to exit append mode.
.SH
Interrupt
.PP
Should you press the \s-2RUB\s+2 key (sometimes labelled \s-2DELETE\s+2)
while working with edit,
it will send this message to you:
.DS I 1i
Interrupt
:
.DE
Any command that edit might be executing
is terminated by rub or delete,
causing edit to prompt you for a new command.
If you are appending text at the time,
you will exit from append mode
and be expected to give another command.
The line of text you were typing
when the append command was interrupted
will not be entered into the buffer.
.SH
Making corrections
.PP
If while typing the line you hit an incorrect key,
recall that
you may delete the incorrect character
or cancel the entire line of input by erasing in the usual way.
Refer either
to the last few pages of Session 1
if you need to review
the procedures for making a correction.
The most important idea to remember is that
erasing a character or cancelling a line must be done
before you press the \s-2RETURN\s+2 key.
.SH
Listing what's in the buffer (p)
.PP
Having appended text to what you wrote in Session 1,
you might want to see all the lines in the buffer.
To print the contents of the buffer, type the command:
.DS I 1i
:\|\fB1,$p
.R
.DE
The ``1''\(dg
.FS
\(dgThe numeral ``one'' is the top left-most key,
and should not be confused with the letter ``el''.
.FE
stands for line 1 of the buffer,
the ``$'' is a special symbol designating the last line
of the buffer,
and ``p'' (or \fBprint\fR) is the command to print from line 1
to the end of the buffer.
The command ``1,$p'' gives you:
.DS I 1i
This is some sample text.
And thiss is some more text.
Text editing is strange, but nice.
This is text added in Session 2.
It doesn't mean much here, but
it does illustrate the editor.
.DE
Occasionally, you may accidentally
type a character that can't be printed,
which can be done by striking a key
while the \s-2CTRL\s0 key is pressed.
In printing lines, edit uses a special notation to
show the existence of non-printing characters.
Suppose you had introduced the non-printing character ``control-A''
into the word ``illustrate''
by accidently pressing the \s-2CTRL\s0 key while
typing ``a''.
This can happen on many terminals
because the \s-2CTRL\s+2 key and the ``A'' key
are beside each other.
If your finger presses between the two keys,
control-A results.
When asked to print the contents of the buffer,
edit would display
.DS I 1i
it does illustr^Ate the editor.
.DE
To represent the control-A, edit shows ``^A''.
The sequence ``^'' followed by a capital
letter stands for the one character
entered by holding down the \s-2CTRL\s0 key and typing the letter
which appears after the ``^''.
We'll soon discuss the commands that can be used
to correct this typing error.
.PP
In looking over the text we see that
``this'' is typed as ``thiss'' in the second line,
a deliberate error so we can learn to make corrections.
Let's correct the spelling.
.SH
Finding things in the buffer
.PP
In order to change something in the buffer we first need to
find it.
We can find ``thiss'' in the text we have
entered by looking at a listing
of the lines.
Physically speaking, we search the lines
of text looking for ``thiss'' and stop searching when
we have found it.
The way to tell edit to search for something
is to type it inside slash marks:
.DS I 1i
:\|\fB/thiss/
.R
.DE
By typing
.B /thiss/
and pressing \s-1RETURN\s0,
you instruct edit to search for ``thiss''.
If you ask edit to look for a pattern of characters
which it cannot find in the buffer,
it will respond ``Pattern not found''.
When edit finds
the characters ``thiss'', it will print the line of text
for your inspection:
.DS I 1i
And thiss is some more text.
.DE
Edit is now positioned in the buffer at the
line it just printed,
ready to make a change in the line.
.bp
.SH
The current line
.PP
Edit keeps track of the line in the buffer where it is located
at all times during an editing session.
In general, the line that has been most recently
printed, entered, or changed
is the current location in the buffer.
The editor is prepared to make changes
at the current location in the buffer,
unless you direct it to another location.
.PP
In particular,
when you bring a file into the buffer,
you will be located at the last line in the file,
where the editor left off copying the lines
from the file to the buffer.
If your first editing command is ``append'',
the lines you enter are added
to the end of the file,
after the current line \(em
the last line in the file.
.PP
You can refer to your current location in the buffer by the
symbol
period (.) usually known by the name ``dot''.
If you type ``.'' and carriage
return you will be instructing edit to print the current line:
.DS I 1i
:\|\fB\s+2\&.\s-2
.R
And thiss is some more text.
.DE
.PP
If you want to know the number of the current line,
you can type
.B \&.=
and press \s-2RETURN\s+2,
and edit will respond with the line number:
.DS I 1i
:\|\fB\s+2.\s-2=
.R
2
.DE
If you type the number of any line and press \s-2RETURN\s+2,
edit will position you at that line and
print its contents:
.DS I 1i
:\|\fB2
.R
And thiss is some more text.
.DE
You should experiment with these commands
to gain experience in using them to make changes.
.SH
Numbering lines (nu)
.PP
The
.B
number (nu)
.R
command is similar to print,
giving both the number and the text of each printed line.
To see the number and the text of the current line type
.DS I 1i
:\|\fBnu
.R
\0\0\0\0\02\0\0And thiss is some more text.
.DE
Note that the shortest abbreviation for the number command is
``nu'' (and not ``n'', which is used for a different command).
You may specify a range of lines
to be listed by the number command in the same way that lines
are specified for print.
For example, \f31,$nu\f1 lists all lines in the buffer with their
corresponding line numbers.
.SH
Substitute command (s)
.PP
Now that you have found the misspelled word, 
you can change it from ``thiss'' to ``this''.
As far as edit is concerned,
changing things is a matter of
substituting one thing for another.
As
.I a
stood for
.I append,
so
.I s
stands for
.I substitute.
We will use the abbreviation ``s'' to reduce the chance
of mistyping the substitute command.
This command will instruct edit to make the change:
.DS I 1i
\f32s/thiss/this/\f1
.DE
We first indicate the line to be changed, line 2,
and then
type an ``s'' to indicate we want
edit to make a substitution.
Inside the first set of slashes
are the characters that we want to change,
followed by the characters to replace them,
and then a closing slash mark.
To summarize:
.DS I 1i
2s/ \fIwhat is to be changed\fR / \fIwhat to change it to \fR/
.DE
If edit finds an exact match of the characters to be
changed it will make the change
.B only
in the first occurrence of the characters.
If it does not find the characters
to be changed, it will respond:
.DS I 1i
Substitute pattern match failed
.DE
indicating that your instructions could not be carried out.
When edit does find the characters that you want to change,
it will make the substitution and automatically print
the changed line, so that you can check that the correct substitution
was made.
In the example,
.DS I 1i
:\|\fB2s/thiss/this/
.R
And this is some more text.
.DE
line 2 (and line 2 only) will be searched for the characters
``thiss'', and when the first exact match is found, ``thiss''
will be changed to ``this''.
Strictly speaking, it was not necessary above to
specify  the number of the line to be changed.
In
.DS I 1i
:\|\fBs/thiss/this/
.R
.DE
edit will assume that we mean to change
the line where we are currently located (``.'').
In this case,
the command without a line number would have produced the same result
because we were already located
at the line we wished to change.
.PP
For another illustration of the substitute command,
let us choose the line:
.DS I 1i
Text editing is strange, but nice.
.DE
You can make this line a bit more positive
by taking out the characters ``strange, but\ '' so the line 
reads:
.DS I 1i
Text editing is nice.
.DE
A command that will first position edit at the desired line
and then make the substitution is:
.DS I 1i
:\|\fB/strange/s/strange, but //
.R
.DE
.LP
What we have done here is combine our search with
our substitution.
Such combinations are perfectly legal,
and speed up editing quite a bit
once you get used to them.
That is, you do not necessarily have to use
line numbers to identify a line to edit.
Instead, you may identify the line you want to change
by asking edit to search for a specified pattern of letters
that occurs in that line.
The parts of the above command are:
.in +1i
.TS
l l.
\fB/strange/\fP	tells edit to find the characters ``strange'' in the text
\fBs\fP	tells edit to make a substitution
\fB/strange, but //\fP	substitutes nothing at all for the characters ``strange, but ''
.TE
.in -1i
.PP
You should note the space after ``but'' in ``/strange, but /''. 
If you do not indicate that the space is to be taken out,
your line will read:
.DS I 1i
.if t Text editing is   nice.
.if n Text editing is  nice.
.DE
which looks a little funny   
because of the extra space between ``is'' and ``nice''.
Again, we realize from this that a blank space
is a real character to a computer, and in editing text
we need to be aware of spaces
within a line just as we would be aware of an ``a'' or 
a ``4''.
.SH
Another way to list what's in the buffer (z)
.PP
Although the print command is useful for looking at specific lines
in the buffer,
other commands may be more convenient for
viewing large sections of text.
You can ask to see a screen full of text at a time
by using the command
.B z.
If you type
.DS I 1i
:\|\fB1z
.R
.DE
edit will start with line 1 and continue printing lines,
stopping either when the screen of
your terminal is full
or when the last line in the buffer has been printed.
If you want to read the next segment of text, type the command
.DS I 1i
:\|\fBz
.DE
If no starting line number is given for the z command,
printing will start at the ``current'' line, in this case the
last line printed.
Viewing lines in the buffer one screen full at a time
is known as \fIpaging\fR.
Paging can also be used to print
a section of text on a hard-copy terminal.
.SH
Saving the modified text
.PP
This seems to be a good place to pause in our work,
and so we should end the second session.
If you (in haste) type ``q'' to quit the session
your dialogue with edit will be:
.DS I 1i
:\|\fBq
.R
No write since last change (:quit! overrides)
:
.DE
This is edit's warning that you have not written
the modified contents of the buffer to disk.
You run the risk of losing the work you did
during the editing session since you typed the latest write
command.
Because in this lesson we have not written
to disk at all, everything we have done
would have been lost
if edit had obeyed the \fBq\fR command.
If you did not want to save the work done during
this editing session, you would have to type ``q!''
or (``quit!'')
to confirm that you indeed wanted to end the session
immediately,
leaving the file as it was
after the most recent ``write'' command.
However,
since you want to save what
you have edited, you need to type:
.DS I 1i
:\|\fBw
.R
"text" 6 lines, 171 characters
.DE
and then follow with the commands to quit and logout:
.DS I 1i
:\|\fBq
% \fBlogout\fR
.DE
and hang up the phone or turn off the terminal when
\s-2UNIX\s0 asks for a name.
Terminals connected to the port selector
will stop after the logout command,
and pressing keys on the keyboard will do nothing.
.sp 1
.PP
This is the end of the second session on \s-2UNIX\s0 text editing.
.bp
.TL
Session 3
.SH
Bringing text into the buffer (e)
.PP
Login to \s-2UNIX\s0 and make contact with edit.  
You should try to login without
looking at the notes, but if you must
then by all means do.
.PP
Did you remember to give the name of the file
you wanted to edit?
That is, did you type
.DS I 1i
% \fBedit text\fR
.DE
or simply
.DS I 1i
% \fBedit\fR
.DE
Both ways get you in contact with edit, but the first way
will bring a copy of the file named ``text'' into
the buffer.  
If you did forget to tell edit the name of your file,
you can get it into the buffer by
typing:
.DS I 1i
:\|\fBe text
.R
"text" 6 lines, 171 characters
.DE
The command
.B edit,
which may be abbreviated \fBe\fR,
tells edit that you want
to erase anything that might already be in 
the buffer and bring a copy of the file ``text'' into the buffer
for editing.
You may also use the edit (e) command to change files in
the middle of an editing session,
or to give edit the name of a new file that you want to create.
Because the edit command clears the buffer,
you will receive a warning if you try to edit a new file without
having saved a copy of the old file.
This gives you a chance to write the contents of the buffer to disk
before editing the next file.
.SH
Moving text in the buffer (m)
.PP
Edit allows you to move lines of text
from one location in the buffer to another
by means of the
.B move
(\fBm\fR) command.
The first two examples are for illustration only,
though after you have read this Session
you are welcome to return to them for practice.
The command
.DS I 1i
:\|\fB2,4m$
.R
.DE
directs edit to move lines 2, 3, and 4
to the end of the buffer ($).  
The format for the move command is that you specify
the first line to be moved, the last line to be moved,
the move command ``m'', and the line after which
the moved text is to be placed.
So,
.DS I 1i
:\|\fB1,3m6
.R
.DE
would instruct edit to move lines 1 through 3 (inclusive) 
to a location after line 6 in the buffer.
To move only one line, say, line 4,
to a location in the buffer after line 5, 
the command would be ``4m5''.
.PP
Let's move some text using the command:
.DS I 1i
:\|\fB5,$m1
.R
2 lines moved
it does illustrate the editor.
.DE
After executing a command that moves more than one line of the buffer,
edit tells how many lines were affected by the move
and prints the last moved line for your inspection.
If you want to see more than just the last line,
you can then
use the print (p), z, or number (nu) command to view more text.
The buffer should now contain:
.DS I 1i
This is some sample text.
It doesn't mean much here, but
it does illustrate the editor.
And this is some more text.
Text editing is nice.
This is text added in Session 2.
.DE
You can restore the original order by typing:
.DS I 1i
:\|\fB4,$m1
.R
.DE
or, combining context searching and the move command:
.DS I 1i
:\|\fB/And this is some/,/This is text/m/This is some sample/
.R
.DE
(Do not type both examples here!)
The problem with combining context searching
with the move command 
is that your chance of making a typing error
in such a long command is greater than
if you type line numbers.
.SH
Copying lines (copy)
.PP
The
.B copy
command
is used to make a second copy of specified lines,
leaving the original lines where they were.
Copy
has the same format as the move command, for example:
.DS I 1i
:\|\fB2,5copy $
.R
.DE
makes a copy of lines 2 through 5,
placing the added lines after the buffer's end ($).
Experiment with the copy command
so that you can become familiar with how it works.
Note that the shortest abbreviation for copy is
\f3co\f1 (and
not the letter ``c'', which has another meaning).
.SH
Deleting lines (d)
.PP
Suppose you want to delete 
the line
.DS I 1i
This is text added in Session 2.
.DE
from the buffer.
If you know the number of the line to be deleted,
you can type
that number followed by
\fBdelete\fR or \fBd\fR.
This example deletes line 4,
which is ``This is text added in Session 2.''
if you typed the commands
suggested so far.
.DS I 1i
:\|\fB4d
.R
It doesn't mean much here, but
.DE
Here ``4'' is the number of the line to be deleted,
and ``delete'' or ``d'' is the command to delete the line.
After executing the delete command,
edit prints the line that has become the current line (``.'').
.PP
If you do not happen to know the line number
you can search for the line and then delete it using this
sequence of commands:
.DS I 1i
:\|\fB/added in Session 2./
.R
This is text added in Session 2.
:\|\fBd
.R
It doesn't mean much here, but
.DE
The ``/added in Session 2./''
asks edit to locate and print
the line containing the indicated text,
starting its search at the current line
and moving line by line
until it finds the text.
Once you are sure that you have correctly specified the line
you want to delete,
you can enter the delete (d) command.
In this case it is not necessary to
specify a line number before the ``d''.
If no line number is given,
edit deletes the current line (``.''),
that is, the line found by our search.
After the deletion, your buffer should contain:
.DS I 1i
This is some sample text.
And this is some more text.
Text editing is nice.
It doesn't mean much here, but
it does illustrate the editor.
And this is some more text.
Text editing is nice.
This is text added in Session 2.
It doesn't mean much here, but
.DE
To delete both lines 2 and 3:
.DS I 1i
And this is some more text.
Text editing is nice.
.DE
you type
.DS I 1i
:\|\f32,3d\f1
2 lines deleted
.DE
which specifies the range of lines from 2 to 3,
and the operation on those lines \(em ``d'' for delete.
If you delete more than one line
you will receive a message
telling you the number of lines deleted,
as indicated in the example above.
.PP
The previous example assumes that you know the line numbers for
the lines to be deleted.
If you do not you might combine the search command
with the delete command:
.DS I 1i
:\|\fB/And this is some/,/Text editing is nice./d
.R
.DE
.SH
A word or two of caution
.PP
In using the search function to locate lines to
be deleted you should be
.B
absolutely sure
.R
the characters you give as the basis for the search
will take edit to the line you want deleted.
Edit will search for the first
occurrence of the characters starting from where
you last edited \-
that is, from the line you see printed if you type dot (.).
.PP
A search based on too few
characters may result in the wrong lines being deleted,
which edit will do as easily as if you had meant it.
For this reason, it is usually safer
to specify the search and then delete in two separate steps,
at least until you become familiar enough with using the editor
that you understand how best to specify searches.
For a beginner it is not a bad idea to double-check
each command before pressing \s-2RETURN\s+2 to send the command on its way.
.SH
Undo (u) to the rescue
.PP
The
.B
undo (u)
.R
command has the ability to
reverse the effects of the last command that changed the buffer.
To undo the previous command, type
``u'' or ``undo''.
Undo can rescue
the contents of the buffer from many an unfortunate mistake.
However, its powers are not unlimited,
so it is still wise to be reasonably
careful about the commands you give.
.PP
It is possible to undo only commands which
have the power to change the buffer \(em for example,
delete, append, move, copy, substitute, and even undo itself.
The commands write (w) and edit (e), which interact with disk files,
cannot be undone, nor can commands that do not change
the buffer, such as print.
Most importantly,
the
.B only
command that can be reversed by undo
is the
last ``undo-able'' command you typed.
You can use control-H and @ to change
commands while you are typing them,
and undo to reverse the effect of the commands
after you have typed them and pressed \s-2RETURN\s+2.
.PP
To illustrate,
let's issue an undo command.
Recall that the last buffer-changing command we gave deleted
the lines formerly numbered 2 and 3.
Typing undo at this moment will reverse the effects
of the deletion, causing those two lines to be
replaced in the buffer.
.DS I 1i
:\|\fBu
.R
2 more lines in file after undo
And this is some more text.
.DE
Here again, edit informs you if the command affects more
than one line,
and prints
the text of the line which is now ``dot'' (the current line).
.SH
More about the dot (.) and buffer end ($)
.PP
The function assumed by the symbol dot depends on its context.
It can be used:
.IP
1.  to exit from append mode; we type dot (and only a dot) on
a line and press \s-2RETURN\s+2;
.IP
2.  to refer to the line we are at in the buffer.
.LP
Dot can also be combined with the equal sign to get
the number of the line currently being edited:
.DS I 1i
:\|\fB\&.=
.R
.DE
If we type ``\fB.\fR='' we are asking for the number of the line,
and if we type ``\fB.\fR'' we are asking for the text of the line.
.PP
In this editing session and the last, we used the dollar
sign to indicate the end of the buffer
in commands such as print, copy, and move.
The dollar sign as a command asks edit to print the last
line in the buffer.
If the dollar sign is combined with the equal sign (\f3$=\f1)
edit will print the line number corresponding to the
last line in the buffer.
.PP
``\fB.\fR'' and ``$'', then, represent line numbers.
Whenever appropriate, these symbols can be used in
place of line numbers in commands.
For example
.DS I 1i
:\|\fB\s+2.\s-2,$d
.R
.DE
instructs edit to delete all lines from the current line (\fB.\fR)
to the end of the buffer.
.SH
Moving around in the buffer  (+ and \-)
.PP
When you are editing
you often want
to go back and re-read a previous line.
You could specify a context search for a line you want to
read if you remember some of its text,
but if you simply want to see what was written a few, say 3, lines
ago, you can type
.DS I 1i
\-3p
.DE
This tells edit to move back to a position 3 lines
before the current line (.)
and print that line.
You can move forward in the buffer similarly:
.DS I 1i
+2p
.DE
instructs edit to print the line that is 2
ahead of your current position.
.PP
You may use ``+'' and ``\-'' in any command where edit
accepts line numbers.
Line numbers specified with ``+'' or ``\-''
can be combined to print a range of lines.
The command
.DS I 1i
:\|\fB\-1,+2copy$
.R
.DE
makes a copy of 4 lines:  the current line, the line before it,
and the two after it.
The copied lines will be placed after the last line
in the buffer ($),
and the original lines referred to by ``\-1'' and ``+2''
remain where they are.
.PP
Try typing only ``\-''; you will move back one line just as
if you had typed ``\-1p''.
Typing the command ``+'' works similarly.
You might also try typing a few plus or minus signs in a row
(such as ``+++'') to see edit's response.
Typing \s-2RETURN\s+2 alone on a line is the equivalent
of typing ``+1p''; it will move you one line ahead in the buffer
and print that line.
.PP
If you are at the last line of the buffer and try
to move further ahead, perhaps by typing a ``+'' or
a carriage return alone on the line,
edit will remind you that you are at the end of the buffer:
.sp
.nf
.ti 1i
At end-of-file
.br
or
.ti 1i
Not that many lines in buffer
.fi
.LP
Similarly, if you try to move to a position before the first line,
edit will print one of these messages:
.sp
.nf
.ti 1i
Nonzero address required on this command
.br
or
.ti 1i
Negative address \- first buffer line is 1
.fi
.LP
The number associated with a buffer line is the line's ``address'',
in that it can be used to locate the line.
.SH
Changing lines (c)
.PP
You can also delete certain lines and
insert new text in their place.
This can be accomplished easily with the
.B "change (c)"
command.
The change command instructs edit to delete specified lines
and then switch to text input mode to
accept the text that will replace them.
Let's say you want to change the first two lines in the buffer:
.DS I 1i
This is some sample text.
And this is some more text.
.DE
to read
.DS I 1i
This text was created with the \s-2UNIX\s0 text editor.
.DE
To do so, you type:
.DS I 1i
:\|\fB1,2c
.R
2 lines changed
.B
This text was created with the \s-2UNIX\s0 text editor.
\s+2\&.\s-2
.R
:
.DE
In the command
.B 1,2c
we specify that we want to change
the range of lines beginning with 1 and ending with 2
by giving line numbers as with the print command.
These lines will be deleted.
After you type \s-2RETURN\s+2 to end the change command,
edit notifies you if more than one line will be changed
and places you in text input mode.
Any text typed on the following lines will be inserted into
the position where lines were deleted by the change command.
.B
You will remain in text input mode until you exit in the usual way,
by typing a period alone on a line.
.R
Note that the number of lines added to the buffer need not be
the same as the number of lines deleted.
.sp 1
.PP
This is the end of the third session on text editing with \s-2UNIX\s0.
.bp
.SH
.ce 1
\s+2Session 4\s0
.sp
.PP
This lesson covers several topics, starting with
commands that apply throughout the buffer,
characters with special meanings,
and how to issue \s-2UNIX\s0 commands while in the editor.
The next topics deal with files:
more on reading and writing,
and methods of recovering files lost in a crash.
The final section suggests sources of further information.
.SH
Making commands global (g)
.PP
One disadvantage to the commands we have used for
searching or substituting is that if you
have a number of instances of a word to change 
it appears that you have to type the command 
repeatedly, once for
each time the change needs to be made.
Edit, however, provides a way to make commands
apply to the entire contents of the buffer \-
the
.B
global (g)
.R
command.
.PP
To print all lines
containing a certain sequence of characters
(say, ``text'')
the command is:
.DS I 1i
:\|\fBg/text/p
.R
.DE
The ``g'' instructs edit to
make a global search for all lines
in the buffer containing the characters  ``text''.
The ``p'' prints the lines found.
.PP
To issue a global command, start by typing a ``g'' and then a search
pattern identifying
the lines to be affected.
Then, on the same line, type the command to be
executed for the identified lines.
Global substitutions are frequently useful.
For example,
to change all instances of the word ``text'' to the word ``material''
the command would be a combination of the global search and the
substitute command:
.DS I 1i
:\|\fBg/text/s/text/material/g
.R
.DE
Note the ``g'' at the end of the global command,
which instructs edit to change
each and every instance of ``text'' to ``material''.
If you do not type the ``g'' at the end of the command
only the
.I first
instance of ``text'' \fIin each line\fR will be changed
(the normal result of the substitute command).
The ``g'' at the end of the command is independent of the ``g''
at the beginning.
You may give a command such as:
.DS I 1i
:\|\fB5s/text/material/g
.R
.DE
to change every instance of ``text'' in line 5 alone.
Further, neither command will change ``text'' to ``material''
if ``Text'' begins with a capital rather than a lower-case
.I t.
.PP
Edit does not automatically print the lines modified by a
global command.
If you want the lines to be printed, type a ``p''
at the end of the global command:
.DS I 1i
:\|\fBg/text/s/text/material/gp
.R
.DE
You should be careful
about using the global command in combination with any other \-
in essence, be sure of what you are telling edit to do
to the entire buffer.
For example,
.DS I 1i
:\|\fBg/ /d
.R
72 less lines in file after global
.DE
will delete every line containing a blank anywhere in it.
This could adversely affect
your document, since most lines have spaces between words
and thus would be deleted.
After executing the global command,
edit will print a warning if the command added or deleted more than one line.
Fortunately, the undo command can reverse
the effects of a global command.
You should experiment with the global command
on a small file of text to see what it can do for you.
.SH
More about searching and substituting
.PP
In using slashes to identify a character string
that we want to search for or change,
we have always specified the exact characters.
There is a less tedious way to
repeat the same string of characters.
To change ``text'' to ``texts'' we may type either
.DS I 1i
:\|\fB/text/s/text/texts/
.R
.DE
as we have done in the past,
or a somewhat abbreviated command:
.DS I 1i
:\|\fB/text/s//texts/
.R
.DE
In this example, the characters to be changed
are not specified \-
there are no characters, not even a space,
between the two slash marks
that indicate what is to be changed.
This lack of characters between the slashes
is taken by the editor to mean
``use the characters we last searched for as the characters to be changed.''
.PP
Similarly, the last context search may be repeated
by typing a pair of slashes with nothing between them:
.DS I 1i
:\|\fB/does/
.R
It doesn't mean much here, but
:\|\fB//
.R
it does illustrate the editor.
.DE
(You should note that the search command found the characters ``does''
in the word ``doesn't'' in the first search request.)
Because no characters are specified for the second search,
the editor scans the buffer for the next occurrence of the
characters ``does''.
.PP
Edit normally searches forward through the buffer,
wrapping around from the end of the buffer to the beginning,
until the specified character string is found.
If you want to search in the reverse direction,
use question marks (?) instead of slashes
to surround the characters you are searching for.
.PP
It is also possible
to repeat the last substitution
without having to retype the entire command.
An ampersand (&) used as a command
repeats the most recent substitute command,
using the same search and replacement patterns.
After altering the current line by typing
.DS I 1i
:\|\fBs/text/texts/
.R
.DE
you type
.DS I 1i
:\|\fB/text/&
.R
.DE
or simply
.DS I 1i
:\|\fB//&
.R
.DE
to make the same change on the next line in the buffer
containing the characters ``text''.
.SH
Special characters
.PP
Two characters have special meanings when
used in specifying searches:  ``$'' and ``^''.
``$'' is taken by the editor to mean ``end of the line''
and is used to identify strings
that occur at the end of a line.
.DS I 1i
:\|\fBg/text.$/s//material./p
.R
.DE
tells the editor to search for all lines ending in ``text.''
(and nothing else, not even a blank space),
to change each final ``text.'' to ``material.'',
and print the changed lines.
.PP
The symbol ``^'' indicates the beginning of a line.
Thus,
.DS I 1i
:\|\fBs/^/1. /
.R
.DE
instructs the editor to insert ``1.'' and a space at the beginning
of the current line.
.PP
The characters ``$'' and ``^'' have special meanings only in the context
of searching.
At other times, they are ordinary characters.
If you ever need to search for a character that has a special meaning,
you must indicate that the
character is to lose temporarily
its special significance by typing another special character,
the backslash (\\), before it.
.DS I 1i
:\|\fBs/\\\\\&$/dollar/
.R
.DE
looks for the character ``$'' in the current
line and replaces it by the word ``dollar''.
Were it not for the backslash, the ``$'' would have represented
``the end of the line'' in your search
rather than the character ``$''.
The backslash retains its special significance
unless it is preceded by another backslash.
.SH
Issuing \s-2UNIX\s0 commands from the editor
.PP
After creating several files with the editor,
you may want to delete files
no longer useful to you or ask for a list of your files.
Removing and listing files are not functions of the editor,
and so they require the use of \s-2UNIX\s0 system commands
(also referred to as ``shell'' commands, as
``shell'' is the name of the program that processes \s-2UNIX\s0 commands).
You do not need to quit the editor to execute a \s-2UNIX\s0 command
as long as you indicate that it
is to be sent to the shell for execution.
To use the \s-2UNIX\s0 command
.B rm
to remove the file named ``junk'' type:
.DS I 1i
:\|\fB!rm junk
.R
!
:
.DE
The exclamation mark (!)
indicates that the rest of the line is to be processed as a shell command.
If the buffer contents have not been written since the last change,
a warning will be printed before the command is executed:
.DS I 1i
[No write since last change]
.DE
The editor prints a ``!'' when the command is completed.
Other tutorials describe useful features of the system,
of which an editor is only one part.
.SH
Filenames and file manipulation
.PP
Throughout each editing session,
edit keeps track of the name of the file being edited as the
.I "current filename."
Edit remembers as the current filename the name given
when you entered the editor.
The current filename changes whenever the edit (e) command
is used to specify a new file.
Once edit has recorded a current filename,
it inserts that name into any command where a filename has been omitted.
If a write command does not specify a file,
edit, as we have seen, supplies the current filename.
If you are editing a file named ``draft3'' having 283 lines in it,
you can have the editor write onto a different file
by including its name in the write command:
.DS I 1i
:\fB\|w chapter3
.R
"chapter3" [new file] 283 lines, 8698 characters
.DE
The current filename remembered by the editor
.I
will not be changed as a result of the write command.
.R
Thus, if the next write command
does not specify a name,
edit will write onto the current file (``draft3'')
and not onto the file ``chapter3''.
.SH
The file (f) command
.PP
To ask for the current filename, type
.B file
(or
.B f ).
In response, the editor provides current information about the buffer,
including the filename, your current position, the number of
lines in the buffer,
and the percent of the distance through the file
your current location is.
.DS I 1i
:\|\fBf
.R
"text" [Modified] line 3 of 4 --75%--
.DE
.\"The expression ``[Edited]'' indicates that the buffer contains
.\"either the editor's copy of the existing file ``text''
.\"or a file which you are just now creating.
If the contents of the buffer have changed
since the last time the file was written,
the editor will tell you that the file has been ``[Modified]''.
After you save the changes by writing onto a disk file,
the buffer will no longer be considered modified:
.DS I 1i
:\|\fBw
.R
"text" 4 lines, 88 characters
:\|\fBf
.R
"text" line 3 of 4 --75%--
.DE
.SH
Reading additional files (r)
.PP
The
\f3read (r)\f1 command allows you to add the contents of a file
to the buffer
at a specified location,
essentially copying new lines
between two existing lines.
To use it, specify the line after which the new text will be placed,
the \f3read (r)\f1 command,
and then the name of the file.
If you have a file named ``example'', the command
.DS I 1i
:\|\fB$r example
.R
"example" 18 lines, 473 characters
.DE
reads the file ``example''
and adds it to the buffer after the last line.
The current filename is not changed by the read command.
.SH
Writing parts of the buffer
.PP
The
.B
write (w)
.R
command can write all or part of the buffer
to a file you specify.
We are already familiar with
writing the entire contents of the
buffer to a disk file.
To write only part of the buffer onto a file,
indicate the beginning and ending lines before the write command,
for example
.DS I 1i
:\|\fB45,$w ending
.R
.DE
Here all lines from 45 through the end of the buffer
are written onto the file named
.I ending.
The lines remain in the buffer
as part of the document you are editing,
and you may continue to edit the entire buffer.
Your original file is unaffected
by your command to write part of the buffer
to another file.
Edit still remembers whether you have saved changes to the buffer
in your original file or not.
.SH
Recovering files
.PP
Although it does not happen very often,
there are times \s-2UNIX\s+2 stops working
because of some malfunction.
This situation is known as a \fIcrash\fR.
Under most circumstances,
edit's crash recovery feature
is able to save work to within a few lines of changes
before a crash (or an accidental phone hang up).
If you lose the contents of an editing buffer in a system crash,
you will normally receive mail when you login that gives
the name of the recovered file.
To recover the file,
enter the editor and type the command
.B recover
(\fBrec\fR),
followed by the name of the lost file.
For example,
to recover the buffer for an edit session
involving the file ``chap6'', the command is:
.DS I 1i
.R
:\|\fBrecover chap6
.R
.DE
Recover is sometimes unable to save the entire buffer successfully,
so always check the contents of the saved buffer carefully
before writing it back onto the original file.
For best results,
write the buffer to a new file temporarily
so you can examine it without risk to the original file.
Unfortunately,
you cannot use the recover command
to retrieve a file you removed
using the shell command \f3rm\f1.
.SH
Other recovery techniques
.PP
If something goes wrong when you are using the editor,
it may be possible to save your work by using the command
.B preserve
(\fBpre\fR),
which saves the buffer as if the system had crashed.
If you are writing a file and you get the message
``Quota exceeded'', you have tried to use more disk storage
than is allotted to your account.
.I
Proceed with caution
.R
because it is likely that only a part
of the editor's buffer is now present in the file you tried to write.
In this case you should use the shell escape from the editor (!)
to remove some files you don't need and try to write
the file again.
If this is not possible and you cannot find someone to help you,
enter the command
.DS I 1i
:\|\fBpreserve
.R
.DE
and wait for the reply,
.DS I 1i
File preserved.
.DE
If you do not receive this reply,
seek help immediately.
Do not simply leave the editor.
If you do, the buffer will be lost, 
and you may not be able to save your file.
If the reply is ``File preserved.''
you can leave the editor
(or logout)
to remedy the situation.
After a preserve, you can use the recover command
once the problem has been corrected,
or the \fB\-r\fR option of the edit command
if you leave the editor and want to return.
.PP
If you make an undesirable change to the buffer
and type a write command before discovering your mistake,
the modified version will replace any previous version of the file.
Should you ever lose a good version of a document in this way,
do not panic and leave the editor.
As long as you stay in the editor,
the contents of the buffer remain accessible.
Depending on the nature of the problem,
it may be possible
to restore the buffer to a more complete
state with the undo command.
After fixing the damaged buffer, you can again write the file
to disk.
.SH
Further reading and other information
.PP
Edit is an editor designed for beginning and casual users.
It is actually a version of a more powerful editor called
.I ex.
These lessons are intended to introduce you to the editor
and its more commonly-used commands.
We have not covered all of the editor's commands,
but a selection of commands
that should be sufficient to accomplish most of your editing tasks.
You can find out more about the editor in the
.I
Ex Reference Manual,
.R
which is applicable to both
.I ex
and
.I edit.
One way to become familiar with the manual is to begin by reading
the description of commands that you already know.
.bd I 3
.SH
Using
.I ex
.fl
.bd I
.PP
As you become more experienced with using the editor,
you may still find that edit continues to meet your needs.
However, should you become interested in using 
.I ex,
it is easy to switch.
To begin an editing session with 
.I ex,
use the name
.B ex
in your command instead of
.B edit.
.PP
Edit commands also work in 
.I ex,
but the editing environment is somewhat different.
You should be aware of a few differences
between 
.I ex
and 
.I edit.
In edit, only the characters ``^'', ``$'', and ``\\'' have
special meanings in searching the buffer
or indicating characters to be changed by a substitute command.
Several additional characters have special
meanings in ex, as described in the
.I
Ex Reference Manual.
.R
Another feature of the edit environment prevents users from
accidently entering two alternative modes of editing,
.I open
and
.I visual,
in which
the editor behaves quite differently from normal command mode.
If you are using ex and you encounter strange behavior,
you may have accidently entered open mode by typing ``o''.
Type the \s-2ESC\s0 key and then a ``Q''
to get out of open or visual mode and back into
the regular editor command mode.
The document
.I
An Introduction to Display Editing with Vi\|\|
.R
provide full details of visual mode.
.bp
.SH
.ce 1
\s+2Index\s0
.LP
.sp 2
.2C
.nf
addressing, \fIsee\fR line numbers
ampersand, 20
append mode, 6-7
append (a) command, 6, 7, 9
``At end of file'' (message), 18
backslash (\\), 21
buffer, 3
caret (^), 10, 20
change (c) command, 18
command mode, 5-6
``Command not found'' (message), 6
context search, 10-12, 19-21
control characters (``^'' notation), 10
control-H, 7
copy (co) command, 15
corrections, 7, 16
current filename, 21
current line (\|.\|), 11, 17
delete (d) command, 15-16
dial-up, 5
disk, 3
documentation, 3, 23
dollar ($), 10, 11, 17, 20-21
dot (\f3\|.\|\f1) 11, 17
edit (text editor), 3, 5, 23
edit (e) command, 5, 9, 14
editing commands:
.in +.25i
append (a), 6, 7, 9
change (c), 18
copy (co), 15
delete (d), 15-16
edit (text editor), 3, 5, 23
edit (e), 5, 9, 14
file (f), 21-22
global (g), 19
move (m), 14-15
number (nu), 11
preserve (pre), 22-23
print (p), 10
quit (q), 8, 13
read (r), 22
recover (rec), 22, 23
substitute (s), 11-12, 19, 20
undo (u), 16-17, 23
write (w), 8, 13, 21, 22
z, 12-13
! (shell escape), 21
$=, 17
+, 17
\-, 17
//, 12, 20
??, 20
\&., 11, 17
\&.=, 11, 17
.in -.25i
entering text, 3, 6-7
erasing
.in +.25i
characters (^H), 7
lines (@), 7
.in -.25i
error corrections, 7, 16
ex (text editor), 23
\fIEx Reference Manual\fR, 23
exclamation (!), 21
file, 3
file (f) command, 21-22
file recovery, 22-23
filename, 3, 21
global (g) command, 19
input mode, 6-7
Interrupt (message), 9
line numbers, \fIsee also\fR current line
.in +.25i
dollar sign ($), 10, 11, 17
dot (\|.\|), 11, 17
relative (+ and \-), 17
.in -.25i
list, 10
logging in, 4-6
logging out, 8
``Login incorrect'' (message), 5
minus (\-), 17
move (m) command, 14-15
``Negative address\(emfirst buffer line is 1'' (message), 18
``No current filename'' (message), 8
``No such file or directory'' (message), 5, 6
``No write since last change'' (message), 21
non-printing characters, 10
``Nonzero address required'' (message), 18
``Not an editor command'' (message), 6
``Not that many lines in buffer'' (message), 18
number (nu) command, 11
password, 5
period (\|.\|), 11, 17
plus (+), 17
preserve (pre) command, 22-23
print (p) command, 10
program, 3
prompts
.in .25i
% (\s-2UNIX\s0), 5
: (edit), 5, 6, 7
\0 (append), 7
.in -.25i
question (?), 20
quit (q) command, 8, 13
read (r) command, 22
recover (rec) command, 22, 23
recovery, \fIsee\fR\| file recovery
references, 3, 23
remove (rm) command, 21, 22
reverse command effects (undo), 16-17, 23
searching, 10-12, 19-21
shell, 21
shell escape (!), 21
slash (/), 11-12, 20
special characters (^, $, \\), 10, 11, 17, 20-21
substitute (s) command, 11-12, 19, 20
terminals, 4-5
text input mode, 7
undo (u) command, 16-17, 23
\s-1UNIX\s0, 3
write (w) command, 8, 13, 21, 22
z command, 12-13

