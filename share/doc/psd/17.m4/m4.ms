.\" Copyright (C) Caldera International Inc. 2001-2002.  All rights reserved.
.\" 
.\" Redistribution and use in source and binary forms, with or without
.\" modification, are permitted provided that the following conditions are
.\" met:
.\" 
.\" Redistributions of source code and documentation must retain the above
.\" copyright notice, this list of conditions and the following
.\" disclaimer.
.\" 
.\" Redistributions in binary form must reproduce the above copyright
.\" notice, this list of conditions and the following disclaimer in the
.\" documentation and/or other materials provided with the distribution.
.\" 
.\" All advertising materials mentioning features or use of this software
.\" must display the following acknowledgement:
.\" 
.\" This product includes software developed or owned by Caldera
.\" International, Inc.  Neither the name of Caldera International, Inc.
.\" nor the names of other contributors may be used to endorse or promote
.\" products derived from this software without specific prior written
.\" permission.
.\" 
.\" USE OF THE SOFTWARE PROVIDED FOR UNDER THIS LICENSE BY CALDERA
.\" INTERNATIONAL, INC.  AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR
.\" IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
.\" WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
.\" DISCLAIMED.  IN NO EVENT SHALL CALDERA INTERNATIONAL, INC. BE LIABLE
.\" FOR ANY DIRECT, INDIRECT INCIDENTAL, SPECIAL, EXEMPLARY, OR
.\" CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
.\" SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
.\" BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
.\" WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
.\" OR OTHERWISE) RISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
.\" IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
.\" 
.\"	@(#)m4.ms	6.3 (Berkeley) 6/5/93
.\"
.\" $FreeBSD$
.de MH
Bell Laboratories, Murray Hill, NJ 07974.
..
.EH 'PSD:17-%''The M4 Macro Processor'
.OH 'The M4 Macro Processor''PSD:17-%'
.if n .ls 2
.tr _\(em
.tr *\(**
.de UC
\&\\$3\s-1\\$1\\s0\&\\$2
..
.de IT
.if n .ul
\&\\$3\f2\\$1\fP\&\\$2
..
.de UL
.if n .ul
\&\\$3\f3\\$1\fP\&\\$2
..
.de P1
.DS I 3n
.if n .ls 2
.nf
.if n .ta 5 10 15 20 25 30 35 40 45 50 55 60
.if t .ta .4i .8i 1.2i 1.6i 2i 2.4i 2.8i 3.2i 3.6i 4i 4.4i 4.8i 5.2i 5.6i
.if t .tr -\(mi|\(bv'\(fm^\(no*\(**
.tr `\(ga'\(aa
.if t .tr _\(ul
.ft 3
.lg 0
..
.de P2
.ps \\n(PS
.vs \\n(VSp
.ft R
.if n .ls 2
.tr --||''^^!!
.if t .tr _\(em
.fi
.lg
.DE
.if t .tr _\(em
..
.hw semi-colon
.hw estab-lished
.hy 14
.		\"2=not last lines; 4= no -xx; 8=no xx-
.		\"special chars in programs
.	\" start of text
.\".RP
.\" .....TR 59
.\" .....TM 77-1273-6 39199 39199-11
.ND "July 1, 1977"
.TL
The M4 Macro Processor
.AU "MH 2C-518" 6021
Brian W. Kernighan
.AU "MH 2C-517" 3770
Dennis M. Ritchie
.AI
.MH
.AB
.PP
M4 is a macro processor available on
.UX
and
.UC GCOS .
Its primary use has been as a
front end for Ratfor for those
cases where parameterless macros 
are not adequately powerful.
It has also been used for languages as disparate as C and Cobol.
M4 is particularly suited for functional languages like Fortran, PL/I and C
since macros are specified in a functional notation.
.PP
M4 provides features seldom found even in much larger
macro processors, 
including
.IP "  \(bu"
arguments
.IP "  \(bu"
condition testing
.IP "  \(bu"
arithmetic capabilities
.IP "  \(bu"
string and substring functions
.IP "  \(bu"
file manipulation
.LP
.PP
This paper is a user's manual for M4.
.AE
.\" .CS 6 0 6 0 0 1
.if t .2C
.SH
Introduction
.PP
A macro processor is a useful way to enhance a programming language,
to make it more palatable
or more readable,
or to tailor it to a particular application.
The
.UL #define
statement in C
and the analogous
.UL define
in Ratfor
are examples of the basic facility provided by
any macro processor _
replacement of text by other text.
.PP
The M4 macro processor is an extension of a macro processor called M3
which was written by D. M. Ritchie
for the AP-3 minicomputer;
M3 was in turn based on a macro processor implemented for [1].
Readers unfamiliar with the basic ideas of macro processing
may wish to read some of the discussion there.
.PP
M4 is a suitable front end for Ratfor and C,
and has also been used successfully with Cobol.
Besides the straightforward replacement of one string of text by another,
it provides
macros with arguments,
conditional macro expansion,
arithmetic,
file manipulation,
and some specialized string processing functions.
.PP
The basic operation of M4
is to copy its input to its output.
As the input is read, however, each alphanumeric ``token''
(that is, string of letters and digits) is checked.
If it is the name of a macro,
then the name of the macro is replaced by its defining text,
and the resulting string is pushed back onto the
input to be rescanned.
Macros may be called with arguments, in which case the arguments are collected
and substituted into the right places in the defining text
before it is rescanned.
.PP
M4 provides a collection of about twenty built-in
macros
which perform various useful operations;
in addition, the user can define new macros.
Built-ins and user-defined macros work exactly the same way, except that
some of the built-in macros have side effects
on the state of the process.
.SH
Usage
.PP
On
.UC UNIX ,
use
.P1
m4 [files]
.P2
Each argument file is processed in order;
if there are no arguments, or if an argument
is `\-',
the standard input is read at that point.
The processed text is written on the standard output,
which may be captured for subsequent processing with
.P1
m4 [files] >outputfile
.P2
On
.UC GCOS ,
usage is identical, but the program is called
.UL \&./m4 .
.SH
Defining Macros
.PP
The primary built-in function of M4
is
.UL define ,
which is used to define new macros.
The input
.P1
define(name, stuff)
.P2
causes the string
.UL name
to be defined as
.UL stuff .
All subsequent occurrences of
.UL name
will be replaced by
.UL stuff .
.UL name
must be alphanumeric and must begin with a letter
(the underscore \(ul counts as a letter).
.UL stuff
is any text that contains balanced parentheses;
it may stretch over multiple lines.
.PP
Thus, as a typical example,
.P1
define(N, 100)
 ...
if (i > N)
.P2
defines
.UL N
to be 100, and uses this ``symbolic constant'' in a later
.UL if
statement.
.PP
The left parenthesis must immediately follow the word
.UL define ,
to signal that
.UL define
has arguments.
If a macro or built-in name is not followed immediately by `(',
it is assumed to have no arguments.
This is the situation for
.UL N
above;
it is actually a macro with no arguments,
and thus when it is used there need be no (...) following it.
.PP
You should also notice that a macro name is only recognized as such
if it appears surrounded by non-alphanumerics.
For example, in
.P1
define(N, 100)
 ...
if (NNN > 100)
.P2
the variable 
.UL NNN
is absolutely unrelated to the defined macro
.UL N ,
even though it contains a lot of
.UL N 's.
.PP
Things may be defined in terms of other things.
For example,
.P1
define(N, 100)
define(M, N)
.P2
defines both M and N to be 100.
.PP
What happens if
.UL N
is redefined?
Or, to say it another way, is
.UL M 
defined as
.UL N
or as 100?
In M4,
the latter is true _
.UL M
is 100, so even if
.UL N 
subsequently changes,
.UL M
does not.
.PP
This behavior arises because
M4 expands macro names into their defining text as soon as it possibly can.
Here, that means that when the string
.UL N
is seen as the arguments of
.UL define
are being collected, it is immediately replaced by 100;
it's just as if you had said
.P1
define(M, 100)
.P2
in the first place.
.PP
If this isn't what you really want, there are two ways out of it.
The first, which is specific to this situation,
is to interchange the order of the definitions:
.P1
define(M, N)
define(N, 100)
.P2
Now
.UL M
is defined to be the string
.UL N ,
so when you ask for 
.UL M
later, you'll always get the value of
.UL N 
at that time
(because the
.UL M
will be replaced by
.UL N
which will be replaced by 100).
.SH
Quoting
.PP
The more general solution is to delay the expansion of
the arguments of
.UL define 
by
.ul
quoting
them.
Any text surrounded by the single quotes \(ga and \(aa
is not expanded immediately, but has the quotes stripped off.
If you say
.P1
define(N, 100)
define(M, `N')
.P2
the quotes around the
.UL N
are stripped off as the argument is being collected,
but they have served their purpose, and 
.UL M
is defined as
the string
.UL N ,
not 100.
The general rule is that M4 always strips off
one level of single quotes whenever it evaluates
something.
This is true even outside of
macros.
If you want the word
.UL define
to appear in the output,
you have to quote it in the input,
as in
.P1
	`define' = 1;
.P2
.PP
As another instance of the same thing, which is a bit more surprising,
consider redefining
.UL N :
.P1
define(N, 100)
 ...
define(N, 200)
.P2
Perhaps regrettably, the
.UL N
in the second definition is
evaluated as soon as it's seen;
that is, it is
replaced by
100, so it's as if you had written
.P1
define(100, 200)
.P2
This statement is ignored by M4, since you can only define things that look
like names, but it obviously doesn't have the effect you wanted.
To really redefine 
.UL N ,
you must delay the evaluation by quoting:
.P1
define(N, 100)
 ...
define(`N', 200)
.P2
In M4,
it is often wise to quote the first argument of a macro.
.PP
If \` and \' are not convenient for some reason,
the quote characters can be changed with the built-in
.UL changequote :
.P1
changequote([, ])
.P2
makes the new quote characters the left and right brackets.
You can restore the original characters with just
.P1
changequote
.P2
.PP
There are two additional built-ins related to
.UL define .
.UL undefine
removes the definition of some macro or built-in:
.P1
undefine(`N')
.P2
removes the definition of
.UL N .
(Why are the quotes absolutely necessary?)
Built-ins can be removed with 
.UL undefine ,
as in
.P1
undefine(`define')
.P2
but once you remove one, you can never get it back.
.PP
The built-in 
.UL ifdef
provides a way to determine if a macro is currently defined.
In particular, M4 has pre-defined the names
.UL unix
and
.UL gcos
on the corresponding systems, so you can
tell which one you're using:
.P1
ifdef(`unix', `define(wordsize,16)' )
ifdef(`gcos', `define(wordsize,36)' )
.P2
makes a definition appropriate for the particular machine.
Don't forget the quotes!
.PP
.UL ifdef
actually permits three arguments;
if the name is undefined, the value of
.UL ifdef
is then the third argument, as in
.P1
ifdef(`unix', on UNIX, not on UNIX)
.P2
.SH
Arguments
.PP
So far we have discussed the simplest form of macro processing _
replacing one string by another (fixed) string.
User-defined macros may also have arguments, so different invocations
can have different results.
Within the replacement text for a macro
(the second argument of its
.UL define )
any occurrence of
.UL $n
will be replaced by the 
.UL n th
argument when the macro
is actually used.
Thus, the macro
.UL bump ,
defined as
.P1
define(bump, $1 = $1 + 1)
.P2
generates code to increment its argument by 1:
.P1
bump(x)
.P2
is
.P1
x = x + 1
.P2
.PP
A macro can have as many arguments as you want,
but only the first nine are accessible,
through
.UL $1
to
.UL $9 .
(The macro name itself is
.UL $0 ,
although that is less commonly used.)
Arguments that are not supplied are replaced by null strings,
so
we can define a macro
.UL cat
which simply concatenates its arguments, like this:
.P1
define(cat, $1$2$3$4$5$6$7$8$9)
.P2
Thus
.P1
cat(x, y, z)
.P2
is equivalent to
.P1
xyz
.P2
.UL $4
through
.UL $9
are null, since no corresponding arguments were provided.
.PP
.PP
Leading unquoted blanks, tabs, or newlines that occur during argument collection
are discarded.
All other white space is retained.
Thus
.P1
define(a,   b   c)
.P2
defines
.UL a
to be
.UL b\ \ \ c .
.PP
Arguments are separated by commas, but parentheses are counted properly,
so a comma ``protected'' by parentheses does not terminate an argument.
That is, in
.P1
define(a, (b,c))
.P2
there are only two arguments;
the second is literally
.UL (b,c) .
And of course a bare comma or parenthesis can be inserted by quoting it.
.SH
Arithmetic Built-ins
.PP
M4 provides two built-in functions for doing arithmetic
on integers (only).
The simplest is
.UL incr ,
which increments its numeric argument by 1.
Thus to handle the common programming situation
where you want a variable to be defined as ``one more than N'',
write
.P1
define(N, 100)
define(N1, `incr(N)')
.P2
Then
.UL N1
is defined as one more than the current value of
.UL N .
.PP
The more general mechanism for arithmetic is a built-in
called
.UL eval ,
which is capable of arbitrary arithmetic on integers.
It provides the operators
(in decreasing order of precedence)
.DS
unary + and \(mi
** or ^	(exponentiation)
*  /  % (modulus)
+  \(mi
==  !=  <  <=  >  >=
!		(not)
& or &&	(logical and)
\(or or \(or\(or		(logical or)
.DE
Parentheses may be used to group operations where needed.
All the operands of
an expression given to
.UL eval
must ultimately be numeric.
The numeric value of a true relation
(like 1>0)
is 1, and false is 0.
The precision in
.UL eval
is
32 bits on
.UC UNIX
and 36 bits on
.UC GCOS .
.PP
As a simple example, suppose we want 
.UL M
to be 
.UL 2**N+1 .
Then
.P1
define(N, 3)	
define(M, `eval(2**N+1)')
.P2
As a matter of principle, it is advisable
to quote the defining text for a macro
unless it is very simple indeed
(say just a number);
it usually gives the result you want,
and is a good habit to get into.
.SH
File Manipulation
.PP
You can include a new file in the input at any time by
the built-in function
.UL include :
.P1
include(filename)
.P2
inserts the contents of
.UL filename
in place of the
.UL include
command.
The contents of the file is often a set of definitions.
The value
of
.UL include
(that is, its replacement text)
is the contents of the file;
this can be captured in definitions, etc.
.PP
It is a fatal error if the file named in
.UL include
cannot be accessed.
To get some control over this situation, the alternate form
.UL sinclude
can be used;
.UL sinclude 
(``silent include'')
says nothing and continues if it can't access the file.
.PP
It is also possible to divert the output of M4 to temporary files during processing,
and output the collected material upon command.
M4 maintains nine of these diversions, numbered 1 through 9.
If you say
.P1
divert(n)
.P2
all subsequent output is put onto the end of a temporary file
referred to as
.UL n .
Diverting to this file is stopped by another 
.UL divert 
command;
in particular,
.UL divert
or
.UL divert(0)
resumes the normal output process.
.PP
Diverted text is normally output all at once
at the end of processing,
with the diversions output in numeric order.
It is possible, however, to bring back diversions
at any time,
that is, to append them to the current diversion.
.P1
undivert
.P2
brings back all diversions in numeric order, and
.UL undivert
with arguments brings back the selected diversions
in the order given.
The act of undiverting discards the diverted stuff,
as does diverting into a diversion 
whose number is not between 0 and 9 inclusive.
.PP
The value of
.UL undivert
is
.ul
not
the diverted stuff.
Furthermore, the diverted material is
.ul
not
rescanned for macros.
.PP
The built-in
.UL divnum
returns the number of the currently active diversion.
This is zero during normal processing.
.SH
System Command
.PP
You can run any program in the local operating system
with the
.UL syscmd
built-in.
For example,
.P1
syscmd(date)
.P2
on
.UC UNIX
runs the
.UL date
command.
Normally
.UL syscmd
would be used to create a file
for a subsequent
.UL include .
.PP
To facilitate making unique file names, the built-in
.UL maketemp
is provided, with specifications identical to the system function
.ul
mktemp:
a string of XXXXX in the argument is replaced
by the process id of the current process.
.SH
Conditionals
.PP
There is a built-in called
.UL ifelse
which enables you to perform arbitrary conditional testing.
In the simplest form,
.P1
ifelse(a, b, c, d)
.P2
compares the two strings
.UL a
and
.UL b .
If these are identical, 
.UL ifelse
returns
the string
.UL c ;
otherwise it returns
.UL d .
Thus we might define a macro called
.UL compare
which compares two strings and returns ``yes'' or ``no''
if they are the same or different.
.P1
define(compare, `ifelse($1, $2, yes, no)')
.P2
Note the quotes,
which prevent too-early evaluation of
.UL ifelse .
.PP
If the fourth argument is missing, it is treated as empty.
.PP
.UL ifelse
can actually have any number of arguments,
and thus provides a limited form of multi-way decision capability.
In the input
.P1
ifelse(a, b, c, d, e, f, g)
.P2
if the string
.UL a
matches the string
.UL b ,
the result is
.UL c .
Otherwise, if
.UL d
is the same as
.UL e ,
the result is
.UL f .
Otherwise the result is
.UL g .
If the final argument
is omitted, the result is null,
so
.P1
ifelse(a, b, c)
.P2
is
.UL c
if 
.UL a
matches
.UL b ,
and null otherwise.
.SH
String Manipulation
.PP
The built-in
.UL len
returns the length of the string that makes up its argument.
Thus
.P1
len(abcdef)
.P2
is 6, and
.UL len((a,b))
is 5.
.PP
The built-in
.UL substr
can be used to produce substrings of strings.
.UL substr(s,\ i,\ n)
returns the substring of
.UL s
that starts at the
.UL i th
position
(origin zero),
and is
.UL n
characters long.
If 
.UL n
is omitted, the rest of the string is returned,
so
.P1
substr(`now is the time', 1)
.P2
is
.P1
ow is the time
.P2
If 
.UL i
or
.UL n
are out of range, various sensible things happen.
.PP
.UL index(s1,\ s2)
returns the index (position) in
.UL s1
where the string
.UL s2
occurs, or \-1
if it doesn't occur.
As with
.UL substr ,
the origin for strings is 0.
.PP
The built-in
.UL translit
performs character transliteration.
.P1
translit(s, f, t)
.P2
modifies
.UL s
by replacing any character found in
.UL f
by the corresponding character of
.UL t .
That is,
.P1
translit(s, aeiou, 12345)
.P2
replaces the vowels by the corresponding digits.
If
.UL t
is shorter than
.UL f ,
characters which don't have an entry in
.UL t
are deleted; as a limiting case,
if
.UL t
is not present at all,
characters from 
.UL f
are deleted from 
.UL s .
So
.P1
translit(s, aeiou)
.P2
deletes vowels from 
.UL s .
.PP
There is also a built-in called
.UL dnl
which deletes all characters that follow it up to
and including the next newline;
it is useful mainly for throwing away 
empty lines that otherwise tend to clutter up M4 output.
For example, if you say
.P1
define(N, 100)
define(M, 200)
define(L, 300)
.P2
the newline at the end of each line is not part of the definition,
so it is copied into the output, where it may not be wanted.
If you add
.UL dnl
to each of these lines, the newlines will disappear.
.PP
Another way to achieve this, due to J. E. Weythman,
is
.P1
divert(-1)
	define(...)
	...
divert
.P2
.SH
Printing
.PP
The built-in
.UL errprint
writes its arguments out on the standard error file.
Thus you can say
.P1
errprint(`fatal error')
.P2
.PP
.UL dumpdef
is a debugging aid which
dumps the current definitions of defined terms.
If there are no arguments, you get everything;
otherwise you get the ones you name as arguments.
Don't forget to quote the names!
.SH
Summary of Built-ins
.PP
Each entry is preceded by the
page number where it is described.
.DS
.tr '\'`\`
.ta .25i
3	changequote(L, R)
1	define(name, replacement)
4	divert(number)
4	divnum
5	dnl
5	dumpdef(`name', `name', ...)
5	errprint(s, s, ...)
4	eval(numeric expression)
3	ifdef(`name', this if true, this if false)
5	ifelse(a, b, c, d)
4	include(file)
3	incr(number)
5	index(s1, s2)
5	len(string)
4	maketemp(...XXXXX...)
4	sinclude(file)
5	substr(string, position, number)
4	syscmd(s)
5	translit(str, from, to)
3	undefine(`name')
4	undivert(number,number,...)
.DE
.SH
Acknowledgements
.PP
We are indebted to Rick Becker, John Chambers,
Doug McIlroy,
and especially Jim Weythman,
whose pioneering use of M4 has led to several valuable improvements.
We are also deeply grateful to Weythman for several substantial contributions
to the code.
.\" .SG
.SH
References
.LP
.IP [1]
B. W. Kernighan and P. J. Plauger,
.ul
Software Tools,
Addison-Wesley, Inc., 1976.
