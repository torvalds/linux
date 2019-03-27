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
.\"	@(#)vi.apwh.ms	8.2 (Berkeley) 8/18/96
.\"
.nr LL 6.5i
.nr FL 6.5i
.TL
Vi Command & Function Reference
.AU CB 2675
Alan P.W. Hewett
.sp
Revised for version 2.12 by Mark Horton
.CB
.NH 1
Author's Disclaimer
.LP
This document does not claim to be 100% complete.  There are a
few commands listed in the original document that I was unable
to test either because I do not speak \fBlisp\fR, because they
required programs we don't have, or because I wasn't able to make
them work.  In these cases I left the command out.  The commands
listed in this document have been tried and are known to work.
It is expected that prospective users of this document will read
it once to get the flavor of everything that \fBvi\fR can do
and then use it as a reference document.  Experimentation is
recommended.  If you don't understand a command, try it and
see what happens.
.LP
[Note: In revising this document, I have attempted to make it
completely reflect version 2.12 of
.B vi .
It does not attempt to document the VAX version (version 3),
but with one or two exceptions (wrapmargin, arrow keys)
everything said about 2.12 should apply to 3.1.
.I "Mark Horton" ]
.NH 1
Notation
.LP
\fB[option]\fR is used to denote optional parts of a command.
Many \fBvi\fR commands have an optional count.  \fB[cnt]\fR
means that an optional number may precede the command to
multiply or iterate the command.
\fB{variable item}\fR is used to denote parts of the command
which must appear, but can take a number of different values.
\fB<character [-character]>\fR means that the character or
one of the characters in the range described between the
two angle brackets is to be typed.
For example \fB<esc>\fR means
the \fBescape\fR key is to be typed.  \fB<a-z>\fR means that a
lower case letter is to be typed.  \fB^<character>\fR means that
the character is to be typed as a \fBcontrol\fR character, that is,
with the \fB<cntl>\fR key held down while simultaneously typing
the specified character.  In this document control characters will
be denoted using the \fIupper case\fR character, but
^<uppercase chr> and ^<lowercase chr> are equivalent.  That is, for
example, \fB<^D>\fR is equal to \fB<^d>\fR.
The most common character abbreviations
used in this list are as follows:
.VL 8
.IP <esc> 8
escape, octal 033
.IP <cr> 8
carriage return, ^M, octal 015
.IP <lf> 8
linefeed ^J, octal 012
.IP <nl> 8
newline, ^J, octal 012 (same as linefeed)
.IP <bs> 8
backspace, ^H, octal 010
.IP <tab> 8
tab, ^I, octal 011
.IP <bell> 8
bell, ^G, octal 07
.IP <ff> 8
formfeed, ^L, octal 014
.IP <sp> 8
space, octal 040
.IP <del> 8
delete, octal 0177
.LE
.sp 1
.NH 1
Basics
.LP
To run \fBvi\fR the shell variable \fBTERM\fR must be defined and
exported to your environment.
How you do this depends on which shell you are using.
You can tell which shell you have by the character it
prompts you for commands with.
The Bourne shell prompts with `$', and the C shell prompts with `%'.
For these examples, we will suppose
that you are using an HP 2621 terminal, whose termcap name is ``2621''.
.NH 2
Bourne Shell
.LP
To manually set your terminal type to 2621 you would type:
.DS
TERM=2621
export TERM
.DE
.PP
There are various ways of having this automatically or
semi-automatically done when you log in.
Suppose you usually dial in on a 2621.
You want to tell this to the machine, but still have it
work when you use a hardwired terminal.
The recommended way, if you have the
.B tset
program, is to use the sequence
.DS
tset \-s \-d 2621 > tset$$
\&. tset$$
rm tset$$
.DE
in your .login (for csh) or the same thing using `.' instead of `source'
in your .profile (for sh).
The above line says that if you are dialing in you are on a 2621,
but if you are on a hardwired terminal it figures out your terminal
type from an on-line list.
.NH 2
The C Shell
.LP
To manually set your terminal type to 2621 you would type:
.DS
setenv TERM 2621
.DE
.PP
There are various ways of having this automatically or
semi-automatically done when you log in.
Suppose you usually dial in on a 2621.
You want to tell this to the machine, but still have it
work when you use a hardwired terminal.
The recommended way, if you have the
.B tset
program, is to use the sequence
.DS
tset \-s \-d 2621 > tset$$
source tset$$
rm tset$$
.DE
in your .login.*
.FS
* On a version 6 system
without environments, the invocation of tset
is simpler, just add the line ``tset \-d 2621''
to your .login or .profile.
.FE
The above line says that if you are dialing in you are on a 2621,
but if you are on a hardwired terminal it figures out your terminal
type from an on-line list.
.NH 1
Normal Commands
.LP
\fBVi\fR is a visual editor with a window on the file.  What
you see on the screen is \fBvi\fR's current notion of
what your file will contain,
(at this point in the file),
when it is written out.
Most commands do not cause any change in the screen until the
complete command is typed.  Should you get confused while
typing a command, you can abort the command by typing an
<del> character.  You will know you are back to command level
when you hear a <bell>.  Usually typing an <esc> will produce the
same result.  When \fBvi\fR gets an improperly formatted command
it rings the <bell>.
Following are the \fBvi\fR commands broken down by function.
.NH 2
Entry and Exit
.LP
To enter
.B vi
on a particular
.I file ,
type
.DS
\fBvi\fP \fIfile\fP
.DE
The file will be read in and the cursor will be placed at the beginning
of the first line.
The first screenfull of the file will be displayed on the terminal.
.PP
To get out of the editor, type
.DS
ZZ
.DE
If you are in some special mode, such as input mode
or the middle of a multi-keystroke command, it may
be necessary to type <esc> first.
.NH 2
Cursor and Page Motion
.LP
.VL 16
.B NOTE:
The arrow keys (see the next four commands)
on certain kinds of terminals will not work with the
PDP-11 version of vi.  The control versions or the hjkl versions will
work on any terminal.  Experienced users prefer the hjkl keys because
they are always right under their fingers.  Beginners often prefer
the arrow keys, since they do not require memorization of which hjkl
key is which.
The mnemonic value of hjkl is clear from looking at the keyboard of an adm3a.
.sp
.IP "[cnt]<bs> or [cnt]h or [cnt]\(<-" 16
.br
Move the cursor to the left one character.  Cursor stops at the left
margin of the page.
If cnt is given, these commands move that many spaces.
.IP "[cnt]^N or [cnt]j or [cnt]\(da or [cnt]<lf>" 16
.br
Move down one line.
Moving off the screen scrolls the window to force a new line
onto the screen.
Mnemonic: \fBN\fRext
.IP "[cnt]^P or [cnt]k or [cnt]\(ua" 16
.br
Move up one line.
Moving off the top of the screen forces new text onto the screen.
Mnemonic: \fBP\fRrevious
.IP "[cnt]<sp> or [cnt]l or [cnt]\(->" 16
.br
Move to the right one character.
Cursor will not go beyond the end of the line.
.IP [cnt]- 16
Move the cursor up the screen to the beginning of the next line.
Scroll if necessary.
.IP "[cnt]+ or [cnt]<cr>" 16
.sp 1
Move the cursor down the screen to the beginning of the next line.
Scroll up if necessary.
.IP "[cnt]$" 16
Move the cursor to the end of the line.
If there is a count, move to the end of the line "cnt" lines
forward in the file.
.IP "^" 16
Move the cursor to the beginning of the first word on the line.
.IP "0" 16
Move the cursor to the left margin of the current line.
.IP "[cnt]|" 16
Move the cursor to the column specified by the count.  The default is
column zero.
.IP "[cnt]w" 16
Move the cursor to the beginning of the next word. If there
is a count, then move forward that many words and
position the cursor at the beginning of the word.
Mnemonic: next-\fBw\fRord
.IP "[cnt]W" 16
Move the cursor to the beginning of the next word which follows
a "white space" (<sp>,<tab>, or <nl>).  Ignore other punctuation.
.IP "[cnt]b" 16
Move the cursor to the preceding word.  Mnemonic: \fBb\fRackup-word
.IP "[cnt]B" 16
Move the cursor to the preceding word that is separated from the
current word by a "white space" (<sp>,<tab>, or <nl>).
.IP "[cnt]e" 16
Move the cursor to the end of the current word or the end of the
"cnt"'th word hence.  Mnemonic: \fBe\fRnd-of-word
.IP "[cnt]E" 16
Move the cursor to the end of the current word which is delimited by
"white space" (<sp>,<tab>, or <nl>).
.IP "[line number]G" 16
.br
Move the cursor to the line specified.  Of particular use are the
sequences "1G" and "G", which move the cursor to the beginning and
the end of the file respectively.  Mnemonic: \fBG\fRo-to
.LP
.B NOTE:
The next four commands (^D, ^U, ^F, ^B)
are not true motion commands, in that they
cannot be used as the object of commands such as delete or change.
.IP "[cnt]^D" 16
Move the cursor down in the file by "cnt" lines (or the last "cnt"
if a new count isn't given.  The initial default is half a page.)  The
screen is simultaneously scrolled up.  Mnemonic: \fBD\fRown
.IP "[cnt]^U" 16
Move the cursor up in the file by "cnt" lines.  The screen is simultaneously
scrolled down.  Mnemonic: \fBU\fRp
.IP "[cnt]^F" 16
Move the cursor to the next page.  A count moves that many pages.
Two lines of the previous page are kept on the screen for continuity if
possible.  Mnemonic: \fBF\fRorward-a-page
.IP "[cnt]^B" 16
Move the cursor to the previous page.  Two lines of the current page
are kept if possible.  Mnemonic: \fBB\fRackup-a-page
.IP "[cnt](" 16
Move the cursor to the beginning of the next sentence.
A sentence is defined as ending with a ".", "!", or "?"
followed by two spaces or a <nl>.
.IP "[cnt])" 16
Move the cursor backwards to the beginning of a sentence.
.IP "[cnt]}" 16
Move the cursor to the beginning of the next paragraph.  This command
works best inside \fBnroff\fR documents.  It understands two sets of
\fBnroff\fR macros, \fB\-ms\fR and \fB\-mm\fR, for which the
commands ".IP", ".LP", ".PP", ".QP", "P", as well as the nroff command ".bp"
are considered to be paragraph delimiters.
A blank line also delimits a paragraph.
The \fBnroff\fR macros that it accepts as paragraph delimiters is
adjustable.  See \fBparagraphs\fR under the \fBSet Commands\fR section.
.IP "[cnt]{" 16
Move the cursor backwards to the beginning of a paragraph.
.IP "]]" 16
Move the cursor to the next "section", where a section is defined by
two sets of \fBnroff\fR macros, \fB\-ms\fR and \fB\-mm\fR, in which
".NH", ".SH", and ".H" delimit a section.  A line beginning with a <ff><nl>
sequence, or a line beginning with a "{" are also considered to
be section delimiters.  The last option makes it
useful for finding the beginnings of C functions.
The \fBnroff\fR macros that are used for section delimiters can be adjusted.
See \fBsections\fR under the \fBSet Commands\fR section.
.IP "[[" 16
Move the cursor backwards to the beginning of a section.
.IP "%" 16
Move the cursor to the matching parenthesis
or brace.  This is very useful in C or lisp code.  If the
cursor is sitting on a \fB( ) {\fR or \fB}\fR the cursor
is moved to the matching character at the other end of the
section.  If the cursor is not sitting on a brace or a
parenthesis, \fBvi\fR searches forward until it finds one
and then jumps to the match mate.
.IP "[cnt]H" 16
If there is no count move the cursor to the top left position on the screen.
If there is a count, then move the cursor to the beginning of the line
"cnt" lines from the top of the screen.  Mnemonic:  \fBH\fRome
.IP "[cnt]L" 16
If there is no count move the cursor to the beginning
of the last line on the screen.
If there is a count, then move the cursor to the beginning of the line
"cnt" lines from the bottom of the screen.  Mnemonic: \fBL\fRast
.IP "M" 16
Move the cursor to the beginning of the middle line on the screen.
Mnemonic: \fBM\fRiddle
.IP "m<a-z>" 16
This command does not move the cursor, but it \fBmarks\fR the place
in the file and the character "<a-z>" becomes the label for referring
to this location in the file.  See the next two commands.  Mnemonic:
\fBm\fRark
.B NOTE:
The mark command is not a motion, and cannot be used as the target
of commands such as delete.
.IP "\(aa<a-z>" 16
Move the cursor to the beginning of the line that is marked with the label
"<a-z>".
.IP "\(ga<a-z>" 16
Move the cursor to the exact position on the line that was marked with
with the label "<a-z>".
.IP "\(aa\(aa" 16
Move the cursor back to the beginning of the line where it was before the
last "non-relative" move.  A "non-relative" move is something such as a
search or a jump to a specific line in the file, rather than moving the
cursor or scrolling the screen.
.IP "\(ga\(ga" 16
Move the cursor back to the exact spot on the line where it was located
before the last "non-relative" move.
.LE
.NH 2
Searches
.LP
The following commands allow you to search for items in a file.
.VL 16
.IP [cnt]f{chr} 16
.sp 1
Search forward on the line for the next or "cnt"'th occurrence of
the character "chr".  The cursor is placed \fBat\fR the character
of interest.  Mnemonic: \fBf\fRind character
.IP [cnt]F{chr} 16
.sp 1
Search backwards on the line for the next or "cnt"'th occurrence of
the character "chr".  The cursor is placed \fBat\fR the character
of interest.
.IP [cnt]t{chr} 16
.sp 1
Search forward on the line for the next or "cnt"'th occurrence of
the character "chr".  The cursor is placed \fBjust preceding\fR
the character of interest.  Mnemonic: move cursor up \fBt\fRo character
.IP [cnt]T{chr} 16
.sp 1
Search backwards on the line for the next or "cnt"'th occurrence of
the character "chr".  The cursor is placed \fBjust preceding\fR
the character of interest.
.IP "[cnt];" 16
Repeat the last "f", "F", "t" or "T" command.
.IP "[cnt]," 16
Repeat the last "f", "F", "t" or "T" command, but in the opposite
search direction.  This is useful if you overshoot.
.IP "[cnt]/[string]/<nl>" 16
.br
Search forward for the next occurrence of "string".
Wrap around at the end of the file
does occur.
The final \fB</>\fR is not required.
.IP "[cnt]?[string]?<nl>" 16
.br
Search backwards for the next occurrence of "string".  If a count is
specified, the count becomes the new window size.  Wrap around at the beginning
of the file does occur.
The final \fB<?>\fR is not required.
.IP n 16
Repeat the last /[string]/ or ?[string]? search.  Mnemonic: \fBn\fRext
occurrence.
.IP N 16
Repeat the last /[string]/ or ?[string]? search, but in the reverse
direction.
.IP ":g/[string]/[editor command]<nl>" 16
.sp 1
Using the \fB:\fR syntax it is possible to do global searches ala the
standard UNIX "ed" editor.
.LE
.NH 2
Text Insertion
.LP
The following commands allow for the insertion of text.  All multicharacter
text insertions are terminated with an <esc> character.
The last change
can always be \fBundone\fR by typing a \fBu\fR.
The text insert in insertion mode can contain newlines.
.VL 16
.IP a{text}<esc> 16
Insert text immediately following the cursor position.
Mnemonic: \fBa\fRppend
.IP A{text}<esc> 16
Insert text at the end of the current line.
Mnemonic: \fBA\fRppend
.IP i{text}<esc> 16
Insert text immediately preceding the cursor position.
Mnemonic: \fBi\fRnsert
.IP I{text}<esc> 16
Insert text at the beginning of the current line.
.IP o{text}<esc> 16
Insert a new line after the line on which the cursor appears and
insert text there.  Mnemonic:  \fBo\fRpen new line
.IP O{text}<esc> 16
Insert a new line preceding the line on which the cursor appears
and insert text there.
.LE
.NH 2
Text Deletion
.LP
The following commands allow the user to delete text in various ways.
All changes can always be \fBundone\fR by typing the \fBu\fR command.
.VL 16
.IP "[cnt]x" 16
Delete the character or characters starting at the cursor position.
.IP "[cnt]X" 16
Delete the character or characters starting at the character preceding
the cursor position.
.IP "D" 16
Deletes the remainder of the line starting at the cursor.
Mnemonic: \fBD\fRelete the rest of line
.IP "[cnt]d{motion}" 16
.br
Deletes one or more occurrences of the specified motion.
Any motion from sections 4.1 and 4.2 can be used here.
The d can be stuttered (e.g. [cnt]dd) to delete cnt lines.
.LE
.NH 2
Text Replacement
.LP
The following commands allow the user to simultaneously delete and
insert new text.  All such actions can be \fBundone\fR by typing
\fBu\fR following the command.
.VL 16
.IP "r<chr>" 16
Replaces the character at the current cursor position with <chr>.  This
is a one character replacement.  No <esc> is required for termination.
Mnemonic:  \fBr\fReplace character
.IP "R{text}<esc>" 16
Starts overlaying the characters on the screen with whatever you type.
It does not stop until an <esc> is typed.
.IP "[cnt]s{text}<esc>" 16
Substitute for "cnt" characters beginning at the current cursor
position.  A "$" will appear at the position in the text where the
"cnt"'th character appears so you will know how much you are erasing.
Mnemonic: \fBs\fRubstitute
.IP "[cnt]S{text}<esc>" 16
Substitute for the entire current line (or lines).  If no count is given,
a "$" appears at the end of the current line.  If a count of more than
1 is given, all the lines to be replaced are deleted before the insertion
begins.
.IP "[cnt]c{motion}{text}<esc>" 16
.br
Change the specified "motion" by replacing it with the
insertion text.  A "$" will appear at the end of the last item
that is being deleted unless the deletion involves whole lines.
Motion's can be any motion from sections 4.1 or 4.2.
Stuttering the c (e.g. [cnt]cc) changes cnt lines.
.LE
.NH 2
Moving Text
.LP
\fBVi\fR provides a number of ways of moving chunks of text around.
There are nine buffers into which each piece of text which is deleted
or "yanked" is put in addition to the "undo" buffer.
The most recent deletion or yank is in the "undo" buffer and also
usually in buffer
1, the next most recent in buffer 2, and so forth.  Each new deletion
pushes down all the older deletions.  Deletions older than 9
disappear.  There is also
a set of named registers, a-z, into which text can optionally
be placed.  If any delete or replacement type command is preceded
by \fB"<a-z>\fR, that named buffer will contain the text deleted
after the command is executed.  For example, \fB"a3dd\fR will delete
three lines starting at the current line and put them in buffer \fB"a\fR.*
.FS
* Referring to an upper case letter as a buffer name (A-Z) is the
same as referring to the lower case letter, except that text placed
in such a buffer is appended to it instead of replacing it.
.FE
There are two more basic commands and
some variations useful in getting and putting text into a file.
.VL 16
.IP ["<a-z>][cnt]y{motion} 16
.sp 1
Yank the specified item or "cnt" items and put in the "undo" buffer or
the specified buffer.  The variety of "items" that can be yanked
is the same as those that can be deleted with the "d" command or
changed with the "c" command.  In the same way that "dd" means
delete the current line and "cc" means replace the current line,
"yy" means yank the current line.
.IP ["<a-z>][cnt]Y 16
Yank the current line or the "cnt" lines starting from the current
line.  If no buffer is specified, they will go into the "undo" buffer,
like any delete would.  It is equivalent to "yy".
Mnemonic:  \fBY\fRank
.IP ["<a-z>]p 16
Put "undo" buffer or the specified buffer down \fBafter\fR the cursor.
If whole lines were yanked or deleted into the buffer, then they will be
put down on the line following the line the cursor is on.  If
something else was deleted, like a word or sentence, then it will
be inserted immediately following the cursor.
Mnemonic:  \fBp\fRut buffer
.IP
It should be noted that text in the named buffers remains there when you
start editing a new file with the \fB:e file<esc>\fR command.  Since
this is so, it is possible to copy or delete text from one file and
carry it over to another file in the buffers.
However, the undo buffer and the ability to undo are lost when
changing files.
.IP ["<a-z>]P 16
Put "undo" buffer or the specified buffer down \fBbefore\fR the cursor.
If whole lines where yanked or deleted into the buffer, then they will be
put down on the line preceding the line the cursor is on.  If
something else was deleted, like a word or sentence, then it will
be inserted immediately preceding the cursor.
.IP [cnt]>{motion} 16
The shift operator will right shift all the text from the line on which
the cursor is located to the line where the \fBmotion\fR is located.
The text is shifted by one \fBshiftwidth\fR.  (See section 6.)
\fB>>\fR means right shift the current line or lines.
.IP [cnt]<{motion} 16
The shift operator will left shift all the text from the line on which
the cursor is located to the line where the \fBitem\fR is located.
The text is shifted by one \fBshiftwidth\fR.  (See section 6.)
\fB<<\fR means left shift the current line or lines.
Once the line has reached the left margin it is not further affected.
.IP [cnt]={motion} 16
Prettyprints the indicated area according to
.B lisp
conventions.
The area should be a lisp s-expression.
.LE
.NH 2
Miscellaneous Commands
.LP
\fBVi\fR has a number of miscellaneous commands that are very
useful.  They are:
.VL 16
.IP ZZ 16
This is the normal way to exit from vi.
If any changes have been made, the file is written out.
Then you are returned to the shell.
.IP ^L 16
Redraw the current screen.  This is useful if someone "write"s you
while you are in "vi" or if for any reason garbage gets onto the
screen.
.IP ^R 16
On dumb terminals, those not having the "delete line" function
(the vt100 is such a terminal), \fBvi\fR saves redrawing the
screen when you delete a line by just marking the line with an
"@" at the beginning and blanking the line.  If you want to
actually get rid of the lines marked with "@" and see what the
page looks like, typing a ^R will do this.
.IP \s+4.\s0 16
"Dot" is a particularly useful command.  It repeats the last
text modifying command.  Therefore you can type a command once and
then to another place and repeat it by just typing ".".
.IP u 16
Perhaps the most important command in the editor,
u undoes the last command that changed the buffer.
Mnemonic:  \fBu\fRndo
.IP U 16
Undo all the text modifying commands performed on the current line
since the last time you moved onto it.
.IP [cnt]J 16
Join the current line and the following line.  The <nl> is deleted
and the two lines joined, usually with a space between the
end of the first line and the beginning of what was the second
line.  If the first line ended with a "period", then two spaces
are inserted.
A count joins the next cnt lines.
Mnemonic: \fBJ\fRoin lines
.IP Q 16
Switch to \fBex\fR editing mode.
In this mode \fBvi\fR will behave very much like \fBed\fR.
The editor in this mode will operate on single lines normally and
will not attempt to keep the "window" up to date.
Once in this mode it is also possible to switch to the \fBopen\fR
mode of editing.  By entering the command \fB[line number]open<nl>\fR
you enter this mode.  It is similar to the normal visual mode
except the window is only \fBone\fR line long.
Mnemonic: \fBQ\fRuit visual mode
.IP ^] 16
An abbreviation for a tag command.
The cursor should be positioned at the beginning of a word.
That word is taken as a tag name, and the tag with that
name is found as if it had been typed in a :tag command.
.IP [cnt]!{motion}{UNIX\ cmd}<nl> 16
.br
Any UNIX filter
(e.g. command that reads the standard input and outputs something
to the standard output) can be sent a section of the current file and
have the output of the command replace the original text.  Useful
examples are programs like \fBcb\fR, \fBsort\fR, and
\fBnroff\fR.  For instance, using \fBsort\fR it would be possible to
sort a section of the current file into a new list.
Using \fB!!\fR means take a line or lines starting at the line the
cursor is currently on and pass them to the UNIX command.
.B NOTE:
To just escape to the shell for one command,
use :!{cmd}<nl>, see section 5.
.IP z{cnt}<nl> 16
This resets the current window size to "cnt" lines and redraws the screen.
.LE
.NH 2
Special Insert Characters
.LP
There are some characters that have special meanings during
insert modes.  They are:
.VL 16
.IP ^V 16
During inserts, typing a ^V allows you to quote control characters
into the file.  Any character typed after the ^V will be inserted
into the file.
.IP [^]^D\ or\ [0]^D 16
<^D> without any argument backs up one \fBshiftwidth\fR.  This is necessary
to remove indentation that was inserted by the \fBautoindent\fR feature.
^<^D> temporarily removes all the autoindentation, thus placing the cursor
at the left margin.  On the next line, the previous indent level will be
restored.  This is useful for putting "labels" at the left margin.
0<^D> says remove all autoindents and stay that way.  Thus the cursor
moves to the left margin and stays there on successive lines until
<tab>'s are typed.  As with the <tab>, the <^D> is only effective before
any other "non-autoindent" controlling characters are typed.
Mnemonic: \fBD\fRelete a shiftwidth
.IP ^W 16
If the cursor is sitting on a word, <^W> moves the cursor back to the beginning
of the word, thus erasing the word from the insert.
Mnemonic: erase \fBW\fRord
.IP <bs> 16
The backspace always serves as an erase during insert modes in addition
to your normal "erase" character.  To insert a <bs> into your file, use
the <^V> to quote it.
.LE
.NH 1
\fB:\fR Commands
.LP
Typing a ":" during command mode causes \fBvi\fR to put the cursor at
the bottom on the screen in preparation for a command.  In the
":" mode, \fBvi\fR can be given most \fBed\fR commands.  It is
also from this mode that you exit from \fBvi\fR or switch to different
files.  All commands of this variety are terminated by a <nl>, <cr>,
or <esc>.
.VL 16
.IP ":w[!] [file]" 16
Causes \fBvi\fR to write out the current text to the disk.  It is
written to the file you are editing unless "file" is supplied.  If
"file" is supplied, the write is directed to that file instead.  If
that file already exists, \fBvi\fR will not perform the write unless
the "!" is supplied indicating you
.I really
want to destroy the older copy of the file.
.IP :q[!] 16
Causes \fBvi\fR to exit.  If you have modified the file you are
looking at currently and haven't written it out, \fBvi\fR will
refuse to exit unless the "!" is supplied.
.IP ":e[!] [+[cmd]] [file]" 16
.sp 1
Start editing a new file called "file" or start editing the current
file over again.  The command ":e!" says "ignore the changes I've made
to this file and start over from the beginning".  It is useful if
you really mess up the file.  The optional "+" says instead of starting
at the beginning, start at the "end", or,
if "cmd" is supplied, execute "cmd" first.
Useful cases of this are where cmd is "n" (any integer) which starts
at line number n,
and "/text", which searches for "text" and starts at the line where
it is found.
.IP "^^" 16
Switch back to the place you were before your last tag command.
If your last tag command stayed within the file, ^^ returns to that tag.
If you have no recent tag command, it will return to the
same place in the previous file that it was showing when you switched
to the current file.
.IP ":n[!]" 16
Start editing the next file in the argument list.  Since \fBvi\fR
can be called with multiple file names, the ":n" command tells it to
stop work on the current file and switch to the next file.  If the
current file was modifies, it has to be written out before the ":n"
will work or else the "!" must be supplied, which says discard the
changes I made to the current file.
.IP ":n[!] file [file file ...]" 16
.sp
Replace the current argument list with a new list of files and start
editing the first file in this new list.
.IP ":r file" 16
Read in a copy of "file" on the line after the cursor.
.IP ":r !cmd" 16
Execute the "cmd" and take its output and put it into the file after
the current line.
.IP ":!cmd" 16
Execute any UNIX shell command.
.IP ":ta[!] tag" 16
.B Vi
looks in the file named
.B tags
in the current directory.
.B Tags
is a file of lines in the format:
.sp 1
.ti +8
tag filename \fBvi\fR-search-command
.sp 1
If \fBvi\fR finds the tag you specified in the \fB:ta\fR command,
it stops editing the current file if necessary and if the current file is
up to date on the disk and switches to the file specified and uses the
search pattern specified to find the "tagged" item of interest.  This
is particularly useful when editing multi-file C programs such as the
operating system.  There is a program called \fBctags\fR which will
generate an appropriate \fBtags\fR file for C and f77
programs so that by saying
\fB:ta function<nl>\fR you will be switched to that function.
It could also be useful when editing multi-file documents, though the
\fBtags\fR file would have to be generated manually.
.LE
.NH 1
Special Arrangements for Startup
.PP
\fBVi\fR takes the value of \fB$TERM\fR and looks up the characteristics
of that terminal in the file \fB/etc/termcap\fR.
If you don't know \fBvi\fR's name for the terminal you are working
on, look in \fB/etc/termcap\fR.
.PP
When \fBvi\fR starts, it attempts to read the variable EXINIT
from your environment.*
If that exists, it takes the values in it as the default values
for certain of its internal constants.  See the section on "Set Values"
for further details.
If EXINIT doesn't exist you will get all the normal defaults.
.FS
* On version 6 systems
Instead of EXINIT, put the startup commands in the file .exrc
in your home directory.
.FE
.PP
Should you inadvertently hang up the phone while inside
.B vi ,
or should the computer crash,
all may not be lost.
Upon returning to the system, type:
.DS
vi \-r file
.DE
This will normally recover the file.  If there is more than one
temporary file for a specific file name, \fBvi\fR recovers the
newest one.  You can get an older version by recovering the
file more than once.
The command "vi -r" without a file name gives you the list of files
that were saved in the last system crash
(but
.I not
the file just saved when the phone was hung up).
.NH 1
Set Commands
.LP
\fBVi\fR has a number of internal variables and switches which can be
set to achieve special affects.
These options come in three forms, those that are switches, which toggle
from off to on and back, those that require a numeric value, and those
that require an alphanumeric string value.
The toggle options are set by a command of the form:
.DS
:set option<nl>
.DE
and turned off with the command:
.DS
:set nooption<nl>
.DE
Commands requiring a value are set with a command of the form:
.DS
:set option=value<nl>
.DE
To display the value of a specific option type:
.DS
:set option?<nl>
.DE
To display only those that you have changed type:
.DS
:set<nl>
.DE
and to display the long table of all the settable parameters and
their current values type:
.DS
:set all<nl>
.DE
.PP
Most of the options have a long form and an abbreviation.  Both are
listed in the following table as well as the normal default value.
.PP
To arrange to have values other than the default used every time you
enter
.B vi ,
place the appropriate
.B set
command in EXINIT in your environment, e.g.
.DS
EXINIT='set ai aw terse sh=/bin/csh'
export EXINIT
.DE
or
.DS
setenv EXINIT 'set ai aw terse sh=/bin/csh'
.DE
for
.B sh
and
.B csh ,
respectively.
These are usually placed in your .profile or .login.
If you are running a system without environments (such as version 6)
you can place the set command in the file .exrc in your home
directory.
.VL 16
.IP autoindent\ ai 16
Default: noai Type: toggle
.br
When in autoindent mode, vi helps you indent code by starting each
line in the same column as the preceding line.
Tabbing to the right with <tab> or <^T> will move this boundary to
the right, and it can be moved to the left with <^D>.
.IP autoprint\ ap 16
Default: ap Type: toggle
.br
Causes the current line to be printed after each ex text modifying command.
This is not of much interest in the normal \fBvi\fR visual mode.
.IP autowrite\ aw 16
Default: noaw type: toggle
.br
Autowrite causes an automatic write to be done if there are unsaved
changes before certain commands which change files or otherwise
interact with the outside world.
These commands are :!, :tag, :next, :rewind, ^^, and ^].
.IP beautify\ bf 16
Default: nobf Type: toggle
.br
Causes all control characters except <tab>, <nl>, and <ff> to be discarded.
.IP directory\ dir 16
Default: dir=/tmp Type: string
.br
This is the directory in which \fBvi\fR puts its temporary file.
.IP errorbells\ eb 16
Default: noeb Type: toggle
.br
Error messages are preceded by a <bell>.
.IP hardtabs\ ht 16
Default: hardtabs=8 Type: numeric
.br
This option contains the value of hardware tabs in your terminal, or
of software tabs expanded by the Unix system.
.IP ignorecase\ ic 16
Default: noic Type: toggle
.br
All upper case characters are mapped to lower case in regular expression
matching.
.IP lisp 16
Default: nolisp Type: toggle
.br
Autoindent for \fBlisp\fR code.  The commands \fB( ) [[\fR and \fB]]\fR
are modified appropriately to affect s-expressions and functions.
.IP list 16
Default: nolist Type: toggle
.br
All printed lines have the <tab> and <nl> characters displayed visually.
.IP magic 16
Default: magic Type: toggle
.br
Enable the metacharacters for matching.  These include \fB. * < > [string]
[^string]\fR and \fB[<chr>-<chr>]\fR.
.IP number\ nu 16
Default: nonu Type: toggle
.br
Each line is displayed with its line number.
.IP open 16
Default: open Type: toggle
.br
When set, prevents entering open or visual modes from ex or edit.
Not of interest from vi.
.IP optimize\ opt 16
Default: opt Type: toggle
.br
Basically of use only when using the \fBex\fR capabilities.  This
option prevents automatic <cr>s from taking place,
and speeds up output of indented lines,
at the expense of losing typeahead on some versions of UNIX.
.IP paragraphs\ para 16
Default: para=IPLPPPQPP\ bp Type: string
.br
Each pair of characters in the string indicate \fBnroff\fR macros
which are to be treated as the beginning of a paragraph for the
\fB{\fR and \fB}\fR commands.  The default string is for the \fB-ms\fR
and \fB-mm\fR macros.
To indicate one letter \fBnroff\fR macros, such as \fB.P\fR or \fB.H\fR,
quote a space in for the second character position.  For example:
.sp 1
.ti +8
:set paragraphs=P\e bp<nl>
.sp 1
would cause \fBvi\fR to consider \fB.P\fR and \fB.bp\fR as paragraph
delimiters.
.IP prompt 16
Default: prompt Type: toggle
.br
In
.B ex
command mode the prompt character \fB:\fR will be printed when
\fBex\fR is waiting for a command.  This is not of interest from vi.
.IP redraw 16
Default: noredraw Type: toggle
.br
On dumb terminals, force the screen to always be up to date,
by sending great amounts of output.  Useful only at high speeds.
.IP report 16
Default: report=5 Type: numeric
.br
This sets the threshold for the number of lines modified.  When
more than this number of lines are modified, removed, or yanked,
\fBvi\fR will report the number of lines changed at the bottom of
the screen.
.IP scroll 16
Default: scroll={1/2 window} Type: numeric
.br
This is the number of lines that the screen scrolls up or down when
using the <^U> and <^D> commands.
.IP sections 16
Default: sections=SHNHH HU Type: string
.br
Each two character pair of this string specify \fBnroff\fR macro names
which are to be treated as the beginning of a section by the
\fB]]\fR and \fB[[\fR commands.  The default string is for the \fB-ms\fR
and \fB-mm\fR macros.
To enter one letter \fBnroff\fR macros, use a quoted space as the
second character.
See \fBparagraphs\fR for a fuller explanation.
.IP shell\ sh 16
Default: sh=from environment SHELL or /bin/sh   Type: string
.br
This is the name of the \fBsh\fR to be used for "escaped" commands.
.IP shiftwidth\ sw 16
Default: sw=8 Type: numeric
.br
This is the number of spaces that a <^T> or <^D> will move over for
indenting, and the amount < and > shift by.
.IP showmatch\ sm 16
Default: nosm Type: toggle
.br
When a \fB)\fR or \fB}\fR is typed, show the matching \fB(\fR or \fB{\fR
by moving the cursor to it for one second if it is on the current screen.
.IP slowopen\ slow 16
Default: terminal dependent Type: toggle
.br
On terminals that are slow and unintelligent, this option prevents the
updating of the screen some of the time to improve speed.
.IP tabstop\ ts 16
Default: ts=8 Type: numeric
.br
<tab>s are expanded to boundaries that are multiples of this value.
.IP taglength\ tl 16
Default: tl=0 Type: numeric
.br
If nonzero, tag names are only significant to this many characters.
.IP term 16
Default: (from environment \fBTERM\fP, else dumb) Type: string
.br
This is the terminal and controls the visual displays.  It cannot be
changed when in "visual" mode,
you have to Q to command mode, type a
set term command, and do ``vi.'' to get back into visual.
Or exit vi, fix $TERM, and reenter.
The definitions that drive a particular
terminal type are found in the file \fB/etc/termcap\fR.
.IP terse 16
Default: terse Type: toggle
.br
When set, the error diagnostics are short.
.IP warn 16
Default: warn Type: toggle
.br
The user is warned if she/he tries to escape to
the shell without writing out the current changes.
.IP window 16
Default: window={8 at 600 baud or less, 16 at 1200 baud, and screen
size \- 1 at 2400 baud or more} Type: numeric
.br
This is the number of lines in the window whenever \fBvi\fR must redraw
an entire screen.  It is useful to make this size smaller if you are
on a slow line.
.IP w300,\ w1200,\ w9600
.br
These set window, but only within the corresponding speed ranges.
They are useful in an EXINIT to fine tune window sizes.
For example,
.DS
set w300=4 w1200=12
.DE
causes a 4 lines window at speed up to 600 baud, a 12 line window at 1200
baud, and a full screen (the default) at over 1200 baud.
.IP wrapscan\ ws 16
Default: ws Type: toggle
.br
Searches will wrap around the end of the file when is option is set.  When
it is off, the search will terminate when it reaches the end or the
beginning of the file.
.IP wrapmargin\ wm 16
Default: wm=0 Type: numeric
.br
\fBVi\fR will automatically insert a <nl> when it finds a natural
break point (usually a <sp> between words) that occurs within
"wm" spaces of the right margin.
Therefore with "wm=0" the option is off.  Setting it to 10 would
mean that any time you are within 10 spaces of the right margin
\fBvi\fR would be looking for a <sp> or <tab> which it could
replace with a <nl>.  This is convenient for people who forget
to look at the screen while they type.
(In version 3, wrapmargin behaves more like nroff, in that the
boundary specified by the distance from the right edge of the screen
is taken as the rightmost edge of the area where a break is allowed,
instead of the leftmost edge.)
.IP writeany\ wa 16
Default: nowa Type: toggle
.br
\fBVi\fR normally makes a number of checks before it writes out a file.
This prevents the user from inadvertently destroying a file.  When the
"writeany" option is enabled, \fBvi\fR no longer makes these checks.
.LE
