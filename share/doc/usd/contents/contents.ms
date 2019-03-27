.\" Copyright (c) 1986, 1993
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
.\"	@(#)00.contents	8.2 (Berkeley) 4/20/94
.\" $FreeBSD$
.\"
.de ND
.KE
.sp
.KS
..
.OH '''USD Contents'
.EH 'USD Contents'''
.ND
.TL
UNIX User's Supplementary Documents (USD)
.if !r.U .nr .U 0
.if \n(.U \{\
.br
.>> <a href="Title.html">Title.html</a>
.\}
.sp
\s-2 4.4 Berkeley Software Distribution\s+2
.sp
\fRJune, 1993\fR
.PP
This volume contains documents which supplement the manual pages in
.I
The Unix User's Reference Manual
.R
for the 4.4BSD system as distributed by U.C. Berkeley.
.sp
.KS
.SH
Getting Started
.ND
.IP
.tl 'Unix for Beginners \- Second Edition''USD:1'
.QP
An introduction to the most basic uses of the system.
.ND
.IP
.tl 'Learn \- Computer\-Aided Instruction on UNIX (Second Edition)''USD:2'
.QP
Describes a computer-aided instruction program that walks new users through
the basics of files, the editor, and document prepararation software.
.ND
.SH
Basic Utilities
.ND
.IP
.tl 'An Introduction to the UNIX Shell''USD:3'
.QP
Steve Bourne's introduction to the capabilities of 
.I sh,
a command interpreter especially popular for writing shell scripts.
.ND
.IP
.tl 'An Introduction to the C shell''USD:4'
.if \n(.U \{\
.br
.>> <a href="04.csh/paper.html">04.csh/paper.html</a>
.\}
.QP
This introduction to
.I csh,
(a command interpreter popular for interactive work)  describes many 
commonly used UNIX commands, assumes little prior knowledge of UNIX,
and has a glossary useful for beginners.
.ND
.IP
.tl 'DC \- An Interactive Desk Calculator''USD:5'
.QP
A super HP calculator, if you do not need floating point.
.ND
.IP
.tl 'BC \- An Arbitrary Precision Desk-Calculator Language''USD:6'
.QP
A front end for DC that provides infix notation, control flow, and 
built\-in functions.
.ND
.SH
Communicating with the World
.ND
.IP
.tl 'Mail Reference Manual''USD:7'
.if \n(.U \{\
.br
.>> <a href="07.mail/paper.html">07.mail/paper.html</a>
.\}
.QP
Complete details on one of the programs for sending and reading your mail.
.ND
.IP
.tl 'The Rand MH Message Handling System''USD:8'
.QP
This system for managing your computer mail uses lots of small programs, 
instead of one large one.
.ND
.SH
Text Editing
.ND
.IP
.tl 'A Tutorial Introduction to the Unix Text Editor''USD:9'
.QP
An easy way to get started with the line editor, 
.I ed.
.ND
.IP
.tl 'Advanced Editing on Unix''USD:10'
.if \n(.U \{\
.br
.>> <a href="10.exref/paper.html">10.exref/paper.html</a>
.\}
.QP
The next step.
.ND
.IP
.tl 'An Introduction to Display Editing with Vi''USD:11'
.if \n(.U \{\
.br
.>> <a href="11.vitut/paper.html">11.vitut/paper.html</a>
.\}
.QP
The document to learn to use the \fIvi\fR screen editor.
.ND
.IP
.tl 'Ex Reference Manual (Version 3.7)''USD:12'
.if \n(.U \{\
.br
.>> <a href="12.vi/paper.html">12.vi/paper.html</a>
.\}
.QP
The final reference for the \fIex\fR editor.
.ND
.IP
.tl 'Vi Reference Manual''USD:13'
.if \n(.U \{\
.br
.>> <a href="13.viref/paper.html">13.viref/paper.html</a>
.\}
.QP
The definitive reference for the \fInvi\fR editor.
.ND
.IP
.tl 'Jove Manual for UNIX Users''USD:14'
.QP
Jove is a small, self-documenting, customizable display editor, based on
EMACS.  A plausible alternative to 
.I vi.
.ND
.IP
.tl 'SED \- A Non-interactive Text Editor''USD:15'
.QP
Describes a one-pass variant of 
.I ed
useful as a filter for processing large files.
.ND
.IP
.tl 'AWK \- A Pattern Scanning and Processing Language (Second Edition)''USD:16'
.QP
A program for data selection and transformation.
.ND
.SH
Document Preparation
.ND
.IP
.tl 'Typing Documents on UNIX: Using the \-ms Macros with Troff and Nroff''USD:17'
.QP
Describes and gives examples of the basic use of the typesetting tools and 
``-ms'', a frequently used package of formatting requests that make it easier 
to lay out most documents.
.ND
.IP
.tl 'A Revised Version of \-ms''USD:18'
.if \n(.U \{\
.br
.>> <a href="18.msdiffs/paper.html">18.msdiffs/paper.html</a>
.\}
.QP
A brief description of the Berkeley revisions made to the \-ms formatting
macros for nroff and troff.
.ND
.IP
.tl 'Writing Papers with \fInroff\fR using \-me''USD:19'
.if \n(.U \{\
.br
.>> <a href="19.memacros/paper.html">19.memacros/paper.html</a>
.\}
.QP
Another popular macro package for
.I nroff.
.ND
.IP
.tl '\-me Reference Manual''USD:20'
.if \n(.U \{\
.br
.>> <a href="20.meref/paper.html">20.meref/paper.html</a>
.\}
.QP
The final word on \-me.
.ND
.IP
.tl 'NROFF/TROFF User\'s Manual''USD:21'
.QP
Extremely detailed information about these document formatting programs.
.ND
.IP
.tl 'A TROFF Tutorial''USD:22'
.QP
An introduction to the most basic uses of
.I troff 
for those who really want to know such things, or want to write their 
own macros.
.ND
.IP
.tl 'A System for Typesetting Mathematics''USD:23'
.QP
Describes 
.I eqn, 
an easy-to-learn language for high-quality mathematical typesetting.
.ND
.IP
.tl 'Typesetting Mathematics \- User\'s Guide (Second Edition)''USD:24'
.QP
More details about how to use 
.I eqn.
.ND
.IP
.tl 'Tbl \- A Program to Format Tables''USD:25'
.QP
A program for easily typesetting tabular material.
.ND
.IP
.tl 'Refer \- A Bibliography System''USD:26'
.QP
An introduction to one set of tools used to maintain bibliographic databases. 
The major program, 
.I refer,
is used to automatically retrieve and format the references 
based on document citations.
.ND
.IP
.tl 'Some Applications of Inverted Indexes on the UNIX System''USD:27'
.QP
Mike Lesk's paper describes the
.I refer 
programs in a somewhat larger context.
.ND
.IP
.tl 'BIB \- A Program for Formatting Bibliographies''USD:28'
.QP
This is an alternative to
.I refer
for expanding citations in documents.
.ND
.IP
.tl 'Writing Tools \- The STYLE and DICTION Programs''USD:29'
.QP
These are programs which can help you understand and improve your 
writing style.
.ND
.SH 
Amusements
.ND
.IP
.tl 'A Guide to the Dungeons of Doom''USD:30'
.if \n(.U \{\
.br
.>> <a href="30.rogue/paper.html">30.rogue/paper.html</a>
.\}
.QP
An introduction to the popular game of \fIrogue\fP, a fantasy game
which is one of the biggest known users of VAX cycles.
.ND
.IP
.tl 'Star Trek''USD:31'
.if \n(.U \{\
.br
.>> <a href="31.trek/paper.html">31.trek/paper.html</a>
.\}
.QP
You are the Captain of the Starship Enterprise.  Wipe out the
Klingons and save the Federation.
.KE
