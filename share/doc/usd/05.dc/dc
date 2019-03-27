.\"	$FreeBSD$
.\"	$OpenBSD: dc,v 1.2 2003/09/22 19:08:27 otto Exp $
.\"
.\" Copyright (C) Caldera International Inc.  2001-2002.
.\" All rights reserved.
.\"
.\" Redistribution and use in source and binary forms, with or without
.\" modification, are permitted provided that the following conditions
.\" are met:
.\" 1. Redistributions of source code and documentation must retain the above
.\"    copyright notice, this list of conditions and the following disclaimer.
.\" 2. Redistributions in binary form must reproduce the above copyright
.\"    notice, this list of conditions and the following disclaimer in the
.\"    documentation and/or other materials provided with the distribution.
.\" 3. All advertising materials mentioning features or use of this software
.\"    must display the following acknowledgement:
.\"	This product includes software developed or owned by Caldera
.\"	International, Inc.
.\" 4. Neither the name of Caldera International, Inc. nor the names of other
.\"    contributors may be used to endorse or promote products derived from
.\"    this software without specific prior written permission.
.\"
.\" USE OF THE SOFTWARE PROVIDED FOR UNDER THIS LICENSE BY CALDERA
.\" INTERNATIONAL, INC. AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR
.\" IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
.\" OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
.\" IN NO EVENT SHALL CALDERA INTERNATIONAL, INC. BE LIABLE FOR ANY DIRECT,
.\" INDIRECT INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
.\" (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
.\" SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
.\" HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
.\" STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
.\" IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
.\" POSSIBILITY OF SUCH DAMAGE.
.\"
.\"	@(#)dc	8.1 (Berkeley) 6/8/93
.\"
.EH 'USD:5-%''DC \- An Interactive Desk Calculator'
.OH 'DC \- An Interactive Desk Calculator''USD:5-%'
.\".RP
.\" ....TM 75-1271-8 39199 39199-11
.ND
.TL
DC \- An Interactive Desk Calculator
.AU "MH 2C-524" 3878
Robert Morris
.AU
Lorinda Cherry
.AI
.\" .MH
.AB
DC is an interactive desk calculator program implemented
on the
.UX
time-sharing system to do arbitrary-precision
integer arithmetic.
It has provision for manipulating scaled fixed-point numbers and
for input and output in bases other than decimal.
.PP
The size of numbers that can be manipulated is limited
only by available core storage.
On typical implementations of
.UX ,
the size of numbers that
can be handled varies from several hundred digits on the smallest
systems to several thousand on the largest.
.AE
.PP
.SH
.PP
.ft I
Editor's note: the description of the implementation details of DC in this
paper is only valid for the original version of DC.
The current version of DC uses a different approach.
.ft
.PP
DC is an arbitrary precision arithmetic package implemented
on the
.UX
time-sharing system
in the form of an interactive desk calculator.
It works like a stacking calculator using reverse Polish notation.
Ordinarily DC operates on decimal integers, but one may
specify an input base, output base, and a number of fractional
digits to be maintained.
.PP
A language called BC [1] has been developed which accepts
programs written in the familiar style of higher-level
programming languages and compiles output which is
interpreted by DC.
Some of the commands described below were designed
for the compiler interface and are not easy for a human user
to manipulate.
.PP
Numbers that are typed into DC are put on a push-down
stack.
DC commands work by taking the top number or two
off the stack, performing the desired operation, and pushing the result
on the stack.
If an argument is given,
input is taken from that file until its end,
then from the standard input.
.SH
SYNOPTIC DESCRIPTION
.PP
Here we describe the DC commands that are intended
for use by people.  The additional commands that are
intended to be invoked by compiled output are
described in the detailed description.
.PP
Any number of commands are permitted on a line.
Blanks and new-line characters are ignored except within numbers
and in places where a register name is expected.
.PP
The following constructions are recognized:
.SH
number
.IP
The value of the number is pushed onto the main stack.
A number is an unbroken string of the digits 0-9
and the capital letters A\-F which are treated as digits
with values 10\-15 respectively.
The number may be preceded by an underscore _ to input a
negative number.
Numbers may contain decimal points.
.SH
+  \-  *  %  ^
.IP
The
top two values on the stack are added
(\fB+\fP),
subtracted
(\fB\-\fP),
multiplied (\fB*\fP),
divided (\fB/\fP),
remaindered (\fB%\fP),
or exponentiated (^).
The two entries are popped off the stack;
the result is pushed on the stack in their place.
The result of a division is an integer truncated toward zero.
See the detailed description below for the treatment of
numbers with decimal points.
An exponent must not have any digits after the decimal point.
.SH
s\fIx\fP
.IP
The
top of the main stack is popped and stored into
a register named \fIx\fP, where \fIx\fP may be any character.
If
the
.ft B
s
.ft
is capitalized,
.ft I
x
.ft
is treated as a stack and the value is pushed onto it.
Any character, even blank or new-line, is a valid register name.
.SH
l\fIx\fP
.IP
The
value in register
.ft I
x
.ft
is pushed onto the stack.
The register
.ft I
x
.ft
is not altered.
If the
.ft B
l
.ft
is capitalized,
register
.ft I
x
.ft
is treated as a stack and its top value is popped onto the main stack.
.LP
All registers start with empty value which is treated as a zero
by the command \fBl\fP and is treated as an error by the command \fBL\fP.
.SH
d
.IP
The
top value on the stack is duplicated.
.SH
p
.IP
The top value on the stack is printed.
The top value remains unchanged.
.SH
f
.IP
All values on the stack and in registers are printed.
.SH
x
.IP
treats the top element of the stack as a character string,
removes it from the stack, and
executes it as a string of DC commands.
.SH
[ ... ]
.IP
puts the bracketed character string onto the top of the stack.
.SH
q
.IP
exits the program.
If executing a string, the recursion level is
popped by two.
If
.ft B
q
.ft
is capitalized,
the top value on the stack is popped and the string execution level is popped
by that value.
.SH
<\fIx\fP  >\fIx\fP  =\fIx\fP  !<\fIx\fP  !>\fIx\fP  !=\fIx\fP
.IP
The
top two elements of the stack are popped and compared.
Register
.ft I
x
.ft
is executed if they obey the stated
relation.
Exclamation point is negation.
.SH
v
.IP
replaces the top element on the stack by its square root.
The square root of an integer is truncated to an integer.
For the treatment of numbers with decimal points, see
the detailed description below.
.SH
!
.IP
interprets the rest of the line as a
.UX
command.
Control returns to DC when the
.UX
command terminates.
.SH
c
.IP
All values on the stack are popped; the stack becomes empty.
.SH
i
.IP
The top value on the stack is popped and used as the
number radix for further input.
If \fBi\fP is capitalized, the value of
the input base is pushed onto the stack.
No mechanism has been provided for the input of arbitrary
numbers in bases less than 1 or greater than 16.
.SH
o
.IP
The top value on the stack is popped and used as the
number radix for further output.
If \fBo\fP is capitalized, the value of the output
base is pushed onto the stack.
.SH
k
.IP
The top of the stack is popped, and that value is used as
a scale factor
that influences the number of decimal places
that are maintained during multiplication, division, and exponentiation.
The scale factor must be greater than or equal to zero and
less than 100.
If \fBk\fP is capitalized, the value of the scale factor
is pushed onto the stack.
.SH
z
.IP
The value of the stack level is pushed onto the stack.
.SH
?
.IP
A line of input is taken from the input source (usually the console)
and executed.
.SH
DETAILED DESCRIPTION
.SH
Internal Representation of Numbers
.PP
Numbers are stored internally using a dynamic storage allocator.
Numbers are kept in the form of a string
of digits to the base 100 stored one digit per byte
(centennial digits).
The string is stored with the low-order digit at the
beginning of the string.
For example, the representation of 157
is 57,1.
After any arithmetic operation on a number, care is taken
that all digits are in the range 0\-99 and that
the number has no leading zeros.
The number zero is represented by the empty string.
.PP
Negative numbers are represented in the 100's complement
notation, which is analogous to two's complement notation for binary
numbers.
The high order digit of a negative number is always \-1
and all other digits are in the range 0\-99.
The digit preceding the high order \-1 digit is never a 99.
The representation of \-157 is 43,98,\-1.
We shall call this the canonical form of a number.
The advantage of this kind of representation of negative
numbers is ease of addition.  When addition is performed digit
by digit, the result is formally correct.  The result need only
be modified, if necessary, to put it into canonical form.
.PP
Because the largest valid digit is 99 and the byte can
hold numbers twice that large, addition can be carried out
and the handling of carries done later when
that is convenient, as it sometimes is.
.PP
An additional byte is stored with each number beyond
the high order digit to indicate the number of
assumed decimal digits after the decimal point.  The representation
of .001 is 1,\fI3\fP
where the scale has been italicized to emphasize the fact that it
is not the high order digit.
The value of this extra byte is called the
.ft B
scale factor
.ft
of the number.
.SH
The Allocator
.PP
DC uses a dynamic string storage allocator
for all of its internal storage.
All reading and writing of numbers internally is done through
the allocator.
Associated with each string in the allocator is a four-word header containing pointers
to the beginning of the string, the end of the string,
the next place to write, and the next place to read.
Communication between the allocator and DC
is done via pointers to these headers.
.PP
The allocator initially has one large string on a list
of free strings.  All headers except the one pointing
to this string are on a list of free headers.
Requests for strings are made by size.
The size of the string actually supplied is the next higher
power of 2.
When a request for a string is made, the allocator
first checks the free list to see if there is
a string of the desired size.
If none is found, the allocator finds the next larger free string and splits it repeatedly until
it has a string of the right size.
Left-over strings are put on the free list.
If there are no larger strings,
the allocator tries to coalesce smaller free strings into
larger ones.
Since all strings are the result
of splitting large strings,
each string has a neighbor that is next to it in core
and, if free, can be combined with it to make a string twice as long.
This is an implementation of the `buddy system' of allocation
described in [2].
.PP
Failing to find a string of the proper length after coalescing,
the allocator asks the system for more space.
The amount of space on the system is the only limitation
on the size and number of strings in DC.
If at any time in the process of trying to allocate a string, the allocator runs out of
headers, it also asks the system for more space.
.PP
There are routines in the allocator for reading, writing, copying, rewinding,
forward-spacing, and backspacing strings.
All string manipulation is done using these routines.
.PP
The reading and writing routines
increment the read pointer or write pointer so that
the characters of a string are read or written in
succession by a series of read or write calls.
The write pointer is interpreted as the end of the
information-containing portion of a string and a call
to read beyond that point returns an end-of-string indication.
An attempt to write beyond the end of a string
causes the allocator to
allocate a larger space and then copy
the old string into the larger block.
.SH
Internal Arithmetic
.PP
All arithmetic operations are done on integers.
The operands (or operand) needed for the operation are popped
from the main stack and their scale factors stripped off.
Zeros are added or digits removed as necessary to get
a properly scaled result from the internal arithmetic routine.
For example, if the scale of the operands is different and decimal
alignment is required, as it is for
addition, zeros are appended to the operand with the smaller
scale.
After performing the required arithmetic operation,
the proper scale factor is appended to the end of the number before
it is pushed on the stack.
.PP
A register called \fBscale\fP plays a part
in the results of most arithmetic operations.
\fBscale\fP is the bound on the number of decimal places retained in
arithmetic computations.
\fBscale\fP may be set to the number on the top of the stack
truncated to an integer with the \fBk\fP command.
\fBK\fP may be used to push the value of \fBscale\fP on the stack.
\fBscale\fP must be greater than or equal to 0 and less than 100.
The descriptions of the individual arithmetic operations will
include the exact effect of \fBscale\fP on the computations.
.SH
Addition and Subtraction
.PP
The scales of the two numbers are compared and trailing
zeros are supplied to the number with the lower scale to give both
numbers the same scale.  The number with the smaller scale is
multiplied by 10 if the difference of the scales is odd.
The scale of the result is then set to the larger of the scales
of the two operands.
.PP
Subtraction is performed by negating the number
to be subtracted and proceeding as in addition.
.PP
Finally, the addition is performed digit by digit from the
low order end of the number.  The carries are propagated
in the usual way.
The resulting number is brought into canonical form, which may
require stripping of leading zeros, or for negative numbers
replacing the high-order configuration 99,\-1 by the digit \-1.
In any case, digits which are not in the range 0\-99 must
be brought into that range, propagating any carries or borrows
that result.
.SH
Multiplication
.PP
The scales are removed from the two operands and saved.
The operands are both made positive.
Then multiplication is performed in
a digit by digit manner that exactly mimics the hand method
of multiplying.
The first number is multiplied by each digit of the second
number, beginning with its low order digit.  The intermediate
products are accumulated into a partial sum which becomes the
final product.
The product is put into the canonical form and its sign is
computed from the signs of the original operands.
.PP
The scale of the result is set equal to the sum
of the scales of the two operands.
If that scale is larger than the internal register
.ft B
scale
.ft
and also larger than both of the scales of the two operands,
then the scale of the result is set equal to the largest
of these three last quantities.
.SH
Division
.PP
The scales are removed from the two operands.
Zeros are appended or digits removed from the dividend to make
the scale of the result of the integer division equal to
the internal quantity
\fBscale\fP.
The signs are removed and saved.
.PP
Division is performed much as it would be done by hand.
The difference of the lengths of the two numbers
is computed.
If the divisor is longer than the dividend,
zero is returned.
Otherwise the top digit of the divisor is divided into the top
two digits of the dividend.
The result is used as the first (high-order) digit of the
quotient.
It may turn out be one unit too low, but if it is, the next
trial quotient will be larger than 99 and this will be
adjusted at the end of the process.
The trial digit is multiplied by the divisor and the result subtracted
from the dividend and the process is repeated to get
additional quotient digits until the remaining
dividend is smaller than the divisor.
At the end, the digits of the quotient are put into
the canonical form, with propagation of carry as needed.
The sign is set from the sign of the operands.
.SH
Remainder
.PP
The division routine is called and division is performed
exactly as described.  The quantity returned is the remains of the
dividend at the end of the divide process.
Since division truncates toward zero, remainders have the same
sign as the dividend.
The scale of the remainder is set to 
the maximum of the scale of the dividend and
the scale of the quotient plus the scale of the divisor.
.SH
Square Root
.PP
The scale is stripped from the operand.
Zeros are added if necessary to make the
integer result have a scale that is the larger of
the internal quantity
\fBscale\fP
and the scale of the operand.
.PP
The method used to compute sqrt(y) is Newton's method
with successive approximations by the rule
.EQ
x sub {n+1} ~=~ half ( x sub n + y over x sub n )
.EN
The initial guess is found by taking the integer square root
of the top two digits.
.SH
Exponentiation
.PP
Only exponents with zero scale factor are handled.  If the exponent is
zero, then the result is 1.  If the exponent is negative, then
it is made positive and the base is divided into one.  The scale
of the base is removed.
.PP
The integer exponent is viewed as a binary number.
The base is repeatedly squared and the result is
obtained as a product of those powers of the base that
correspond to the positions of the one-bits in the binary
representation of the exponent.
Enough digits of the result
are removed to make the scale of the result the same as if the
indicated multiplication had been performed.
.SH
Input Conversion and Base
.PP
Numbers are converted to the internal representation as they are read
in.
The scale stored with a number is simply the number of fractional digits input.
Negative numbers are indicated by preceding the number with a \fB\_\fP (an 
underscore).
The hexadecimal digits A\-F correspond to the numbers 10\-15 regardless of input base.
The \fBi\fP command can be used to change the base of the input numbers.
This command pops the stack, truncates the resulting number to an integer,
and uses it as the input base for all further input.
The input base is initialized to 10 but may, for example be changed to
8 or 16 to do octal or hexadecimal to decimal conversions.
The command \fBI\fP will push the value of the input base on the stack.
.SH
Output Commands
.PP
The command \fBp\fP causes the top of the stack to be printed.
It does not remove the top of the stack.
All of the stack and internal registers can be output
by typing the command \fBf\fP.
The \fBo\fP command can be used to change the output base.
This command uses the top of the stack, truncated to an integer as
the base for all further output.
The output base in initialized to 10.
It will work correctly for any base.
The command \fBO\fP pushes the value of the output base on the stack.
.SH
Output Format and Base
.PP
The input and output bases only affect
the interpretation of numbers on input and output; they have no
effect on arithmetic computations.
Large numbers are output with 70 characters per line;
a \\ indicates a continued line.
All choices of input and output bases work correctly, although not all are
useful.
A particularly useful output base is 100000, which has the effect of
grouping digits in fives.
Bases of 8 and 16 can be used for decimal-octal or decimal-hexadecimal
conversions.
.SH
Internal Registers
.PP
Numbers or strings may be stored in internal registers or loaded on the stack
from registers with the commands \fBs\fP and \fBl\fP.
The command \fBs\fIx\fR pops the top of the stack and
stores the result in register \fBx\fP.
\fIx\fP can be any character.
\fBl\fIx\fR puts the contents of register \fBx\fP on the top of the stack.
The \fBl\fP command has no effect on the contents of register \fIx\fP.
The \fBs\fP command, however, is destructive.
.SH
Stack Commands
.PP
The command \fBc\fP clears the stack.
The command \fBd\fP pushes a duplicate of the number on the top of the stack
on the stack.
The command \fBz\fP pushes the stack size on the stack.
The command \fBX\fP replaces the number on the top of the stack
with its scale factor.
The command \fBZ\fP replaces the top of the stack
with its length.
.SH
Subroutine Definitions and Calls
.PP
Enclosing a string in \fB[ ]\fP pushes the ascii string on the stack.
The \fBq\fP command quits or in executing a string, pops the recursion levels by two.
.SH
Internal Registers \- Programming DC
.PP
The load and store
commands together with \fB[ ]\fP to store strings, \fBx\fP to execute
and the testing commands `<', `>', `=', `!<', `!>', `!=' can be used to program
DC.
The \fBx\fP command assumes the top of the stack is an string of DC commands
and executes it.
The testing commands compare the top two elements on the stack and if the relation holds, execute the register
that follows the relation.
For example, to print the numbers 0-9,
.DS
[lip1+  si  li10>a]sa
0si  lax
.DE
.SH
Push-Down Registers and Arrays
.PP
These commands were designed for used by a compiler, not by
people.
They involve push-down registers and arrays.
In addition to the stack that commands work on, DC can be thought
of as having individual stacks for each register.
These registers are operated on by the commands \fBS\fP and \fBL\fP.
\fBS\fIx\fR pushes the top value of the main stack onto the stack for
the register \fIx\fP.
\fBL\fIx\fR pops the stack for register \fIx\fP and puts the result on the main
stack.
The commands \fBs\fP and \fBl\fP also work on registers but not as push-down
stacks.
\fBl\fP doesn't effect the top of the
register stack, and \fBs\fP destroys what was there before.
.PP
The commands to work on arrays are \fB:\fP and \fB;\fP.
\fB:\fIx\fR pops the stack and uses this value as an index into
the array \fIx\fP.
The next element on the stack is stored at this index in \fIx\fP.
An index must be greater than or equal to 0 and
less than 2048.
\fB;\fIx\fR is the command to load the main stack from the array \fIx\fP.
The value on the top of the stack is the index
into the array \fIx\fP of the value to be loaded.
.SH
Miscellaneous Commands
.PP
The command \fB!\fP interprets the rest of the line as a 
.UX 
command and passes it to 
.UX
to execute.
One other compiler command is \fBQ\fP.
This command uses the top of the stack as the number of levels of recursion to skip.
.SH
DESIGN CHOICES
.PP
The real reason for the use of a dynamic storage allocator was
that a general purpose program could be (and in fact has been)
used for a variety of other tasks.
The allocator has some value for input and for compiling (i.e.
the bracket [...] commands) where it cannot be known in advance
how long a string will be.
The result was that at a modest
cost in execution time, all considerations of string allocation
and sizes of strings were removed from the remainder of the program
and debugging was made easier.  The allocation method
used wastes approximately 25% of available space.
.PP
The choice of 100 as a base for internal arithmetic
seemingly has no compelling advantage.  Yet the base cannot
exceed 127 because of hardware limitations and at the cost
of 5% in space, debugging was made a great deal easier and
decimal output was made much faster.
.PP
The reason for a stack-type arithmetic design was
to permit all DC commands from addition to subroutine execution
to be implemented in essentially the same way.  The result
was a considerable degree of logical separation of the final
program into modules with very little communication between
modules.
.PP
The rationale for the lack of interaction between the scale and the bases
was to provide an understandable means of proceeding after
a change of base or scale when numbers had already been entered.
An earlier implementation which had global notions of
scale and base did not work out well.
If the value of
.ft B
scale
.ft
were to be interpreted in the current
input or output base,
then a change of base or scale in the midst of a
computation would cause great confusion in the interpretation
of the results.
The current scheme has the advantage that the value of
the input and output bases
are only used for input and output, respectively, and they
are ignored in all other operations.
The value of
scale
is not used for any essential purpose by any part of the program
and it is used only to prevent the number of
decimal places resulting from the arithmetic operations from
growing beyond all bounds.
.PP
The design rationale for the choices for the scales of
the results of arithmetic were that in no case should
any significant digits be thrown away if, on appearances, the
user actually wanted them.  Thus, if the user wants
to add the numbers 1.5 and 3.517, it seemed reasonable to give
him the result 5.017 without requiring him to unnecessarily
specify his rather obvious requirements for precision.
.PP
On the other hand, multiplication and exponentiation produce
results with many more digits than their operands and it
seemed reasonable to give as a minimum the number of decimal
places in the operands but not to give more than that
number of digits
unless the user asked for them by specifying a value for \fBscale\fP.
Square root can be handled in just the same way as multiplication.
The operation of division gives arbitrarily many decimal places
and there is simply no way to guess how many places the user
wants.
In this case only, the user must
specify a \fBscale\fP to get any decimal places at all.
.PP
The scale of remainder was chosen to make it possible
to recreate the dividend from the quotient and remainder.
This is easy to implement; no digits are thrown away.
.SH
References
.IP [1]
L. L. Cherry, R. Morris,
.ft I
BC \- An Arbitrary Precision Desk-Calculator Language.
.ft
.IP [2]
K. C. Knowlton,
.ft I
A Fast Storage Allocator,
.ft
Comm. ACM \fB8\fP, pp. 623-625 (Oct. 1965).
