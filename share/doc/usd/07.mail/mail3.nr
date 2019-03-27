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
.\"	@(#)mail3.nr	8.1 (Berkeley) 6/8/93
.\"
.sh 1 "Maintaining folders"
.pp
.i Mail
includes a simple facility for maintaining groups of messages together
in folders.  This section describes this facility.
.pp
To use the folder facility, you must tell
.i Mail
where you wish to keep your folders.  Each folder of messages will
be a single file.  For convenience, all of your folders are kept in
a single directory of your choosing.  To tell
.i Mail
where your folder directory is, put a line of the form
.(l
set folder=letters
.)l
in your
.i .mailrc
file.  If, as in the example above, your folder directory does not
begin with a `/,'
.i Mail
will assume that your folder directory is to be found starting from
your home directory.  Thus, if your home directory is
.b /home/person
the above example told
.i Mail
to find your folder directory in
.b /home/person/letters .
.pp
Anywhere a file name is expected, you can use a folder name, preceded
with `+.'  For example, to put a message into a folder with the
.b save
command, you can use:
.(l
save +classwork
.)l
to save the current message in the
.i classwork
folder.  If the
.i classwork
folder does not yet exist, it will be created.  Note that messages
which are saved with the
.b save
command are automatically removed from your system mailbox.
.pp
In order to make a copy of a message in a folder without causing
that message to be removed from your system mailbox, use the
.b copy
command, which is identical in all other respects to the
.b save
command.  For example,
.(l
copy +classwork
.)l
copies the current message into the
.i classwork
folder and leaves a copy in your system mailbox.
.pp
The
.b folder
command
can be used to direct
.i Mail
to the contents of a different folder.
For example,
.(l
folder +classwork
.)l
directs
.i Mail
to read the contents of the
.i classwork
folder.  All of the commands that you can use on your system
mailbox are also applicable to folders, including
.b type ,
.b delete ,
and
.b reply .
To inquire which folder you are currently editing, use simply:
.(l
folder
.)l
.pp
To list your current set of folders, use the
.b folders
command.
.pp
To start
.i Mail
reading one of your folders, you can use the
.b \-f
option described in section 2.  For example:
.(l
% Mail \-f +classwork
.)l
will cause
.i Mail
to read your
.i classwork
folder without looking at your system mailbox.
