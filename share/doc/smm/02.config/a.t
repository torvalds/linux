.\" Copyright (c) 1983, 1993
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
.\"	@(#)a.t	8.1 (Berkeley) 6/8/93
.\"
.\".ds RH "Configuration File Grammar
.bp
.LG
.B
.ce
APPENDIX A. CONFIGURATION FILE GRAMMAR
.sp
.R
.NL
.PP
The following grammar is a compressed form of the actual
\fIyacc\fP\|(1) grammar used by
.I config
to parse configuration files.
Terminal symbols are shown all in upper case, literals
are emboldened; optional clauses are enclosed in brackets, ``[''
and ``]'';  zero or more instantiations are denoted with ``*''.
.sp
.nf
.DT
Configuration ::=  [ Spec \fB;\fP ]*

Spec ::= Config_spec
	| Device_spec
	| \fBtrace\fP
	| /* lambda */

/* configuration specifications */

Config_spec ::=  \fBmachine\fP ID
	| \fBcpu\fP ID
	| \fBoptions\fP Opt_list
	| \fBident\fP ID
	| System_spec
	| \fBtimezone\fP [ \fB\-\fP ] NUMBER [ \fBdst\fP [ NUMBER ] ]
	| \fBtimezone\fP [ \fB\-\fP ] FPNUMBER [ \fBdst\fP [ NUMBER ] ]
	| \fBmaxusers\fP NUMBER

/* system configuration specifications */

System_spec ::= \fBconfig\fP ID System_parameter [ System_parameter ]*

System_parameter ::=  swap_spec | root_spec | dump_spec | arg_spec

swap_spec ::=  \fBswap\fP [ \fBon\fP ] swap_dev [ \fBand\fP swap_dev ]*

swap_dev ::=  dev_spec [ \fBsize\fP NUMBER ]

root_spec ::=  \fBroot\fP [ \fBon\fP ] dev_spec

dump_spec ::=  \fBdumps\fP [ \fBon\fP ] dev_spec

arg_spec ::=  \fBargs\fP [ \fBon\fP ] dev_spec

dev_spec ::=  dev_name | major_minor

major_minor ::=  \fBmajor\fP NUMBER \fBminor\fP NUMBER

dev_name ::=  ID [ NUMBER [ ID ] ]

/* option specifications */

Opt_list ::=  Option [ \fB,\fP Option ]*

Option ::=  ID [ \fB=\fP Opt_value ]

Opt_value ::=  ID | NUMBER

Mkopt_list ::=  Mkoption [ \fB,\fP Mkoption ]*

Mkoption ::=  ID \fB=\fP Opt_value

/* device specifications */

Device_spec ::= \fBdevice\fP Dev_name Dev_info Int_spec
	| \fBmaster\fP Dev_name Dev_info
	| \fBdisk\fP Dev_name Dev_info
	| \fBtape\fP Dev_name Dev_info
	| \fBcontroller\fP Dev_name Dev_info [ Int_spec ]
	| \fBpseudo-device\fP Dev [ NUMBER ]

Dev_name ::=  Dev NUMBER

Dev ::=  \fBuba\fP | \fBmba\fP | ID

Dev_info ::=  Con_info [ Info ]*

Con_info ::=  \fBat\fP Dev NUMBER
	| \fBat\fP \fBnexus\fP NUMBER

Info ::=  \fBcsr\fP NUMBER
	| \fBdrive\fP NUMBER
	| \fBslave\fP NUMBER
	| \fBflags\fP NUMBER

Int_spec ::=  \fBvector\fP ID [ ID ]*
	| \fBpriority\fP NUMBER
.fi
.sp
.SH
Lexical Conventions
.LP
The terminal symbols are loosely defined as:
.IP ID
.br
One or more alphabetics, either upper or lower case, and underscore,
``_''.
.IP NUMBER
.br
Approximately the C language specification for an integer number.
That is, a leading ``0x'' indicates a hexadecimal value,
a leading ``0'' indicates an octal value, otherwise the number is
expected to be a decimal value.  Hexadecimal numbers may use either
upper or lower case alphabetics.
.IP FPNUMBER
.br
A floating point number without exponent.  That is a number of the
form ``nnn.ddd'', where the fractional component is optional.
.LP
In special instances a question mark, ``?'', can be substituted for
a ``NUMBER'' token.  This is used to effect wildcarding in device
interconnection specifications.
.LP
Comments in configuration files are indicated by a ``#'' character
at the beginning of the line; the remainder of the line is discarded.
.LP
A specification
is interpreted as a continuation of the previous line
if the first character of the line is tab.
