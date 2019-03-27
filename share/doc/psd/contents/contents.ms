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
.\"	@(#)00.contents	8.1 (Berkeley) 6/8/93
.\" $FreeBSD$
.\"
.OH '''PSD Contents'
.EH 'PSD Contents'''
.TL
UNIX Programmer's Supplementary Documents (PSD)
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
The
.UX
Programmer's Reference Manual
.R
for the FreeBSD system as distributed by the FreeBSD Project.
.SH
Documents of Historical Interest
.IP
.tl 'The Unix Time\-Sharing System''PSD:1'
.QP
Dennis Ritchie and Ken Thompson's original paper about UNIX, reprinted 
from Communications of the ACM.
.sp
.IP
.tl 'Unix Implementation''PSD:2'
.QP
Ken Thompson's description of the implementation of the Version 7
kernel and file system.
.sp
.IP
.tl 'The Unix I/O System''PSD:3'
.QP
Dennis Ritchie's overview of the I/O System of Version 7; still helpful for 
those writing device drivers.
.sp
.IP
.tl 'Unix Programming \- Second Edition ''PSD:4'
.QP
Describes the programming interface to the UNIX version 7 operating
system and the standard I/O library.  Should be supplemented by
Kernighan and Pike, ``The UNIX Programming Environment'',
Prentice-Hall, 1984 and especially by the Programmer Reference Manual
section 2 (system calls) and 3 (library routines).
.sp
.IP
.tl 'Berkeley Software Architecture Manual (4.4 Edition)''PSD:5'
.if \n(.U \{\
.br
.>> <a href="05.sysman/paper.html">05.sysman/paper.html</a>
.\}
.QP
A concise and terse description of the system call interface
provided in Berkeley Unix, as revised for 4.4BSD.
This will never be a best seller.

.SH
Languages in common use
.IP
.tl 'The C Programming Language \- Reference Manual''PSD:6'
.QP
Official statement of the syntax of C.
Should be supplemented by ``The C Programming Language,''
B.W. Kernighan and D.M. Ritchie, Prentice-Hall, 1978, that
contains a tutorial introduction and many examples.
.sp
.IP
.tl 'Berkeley Pascal User\'s Manual''PSD:7'
.QP
An implementation of this language popular for learning to program.  
(Not provided in FreeBSD.)
.sp
.IP
.tl 'A Portable Fortran 77 Compiler''PSD:8'
.QP
A revised version of the document which originally appeared in
Volume 2b of the Bell Labs documentation;
this version reflects the work done at Berkeley.
(Not provided in FreeBSD.)
.sp
.IP
.tl 'Introduction to the f77 I/O Library''PSD:9'
.QP
A description of the revised input/output library for Fortran 77, 
reflecting work carried out at Berkeley.  (Not provided in FreeBSD.)

.SH
Programming Tools
.IP
.tl 'Debugging with GDB: The GNU Source-Level Debugger''PSD:10'
.QP
How to debug programs using the source level \fIgdb\fP debugger
(or how to debug programs without having to know much about machine language).
(A TeXinfo version is provided separately.)
.sp
.IP
.tl 'A Tutorial Introduction to ADB''PSD:11'
.QP
How to debug programs using the assembly-language level \fIadb\fP debugger.
(Not provided in FreeBSD.)
.sp
.IP
.tl 'Make \- A Program for Maintaining Computer Programs''PSD:12'
.if \n(.U \{\
.br
.>> <a href="12.make/paper.html">12.make/paper.html</a>
.\}
.QP
Indispensable tool for making sure large programs are properly
compiled with minimal effort. 
.sp
.IP
.tl 'An Introduction to the Source Code Control System''PSD:14'
.QP
A useful introductory article for those users with
installations licensed for SCCS.
.sp
.IP
.tl 'YACC: Yet Another Compiler-Compiler''PSD:15'
.QP
Converts a BNF specification of a language and semantic actions
written in C into a compiler for that language.
.sp
.IP
.tl 'LEX \- A Lexical Analyzer Generator''PSD:16'
.QP
Creates a recognizer for a set of regular expressions:
each regular expression can be followed by arbitrary C code
to be executed upon finding the regular expression.
.sp
.IP
.tl 'The M4 Macro Processor''PSD:17'
.QP
M4 is a macro processor useful in its own right and as a
front-end for C, Ratfor, and Cobol.
.sp
.IP
.tl 'gprof: a Call Graph Execution Profiler''PSD:18'
.if \n(.U \{\
.br
.>> <a href="18.gprof/paper.html">18.gprof/paper.html
.\}
.QP
A program to show the call graph and execution time of a program.
Indispensable aid for improving the running time of almost everything.

.SH
General Reference
.IP
.tl 'An Introductory 4.4BSD Interprocess Communication Tutorial''PSD:20'
.if \n(.U \{\
.br
.>> <a href="20.ipctut/paper.html">20.ipctut/paper.html
.\}
.QP
How to write programs that use the Interprocess Communication Facilities
of 4.4BSD.
.sp
.IP
.tl 'An Advanced 4.4BSD Interprocess Communication Tutorial''PSD:21'
.if \n(.U \{\
.br
.>> <a href="21.ipc/paper.html">21.ipc/paper.html
.\}
.QP
The reference document (with some examples) for the Interprocess Communication 
Facilities of 4.4BSD.
.sp
.IP
.tl 'RPCGEN Programming Guide''PSD:22'
.if \n(.U \{\
.br
.>> <a href="22.rpcgen/paper.html">22.rpcgen/paper.html
.\}
.QP
Manual for the ONC RPC stub-generating program, provided by Sun Microsystems.
.sp
.IP
.tl 'Remote Procedure Call Programming Guide''PSD:23'
.if \n(.U \{\
.br
.>> <a href="23.rpc/paper.html">23.rpc/paper.html
.\}
.QP
A tutorial introduction to programming the ONC RPC system, provided by
Sun Microsystems.
.sp
.IP
.tl 'External Data Representation: Sun Technical Notes''PSD:24'
.if \n(.U \{\
.br
.>> <a href="24.xdr/paper.html">24.xdr/paper.html
.\}
.QP
Technical details about the design of the XDR component of ONC RPC,
provided by Sun Microsystems.
.sp
.IP
.tl 'External Data Representation Standard: Protocol Specification''PSD:25'
.if \n(.U \{\
.br
.>> <a href="25.xdrrfc/paper.html">25.xdrrfc/paper.html
.\}
.QP
The Internet RFC specifying ONC XDR, provided by Sun Microsystems.
.sp
.IP
.tl 'Remote Procedure Calls: Protocol Specification''PSD:26'
.if \n(.U \{\
.br
.>> <a href="26.rpcrfc/paper.html">26.rpcrfc/paper.html
.\}
.QP
The Internet RFC specifying ONC RPC, RFC 1050, as provided by Sun
Microsystems.
.sp
.IP
.tl 'Network File System: Version 2 Protocol Specification''PSD:27'
.if \n(.U \{\
.br
.>> <a href="27.nfsrpc/paper.html">27.nfsrpc/paper.html
.\}
.QP
The Internet RFC specifying NFS, as provided by Sun Microsystems.
Note that the NFS-compatible filesystem itself, while
compliant with this specification, was not provided by Sun.
.sp
.IP
.tl 'CVS II: Parallelizing Software Development''PSD:28'
.if \n(.U \{\
.br
.>> <a href="28.cvs/paper.html">28.cvs/paper.html
.\}
.QP
CVS  (Concurrent Versions System) is a front end to the
RCS revision control system  which  extends  the  notion  of
revision  control  from  a  collection  of files in a single
directory to a hierarchical collection of  directories  each
containing revision controlled files.
