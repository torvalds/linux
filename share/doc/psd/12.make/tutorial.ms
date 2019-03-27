.\" Copyright (c) 1988, 1989 by Adam de Boor
.\" Copyright (c) 1989 by Berkeley Softworks
.\" Copyright (c) 1988, 1989, 1993
.\"	The Regents of the University of California.  All rights reserved.
.\"
.\" This code is derived from software contributed to Berkeley by
.\" Adam de Boor.
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
.\"	@(#)tutorial.ms	8.1 (Berkeley) 8/18/93
.\" $FreeBSD$
.\"
.EH 'PSD:12-%''PMake \*- A Tutorial'
.OH 'PMake \*- A Tutorial''PSD:12-%'
.\" xH is a macro to provide numbered headers that are automatically stuffed
.\" into a table-of-contents, properly indented, etc. If the first argument
.\" is numeric, it is taken as the depth for numbering (as for .NH), else
.\" the default (1) is assumed.
.\"
.\" @P The initial paragraph distance.
.\" @Q The piece of section number to increment (or 0 if none given)
.\" @R Section header.
.\" @S Indent for toc entry
.\" @T Argument to NH (can't use @Q b/c giving 0 to NH resets the counter)
.de xH
.NH \\$1
\\$2 \\$3 \\$4 \\$5 \\$6 \\$7 \\$8 \\$9
.nr PD .1v
.XS \\n%
.ta 0.6i
\\*(SN	\\$2 \\$3 \\$4 \\$5 \\$6 \\$7 \\$8 \\$9
.XE
.nr PD .3v
..
.ig
.\" CW is used to place a string in fixed-width or switch to a
.\" fixed-width font.
.\" C is a typewriter font for a laserwriter. Use something else if
.\" you don't have one...
.de CW
.ie !\\n(.$ .ft S
.el \&\\$3\fS\\$1\fP\\$2
..
.\" Anything I put in a display I want to be in fixed-width
.am DS
.CW
..
.\" The stuff in .No produces a little stop sign in the left margin
.\" that says NOTE in it. Unfortunately, it does cause a break, but
.\" hey. Can't have everything. In case you're wondering how I came
.\" up with such weird commands, they came from running grn on a
.\" gremlin file...
.de No
.br
.ne 0.5i
.po -0.5i
.br
.mk 
.nr g3 \\n(.f
.nr g4 \\n(.s
.ig ft
.sp -1
.\" .st cf
\D's -1u'\D't 5u'
.sp -1
\h'50u'\D'l 71u 0u'\D'l 50u 50u'\D'l 0u 71u'\D'l -50u 50u'\D'l -71u 0u'\D'l -50u -50u'\D'l 0u -71u'\D'l 50u -50u'
.sp -1
\D't 3u'
.sp -1
.sp 7u
\h'53u'\D'p 14 68u 0u 46u 46u 0u 68u -46u 46u -68u 0u -47u -46u 0u -68u 47u -46u'
.sp -1
.ft R
.ps 6
.nr g8 \\n(.d
.ds g9 "NOTE
.sp 74u
\h'85u'\v'0.85n'\h-\w\\*(g9u/2u\&\\*(g9
.sp |\\n(g8u
.sp 166u
.ig br
\D't 3u'\D's -1u'
.br
.po
.rt 
.ft \\n(g3
.ps \\n(g4
..
.de Bp
.ie !\\n(.$ .IP \(bu 2
.el .IP "\&" 2
..
.po +.3i
.TL
PMake \*- A Tutorial
.AU
Adam de Boor
.AI
Berkeley Softworks
2150 Shattuck Ave, Penthouse
Berkeley, CA 94704
adam@bsw.uu.net
\&...!uunet!bsw!adam
.FS
Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appears in all copies.
The University of California, Berkeley Softworks, and Adam de Boor make no
representations about the suitability of this software for any
purpose.  It is provided "as is" without express or implied warranty.
.FE
.PP
.xH 1 Introduction
.LP
PMake is a program for creating other programs, or anything else you
can think of for it to do.  The basic idea behind PMake is that, for
any given system, be it a program or a document or whatever, there
will be some files that depend on the state of other files (on when
they were last modified). PMake takes these dependencies, which you
must specify, and uses them to build whatever it is you want it to
build.
.LP
PMake is almost fully-compatible with Make, with which you may already
be familiar. PMake's most important feature is its ability to run
several different jobs at once, making the creation of systems
considerably faster. It also has a great deal more functionality than
Make. Throughout the text, whenever something is mentioned that is an
important difference between PMake and Make (i.e.  something that will
cause a makefile to fail if you don't do something about it), or is
simply important, it will be flagged with a little sign in the left
margin, like this:
.No
.LP
This tutorial is divided into three main sections corresponding to basic,
intermediate and advanced PMake usage. If you already know Make well,
you will only need to skim chapter 2 (there are some aspects of
PMake that I consider basic to its use that didn't exist in Make).
Things in chapter 3 make life much easier, while those in chapter 4
are strictly for those who know what they are doing. Chapter 5 has
definitions for the jargon I use and chapter 6 contains possible
solutions to the problems presented throughout the tutorial.
.xH 1 The Basics of PMake
.LP
PMake takes as input a file that tells a) which files depend on which
other files to be complete and b) what to do about files that are
``out-of-date.'' This file is known as a ``makefile'' and is usually
.Ix 0 def makefile
kept in the top-most directory of the system to be built. While you
can call the makefile anything you want, PMake will look for
.CW Makefile
and
.CW makefile
(in that order) in the current directory if you don't tell it
otherwise.
.Ix 0 def makefile default
To specify a different makefile, use the
.B \-f
flag (e.g.
.CW "pmake -f program.mk" ''). ``
.Ix 0 ref flags -f
.Ix 0 ref makefile other
.LP
A makefile has four different types of lines in it:
.RS
.IP \(bu 2
File dependency specifications
.IP \(bu 2
Creation commands
.IP \(bu 2
Variable assignments
.IP \(bu 2
Comments, include statements and conditional directives
.RE
.LP
Any line may be continued over multiple lines by ending it with a
backslash.
.Ix 0 def "continuation line"
The backslash, following newline and any initial whitespace
on the following line are compressed into a single space before the
input line is examined by PMake.
.xH 2 Dependency Lines
.LP
As mentioned in the introduction, in any system, there are
dependencies between the files that make up the system.  For instance,
in a program made up of several C source files and one header file,
the C files will need to be re-compiled should the header file be
changed. For a document of several chapters and one macro file, the
chapters will need to be reprocessed if any of the macros changes.
.Ix 0 def "dependency"
These are dependencies and are specified by means of dependency lines in
the makefile.
.LP
.Ix 0 def "dependency line"
On a dependency line, there are targets and sources, separated by a
one- or two-character operator.
The targets ``depend'' on the sources and are usually created from
them.
.Ix 0 def target
.Ix 0 def source
.Ix 0 ref operator
Any number of targets and sources may be specified on a dependency line.
All the targets in the line are made to depend on all the sources.
Targets and sources need not be actual files, but every source must be
either an actual file or another target in the makefile.
If you run out of room, use a backslash at the end of the line to continue onto
the next one.
.LP
Any file may be a target and any file may be a source, but the
relationship between the two (or however many) is determined by the
``operator'' that separates them.
.Ix 0 def operator
Three types of operators exist: one specifies that the datedness of a
target is determined by the state of its sources, while another
specifies other files (the sources) that need to be dealt with before
the target can be re-created. The third operator is very similar to
the first, with the additional condition that the target is
out-of-date if it has no sources. These operations are represented by
the colon, the exclamation point and the double-colon, respectively, and are
mutually exclusive. Their exact semantics are as follows:
.IP ":"
.Ix 0 def operator colon
.Ix 0 def :
If a colon is used, a target on the line is considered to be
``out-of-date'' (and in need of creation) if 
.RS
.IP \(bu 2
any of the sources has been modified more recently than the target, or
.IP \(bu 2
the target doesn't exist.
.RE
.Ix 0 def out-of-date
.IP "\&"
Under this operation, steps will be taken to re-create the target only
if it is found to be out-of-date by using these two rules.
.IP "!"
.Ix 0 def operator force
.Ix 0 def !
If an exclamation point is used, the target will always be re-created,
but this will not happen until all of its sources have been examined
and re-created, if necessary.
.IP "::"
.Ix 0 def operator double-colon
.Ix 0 def ::
If a double-colon is used, a target is out-of-date if:
.RS
.IP \(bu 2
any of the sources has been modified more recently than the target, or
.IP \(bu 2
the target doesn't exist, or
.IP \(bu 2
the target has no sources.
.RE
.IP "\&"
If the target is out-of-date according to these rules, it will be re-created.
This operator also does something else to the targets, but I'll go
into that in the next section (``Shell Commands'').
.LP
Enough words, now for an example. Take that C program I mentioned
earlier. Say there are three C files
.CW a.c , (
.CW b.c
and
.CW  c.c )
each of which
includes the file
.CW defs.h .
The dependencies between the files could then be expressed as follows:
.DS
program         : a.o b.o c.o
a.o b.o c.o     : defs.h
a.o             : a.c
b.o             : b.c
c.o             : c.c
.DE
.LP
You may be wondering at this point, where
.CW a.o ,
.CW b.o
and
.CW c.o
came in and why
.I they
depend on
.CW defs.h
and the C files don't. The reason is quite simple:
.CW program
cannot be made by linking together .c files \*- it must be
made from .o files. Likewise, if you change
.CW defs.h ,
it isn't the .c files that need to be re-created, it's the .o files.
If you think of dependencies in these terms \*- which files (targets)
need to be created from which files (sources) \*- you should have no problems.
.LP
An important thing to notice about the above example, is that all the
\&.o files appear as targets on more than one line. This is perfectly
all right: the target is made to depend on all the sources mentioned
on all the dependency lines. E.g.
.CW a.o
depends on both
.CW defs.h
and
.CW a.c .
.Ix 0 ref dependency
.No
.LP
The order of the dependency lines in the makefile is
important: the first target on the first dependency line in the
makefile will be the one that gets made if you don't say otherwise.
That's why
.CW program
comes first in the example makefile, above.
.LP
Both targets and sources may contain the standard C-Shell wildcard
characters
.CW { , (
.CW } ,
.CW * ,
.CW ? ,
.CW [ ,
and
.CW ] ),
but the non-curly-brace ones may only appear in the final component
(the file portion) of the target or source. The characters mean the
following things:
.IP \fB{}\fP
These enclose a comma-separated list of options and cause the pattern
to be expanded once for each element of the list. Each expansion
contains a different element. For example, 
.CW src/{whiffle,beep,fish}.c
expands to the three words
.CW src/whiffle.c ,
.CW src/beep.c ,
and 
.CW src/fish.c .
These braces may be nested and, unlike the other wildcard characters,
the resulting words need not be actual files. All other wildcard
characters are expanded using the files that exist when PMake is
started.
.IP \fB*\fP
This matches zero or more characters of any sort. 
.CW src/*.c
will expand to the same three words as above as long as 
.CW src
contains those three files (and no other files that end in 
.CW .c ).
.IP \fB?\fP
Matches any single character.
.IP \fB[]\fP
This is known as a character class and contains either a list of
single characters, or a series of character ranges 
.CW a-z , (
for example means all characters between a and z), or both. It matches
any single character contained in the list. E.g.
.CW [A-Za-z]
will match all letters, while
.CW [0123456789]
will match all numbers.
.xH 2 Shell Commands
.LP
``Isn't that nice,'' you say to yourself, ``but how are files
actually `re-created,' as he likes to spell it?''
The re-creation is accomplished by commands you place in the makefile.
These commands are passed to the Bourne shell (better known as
``/bin/sh'') to be executed and are
.Ix 0 ref shell
.Ix 0 ref re-creation
.Ix 0 ref update
expected to do what's necessary to update the target file (PMake
doesn't actually check to see if the target was created. It just
assumes it's there).
.Ix 0 ref target
.LP
Shell commands in a makefile look a lot like shell commands you would
type at a terminal, with one important exception: each command in a
makefile
.I must
be preceded by at least one tab.
.LP
Each target has associated with it a shell script made up of
one or more of these shell commands. The creation script for a target
should immediately follow the dependency line for that target. While
any given target may appear on more than one dependency line, only one
of these dependency lines may be followed by a creation script, unless
the `::' operator was used on the dependency line.
.Ix 0 ref operator double-colon
.Ix 0 ref ::
.No
.LP
If the double-colon was used, each dependency line for the target
may be followed by a shell script. That script will only be executed
if the target on the associated dependency line is out-of-date with
respect to the sources on that line, according to the rules I gave
earlier.
I'll give you a good example of this later on.
.LP
To expand on the earlier makefile, you might add commands as follows:
.DS
program         : a.o b.o c.o
        cc a.o b.o c.o \-o program
a.o b.o c.o     : defs.h
a.o             : a.c
        cc \-c a.c
b.o             : b.c
        cc \-c b.c
c.o             : c.c
        cc \-c c.c
.DE
.LP
Something you should remember when writing a makefile is, the
commands will be executed if the
.I target
on the dependency line is out-of-date, not the sources.
.Ix 0 ref target
.Ix 0 ref source
.Ix 0 ref out-of-date
In this example, the command
.CW "cc \-c a.c" '' ``
will be executed if
.CW a.o
is out-of-date. Because of the `:' operator,
.Ix 0 ref :
.Ix 0 ref operator colon
this means that should
.CW a.c
.I or
.CW defs.h
have been modified more recently than
.CW a.o ,
the command will be executed
.CW a.o "\&" (
will be considered out-of-date).
.Ix 0 ref out-of-date
.LP
Remember how I said the only difference between a makefile shell
command and a regular shell command was the leading tab? I lied. There
is another way in which makefile commands differ from regular ones.
The first two characters after the initial whitespace are treated
specially.
If they are any combination of `@' and `\-', they cause PMake to do
different things.
.LP
In most cases, shell commands are printed before they're
actually executed. This is to keep you informed of what's going on. If
an `@' appears, however, this echoing is suppressed. In the case of an
.CW echo
command, say
.CW "echo Linking index" ,'' ``
it would be
rather silly to see
.DS
echo Linking index
Linking index
.DE
.LP
so PMake allows you to place an `@' before the command
.CW "@echo Linking index" '') (``
to prevent the command from being printed.
.LP
The other special character is the `\-'. In case you didn't know,
shell commands finish with a certain ``exit status.'' This status is
made available by the operating system to whatever program invoked the
command. Normally this status will be 0 if everything went ok and
non-zero if something went wrong. For this reason, PMake will consider
an error to have occurred if one of the shells it invokes returns a non-zero
status. When it detects an error, PMake's usual action is to abort
whatever it's doing and exit with a non-zero status itself (any other
targets that were being created will continue being made, but nothing
new will be started. PMake will exit after the last job finishes).
This behavior can be altered, however, by placing a `\-' at the front
of a command
.CW "\-mv index index.old" ''), (``
certain command-line arguments,
or doing other things, to be detailed later. In such
a case, the non-zero status is simply ignored and PMake keeps chugging
along.
.No
.LP
Because all the commands are given to a single shell to execute, such
things as setting shell variables, changing directories, etc., last
beyond the command in which they are found. This also allows shell
compound commands (like
.CW for
loops) to be entered in a natural manner.
Since this could cause problems for some makefiles that depend on
each command being executed by a single shell, PMake has a
.B \-B
.Ix 0 ref compatibility
.Ix 0 ref flags -B
flag (it stands for backwards-compatible) that forces each command to
be given to a separate shell. It also does several other things, all
of which I discourage since they are now old-fashioned.\|.\|.\|.
.No
.LP
A target's shell script is fed to the shell on its (the shell's) input stream.
This means that any commands, such as
.CW ci
that need to get input from the terminal won't work right \*- they'll
get the shell's input, something they probably won't find to their
liking. A simple way around this is to give a command like this:
.DS
ci $(SRCS) < /dev/tty
.DE
This would force the program's input to come from the terminal. If you
can't do this for some reason, your only other alternative is to use
PMake in its fullest compatibility mode. See 
.B Compatibility
in chapter 4.
.Ix 0 ref compatibility
.LP
.xH 2 Variables
.LP
PMake, like Make before it, has the ability to save text in variables
to be recalled later at your convenience. Variables in PMake are used
much like variables in the shell and, by tradition, consist of
all upper-case letters (you don't
.I have
to use all upper-case letters.
In fact there's nothing to stop you from calling a variable
.CW @^&$%$ .
Just tradition). Variables are assigned-to using lines of the form
.Ix 0 def variable assignment
.DS
VARIABLE = value
.DE
.Ix 0 def variable assignment
appended-to by
.DS
VARIABLE += value
.DE
.Ix 0 def variable appending
.Ix 0 def variable assignment appended
.Ix 0 def +=
conditionally assigned-to (if the variable isn't already defined) by
.DS
VARIABLE ?= value
.DE
.Ix 0 def variable assignment conditional
.Ix 0 def ?=
and assigned-to with expansion (i.e. the value is expanded (see below)
before being assigned to the variable\*-useful for placing a value at
the beginning of a variable, or other things) by
.DS
VARIABLE := value
.DE
.Ix 0 def variable assignment expanded
.Ix 0 def :=
.LP
Any whitespace before
.I value
is stripped off. When appending, a space is placed between the old
value and the stuff being appended.
.LP
The final way a variable may be assigned to is using
.DS
VARIABLE != shell-command
.DE
.Ix 0 def variable assignment shell-output
.Ix 0 def !=
In this case, 
.I shell-command
has all its variables expanded (see below) and is passed off to a
shell to execute. The output of the shell is then placed in the
variable. Any newlines (other than the final one) are replaced by
spaces before the assignment is made. This is typically used to find
the current directory via a line like:
.DS
CWD             != pwd
.DE
.LP
.B Note:
this is intended to be used to execute commands that produce small amounts
of output (e.g. ``pwd''). The implementation is less than intelligent and will
likely freeze if you execute something that produces thousands of
bytes of output (8 Kb is the limit on many UNIX systems).
.LP
The value of a variable may be retrieved by enclosing the variable
name in parentheses or curly braces and preceding the whole thing
with a dollar sign.
.LP
For example, to set the variable CFLAGS to the string
.CW "\-I/sprite/src/lib/libc \-O" ,'' ``
you would place a line
.DS
CFLAGS = \-I/sprite/src/lib/libc \-O
.DE
in the makefile and use the word
.CW "$(CFLAGS)"
wherever you would like the string
.CW "\-I/sprite/src/lib/libc \-O"
to appear. This is called variable expansion.
.Ix 0 def variable expansion
.No
.LP
Unlike Make, PMake will not expand a variable unless it knows
the variable exists. E.g. if you have a
.CW "${i}"
in a shell command and you have not assigned a value to the variable
.CW i 
(the empty string is considered a value, by the way), where Make would have
substituted the empty string, PMake will leave the
.CW "${i}"
alone.
To keep PMake from substituting for a variable it knows, precede the
dollar sign with another dollar sign.
(e.g. to pass
.CW "${HOME}"
to the shell, use
.CW "$${HOME}" ).
This causes PMake, in effect, to expand the
.CW $
macro, which expands to a single
.CW $ .
For compatibility, Make's style of variable expansion will be used
if you invoke PMake with any of the compatibility flags (\c
.B \-V ,
.B \-B
or
.B \-M .
The
.B \-V
flag alters just the variable expansion).
.Ix 0 ref flags -V
.Ix 0 ref flags -B
.Ix 0 ref flags -M
.Ix 0 ref compatibility
.LP
.Ix 0 ref variable expansion
There are two different times at which variable expansion occurs:
When parsing a dependency line, the expansion occurs immediately
upon reading the line. If any variable used on a dependency line is
undefined, PMake will print a message and exit.
Variables in shell commands are expanded when the command is
executed.
Variables used inside another variable are expanded whenever the outer
variable is expanded (the expansion of an inner variable has no effect
on the outer variable. I.e. if the outer variable is used on a dependency
line and in a shell command, and the inner variable changes value
between when the dependency line is read and the shell command is
executed, two different values will be substituted for the outer
variable).
.Ix 0 def variable types
.LP
Variables come in four flavors, though they are all expanded the same
and all look about the same. They are (in order of expanding scope):
.RS
.IP \(bu 2
Local variables.
.Ix 0 ref variable local
.IP \(bu 2
Command-line variables.
.Ix 0 ref variable command-line
.IP \(bu 2
Global variables.
.Ix 0 ref variable global
.IP \(bu 2
Environment variables.
.Ix 0 ref variable environment
.RE
.LP
The classification of variables doesn't matter much, except that the
classes are searched from the top (local) to the bottom (environment)
when looking up a variable. The first one found wins.
.xH 3 Local Variables
.LP
.Ix 0 def variable local
Each target can have as many as seven local variables. These are
variables that are only ``visible'' within that target's shell script
and contain such things as the target's name, all of its sources (from
all its dependency lines), those sources that were out-of-date, etc.
Four local variables are defined for all targets. They are:
.RS
.IP ".TARGET"
.Ix 0 def variable local .TARGET
.Ix 0 def .TARGET
The name of the target.
.IP ".OODATE"
.Ix 0 def variable local .OODATE
.Ix 0 def .OODATE
The list of the sources for the target that were considered out-of-date.
The order in the list is not guaranteed to be the same as the order in
which the dependencies were given.
.IP ".ALLSRC"
.Ix 0 def variable local .ALLSRC
.Ix 0 def .ALLSRC
The list of all sources for this target in the order in which they
were given.
.IP ".PREFIX"
.Ix 0 def variable local .PREFIX
.Ix 0 def .PREFIX
The target without its suffix and without any leading path. E.g. for
the target
.CW ../../lib/compat/fsRead.c ,
this variable would contain
.CW fsRead .
.RE
.LP
Three other local variables are set only for certain targets under
special circumstances. These are the ``.IMPSRC,''
.Ix 0 ref variable local .IMPSRC
.Ix 0 ref .IMPSRC
``.ARCHIVE,''
.Ix 0 ref variable local .ARCHIVE
.Ix 0 ref .ARCHIVE
and ``.MEMBER''
.Ix 0 ref variable local .MEMBER
.Ix 0 ref .MEMBER
variables. When they are set and how they are used is described later.
.LP
Four of these variables may be used in sources as well as in shell
scripts.
.Ix 0 def "dynamic source"
.Ix 0 def source dynamic
These are ``.TARGET'', ``.PREFIX'', ``.ARCHIVE'' and ``.MEMBER''. The
variables in the sources are expanded once for each target on the
dependency line, providing what is known as a ``dynamic source,''
.Rd 0
allowing you to specify several dependency lines at once. For example,
.DS
$(OBJS)         : $(.PREFIX).c
.DE
will create a dependency between each object file and its
corresponding C source file.
.xH 3 Command-line Variables
.LP
.Ix 0 def variable command-line
Command-line variables are set when PMake is first invoked by giving a
variable assignment as one of the arguments. For example,
.DS
pmake "CFLAGS = -I/sprite/src/lib/libc -O"
.DE
would make 
.CW CFLAGS
be a command-line variable with the given value. Any assignments to
.CW CFLAGS
in the makefile will have no effect, because once it
is set, there is (almost) nothing you can do to change a command-line
variable (the search order, you see). Command-line variables may be
set using any of the four assignment operators, though only
.CW =
and
.CW ?=
behave as you would expect them to, mostly because assignments to
command-line variables are performed before the makefile is read, thus
the values set in the makefile are unavailable at the time.
.CW +=
.Ix 0 ref +=
.Ix 0 ref variable assignment appended
is the same as
.CW = ,
because the old value of the variable is sought only in the scope in
which the assignment is taking place (for reasons of efficiency that I
won't get into here).
.CW :=
and
.CW ?=
.Ix 0 ref :=
.Ix 0 ref ?=
.Ix 0 ref variable assignment expanded
.Ix 0 ref variable assignment conditional
will work if the only variables used are in the environment.
.CW !=
is sort of pointless to use from the command line, since the same
effect can no doubt be accomplished using the shell's own command
substitution mechanisms (backquotes and all that).
.xH 3 Global Variables
.LP
.Ix 0 def variable global
Global variables are those set or appended-to in the makefile.
There are two classes of global variables: those you set and those PMake sets.
As I said before, the ones you set can have any name you want them to have,
except they may not contain a colon or an exclamation point.
The variables PMake sets (almost) always begin with a
period and always contain upper-case letters, only. The variables are
as follows:
.RS
.IP .PMAKE
.Ix 0 def variable global .PMAKE
.Ix 0 def .PMAKE
.Ix 0 def variable global MAKE
.Ix 0 def MAKE
The name by which PMake was invoked is stored in this variable. For
compatibility, the name is also stored in the MAKE variable.
.IP .MAKEFLAGS
.Ix 0 def variable global .MAKEFLAGS
.Ix 0 def .MAKEFLAGS variable
.Ix 0 def variable global MFLAGS
.Ix 0 def MFLAGS
All the relevant flags with which PMake was invoked. This does not
include such things as
.B \-f
or variable assignments. Again for compatibility, this value is stored
in the MFLAGS variable as well.
.RE
.LP
Two other variables, ``.INCLUDES'' and ``.LIBS,'' are covered in the
section on special targets in chapter 3.
.Ix 0 ref variable global .INCLUDES
.Ix 0 ref variable global .LIBS
.LP
Global variables may be deleted using lines of the form:
.Ix 0 def #undef
.Ix 0 def variable deletion
.DS
#undef \fIvariable\fP
.DE
The
.CW # ' `
must be the first character on the line. Note that this may only be
done on global variables.
.xH 3 Environment Variables
.LP
.Ix 0 def variable environment
Environment variables are passed by the shell that invoked PMake and
are given by PMake to each shell it invokes. They are expanded like
any other variable, but they cannot be altered in any way.
.LP
One special environment variable,
.CW PMAKE ,
.Ix 0 def variable environment PMAKE
is examined by PMake for command-line flags, variable assignments,
etc., it should always use. This variable is examined before the
actual arguments to PMake are. In addition, all flags given to PMake,
either through the
.CW PMAKE
variable or on the command line, are placed in this environment
variable and exported to each shell PMake executes. Thus recursive
invocations of PMake automatically receive the same flags as the
top-most one.
.LP
Using all these variables, you can compress the sample makefile even more:
.DS
OBJS            = a.o b.o c.o
program         : $(OBJS)
        cc $(.ALLSRC) \-o $(.TARGET)
$(OBJS)         : defs.h
a.o             : a.c
        cc \-c a.c
b.o             : b.c
        cc \-c b.c
c.o             : c.c
        cc \-c c.c
.DE
.Ix 0 ref variable local .ALLSRC
.Ix 0 ref .ALLSRC
.Ix 0 ref variable local .TARGET
.Ix 0 ref .TARGET
.Rd 3
.xH 2 Comments
.LP
.Ix 0 def comments
Comments in a makefile start with a `#' character and extend to the
end of the line. They may appear
anywhere you want them, except in a shell command (though the shell
will treat it as a comment, too). If, for some reason, you need to use the `#'
in a variable or on a dependency line, put a backslash in front of it.
PMake will compress the two into a single `#' (Note: this isn't true
if PMake is operating in full-compatibility mode).
.Ix 0 ref flags -M
.Ix 0 ref compatibility
.xH 2 Parallelism
.No
.LP
PMake was specifically designed to re-create several targets at once,
when possible. You do not have to do anything special to cause this to
happen (unless PMake was configured to not act in parallel, in which
case you will have to make use of the
.B \-L
and
.B \-J
flags (see below)),
.Ix 0 ref flags -L
.Ix 0 ref flags -J
but you do have to be careful at times.
.LP
There are several problems you are likely to encounter. One is
that some makefiles (and programs) are written in such a way that it is
impossible for two targets to be made at once. The program
.CW xstr ,
for example,
always modifies the files
.CW strings
and
.CW x.c .
There is no way to change it. Thus you cannot run two of them at once
without something being trashed. Similarly, if you have commands
in the makefile that always send output to the same file, you will not
be able to make more than one target at once unless you change the
file you use. You can, for instance, add a
.CW $$$$
to the end of the file name to tack on the process ID of the shell
executing the command (each
.CW $$
expands to a single
.CW $ ,
thus giving you the shell variable
.CW $$ ).
Since only one shell is used for all the
commands, you'll get the same file name for each command in the
script.
.LP
The other problem comes from improperly-specified dependencies that
worked in Make because of its sequential, depth-first way of examining
them. While I don't want to go into depth on how PMake
works (look in chapter 4 if you're interested), I will warn you that
files in two different ``levels'' of the dependency tree may be
examined in a different order in PMake than they were in Make. For
example, given the makefile
.DS
a               : b c
b               : d
.DE
PMake will examine the targets in the order
.CW c ,
.CW d ,
.CW b ,
.CW a .
If the makefile's author expected PMake to abort before making
.CW c
if an error occurred while making
.CW b ,
or if
.CW b
needed to exist before
.CW c
was made,
s/he will be sorely disappointed. The dependencies are
incomplete, since in both these cases,
.CW c
would depend on
.CW b .
So watch out.
.LP
Another problem you may face is that, while PMake is set up to handle the
output from multiple jobs in a graceful fashion, the same is not so for input.
It has no way to regulate input to different jobs,
so if you use the redirection from
.CW /dev/tty
I mentioned earlier, you must be careful not to run two of the jobs at once.
.xH 2 Writing and Debugging a Makefile
.LP
Now you know most of what's in a makefile, what do you do next? There
are two choices: (1) use one of the uncommonly-available makefile
generators or (2) write your own makefile (I leave out the third choice of
ignoring PMake and doing everything by hand as being beyond the bounds
of common sense).
.LP
When faced with the writing of a makefile, it is usually best to start
from first principles: just what
.I are
you trying to do? What do you want the makefile finally to produce?
.LP
To begin with a somewhat traditional example, let's say you need to
write a makefile to create a program,
.CW expr ,
that takes standard infix expressions and converts them to prefix form (for
no readily apparent reason). You've got three source files, in C, that
make up the program:
.CW main.c ,
.CW parse.c ,
and
.CW output.c .
Harking back to my pithy advice about dependency lines, you write the
first line of the file:
.DS
expr            : main.o parse.o output.o
.DE
because you remember
.CW expr
is made from
.CW .o
files, not
.CW .c
files. Similarly for the
.CW .o
files you produce the lines:
.DS
main.o          : main.c
parse.o         : parse.c
output.o        : output.c
main.o parse.o output.o : defs.h
.DE
.LP
Great. You've now got the dependencies specified. What you need now is
commands. These commands, remember, must produce the target on the
dependency line, usually by using the sources you've listed.
You remember about local variables? Good, so it should come
to you as no surprise when you write
.DS
expr            : main.o parse.o output.o
        cc -o $(.TARGET) $(.ALLSRC)
.DE
Why use the variables? If your program grows to produce postfix
expressions too (which, of course, requires a name change or two), it
is one fewer place you have to change the file. You cannot do this for
the object files, however, because they depend on their corresponding
source files
.I and
.CW defs.h ,
thus if you said
.DS
	cc -c $(.ALLSRC)
.DE
you'd get (for
.CW main.o ):
.DS
	cc -c main.c defs.h
.DE
which is wrong. So you round out the makefile with these lines:
.DS
main.o          : main.c
        cc -c main.c
parse.o         : parse.c
        cc -c parse.c
output.o        : output.c
        cc -c output.c
.DE
.LP
The makefile is now complete and will, in fact, create the program you
want it to without unnecessary compilations or excessive typing on
your part. There are two things wrong with it, however (aside from it
being altogether too long, something I'll address in chapter 3):
.IP 1)
The string
.CW "main.o parse.o output.o" '' ``
is repeated twice, necessitating two changes when you add postfix
(you were planning on that, weren't you?). This is in direct violation
of de Boor's First Rule of writing makefiles:
.QP
.I
Anything that needs to be written more than once
should be placed in a variable.
.IP "\&"
I cannot emphasize this enough as being very important to the
maintenance of a makefile and its program.
.IP 2)
There is no way to alter the way compilations are performed short of
editing the makefile and making the change in all places. This is evil
and violates de Boor's Second Rule, which follows directly from the
first:
.QP
.I
Any flags or programs used inside a makefile should be placed in a variable so
they may be changed, temporarily or permanently, with the greatest ease.
.LP
The makefile should more properly read:
.DS
OBJS            = main.o parse.o output.o
expr            : $(OBJS)
        $(CC) $(CFLAGS) -o $(.TARGET) $(.ALLSRC)
main.o          : main.c
        $(CC) $(CFLAGS) -c main.c
parse.o         : parse.c
        $(CC) $(CFLAGS) -c parse.c
output.o        : output.c
        $(CC) $(CFLAGS) -c output.c
$(OBJS)         : defs.h
.DE
Alternatively, if you like the idea of dynamic sources mentioned in
section 2.3.1,
.Rm 0 2.3.1
.Rd 4
.Ix 0 ref "dynamic source"
.Ix 0 ref source dynamic
you could write it like this:
.DS
OBJS            = main.o parse.o output.o
expr            : $(OBJS)
        $(CC) $(CFLAGS) -o $(.TARGET) $(.ALLSRC)
$(OBJS)         : $(.PREFIX).c defs.h
        $(CC) $(CFLAGS) -c $(.PREFIX).c
.DE
These two rules and examples lead to de Boor's First Corollary:
.QP
.I
Variables are your friends.
.LP
Once you've written the makefile comes the sometimes-difficult task of
.Ix 0 ref debugging
making sure the darn thing works. Your most helpful tool to make sure
the makefile is at least syntactically correct is the
.B \-n
.Ix 0 ref flags -n
flag, which allows you to see if PMake will choke on the makefile. The
second thing the
.B \-n
flag lets you do is see what PMake would do without it actually doing
it, thus you can make sure the right commands would be executed were
you to give PMake its head.
.LP
When you find your makefile isn't behaving as you hoped, the first
question that comes to mind (after ``What time is it, anyway?'') is
``Why not?'' In answering this, two flags will serve you well:
.CW "-d m" '' ``
.Ix 0 ref flags -d
and
.CW "-p 2" .'' ``
.Ix 0 ref flags -p
The first causes PMake to tell you as it examines each target in the
makefile and indicate why it is deciding whatever it is deciding. You
can then use the information printed for other targets to see where
you went wrong. The
.CW "-p 2" '' ``
flag makes PMake print out its internal state when it is done,
allowing you to see that you forgot to make that one chapter depend on
that file of macros you just got a new version of. The output from
.CW "-p 2" '' ``
is intended to resemble closely a real makefile, but with additional
information provided and with variables expanded in those commands
PMake actually printed or executed.
.LP
Something to be especially careful about is circular dependencies.
.Ix 0 def dependency circular
E.g.
.DS
a		: b
b		: c d
d		: a
.DE
In this case, because of how PMake works,
.CW c
is the only thing PMake will examine, because
.CW d
and
.CW a
will effectively fall off the edge of the universe, making it
impossible to examine
.CW b
(or them, for that matter).
PMake will tell you (if run in its normal mode) all the targets
involved in any cycle it looked at (i.e. if you have two cycles in the
graph (naughty, naughty), but only try to make a target in one of
them, PMake will only tell you about that one. You'll have to try to
make the other to find the second cycle). When run as Make, it will
only print the first target in the cycle.
.xH 2 Invoking PMake
.LP
.Ix 0 ref flags
.Ix 0 ref arguments
.Ix 0 ref usage
PMake comes with a wide variety of flags to choose from.
They may appear in any order, interspersed with command-line variable
assignments and targets to create.
The flags are as follows:
.IP "\fB\-d\fP \fIwhat\fP"
.Ix 0 def flags -d
.Ix 0 ref debugging
This causes PMake to spew out debugging information that
may prove useful to you. If you can't
figure out why PMake is doing what it's doing, you might try using
this flag. The
.I what
parameter is a string of single characters that tell PMake what
aspects you are interested in. Most of what I describe will make
little sense to you, unless you've dealt with Make before. Just
remember where this table is and come back to it as you read on.
The characters and the information they produce are as follows:
.RS
.IP a
Archive searching and caching.
.IP c
Conditional evaluation.
.IP d
The searching and caching of directories.
.IP j
Various snippets of information related to the running of the multiple
shells. Not particularly interesting.
.IP m
The making of each target: what target is being examined; when it was
last modified; whether it is out-of-date; etc.
.IP p
Makefile parsing.
.IP r
Remote execution.
.IP s
The application of suffix-transformation rules. (See chapter 3)
.IP t
The maintenance of the list of targets.
.IP v
Variable assignment.
.RE
.IP "\&"
Of these all, the
.CW m
and
.CW s
letters will be most useful to you.
If the
.B \-d
is the final argument or the argument from which it would get these
key letters (see below for a note about which argument would be used)
begins with a
.B \- ,
all of these debugging flags will be set, resulting in massive amounts
of output.
.IP "\fB\-f\fP \fImakefile\fP"
.Ix 0 def flags -f
Specify a makefile to read different from the standard makefiles
.CW Makefile "\&" (
or
.CW makefile ).
.Ix 0 ref makefile default
.Ix 0 ref makefile other
If
.I makefile
is ``\-'', PMake uses the standard input. This is useful for making
quick and dirty makefiles.\|.\|.
.Ix 0 ref makefile "quick and dirty"
.IP \fB\-h\fP
.Ix 0 def flags -h
Prints out a summary of the various flags PMake accepts. It can also
be used to find out what level of concurrency was compiled into the
version of PMake you are using (look at
.B \-J
and
.B \-L )
and various other information on how PMake was configured.
.Ix 0 ref configuration
.Ix 0 ref makefilesystem
.IP \fB\-i\fP
.Ix 0 def flags -i
If you give this flag, PMake will ignore non-zero status returned
by any of its shells. It's like placing a `\-' before all the commands
in the makefile.
.IP \fB\-k\fP
.Ix 0 def flags -k
This is similar to
.B \-i
in that it allows PMake to continue when it sees an error, but unlike
.B \-i ,
where PMake continues blithely as if nothing went wrong,
.B \-k
causes it to recognize the error and only continue work on those
things that don't depend on the target, either directly or indirectly (through
depending on something that depends on it), whose creation returned the error.
The `k' is for ``keep going''.\|.\|.
.Ix 0 ref target
.IP \fB\-l\fP
.Ix 0 def flags -l
PMake has the ability to lock a directory against other
people executing it in the same directory (by means of a file called
``LOCK.make'' that it creates and checks for in the directory). This
is a Good Thing because two people doing the same thing in the same place
can be disastrous for the final product (too many cooks and all that).
Whether this locking is the default is up to your system
administrator. If locking is on,
.B \-l
will turn it off, and vice versa. Note that this locking will not
prevent \fIyou\fP from invoking PMake twice in the same place \*- if
you own the lock file, PMake will warn you about it but continue to execute.
.IP "\fB\-m\fP \fIdirectory\fP"
.Ix 0 def flags -m
Tells PMake another place to search for included makefiles via the <...>
style.  Several
.B \-m
options can be given to form a search path.  If this construct is used the
default system makefile search path is completely overridden.
To be explained in chapter 3, section 3.2.
.Rm 2 3.2
.IP \fB\-n\fP
.Ix 0 def flags -n
This flag tells PMake not to execute the commands needed to update the
out-of-date targets in the makefile. Rather, PMake will simply print
the commands it would have executed and exit. This is particularly
useful for checking the correctness of a makefile. If PMake doesn't do
what you expect it to, it's a good chance the makefile is wrong.
.IP "\fB\-p\fP \fInumber\fP"
.Ix 0 def flags -p
.Ix 0 ref debugging
This causes PMake to print its input in a reasonable form, though
not necessarily one that would make immediate sense to anyone but me. The
.I number
is a bitwise-or of 1 and 2 where 1 means it should print the input
before doing any processing and 2 says it should print it after
everything has been re-created. Thus
.CW "\-p 3"
would print it twice\*-once before processing and once after (you
might find the difference between the two interesting). This is mostly
useful to me, but you may find it informative in some bizarre circumstances.
.IP \fB\-q\fP
.Ix 0 def flags -q
If you give PMake this flag, it will not try to re-create anything. It
will just see if anything is out-of-date and exit non-zero if so.
.IP \fB\-r\fP
.Ix 0 def flags -r
When PMake starts up, it reads a default makefile that tells it what
sort of system it's on and gives it some idea of what to do if you
don't tell it anything. I'll tell you about it in chapter 3. If you
give this flag, PMake won't read the default makefile.
.IP \fB\-s\fP
.Ix 0 def flags -s
This causes PMake to not print commands before they're executed. It
is the equivalent of putting an `@' before every command in the
makefile.
.IP \fB\-t\fP
.Ix 0 def flags -t
Rather than try to re-create a target, PMake will simply ``touch'' it
so as to make it appear up-to-date. If the target didn't exist before,
it will when PMake finishes, but if the target did exist, it will
appear to have been updated.
.IP \fB\-v\fP
.Ix 0 def flags -v
This is a mixed-compatibility flag intended to mimic the System V
version of Make. It is the same as giving
.B \-B ,
and
.B \-V
as well as turning off directory locking. Targets can still be created
in parallel, however. This is the mode PMake will enter if it is
invoked either as
.CW smake '' ``
or
.CW vmake ''. ``
.IP \fB\-x\fP
.Ix 0 def flags -x
This tells PMake it's ok to export jobs to other machines, if they're
available. It is used when running in Make mode, as exporting in this
mode tends to make things run slower than if the commands were just
executed locally.
.IP \fB\-B\fP
.Ix 0 ref compatibility
.Ix 0 def flags -B
Forces PMake to be as backwards-compatible with Make as possible while
still being itself.
This includes:
.RS
.IP \(bu 2
Executing one shell per shell command
.IP \(bu 2
Expanding anything that looks even vaguely like a variable, with the
empty string replacing any variable PMake doesn't know.
.IP \(bu 2
Refusing to allow you to escape a `#' with a backslash.
.IP \(bu 2
Permitting undefined variables on dependency lines and conditionals
(see below). Normally this causes PMake to abort.
.RE
.IP \fB\-C\fP
.Ix 0 def flags -C
This nullifies any and all compatibility mode flags you may have given
or implied up to the time the
.B \-C
is encountered. It is useful mostly in a makefile that you wrote for PMake
to avoid bad things happening when someone runs PMake as
.CW make '' ``
or has things set in the environment that tell it to be compatible.
.B \-C
is
.I not
placed in the
.CW PMAKE
environment variable or the
.CW .MAKEFLAGS
or
.CW MFLAGS
global variables.
.Ix 0 ref variable environment PMAKE
.Ix 0 ref variable global .MAKEFLAGS
.Ix 0 ref variable global MFLAGS
.Ix 0 ref .MAKEFLAGS variable
.Ix 0 ref MFLAGS
.IP "\fB\-D\fP \fIvariable\fP"
.Ix 0 def flags -D
Allows you to define a variable to have 
.CW 1 '' ``
as its value.  The variable is a global variable, not a command-line
variable. This is useful mostly for people who are used to the C
compiler arguments and those using conditionals, which I'll get into
in section 4.3
.Rm 1 4.3
.IP "\fB\-I\fP \fIdirectory\fP"
.Ix 0 def flags -I
Tells PMake another place to search for included makefiles. Yet
another thing to be explained in chapter 3 (section 3.2, to be
precise).
.Rm 2 3.2
.IP "\fB\-J\fP \fInumber\fP"
.Ix 0 def flags -J
Gives the absolute maximum number of targets to create at once on both
local and remote machines.
.IP "\fB\-L\fP \fInumber\fP"
.Ix 0 def flags -L
This specifies the maximum number of targets to create on the local
machine at once. This may be 0, though you should be wary of doing
this, as PMake may hang until a remote machine becomes available, if
one is not available when it is started.
.IP \fB\-M\fP
.Ix 0 ref compatibility
.Ix 0 def flags -M
This is the flag that provides absolute, complete, full compatibility
with Make. It still allows you to use all but a few of the features of
PMake, but it is non-parallel. This is the mode PMake enters if you
call it
.CW make .'' ``
.IP \fB\-P\fP
.Ix 0 def flags -P
.Ix 0 ref "output control"
When creating targets in parallel, several shells are executing at
once, each wanting to write its own two cent's-worth to the screen.
This output must be captured by PMake in some way in order to prevent
the screen from being filled with garbage even more indecipherable
than you usually see. PMake has two ways of doing this, one of which
provides for much cleaner output and a clear separation between the
output of different jobs, the other of which provides a more immediate
response so one can tell what is really happening. The former is done
by notifying you when the creation of a target starts, capturing the
output and transferring it to the screen all at once when the job
finishes. The latter is done by catching the output of the shell (and
its children) and buffering it until an entire line is received, then
printing that line preceded by an indication of which job produced
the output. Since I prefer this second method, it is the one used by
default. The first method will be used if you give the
.B \-P
flag to PMake.
.IP \fB\-V\fP
.Ix 0 def flags -V
As mentioned before, the
.B \-V
flag tells PMake to use Make's style of expanding variables,
substituting the empty string for any variable it doesn't know.
.IP \fB\-W\fP
.Ix 0 def flags -W
There are several times when PMake will print a message at you that is
only a warning, i.e. it can continue to work in spite of your having
done something silly (such as forgotten a leading tab for a shell
command). Sometimes you are well aware of silly things you have done
and would like PMake to stop bothering you. This flag tells it to shut
up about anything non-fatal.
.IP \fB\-X\fP
.Ix 0 def flags -X
This flag causes PMake to not attempt to export any jobs to another
machine.
.LP
Several flags may follow a single `\-'. Those flags that require
arguments take them from successive parameters. E.g.
.DS
pmake -fDnI server.mk DEBUG /chip2/X/server/include
.DE
will cause PMake to read
.CW server.mk
as the input makefile, define the variable
.CW DEBUG
as a global variable and look for included makefiles in the directory
.CW /chip2/X/server/include .
.xH 2 Summary
.LP
A makefile is made of four types of lines:
.RS
.IP \(bu 2
Dependency lines
.IP \(bu 2
Creation commands
.IP \(bu 2
Variable assignments
.IP \(bu 2
Comments, include statements and conditional directives
.RE
.LP
A dependency line is a list of one or more targets, an operator
.CW : ', (`
.CW :: ', `
or
.CW ! '), `
and a list of zero or more sources. Sources may contain wildcards and
certain local variables.
.LP
A creation command is a regular shell command preceded by a tab. In
addition, if the first two characters after the tab (and other
whitespace) are a combination of
.CW @ ' `
or
.CW - ', `
PMake will cause the command to not be printed (if the character is
.CW @ ') `
or errors from it to be ignored (if
.CW - '). `
A blank line, dependency line or variable assignment terminates a
creation script. There may be only one creation script for each target
with a
.CW : ' `
or
.CW ! ' `
operator.
.LP
Variables are places to store text. They may be unconditionally
assigned-to using the
.CW = ' `
.Ix 0 ref =
.Ix 0 ref variable assignment
operator, appended-to using the
.CW += ' `
.Ix 0 ref +=
.Ix 0 ref variable assignment appended
operator, conditionally (if the variable is undefined) assigned-to
with the
.CW ?= ' `
.Ix 0 ref ?=
.Ix 0 ref variable assignment conditional
operator, and assigned-to with variable expansion with the
.CW := ' `
.Ix 0 ref :=
.Ix 0 ref variable assignment expanded
operator. The output of a shell command may be assigned to a variable
using the
.CW != ' `
.Ix 0 ref !=
.Ix 0 ref variable assignment shell-output
operator.  Variables may be expanded (their value inserted) by enclosing
their name in parentheses or curly braces, preceded by a dollar sign.
A dollar sign may be escaped with another dollar sign. Variables are
not expanded if PMake doesn't know about them. There are seven local
variables:
.CW .TARGET ,
.CW .ALLSRC ,
.CW .OODATE ,
.CW .PREFIX ,
.CW .IMPSRC ,
.CW .ARCHIVE ,
and
.CW .MEMBER .
Four of them
.CW .TARGET , (
.CW .PREFIX ,
.CW .ARCHIVE ,
and
.CW .MEMBER )
may be used to specify ``dynamic sources.''
.Ix 0 ref "dynamic source"
.Ix 0 ref source dynamic
Variables are good. Know them. Love them. Live them.
.LP
Debugging of makefiles is best accomplished using the
.B \-n ,
.B "\-d m" ,
and
.B "\-p 2"
flags.
.xH 2 Exercises
.ce
\s+4\fBTBA\fP\s0
.xH 1 Short-cuts and Other Nice Things
.LP
Based on what I've told you so far, you may have gotten the impression
that PMake is just a way of storing away commands and making sure you
don't forget to compile something. Good. That's just what it is.
However, the ways I've described have been inelegant, at best, and
painful, at worst.
This chapter contains things that make the
writing of makefiles easier and the makefiles themselves shorter and
easier to modify (and, occasionally, simpler). In this chapter, I
assume you are somewhat more
familiar with Sprite (or UNIX, if that's what you're using) than I did
in chapter 2, just so you're on your toes.
So without further ado...
.xH 2 Transformation Rules
.LP
As you know, a file's name consists of two parts: a base name, which
gives some hint as to the contents of the file, and a suffix, which
usually indicates the format of the file.
Over the years, as
.UX
has developed,
naming conventions, with regard to suffixes, have also developed that have
become almost as incontrovertible as Law. E.g. a file ending in
.CW .c
is assumed to contain C source code; one with a
.CW .o
suffix is assumed to be a compiled, relocatable object file that may
be linked into any program; a file with a
.CW .ms
suffix is usually a text file to be processed by Troff with the \-ms
macro package, and so on.
One of the best aspects of both Make and PMake comes from their
understanding of how the suffix of a file pertains to its contents and
their ability to do things with a file based solely on its suffix. This
ability comes from something known as a transformation rule. A
transformation rule specifies how to change a file with one suffix
into a file with another suffix.
.LP
A transformation rule looks much like a dependency line, except the
target is made of two known suffixes stuck together. Suffixes are made
known to PMake by placing them as sources on a dependency line whose
target is the special target
.CW .SUFFIXES .
E.g.
.DS
\&.SUFFIXES       : .o .c
\&.c.o            :
        $(CC) $(CFLAGS) -c $(.IMPSRC)
.DE
The creation script attached to the target is used to transform a file with
the first suffix (in this case,
.CW .c )
into a file with the second suffix (here,
.CW .o ).
In addition, the target inherits whatever attributes have been applied
to the transformation rule.
The simple rule given above says that to transform a C source file
into an object file, you compile it using
.CW cc
with the
.CW \-c
flag.
This rule is taken straight from the system makefile. Many
transformation rules (and suffixes) are defined there, and I refer you
to it for more examples (type
.CW "pmake -h" '' ``
to find out where it is).
.LP
There are several things to note about the transformation rule given
above:
.RS
.IP 1)
The
.CW .IMPSRC 
variable.
.Ix 0 def variable local .IMPSRC
.Ix 0 def .IMPSRC
This variable is set to the ``implied source'' (the file from which
the target is being created; the one with the first suffix), which, in this
case, is the .c file.
.IP 2)
The
.CW CFLAGS
variable. Almost all of the transformation rules in the system
makefile are set up using variables that you can alter in your
makefile to tailor the rule to your needs. In this case, if you want
all your C files to be compiled with the
.B \-g
flag, to provide information for
.CW dbx ,
you would set the
.CW CFLAGS
variable to contain
.CW -g
.CW "CFLAGS = -g" '') (``
and PMake would take care of the rest.
.RE
.LP
To give you a quick example, the makefile in 2.3.4 
.Rm 3 2.3.4
could be changed to this:
.DS
OBJS            = a.o b.o c.o
program         : $(OBJS)
        $(CC) -o $(.TARGET) $(.ALLSRC)
$(OBJS)         : defs.h
.DE
The transformation rule I gave above takes the place of the 6 lines\**
.FS
This is also somewhat cleaner, I think, than the dynamic source
solution presented in 2.6
.FE
.Rm 4 2.6
.DS
a.o             : a.c
        cc -c a.c
b.o             : b.c
        cc -c b.c
c.o             : c.c
        cc -c c.c
.DE
.LP
Now you may be wondering about the dependency between the
.CW .o
and
.CW .c
files \*- it's not mentioned anywhere in the new makefile. This is
because it isn't needed: one of the effects of applying a
transformation rule is the target comes to depend on the implied
source. That's why it's called the implied
.I source .
.LP
For a more detailed example. Say you have a makefile like this:
.DS
a.out           : a.o b.o
        $(CC) $(.ALLSRC)
.DE
and a directory set up like this:
.DS
total 4
-rw-rw-r--  1 deboor         34 Sep  7 00:43 Makefile
-rw-rw-r--  1 deboor        119 Oct  3 19:39 a.c
-rw-rw-r--  1 deboor        201 Sep  7 00:43 a.o
-rw-rw-r--  1 deboor         69 Sep  7 00:43 b.c
.DE
While just typing
.CW pmake '' ``
will do the right thing, it's much more informative to type
.CW "pmake -d s" ''. ``
This will show you what PMake is up to as it processes the files. In
this case, PMake prints the following:
.DS
Suff_FindDeps (a.out)
	using existing source a.o
	applying .o -> .out to "a.o"
Suff_FindDeps (a.o)
	trying a.c...got it
	applying .c -> .o to "a.c"
Suff_FindDeps (b.o)
	trying b.c...got it
	applying .c -> .o to "b.c"
Suff_FindDeps (a.c)
	trying a.y...not there
	trying a.l...not there
	trying a.c,v...not there
	trying a.y,v...not there
	trying a.l,v...not there
Suff_FindDeps (b.c)
	trying b.y...not there
	trying b.l...not there
	trying b.c,v...not there
	trying b.y,v...not there
	trying b.l,v...not there
--- a.o ---
cc  -c a.c
--- b.o ---
cc  -c b.c
--- a.out ---
cc a.o b.o
.DE
.LP
.CW Suff_FindDeps
is the name of a function in PMake that is called to check for implied
sources for a target using transformation rules.
The transformations it tries are, naturally
enough, limited to the ones that have been defined (a transformation
may be defined multiple times, by the way, but only the most recent
one will be used). You will notice, however, that there is a definite
order to the suffixes that are tried. This order is set by the
relative positions of the suffixes on the
.CW .SUFFIXES
line \*- the earlier a suffix appears, the earlier it is checked as
the source of a transformation. Once a suffix has been defined, the
only way to change its position in the pecking order is to remove all
the suffixes (by having a
.CW .SUFFIXES
dependency line with no sources) and redefine them in the order you
want. (Previously-defined transformation rules will be automatically
redefined as the suffixes they involve are re-entered.)
.LP
Another way to affect the search order is to make the dependency
explicit. In the above example,
.CW a.out
depends on
.CW a.o
and
.CW b.o .
Since a transformation exists from
.CW .o
to
.CW .out ,
PMake uses that, as indicated by the
.CW "using existing source a.o" '' ``
message.
.LP
The search for a transformation starts from the suffix of the target
and continues through all the defined transformations, in the order
dictated by the suffix ranking, until an existing file with the same
base (the target name minus the suffix and any leading directories) is
found. At that point, one or more transformation rules will have been
found to change the one existing file into the target.
.LP
For example, ignoring what's in the system makefile for now, say you
have a makefile like this:
.DS
\&.SUFFIXES       : .out .o .c .y .l
\&.l.c            :
        lex $(.IMPSRC)
        mv lex.yy.c $(.TARGET)
\&.y.c            :
        yacc $(.IMPSRC)
        mv y.tab.c $(.TARGET)
\&.c.o            :
        cc -c $(.IMPSRC)
\&.o.out          :
        cc -o $(.TARGET) $(.IMPSRC)
.DE
and the single file
.CW jive.l .
If you were to type
.CW "pmake -rd ms jive.out" ,'' ``
you would get the following output for
.CW jive.out :
.DS
Suff_FindDeps (jive.out)
	trying jive.o...not there
	trying jive.c...not there
	trying jive.y...not there
	trying jive.l...got it
	applying .l -> .c to "jive.l"
	applying .c -> .o to "jive.c"
	applying .o -> .out to "jive.o"
.DE
and this is why: PMake starts with the target
.CW jive.out ,
figures out its suffix
.CW .out ) (
and looks for things it can transform to a
.CW .out
file. In this case, it only finds
.CW .o ,
so it looks for the file
.CW jive.o .
It fails to find it, so it looks for transformations into a
.CW .o
file. Again it has only one choice:
.CW .c .
So it looks for
.CW jive.c
and, as you know, fails to find it. At this point it has two choices:
it can create the
.CW .c
file from either a
.CW .y
file or a
.CW .l
file. Since
.CW .y
came first on the
.CW .SUFFIXES
line, it checks for
.CW jive.y
first, but can't find it, so it looks for
.CW jive.l
and, lo and behold, there it is.
At this point, it has defined a transformation path as follows:
.CW .l
\(->
.CW .c
\(->
.CW .o
\(->
.CW .out
and applies the transformation rules accordingly. For completeness,
and to give you a better idea of what PMake actually did with this
three-step transformation, this is what PMake printed for the rest of
the process:
.DS
Suff_FindDeps (jive.o)
	using existing source jive.c
	applying .c -> .o to "jive.c"
Suff_FindDeps (jive.c)
	using existing source jive.l
	applying .l -> .c to "jive.l"
Suff_FindDeps (jive.l)
Examining jive.l...modified 17:16:01 Oct 4, 1987...up-to-date
Examining jive.c...non-existent...out-of-date
--- jive.c ---
lex jive.l
\&.\|.\|. meaningless lex output deleted .\|.\|.
mv lex.yy.c jive.c
Examining jive.o...non-existent...out-of-date
--- jive.o ---
cc -c jive.c
Examining jive.out...non-existent...out-of-date
--- jive.out ---
cc -o jive.out jive.o
.DE
.LP
One final question remains: what does PMake do with targets that have
no known suffix? PMake simply pretends it actually has a known suffix
and searches for transformations accordingly.
The suffix it chooses is the source for the
.CW .NULL
.Ix 0 ref .NULL
target mentioned later. In the system makefile, 
.CW .out
is chosen as the ``null suffix''
.Ix 0 def suffix null
.Ix 0 def "null suffix"
because most people use PMake to create programs. You are, however,
free and welcome to change it to a suffix of your own choosing.
The null suffix is ignored, however, when PMake is in compatibility
mode (see chapter 4).
.xH 2 Including Other Makefiles
.Ix 0 def makefile inclusion
.Rd 2
.LP
Just as for programs, it is often useful to extract certain parts of a
makefile into another file and just include it in other makefiles
somehow. Many compilers allow you say something like
.DS
#include "defs.h"
.DE
to include the contents of
.CW defs.h
in the source file. PMake allows you to do the same thing for
makefiles, with the added ability to use variables in the filenames.
An include directive in a makefile looks either like this:
.DS
#include <file>
.DE
or this
.DS
#include "file"
.DE
The difference between the two is where PMake searches for the file:
the first way, PMake will look for
the file only in the system makefile directory (or directories)
(to find out what that directory is, give PMake the
.B \-h
flag).
.Ix 0 ref flags -h
The system makefile directory search path can be overridden via the
.B \-m
option.
.Ix 0 ref flags -m
For files in double-quotes, the search is more complex:
.RS
.IP 1)
The directory of the makefile that's including the file.
.IP 2)
The current directory (the one in which you invoked PMake).
.IP 3)
The directories given by you using
.B \-I
flags, in the order in which you gave them.
.IP 4)
Directories given by
.CW .PATH
dependency lines (see chapter 4).
.IP 5)
The system makefile directory.
.RE
.LP
in that order.
.LP
You are free to use PMake variables in the filename\*-PMake will
expand them before searching for the file. You must specify the
searching method with either angle brackets or double-quotes
.I outside
of a variable expansion. I.e. the following
.DS
SYSTEM	= <command.mk>

#include $(SYSTEM)
.DE
won't work.
.xH 2 Saving Commands
.LP
.Ix 0 def ...
There may come a time when you will want to save certain commands to
be executed when everything else is done. For instance: you're
making several different libraries at one time and you want to create the
members in parallel. Problem is,
.CW ranlib
is another one of those programs that can't be run more than once in
the same directory at the same time (each one creates a file called
.CW __.SYMDEF
into which it stuffs information for the linker to use. Two of them
running at once will overwrite each other's file and the result will
be garbage for both parties). You might want a way to save the ranlib
commands til the end so they can be run one after the other, thus
keeping them from trashing each other's file. PMake allows you to do
this by inserting an ellipsis (``.\|.\|.'') as a command between
commands to be run at once and those to be run later.
.LP
So for the
.CW ranlib
case above, you might do this:
.Rd 5
.DS
lib1.a          : $(LIB1OBJS)
        rm -f $(.TARGET)
        ar cr $(.TARGET) $(.ALLSRC)
        ...
        ranlib $(.TARGET)

lib2.a          : $(LIB2OBJS)
        rm -f $(.TARGET)
        ar cr $(.TARGET) $(.ALLSRC)
        ...
        ranlib $(.TARGET)
.DE
.Ix 0 ref variable local .TARGET
.Ix 0 ref variable local .ALLSRC
This would save both
.DS
ranlib $(.TARGET)
.DE
commands until the end, when they would run one after the other
(using the correct value for the
.CW .TARGET
variable, of course).
.LP
Commands saved in this manner are only executed if PMake manages to
re-create everything without an error.
.xH 2 Target Attributes
.LP
PMake allows you to give attributes to targets by means of special
sources. Like everything else PMake uses, these sources begin with a
period and are made up of all upper-case letters. There are various
reasons for using them, and I will try to give examples for most of
them. Others you'll have to find uses for yourself. Think of it as ``an
exercise for the reader.'' By placing one (or more) of these as a source on a
dependency line, you are ``marking the target(s) with that
attribute.'' That's just the way I phrase it, so you know.
.LP
Any attributes given as sources for a transformation rule are applied
to the target of the transformation rule when the rule is applied.
.Ix 0 def attributes
.Ix 0 ref source
.Ix 0 ref target
.nr pw 12
.IP .DONTCARE \n(pw
.Ix 0 def attributes .DONTCARE
.Ix 0 def .DONTCARE
If a target is marked with this attribute and PMake can't figure out
how to create it, it will ignore this fact and assume the file isn't
really needed or actually exists and PMake just can't find it. This may prove
wrong, but the error will be noted later on, not when PMake tries to create
the target so marked. This attribute also prevents PMake from
attempting to touch the target if it is given the
.B \-t
flag.
.Ix 0 ref flags -t
.IP .EXEC \n(pw
.Ix 0 def attributes .EXEC
.Ix 0 def .EXEC
This attribute causes its shell script to be executed while having no
effect on targets that depend on it. This makes the target into a sort
of subroutine.  An example. Say you have some LISP files that need to
be compiled and loaded into a LISP process. To do this, you echo LISP
commands into a file and execute a LISP with this file as its input
when everything's done. Say also that you have to load other files
from another system before you can compile your files and further,
that you don't want to go through the loading and dumping unless one
of
.I your
files has changed. Your makefile might look a little bit
like this (remember, this is an educational example, and don't worry
about the
.CW COMPILE
rule, all will soon become clear, grasshopper):
.DS
system          : init a.fasl b.fasl c.fasl
        for i in $(.ALLSRC);
        do
                echo -n '(load "' >> input
                echo -n ${i} >> input
                echo '")' >> input
        done
        echo '(dump "$(.TARGET)")' >> input
        lisp < input

a.fasl          : a.l init COMPILE
b.fasl          : b.l init COMPILE
c.fasl          : c.l init COMPILE
COMPILE         : .USE
        echo '(compile "$(.ALLSRC)")' >> input
init            : .EXEC
        echo '(load-system)' > input
.DE
.Ix 0 ref .USE
.Ix 0 ref attributes .USE
.Ix 0 ref variable local .ALLSRC
.IP "\&"
.CW .EXEC
sources, don't appear in the local variables of targets that depend on
them (nor are they touched if PMake is given the
.B \-t
flag).
.Ix 0 ref flags -t
Note that all the rules, not just that for
.CW system ,
include
.CW init
as a source. This is because none of the other targets can be made
until
.CW init
has been made, thus they depend on it.
.IP .EXPORT \n(pw
.Ix 0 def attributes .EXPORT
.Ix 0 def .EXPORT
This is used to mark those targets whose creation should be sent to
another machine if at all possible. This may be used by some
exportation schemes if the exportation is expensive. You should ask
your system administrator if it is necessary.
.IP .EXPORTSAME \n(pw
.Ix 0 def attributes .EXPORTSAME
.Ix 0 def .EXPORTSAME
Tells the export system that the job should be exported to a machine
of the same architecture as the current one. Certain operations (e.g.
running text through
.CW nroff )
can be performed the same on any architecture (CPU and
operating system type), while others (e.g. compiling a program with
.CW cc )
must be performed on a machine with the same architecture. Not all
export systems will support this attribute.
.IP .IGNORE \n(pw
.Ix 0 def attributes .IGNORE
.Ix 0 def .IGNORE attribute
Giving a target the
.CW .IGNORE
attribute causes PMake to ignore errors from any of the target's commands, as
if they all had `\-' before them.
.IP .INVISIBLE \n(pw
.Ix 0 def attributes .INVISIBLE
.Ix 0 def .INVISIBLE
This allows you to specify one target as a source for another without
the one affecting the other's local variables. Useful if, say, you
have a makefile that creates two programs, one of which is used to
create the other, so it must exist before the other is created. You
could say
.DS
prog1           : $(PROG1OBJS) prog2 MAKEINSTALL
prog2           : $(PROG2OBJS) .INVISIBLE MAKEINSTALL
.DE
where
.CW MAKEINSTALL
is some complex .USE rule (see below) that depends on the
.Ix 0 ref .USE
.CW .ALLSRC
variable containing the right things. Without the
.CW .INVISIBLE
attribute for
.CW prog2 ,
the
.CW MAKEINSTALL
rule couldn't be applied. This is not as useful as it should be, and
the semantics may change (or the whole thing go away) in the
not-too-distant future.
.IP .JOIN \n(pw
.Ix 0 def attributes .JOIN
.Ix 0 def .JOIN
This is another way to avoid performing some operations in parallel
while permitting everything else to be done so. Specifically it
forces the target's shell script to be executed only if one or more of the
sources was out-of-date. In addition, the target's name,
in both its
.CW .TARGET
variable and all the local variables of any target that depends on it,
is replaced by the value of its
.CW .ALLSRC
variable.
As an example, suppose you have a program that has four libraries that
compile in the same directory along with, and at the same time as, the
program. You again have the problem with
.CW ranlib
that I mentioned earlier, only this time it's more severe: you
can't just put the ranlib off to the end since the program
will need those libraries before it can be re-created. You can do
something like this:
.DS
program         : $(OBJS) libraries
        cc -o $(.TARGET) $(.ALLSRC)

libraries       : lib1.a lib2.a lib3.a lib4.a .JOIN
        ranlib $(.OODATE)
.DE
.Ix 0 ref variable local .TARGET
.Ix 0 ref variable local .ALLSRC
.Ix 0 ref variable local .OODATE
.Ix 0 ref .TARGET
.Ix 0 ref .ALLSRC
.Ix 0 ref .OODATE
In this case, PMake will re-create the
.CW $(OBJS)
as necessary, along with
.CW lib1.a ,
.CW lib2.a ,
.CW lib3.a
and
.CW lib4.a .
It will then execute
.CW ranlib
on any library that was changed and set
.CW program 's
.CW .ALLSRC
variable to contain what's in
.CW $(OBJS)
followed by
.CW "lib1.a lib2.a lib3.a lib4.a" .'' ``
In case you're wondering, it's called
.CW .JOIN
because it joins together different threads of the ``input graph'' at
the target marked with the attribute.
Another aspect of the .JOIN attribute is it keeps the target from
being created if the
.B \-t
flag was given.
.Ix 0 ref flags -t
.IP .MAKE \n(pw
.Ix 0 def attributes .MAKE
.Ix 0 def .MAKE
The
.CW .MAKE
attribute marks its target as being a recursive invocation of PMake.
This forces PMake to execute the script associated with the target (if
it's out-of-date) even if you gave the
.B \-n
or
.B \-t
flag. By doing this, you can start at the top of a system and type
.DS
pmake -n
.DE
and have it descend the directory tree (if your makefiles are set up
correctly), printing what it would have executed if you hadn't
included the
.B \-n
flag.
.IP .NOEXPORT \n(pw
.Ix 0 def attributes .NOEXPORT
.Ix 0 def .NOEXPORT attribute
If possible, PMake will attempt to export the creation of all targets to
another machine (this depends on how PMake was configured). Sometimes,
the creation is so simple, it is pointless to send it to another
machine. If you give the target the
.CW .NOEXPORT
attribute, it will be run locally, even if you've given PMake the
.B "\-L 0"
flag.
.IP .NOTMAIN \n(pw
.Ix 0 def attributes .NOTMAIN
.Ix 0 def .NOTMAIN
Normally, if you do not specify a target to make in any other way,
PMake will take the first target on the first dependency line of a
makefile as the target to create. That target is known as the ``Main
Target'' and is labeled as such if you print the dependencies out
using the
.B \-p
flag.
.Ix 0 ref flags -p
Giving a target this attribute tells PMake that the target is
definitely
.I not
the Main Target.
This allows you to place targets in an included makefile and
have PMake create something else by default.
.IP .PRECIOUS \n(pw
.Ix 0 def attributes .PRECIOUS
.Ix 0 def .PRECIOUS attribute
When PMake is interrupted (you type control-C at the keyboard), it
will attempt to clean up after itself by removing any half-made
targets. If a target has the
.CW .PRECIOUS
attribute, however, PMake will leave it alone. An additional side
effect of the `::' operator is to mark the targets as
.CW .PRECIOUS .
.Ix 0 ref operator double-colon
.Ix 0 ref ::
.IP .SILENT \n(pw
.Ix 0 def attributes .SILENT
.Ix 0 def .SILENT attribute
Marking a target with this attribute keeps its commands from being
printed when they're executed, just as if they had an `@' in front of them.
.IP .USE \n(pw
.Ix 0 def attributes .USE
.Ix 0 def .USE
By giving a target this attribute, you turn it into PMake's equivalent
of a macro. When the target is used as a source for another target,
the other target acquires the commands, sources and attributes (except
.CW .USE )
of the source.
If the target already has commands, the
.CW .USE
target's commands are added to the end. If more than one .USE-marked
source is given to a target, the rules are applied sequentially.
.IP "\&" \n(pw
The typical .USE rule (as I call them) will use the sources of the
target to which it is applied (as stored in the
.CW .ALLSRC
variable for the target) as its ``arguments,'' if you will.
For example, you probably noticed that the commands for creating
.CW lib1.a
and
.CW lib2.a
in the example in section 3.3
.Rm 5 3.3
were exactly the same. You can use the
.CW .USE
attribute to eliminate the repetition, like so:
.DS
lib1.a          : $(LIB1OBJS) MAKELIB
lib2.a          : $(LIB2OBJS) MAKELIB

MAKELIB         : .USE
        rm -f $(.TARGET)
        ar cr $(.TARGET) $(.ALLSRC)
        ...
        ranlib $(.TARGET)
.DE
.Ix 0 ref variable local .TARGET
.Ix 0 ref variable local .ALLSRC
.IP "\&" \n(pw
Several system makefiles (not to be confused with The System Makefile)
make use of these  .USE rules to make your
life easier (they're in the default, system makefile directory...take a look).
Note that the .USE rule source itself
.CW MAKELIB ) (
does not appear in any of the targets's local variables.
There is no limit to the number of times I could use the
.CW MAKELIB
rule. If there were more libraries, I could continue with
.CW "lib3.a : $(LIB3OBJS) MAKELIB" '' ``
and so on and so forth.
.xH 2 Special Targets
.LP
As there were in Make, so there are certain targets that have special
meaning to PMake. When you use one on a dependency line, it is the
only target that may appear on the left-hand-side of the operator.
.Ix 0 ref target
.Ix 0 ref operator
As for the attributes and variables, all the special targets
begin with a period and consist of upper-case letters only.
I won't describe them all in detail because some of them are rather
complex and I'll describe them in more detail than you'll want in
chapter 4.
The targets are as follows:
.nr pw 10
.IP .BEGIN \n(pw
.Ix 0 def .BEGIN
Any commands attached to this target are executed before anything else
is done. You can use it for any initialization that needs doing.
.IP .DEFAULT \n(pw
.Ix 0 def .DEFAULT
This is sort of a .USE rule for any target (that was used only as a
source) that PMake can't figure out any other way to create. It's only
``sort of'' a .USE rule because only the shell script attached to the
.CW .DEFAULT
target is used. The
.CW .IMPSRC
variable of a target that inherits
.CW .DEFAULT 's
commands is set to the target's own name.
.Ix 0 ref .IMPSRC
.Ix 0 ref variable local .IMPSRC
.IP .END \n(pw
.Ix 0 def .END
This serves a function similar to
.CW .BEGIN ,
in that commands attached to it are executed once everything has been
re-created (so long as no errors occurred). It also serves the extra
function of being a place on which PMake can hang commands you put off
to the end. Thus the script for this target will be executed before
any of the commands you save with the ``.\|.\|.''.
.Ix 0 ref ...
.IP .EXPORT \n(pw
The sources for this target are passed to the exportation system compiled
into PMake. Some systems will use these sources to configure
themselves. You should ask your system administrator about this.
.IP .IGNORE \n(pw
.Ix 0 def .IGNORE target
.Ix 0 ref .IGNORE attribute
.Ix 0 ref attributes .IGNORE
This target marks each of its sources with the
.CW .IGNORE
attribute. If you don't give it any sources, then it is like
giving the
.B \-i
flag when you invoke PMake \*- errors are ignored for all commands.
.Ix 0 ref flags -i
.IP .INCLUDES \n(pw
.Ix 0 def .INCLUDES target
.Ix 0 def variable global .INCLUDES
.Ix 0 def .INCLUDES variable
The sources for this target are taken to be suffixes that indicate a
file that can be included in a program source file.
The suffix must have already been declared with
.CW .SUFFIXES
(see below).
Any suffix so marked will have the directories on its search path
(see
.CW .PATH ,
below) placed in the
.CW .INCLUDES
variable, each preceded by a
.B \-I
flag. This variable can then be used as an argument for the compiler
in the normal fashion. The
.CW .h
suffix is already marked in this way in the system makefile.
.Ix 0 ref makefilesystem
E.g. if you have
.DS
\&.SUFFIXES       : .bitmap
\&.PATH.bitmap    : /usr/local/X/lib/bitmaps
\&.INCLUDES       : .bitmap
.DE
PMake will place
.CW "-I/usr/local/X/lib/bitmaps" '' ``
in the
.CW .INCLUDES
variable and you can then say
.DS
cc $(.INCLUDES) -c xprogram.c
.DE
(Note: the
.CW .INCLUDES
variable is not actually filled in until the entire makefile has been read.)
.IP .INTERRUPT \n(pw
.Ix 0 def .INTERRUPT
When PMake is interrupted,
it will execute the commands in the script for this target, if it
exists.
.IP .LIBS \n(pw
.Ix 0 def .LIBS target
.Ix 0 def .LIBS variable
.Ix 0 def variable global .LIBS
This does for libraries what
.CW .INCLUDES
does for include files, except the flag used is
.B \-L ,
as required by those linkers that allow you to tell them where to find
libraries. The variable used is
.CW .LIBS .
Be forewarned that PMake may not have been compiled to do this if the
linker on your system doesn't accept the
.B \-L
flag, though the
.CW .LIBS
variable will always be defined once the makefile has been read.
.IP .MAIN \n(pw
.Ix 0 def .MAIN
If you didn't give a target (or targets) to create when you invoked
PMake, it will take the sources of this target as the targets to
create.
.IP .MAKEFLAGS \n(pw
.Ix 0 def .MAKEFLAGS target
This target provides a way for you to always specify flags for PMake
when the makefile is used. The flags are just as they would be typed
to the shell (except you can't use shell variables unless they're in
the environment),
though the
.B \-f
and
.B \-r
flags have no effect.
.IP .NULL \n(pw
.Ix 0 def .NULL
.Ix 0 ref suffix null
.Ix 0 ref "null suffix"
This allows you to specify what suffix PMake should pretend a file has
if, in fact, it has no known suffix. Only one suffix may be so
designated. The last source on the dependency line is the suffix that
is used (you should, however, only give one suffix.\|.\|.).
.IP .PATH \n(pw
.Ix 0 def .PATH
If you give sources for this target, PMake will take them as
directories in which to search for files it cannot find in the current
directory. If you give no sources, it will clear out any directories
added to the search path before. Since the effects of this all get
very complex, I'll leave it til chapter four to give you a complete
explanation.
.IP .PATH\fIsuffix\fP \n(pw
.Ix 0 ref .PATH
This does a similar thing to
.CW .PATH ,
but it does it only for files with the given suffix. The suffix must
have been defined already. Look at
.B "Search Paths"
(section 4.1)
.Rm 6 4.1
for more information.
.IP .PRECIOUS \n(pw
.Ix 0 def .PRECIOUS target
.Ix 0 ref .PRECIOUS attribute
.Ix 0 ref attributes .PRECIOUS
Similar to
.CW .IGNORE ,
this gives the
.CW .PRECIOUS
attribute to each source on the dependency line, unless there are no
sources, in which case the
.CW .PRECIOUS
attribute is given to every target in the file.
.IP .RECURSIVE \n(pw
.Ix 0 def .RECURSIVE
.Ix 0 ref attributes .MAKE
.Ix 0 ref .MAKE
This target applies the
.CW .MAKE
attribute to all its sources. It does nothing if you don't give it any sources.
.IP .SHELL \n(pw
.Ix 0 def .SHELL
PMake is not constrained to only using the Bourne shell to execute
the commands you put in the makefile. You can tell it some other shell
to use with this target. Check out
.B "A Shell is a Shell is a Shell"
(section 4.4)
.Rm 7 4.4
for more information.
.IP .SILENT \n(pw
.Ix 0 def .SILENT target
.Ix 0 ref .SILENT attribute
.Ix 0 ref attributes .SILENT
When you use
.CW .SILENT
as a target, it applies the
.CW .SILENT
attribute to each of its sources. If there are no sources on the
dependency line, then it is as if you gave PMake the
.B \-s
flag and no commands will be echoed.
.IP .SUFFIXES \n(pw
.Ix 0 def .SUFFIXES
This is used to give new file suffixes for PMake to handle. Each
source is a suffix PMake should recognize. If you give a
.CW .SUFFIXES
dependency line with no sources, PMake will forget about all the
suffixes it knew (this also nukes the null suffix).
For those targets that need to have suffixes defined, this is how you do it.
.LP
In addition to these targets, a line of the form
.DS
\fIattribute\fP : \fIsources\fP
.DE
applies the
.I attribute
to all the targets listed as
.I sources .
.xH 2 Modifying Variable Expansion
.LP
.Ix 0 def variable expansion modified
.Ix 0 ref variable expansion
.Ix 0 def variable modifiers
Variables need not always be expanded verbatim. PMake defines several
modifiers that may be applied to a variable's value before it is
expanded. You apply a modifier by placing it after the variable name
with a colon between the two, like so:
.DS
${\fIVARIABLE\fP:\fImodifier\fP}
.DE
Each modifier is a single character followed by something specific to
the modifier itself.
You may apply as many modifiers as you want \*- each one is applied to
the result of the previous and is separated from the previous by
another colon.
.LP
There are seven ways to modify a variable's expansion, most of which
come from the C shell variable modification characters:
.RS
.IP "M\fIpattern\fP"
.Ix 0 def :M
.Ix 0 def modifier match
This is used to select only those words (a word is a series of
characters that are neither spaces nor tabs) that match the given
.I pattern .
The pattern is a wildcard pattern like that used by the shell, where
.CW *
means 0 or more characters of any sort;
.CW ?
is any single character;
.CW [abcd]
matches any single character that is either `a', `b', `c' or `d'
(there may be any number of characters between the brackets);
.CW [0-9]
matches any single character that is between `0' and `9' (i.e. any
digit. This form may be freely mixed with the other bracket form), and
`\\' is used to escape any of the characters `*', `?', `[' or `:',
leaving them as regular characters to match themselves in a word.
For example, the system makefile
.CW <makedepend.mk>
uses
.CW "$(CFLAGS:M-[ID]*)" '' ``
to extract all the
.CW \-I
and
.CW \-D
flags that would be passed to the C compiler. This allows it to
properly locate include files and generate the correct dependencies.
.IP "N\fIpattern\fP"
.Ix 0 def :N
.Ix 0 def modifier nomatch
This is identical to
.CW :M
except it substitutes all words that don't match the given pattern.
.IP "S/\fIsearch-string\fP/\fIreplacement-string\fP/[g]"
.Ix 0 def :S
.Ix 0 def modifier substitute
Causes the first occurrence of
.I search-string
in the variable to be replaced by
.I replacement-string ,
unless the
.CW g
flag is given at the end, in which case all occurrences of the string
are replaced. The substitution is performed on each word in the
variable in turn. If 
.I search-string
begins with a
.CW ^ ,
the string must match starting at the beginning of the word. If
.I search-string
ends with a
.CW $ ,
the string must match to the end of the word (these two may be
combined to force an exact match). If a backslash precedes these two
characters, however, they lose their special meaning. Variable
expansion also occurs in the normal fashion inside both the
.I search-string
and the
.I replacement-string ,
.B except
that a backslash is used to prevent the expansion of a
.CW $ ,
not another dollar sign, as is usual.
Note that
.I search-string
is just a string, not a pattern, so none of the usual
regular-expression/wildcard characters have any special meaning save
.CW ^
and
.CW $ .
In the replacement string,
the
.CW &
character is replaced by the
.I search-string
unless it is preceded by a backslash.
You are allowed to use any character except
colon or exclamation point to separate the two strings. This so-called
delimiter character may be placed in either string by preceding it
with a backslash.
.IP T
.Ix 0 def :T
.Ix 0 def modifier tail
Replaces each word in the variable expansion by its last
component (its ``tail''). For example, given
.DS
OBJS = ../lib/a.o b /usr/lib/libm.a
TAILS = $(OBJS:T)
.DE
the variable
.CW TAILS
would expand to
.CW "a.o b libm.a" .'' ``
.IP H
.Ix 0 def :H
.Ix 0 def modifier head
This is similar to
.CW :T ,
except that every word is replaced by everything but the tail (the
``head''). Using the same definition of
.CW OBJS ,
the string
.CW "$(OBJS:H)" '' ``
would expand to
.CW "../lib /usr/lib" .'' ``
Note that the final slash on the heads is removed and
anything without a head is replaced by the empty string.
.IP E
.Ix 0 def :E
.Ix 0 def modifier extension
.Ix 0 def modifier suffix
.Ix 0 ref suffix "variable modifier"
.CW :E
replaces each word by its suffix (``extension''). So
.CW "$(OBJS:E)" '' ``
would give you
.CW ".o .a" .'' ``
.IP R
.Ix 0 def :R
.Ix 0 def modifier root
.Ix 0 def modifier base
This replaces each word by everything but the suffix (the ``root'' of
the word).
.CW "$(OBJS:R)" '' ``
expands to ``
.CW "../lib/a b /usr/lib/libm" .''
.RE
.LP
In addition, the System V style of substitution is also supported.
This looks like:
.DS
$(\fIVARIABLE\fP:\fIsearch-string\fP=\fIreplacement\fP)
.DE
It must be the last modifier in the chain. The search is anchored at
the end of each word, so only suffixes or whole words may be replaced.
.xH 2 More on Debugging
.xH 2 More Exercises
.IP (3.1)
You've got a set programs, each of which is created from its own
assembly-language source file (suffix
.CW .asm ).
Each program can be assembled into two versions, one with error-checking
code assembled in and one without. You could assemble them into files
with different suffixes
.CW .eobj \& (
and
.CW .obj ,
for instance), but your linker only understands files that end in
.CW .obj .
To top it all off, the final executables
.I must
have the suffix
.CW .exe .
How can you still use transformation rules to make your life easier
(Hint: assume the error-checking versions have
.CW ec
tacked onto their prefix)?
.IP (3.2)
Assume, for a moment or two, you want to perform a sort of
``indirection'' by placing the name of a variable into another one,
then you want to get the value of the first by expanding the second
somehow. Unfortunately, PMake doesn't allow constructs like
.DS I
$($(FOO))
.DE
What do you do? Hint: no further variable expansion is performed after
modifiers are applied, thus if you cause a $ to occur in the
expansion, that's what will be in the result.
.xH 1 PMake for Gods
.LP
This chapter is devoted to those facilities in PMake that allow you to
do a great deal in a makefile with very little work, as well as do
some things you couldn't do in Make without a great deal of work (and
perhaps the use of other programs). The problem with these features,
is they must be handled with care, or you will end up with a mess.
.LP
Once more, I assume a greater familiarity with
.UX
or Sprite than I did in the previous two chapters.
.xH 2 Search Paths
.Rd 6
.LP
PMake supports the dispersal of files into multiple directories by
allowing you to specify places to look for sources with
.CW .PATH
targets in the makefile. The directories you give as sources for these
targets make up a ``search path.'' Only those files used exclusively
as sources are actually sought on a search path, the assumption being
that anything listed as a target in the makefile can be created by the
makefile and thus should be in the current directory.
.LP
There are two types of search paths
in PMake: one is used for all types of files (including included
makefiles) and is specified with a plain
.CW .PATH
target (e.g.
.CW ".PATH : RCS" ''), ``
while the other is specific to a certain type of file, as indicated by
the file's suffix. A specific search path is indicated by immediately following
the
.CW .PATH
with the suffix of the file. For instance
.DS
\&.PATH.h         : /sprite/lib/include /sprite/att/lib/include
.DE
would tell PMake to look in the directories
.CW /sprite/lib/include
and
.CW /sprite/att/lib/include
for any files whose suffix is
.CW .h .
.LP
The current directory is always consulted first to see if a file
exists. Only if it cannot be found there are the directories in the
specific search path, followed by those in the general search path,
consulted.
.LP
A search path is also used when expanding wildcard characters. If the
pattern has a recognizable suffix on it, the path for that suffix will
be used for the expansion. Otherwise the default search path is employed.
.LP
When a file is found in some directory other than the current one, all
local variables that would have contained the target's name
.CW .ALLSRC , (
and
.CW .IMPSRC )
will instead contain the path to the file, as found by PMake.
Thus if you have a file
.CW ../lib/mumble.c
and a makefile
.DS
\&.PATH.c         : ../lib
mumble          : mumble.c
        $(CC) -o $(.TARGET) $(.ALLSRC)
.DE
the command executed to create
.CW mumble
would be
.CW "cc -o mumble ../lib/mumble.c" .'' ``
(As an aside, the command in this case isn't strictly necessary, since
it will be found using transformation rules if it isn't given. This is because
.CW .out
is the null suffix by default and a transformation exists from
.CW .c
to
.CW .out .
Just thought I'd throw that in.)
.LP
If a file exists in two directories on the same search path, the file
in the first directory on the path will be the one PMake uses. So if
you have a large system spread over many directories, it would behoove
you to follow a naming convention that avoids such conflicts.
.LP
Something you should know about the way search paths are implemented
is that each directory is read, and its contents cached, exactly once
\&\*- when it is first encountered \*- so any changes to the
directories while PMake is running will not be noted when searching
for implicit sources, nor will they be found when PMake attempts to
discover when the file was last modified, unless the file was created in the
current directory. While people have suggested that PMake should read
the directories each time, my experience suggests that the caching seldom
causes problems. In addition, not caching the directories slows things
down enormously because of PMake's attempts to apply transformation
rules through non-existent files \*- the number of extra file-system
searches is truly staggering, especially if many files without
suffixes are used and the null suffix isn't changed from
.CW .out .
.xH 2 Archives and Libraries
.LP
.UX
and Sprite allow you to merge files into an archive using the
.CW ar
command. Further, if the files are relocatable object files, you can
run
.CW ranlib
on the archive and get yourself a library that you can link into any
program you want. The main problem with archives is they double the
space you need to store the archived files, since there's one copy in
the archive and one copy out by itself. The problem with libraries is
you usually think of them as
.CW -lm
rather than
.CW /usr/lib/libm.a
and the linker thinks they're out-of-date if you so much as look at
them.
.LP
PMake solves the problem with archives by allowing you to tell it to
examine the files in the archives (so you can remove the individual
files without having to regenerate them later). To handle the problem
with libraries, PMake adds an additional way of deciding if a library
is out-of-date:
.IP \(bu 2
If the table of contents is older than the library, or is missing, the
library is out-of-date.
.LP
A library is any target that looks like
.CW \-l name'' ``
or that ends in a suffix that was marked as a library using the
.CW .LIBS
target.
.CW .a
is so marked in the system makefile.
.LP
Members of an archive are specified as
``\fIarchive\fP(\fImember\fP[ \fImember\fP...])''.
Thus
.CW libdix.a(window.o) '' ``'
specifies the file
.CW window.o
in the archive
.CW libdix.a .
You may also use wildcards to specify the members of the archive. Just
remember that most the wildcard characters will only find 
.I existing
files.
.LP
A file that is a member of an archive is treated specially. If the
file doesn't exist, but it is in the archive, the modification time
recorded in the archive is used for the file when determining if the
file is out-of-date. When figuring out how to make an archived member target
(not the file itself, but the file in the archive \*- the
\fIarchive\fP(\fImember\fP) target), special care is
taken with the transformation rules, as follows:
.IP \(bu 2
\&\fIarchive\fP(\fImember\fP) is made to depend on \fImember\fP.
.IP \(bu 2
The transformation from the \fImember\fP's suffix to the
\fIarchive\fP's suffix is applied to the \fIarchive\fP(\fImember\fP) target.
.IP \(bu 2
The \fIarchive\fP(\fImember\fP)'s
.CW .TARGET
variable is set to the name of the \fImember\fP if \fImember\fP is
actually a target, or the path to the member file if \fImember\fP is
only a source.
.IP \(bu 2
The
.CW .ARCHIVE
variable for the \fIarchive\fP(\fImember\fP) target is set to the name
of the \fIarchive\fP.
.Ix 0 def variable local .ARCHIVE
.Ix 0 def .ARCHIVE
.IP \(bu 2
The
.CW .MEMBER
variable is set to the actual string inside the parentheses. In most
cases, this will be the same as the
.CW .TARGET
variable.
.Ix 0 def variable local .MEMBER
.Ix 0 def .MEMBER
.IP \(bu 2
The \fIarchive\fP(\fImember\fP)'s place in the local variables of the
targets that depend on it is taken by the value of its
.CW .TARGET
variable.
.LP
Thus, a program library could be created with the following makefile:
.DS
\&.o.a            :
        ...
        rm -f $(.TARGET:T)
OBJS            = obj1.o obj2.o obj3.o
libprog.a       : libprog.a($(OBJS))
        ar cru $(.TARGET) $(.OODATE)
        ranlib $(.TARGET)
.DE
This will cause the three object files to be compiled (if the
corresponding source files were modified after the object file or, if
that doesn't exist, the archived object file), the out-of-date ones
archived in
.CW libprog.a ,
a table of contents placed in the archive and the newly-archived
object files to be removed.
.LP
All this is used in the 
.CW makelib.mk
system makefile to create a single library with ease. This makefile
looks like this:
.DS
.SM
#
# Rules for making libraries. The object files that make up the library
# are removed once they are archived.
#
# To make several libraries in parallel, you should define the variable
# "many_libraries". This will serialize the invocations of ranlib.
#
# To use, do something like this:
#
# OBJECTS = <files in the library>
#
# fish.a: fish.a($(OBJECTS)) MAKELIB
#
#

#ifndef _MAKELIB_MK
_MAKELIB_MK	=

#include	<po.mk>

\&.po.a .o.a	:
	...
	rm -f $(.MEMBER)

ARFLAGS		?= crl

#
# Re-archive the out-of-date members and recreate the library's table of
# contents using ranlib. If many_libraries is defined, put the ranlib
# off til the end so many libraries can be made at once.
#
MAKELIB		: .USE .PRECIOUS
	ar $(ARFLAGS) $(.TARGET) $(.OODATE)
#ifndef no_ranlib
# ifdef many_libraries
	...
# endif many_libraries
	ranlib $(.TARGET)
#endif no_ranlib

#endif _MAKELIB_MK
.DE
.xH 2 On the Condition...
.Rd 1
.LP
Like the C compiler before it, PMake allows you to configure the makefile,
based on the current environment, using conditional statements. A
conditional looks like this:
.DS
#if \fIboolean expression\fP
\fIlines\fP
#elif \fIanother boolean expression\fP
\fImore lines\fP
#else
\fIstill more lines\fP
#endif
.DE
They may be nested to a maximum depth of 30 and may occur anywhere
(except in a comment, of course). The
.CW # '' ``
must the very first character on the line.
.LP
Each
.I "boolean expression"
is made up of terms that look like function calls, the standard C
boolean operators
.CW && ,
.CW || ,
and
.CW ! ,
and the standard relational operators
.CW == ,
.CW != ,
.CW > ,
.CW >= ,
.CW < ,
and
.CW <= ,
with
.CW ==
and
.CW !=
being overloaded to allow string comparisons as well.
.CW &&
represents logical AND;
.CW ||
is logical OR and
.CW !
is logical NOT.  The arithmetic and string operators take precedence
over all three of these operators, while NOT takes precedence over
AND, which takes precedence over OR.  This precedence may be
overridden with parentheses, and an expression may be parenthesized to
your heart's content.  Each term looks like a call on one of four
functions:
.nr pw 9
.Ix 0 def make
.Ix 0 def conditional make
.Ix 0 def if make
.IP make \n(pw
The syntax is
.CW make( \fItarget\fP\c
.CW )
where
.I target
is a target in the makefile. This is true if the given target was
specified on the command line, or as the source for a
.CW .MAIN
target (note that the sources for
.CW .MAIN
are only used if no targets were given on the command line).
.IP defined \n(pw
.Ix 0 def defined
.Ix 0 def conditional defined
.Ix 0 def if defined
The syntax is
.CW defined( \fIvariable\fP\c
.CW )
and is true if
.I variable
is defined. Certain variables are defined in the system makefile that
identify the system on which PMake is being run.
.IP exists \n(pw
.Ix 0 def exists
.Ix 0 def conditional exists
.Ix 0 def if exists
The syntax is
.CW exists( \fIfile\fP\c
.CW )
and is true if the file can be found on the global search path (i.e.
that defined by
.CW .PATH
targets, not by
.CW .PATH \fIsuffix\fP
targets).
.IP empty \n(pw
.Ix 0 def empty
.Ix 0 def conditional empty
.Ix 0 def if empty
This syntax is much like the others, except the string inside the
parentheses is of the same form as you would put between parentheses
when expanding a variable, complete with modifiers and everything. The
function returns true if the resulting string is empty (NOTE: an undefined
variable in this context will cause at the very least a warning
message about a malformed conditional, and at the worst will cause the
process to stop once it has read the makefile. If you want to check
for a variable being defined or empty, use the expression
.CW !defined( \fIvar\fP\c ``
.CW ") || empty(" \fIvar\fP\c
.CW ) ''
as the definition of
.CW ||
will prevent the
.CW empty()
from being evaluated and causing an error, if the variable is
undefined). This can be used to see if a variable contains a given
word, for example:
.DS
#if !empty(\fIvar\fP:M\fIword\fP)
.DE
.LP
The arithmetic and string operators may only be used to test the value
of a variable. The lefthand side must contain the variable expansion,
while the righthand side contains either a string, enclosed in
double-quotes, or a number. The standard C numeric conventions (except
for specifying an octal number) apply to both sides. E.g.
.DS
#if $(OS) == 4.3

#if $(MACHINE) == "sun3"

#if $(LOAD_ADDR) < 0xc000
.DE
are all valid conditionals. In addition, the numeric value of a
variable can be tested as a boolean as follows:
.DS
#if $(LOAD)
.DE
would see if
.CW LOAD
contains a non-zero value and
.DS
#if !$(LOAD)
.DE
would test if
.CW LOAD
contains a zero value.
.LP
In addition to the bare
.CW #if ,'' ``
there are other forms that apply one of the first two functions to each
term. They are as follows:
.DS
	ifdef	\fRdefined\fP
	ifndef	\fR!defined\fP
	ifmake	\fRmake\fP
	ifnmake	\fR!make\fP
.DE
There are also the ``else if'' forms:
.CW elif ,
.CW elifdef ,
.CW elifndef ,
.CW elifmake ,
and
.CW elifnmake .
.LP
For instance, if you wish to create two versions of a program, one of which
is optimized (the production version) and the other of which is for debugging
(has symbols for dbx), you have two choices: you can create two
makefiles, one of which uses the
.CW \-g
flag for the compilation, while the other uses the
.CW \-O
flag, or you can use another target (call it
.CW debug )
to create the debug version. The construct below will take care of
this for you. I have also made it so defining the variable
.CW DEBUG
(say with
.CW "pmake -D DEBUG" )
will also cause the debug version to be made.
.DS
#if defined(DEBUG) || make(debug)
CFLAGS		+= -g
#else
CFLAGS		+= -O
#endif
.DE
There are, of course, problems with this approach. The most glaring
annoyance is that if you want to go from making a debug version to
making a production version, you have to remove all the object files,
or you will get some optimized and some debug versions in the same
program. Another annoyance is you have to be careful not to make two
targets that ``conflict'' because of some conditionals in the
makefile. For instance
.DS
#if make(print)
FORMATTER	= ditroff -Plaser_printer
#endif
#if make(draft)
FORMATTER	= nroff -Pdot_matrix_printer
#endif
.DE
would wreak havoc if you tried
.CW "pmake draft print" '' ``
since you would use the same formatter for each target. As I said,
this all gets somewhat complicated.
.xH 2 A Shell is a Shell is a Shell
.Rd 7
.LP
In normal operation, the Bourne Shell (better known as
.CW sh '') ``
is used to execute the commands to re-create targets. PMake also allows you
to specify a different shell for it to use when executing these
commands. There are several things PMake must know about the shell you
wish to use. These things are specified as the sources for the
.CW .SHELL
.Ix 0 ref .SHELL
.Ix 0 ref target .SHELL
target by keyword, as follows:
.IP "\fBpath=\fP\fIpath\fP"
PMake needs to know where the shell actually resides, so it can
execute it. If you specify this and nothing else, PMake will use the
last component of the path and look in its table of the shells it
knows and use the specification it finds, if any. Use this if you just
want to use a different version of the Bourne or C Shell (yes, PMake knows
how to use the C Shell too).
.IP "\fBname=\fP\fIname\fP"
This is the name by which the shell is to be known. It is a single
word and, if no other keywords are specified (other than
.B path ),
it is the name by which PMake attempts to find a specification for
it (as mentioned above). You can use this if you would just rather use
the C Shell than the Bourne Shell
.CW ".SHELL: name=csh" '' (``
will do it).
.IP "\fBquiet=\fP\fIecho-off command\fP"
As mentioned before, PMake actually controls whether commands are
printed by introducing commands into the shell's input stream. This
keyword, and the next two, control what those commands are. The
.B quiet
keyword is the command used to turn echoing off. Once it is turned
off, echoing is expected to remain off until the echo-on command is given.
.IP "\fBecho=\fP\fIecho-on command\fP"
The command PMake should give to turn echoing back on again.
.IP "\fBfilter=\fP\fIprinted echo-off command\fP"
Many shells will echo the echo-off command when it is given. This
keyword tells PMake in what format the shell actually prints the
echo-off command. Wherever PMake sees this string in the shell's
output, it will delete it and any following whitespace, up to and
including the next newline. See the example at the end of this section
for more details.
.IP "\fBechoFlag=\fP\fIflag to turn echoing on\fP"
Unless a target has been marked
.CW .SILENT ,
PMake wants to start the shell running with echoing on. To do this, it
passes this flag to the shell as one of its arguments. If either this
or the next flag begins with a `\-', the flags will be passed to the
shell as separate arguments. Otherwise, the two will be concatenated
(if they are used at the same time, of course).
.IP "\fBerrFlag=\fP\fIflag to turn error checking on\fP"
Likewise, unless a target is marked
.CW .IGNORE ,
PMake wishes error-checking to be on from the very start. To this end,
it will pass this flag to the shell as an argument. The same rules for
an initial `\-' apply as for the
.B echoFlag .
.IP "\fBcheck=\fP\fIcommand to turn error checking on\fP"
Just as for echo-control, error-control is achieved by inserting
commands into the shell's input stream. This is the command to make
the shell check for errors. It also serves another purpose if the
shell doesn't have error-control as commands, but I'll get into that
in a minute. Again, once error checking has been turned on, it is
expected to remain on until it is turned off again.
.IP "\fBignore=\fP\fIcommand to turn error checking off\fP"
This is the command PMake uses to turn error checking off. It has
another use if the shell doesn't do error-control, but I'll tell you
about that.\|.\|.\|now.
.IP "\fBhasErrCtl=\fP\fIyes or no\fP"
This takes a value that is either
.B yes
or
.B no .
Now you might think that the existence of the
.B check
and
.B ignore
keywords would be enough to tell PMake if the shell can do
error-control, but you'd be wrong. If
.B hasErrCtl
is
.B yes ,
PMake uses the check and ignore commands in a straight-forward manner.
If this is
.B no ,
however, their use is rather different. In this case, the check
command is used as a template, in which the string
.B %s
is replaced by the command that's about to be executed, to produce a
command for the shell that will echo the command to be executed. The
ignore command is also used as a template, again with
.B %s
replaced by the command to be executed, to produce a command that will
execute the command to be executed and ignore any error it returns.
When these strings are used as templates, you must provide newline(s)
.CW \en '') (``
in the appropriate place(s).
.LP
The strings that follow these keywords may be enclosed in single or
double quotes (the quotes will be stripped off) and may contain the
usual C backslash-characters (\en is newline, \er is return, \eb is
backspace, \e' escapes a single-quote inside single-quotes, \e"
escapes a double-quote inside double-quotes). Now for an example.
.LP
This is actually the contents of the
.CW <shx.mk>
system makefile, and causes PMake to use the Bourne Shell in such a
way that each command is printed as it is executed. That is, if more
than one command is given on a line, each will be printed separately.
Similarly, each time the body of a loop is executed, the commands
within that loop will be printed, etc. The specification runs like
this:
.DS
#
# This is a shell specification to have the Bourne shell echo
# the commands just before executing them, rather than when it reads
# them. Useful if you want to see how variables are being expanded, etc.
#
\&.SHELL 	: path=/bin/sh \e
	quiet="set -" \e
	echo="set -x" \e
	filter="+ set - " \e
	echoFlag=x \e
	errFlag=e \e
	hasErrCtl=yes \e
	check="set -e" \e
	ignore="set +e"
.DE
.LP
It tells PMake the following:
.Bp
The shell is located in the file
.CW /bin/sh .
It need not tell PMake that the name of the shell is
.CW sh 
as PMake can figure that out for itself (it's the last component of
the path).
.Bp
The command to stop echoing is
.CW "set -" .
.Bp
The command to start echoing is
.CW "set -x" .
.Bp
When the echo off command is executed, the shell will print
.CW "+ set - " 
(The `+' comes from using the
.CW \-x
flag (rather than the
.CW \-v
flag PMake usually uses)). PMake will remove all occurrences of this
string from the output, so you don't notice extra commands you didn't
put there.
.Bp
The flag the Bourne Shell will take to start echoing in this way is
the
.CW \-x
flag. The Bourne Shell will only take its flag arguments concatenated
as its first argument, so neither this nor the
.B errFlag
specification begins with a \-.
.Bp
The flag to use to turn error-checking on from the start is
.CW \-e .
.Bp
The shell can turn error-checking on and off, and the commands to do
so are
.CW "set +e"
and
.CW "set -e" ,
respectively.
.LP
I should note that this specification is for Bourne Shells that are
not part of Berkeley
.UX ,
as shells from Berkeley don't do error control. You can get a similar
effect, however, by changing the last three lines to be:
.DS
	hasErrCtl=no \e
	check="echo \e"+ %s\e"\en" \e
	ignore="sh -c '%s || exit 0\en"
.DE
.LP
This will cause PMake to execute the two commands
.DS
echo "+ \fIcmd\fP"
sh -c '\fIcmd\fP || true'
.DE
for each command for which errors are to be ignored. (In case you are
wondering, the thing for
.CW ignore
tells the shell to execute another shell without error checking on and
always exit 0, since the
.B ||
causes the
.CW "exit 0"
to be executed only if the first command exited non-zero, and if the
first command exited zero, the shell will also exit zero, since that's
the last command it executed).
.xH 2 Compatibility
.Ix 0 ref compatibility
.LP
There are three (well, 3 \(12) levels of backwards-compatibility built
into PMake.  Most makefiles will need none at all. Some may need a
little bit of work to operate correctly when run in parallel. Each
level encompasses the previous levels (e.g.
.B \-B
(one shell per command) implies
.B \-V )
The three levels are described in the following three sections.
.xH 3 DEFCON 3 \*- Variable Expansion
.Ix 0 ref compatibility
.LP
As noted before, PMake will not expand a variable unless it knows of a
value for it. This can cause problems for makefiles that expect to
leave variables undefined except in special circumstances (e.g. if
more flags need to be passed to the C compiler or the output from a
text processor should be sent to a different printer). If the
variables are enclosed in curly braces
.CW ${PRINTER} ''), (``
the shell will let them pass. If they are enclosed in parentheses,
however, the shell will declare a syntax error and the make will come
to a grinding halt.
.LP
You have two choices: change the makefile to define the variables
(their values can be overridden on the command line, since that's
where they would have been set if you used Make, anyway) or always give the
.B \-V
flag (this can be done with the
.CW .MAKEFLAGS
target, if you want).
.xH 3 DEFCON 2 \*- The Number of the Beast
.Ix 0 ref compatibility
.LP
Then there are the makefiles that expect certain commands, such as
changing to a different directory, to not affect other commands in a
target's creation script. You can solve this is either by going
back to executing one shell per command (which is what the
.B \-B
flag forces PMake to do), which slows the process down a good bit and
requires you to use semicolons and escaped newlines for shell constructs, or
by changing the makefile to execute the offending command(s) in a subshell
(by placing the line inside parentheses), like so:
.DS
install :: .MAKE
	(cd src; $(.PMAKE) install)
	(cd lib; $(.PMAKE) install)
	(cd man; $(.PMAKE) install)
.DE
.Ix 0 ref operator double-colon
.Ix 0 ref variable global .PMAKE
.Ix 0 ref .PMAKE
.Ix 0 ref .MAKE
.Ix 0 ref attribute .MAKE
This will always execute the three makes (even if the
.B \-n
flag was given) because of the combination of the ``::'' operator and
the
.CW .MAKE
attribute. Each command will change to the proper directory to perform
the install, leaving the main shell in the directory in which it started.
.xH 3 "DEFCON 1 \*- Imitation is the Not the Highest Form of Flattery"
.Ix 0 ref compatibility
.LP
The final category of makefile is the one where every command requires
input, the dependencies are incompletely specified, or you simply
cannot create more than one target at a time, as mentioned earlier. In
addition, you may not have the time or desire to upgrade the makefile
to run smoothly with PMake. If you are the conservative sort, this is
the compatibility mode for you. It is entered either by giving PMake
the
.B \-M
flag (for Make), or by executing PMake as
.CW make .'' ``
In either case, PMake performs things exactly like Make (while still
supporting most of the nice new features PMake provides). This
includes:
.IP \(bu 2
No parallel execution.
.IP \(bu 2
Targets are made in the exact order specified by the makefile. The
sources for each target are made in strict left-to-right order, etc.
.IP \(bu 2
A single Bourne shell is used to execute each command, thus the
shell's
.CW $$
variable is useless, changing directories doesn't work across command
lines, etc.
.IP \(bu 2
If no special characters exist in a command line, PMake will break the
command into words itself and execute the command directly, without
executing a shell first. The characters that cause PMake to execute a
shell are:
.CW # ,
.CW = ,
.CW | ,
.CW ^ ,
.CW ( ,
.CW ) ,
.CW { ,
.CW } ,
.CW ; ,
.CW & ,
.CW < ,
.CW > ,
.CW * ,
.CW ? ,
.CW [ ,
.CW ] ,
.CW : ,
.CW $ ,
.CW ` ,
and
.CW \e .
You should notice that these are all the characters that are given
special meaning by the shell (except
.CW '
and
.CW " ,
which PMake deals with all by its lonesome).
.IP \(bu 2
The use of the null suffix is turned off.
.Ix 0 ref "null suffix"
.Ix 0 ref suffix null
.xH 2 The Way Things Work
.LP
When PMake reads the makefile, it parses sources and targets into
nodes in a graph. The graph is directed only in the sense that PMake
knows which way is up. Each node contains not only links to all its
parents and children (the nodes that depend on it and those on which
it depends, respectively), but also a count of the number of its
children that have already been processed.
.LP
The most important thing to know about how PMake uses this graph is
that the traversal is breadth-first and occurs in two passes.
.LP
After PMake has parsed the makefile, it begins with the nodes the user
has told it to make (either on the command line, or via a 
.CW .MAIN
target, or by the target being the first in the file not labeled with
the
.CW .NOTMAIN
attribute) placed in a queue. It continues to take the node off the
front of the queue, mark it as something that needs to be made, pass
the node to 
.CW Suff_FindDeps
(mentioned earlier) to find any implicit sources for the node, and
place all the node's children that have yet to be marked at the end of
the queue. If any of the children is a
.CW .USE
rule, its attributes are applied to the parent, then its commands are
appended to the parent's list of commands and its children are linked
to its parent. The parent's unmade children counter is then decremented
(since the
.CW .USE
node has been processed). You will note that this allows a
.CW .USE
node to have children that are
.CW .USE
nodes and the rules will be applied in sequence.
If the node has no children, it is placed at the end of
another queue to be examined in the second pass. This process
continues until the first queue is empty.
.LP
At this point, all the leaves of the graph are in the examination
queue. PMake removes the node at the head of the queue and sees if it
is out-of-date. If it is, it is passed to a function that will execute
the commands for the node asynchronously. When the commands have
completed, all the node's parents have their unmade children counter
decremented and, if the counter is then 0, they are placed on the
examination queue. Likewise, if the node is up-to-date. Only those
parents that were marked on the downward pass are processed in this
way. Thus PMake traverses the graph back up to the nodes the user
instructed it to create. When the examination queue is empty and no
shells are running to create a target, PMake is finished.
.LP
Once all targets have been processed, PMake executes the commands
attached to the
.CW .END
target, either explicitly or through the use of an ellipsis in a shell
script. If there were no errors during the entire process but there
are still some targets unmade (PMake keeps a running count of how many
targets are left to be made), there is a cycle in the graph. PMake does
a depth-first traversal of the graph to find all the targets that
weren't made and prints them out one by one.
.xH 1 Answers to Exercises
.IP (3.1)
This is something of a trick question, for which I apologize. The
trick comes from the UNIX definition of a suffix, which PMake doesn't
necessarily share. You will have noticed that all the suffixes used in
this tutorial (and in UNIX in general) begin with a period
.CW .ms , (
.CW .c ,
etc.). Now, PMake's idea of a suffix is more like English's: it's the
characters at the end of a word. With this in mind, one possible
.Ix 0 def suffix
solution to this problem goes as follows:
.DS I
\&.SUFFIXES       : ec.exe .exe ec.obj .obj .asm
ec.objec.exe .obj.exe :
        link -o $(.TARGET) $(.IMPSRC)
\&.asmec.obj      :
        asm -o $(.TARGET) -DDO_ERROR_CHECKING $(.IMPSRC)
\&.asm.obj        :
        asm -o $(.TARGET) $(.IMPSRC)
.DE
.IP (3.2)
The trick to this one lies in the ``:='' variable-assignment operator
and the ``:S'' variable-expansion modifier. 
.Ix 0 ref variable assignment expanded
.Ix 0 ref variable expansion modified
.Ix 0 ref modifier substitute
.Ix 0 ref :S
.Ix 0 ref :=
Basically what you want is to take the pointer variable, so to speak,
and transform it into an invocation of the variable at which it
points. You might try something like
.DS I
$(PTR:S/^/\e$(/:S/$/))
.DE
which places
.CW $( '' ``
at the front of the variable name and
.CW ) '' ``
at the end, thus transforming
.CW VAR ,'' ``
for example, into
.CW $(VAR) ,'' ``
which is just what we want. Unfortunately (as you know if you've tried
it), since, as it says in the hint, PMake does no further substitution
on the result of a modified expansion, that's \fIall\fP you get. The
solution is to make use of ``:='' to place that string into yet
another variable, then invoke the other variable directly:
.DS I
*PTR            := $(PTR:S/^/\e$(/:S/$/)/)
.DE
You can then use
.CW $(*PTR) '' ``
to your heart's content.
.de Gp
.XP
\&\fB\\$1:\fP
..
.xH 1 Glossary of Jargon
.Gp "attribute"
A property given to a target that causes PMake to treat it differently.
.Gp "command script"
The lines immediately following a dependency line that specify
commands to execute to create each of the targets on the dependency
line. Each line in the command script must begin with a tab.
.Gp "command-line variable"
A variable defined in an argument when PMake is first executed.
Overrides all assignments to the same variable name in the makefile.
.Gp "conditional"
A construct much like that used in C that allows a makefile to be
configured on the fly based on the local environment, or on what is being
made by that invocation of PMake.
.Gp "creation script"
Commands used to create a target. See ``command script.''
.Gp "dependency"
The relationship between a source and a target. This comes in three
flavors, as indicated by the operator between the target and the
source. `:' gives a straight time-wise dependency (if the target is
older than the source, the target is out-of-date), while `!' provides
simply an ordering and always considers the target out-of-date. `::'
is much like `:', save it creates multiple instances of a target each
of which depends on its own list of sources.
.Gp "dynamic source"
This refers to a source that has a local variable invocation in it. It
allows a single dependency line to specify a different source for each
target on the line.
.Gp "global variable"
Any variable defined in a makefile. Takes precedence over variables
defined in the environment, but not over command-line or local variables.
.Gp "input graph"
What PMake constructs from a makefile. Consists of nodes made of the
targets in the makefile, and the links between them (the
dependencies). The links are directed (from source to target) and
there may not be any cycles (loops) in the graph.
.Gp "local variable"
A variable defined by PMake visible only in a target's shell script.
There are seven local variables, not all of which are defined for
every target:
.CW .TARGET ,
.CW .ALLSRC ,
.CW .OODATE ,
.CW .PREFIX ,
.CW .IMPSRC ,
.CW .ARCHIVE ,
and
.CW .MEMBER .
.CW .TARGET ,
.CW .PREFIX ,
.CW .ARCHIVE ,
and 
.CW .MEMBER
may be used on dependency lines to create ``dynamic sources.''
.Gp "makefile"
A file that describes how a system is built. If you don't know what it
is after reading this tutorial.\|.\|.\|.
.Gp "modifier"
A letter, following a colon, used to alter how a variable is expanded.
It has no effect on the variable itself.
.Gp "operator"
What separates a source from a target (on a dependency line) and specifies
the relationship between the two. There are three:
.CW : ', `
.CW :: ', `
and
.CW ! '. `
.Gp "search path"
A list of directories in which a file should be sought. PMake's view
of the contents of directories in a search path does not change once
the makefile has been read. A file is sought on a search path only if
it is exclusively a source.
.Gp "shell"
A program to which commands are passed in order to create targets.
.Gp "source"
Anything to the right of an operator on a dependency line. Targets on
the dependency line are usually created from the sources.
.Gp "special target"
A target that causes PMake to do special things when it's encountered.
.Gp "suffix"
The tail end of a file name. Usually begins with a period,
.CW .c
or
.CW .ms ,
e.g.
.Gp "target"
A word to the left of the operator on a dependency line. More
generally, any file that PMake might create. A file may be (and often
is) both a target and a source (what it is depends on how PMake is
looking at it at the time \*- sort of like the wave/particle duality
of light, you know).
.Gp "transformation rule"
A special construct in a makefile that specifies how to create a file
of one type from a file of another, as indicated by their suffixes.
.Gp "variable expansion"
The process of substituting the value of a variable for a reference to
it. Expansion may be altered by means of modifiers.
.Gp "variable"
A place in which to store text that may be retrieved later. Also used
to define the local environment. Conditionals exist that test whether
a variable is defined or not.
.bp
.\" Output table of contents last, with an entry for the index, making
.\" sure to save and restore the last real page number for the index...
.nr @n \n(PN+1
.\" We are not generating an index
.\" .XS \n(@n
.\" Index
.\" .XE
.nr %% \n%
.PX
.nr % \n(%%
