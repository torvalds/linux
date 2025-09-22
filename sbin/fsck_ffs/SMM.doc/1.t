.\"	$OpenBSD: 1.t,v 1.5 2003/06/02 20:06:15 millert Exp $
.\"	$NetBSD: 1.t,v 1.3 1996/04/05 01:45:44 cgd Exp $
.\"
.\" Copyright (c) 1982, 1993
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
.\"	@(#)1.t	8.1 (Berkeley) 6/5/93
.\"
.ds RH Introduction
.NH
Introduction
.PP
This document reflects the use of
.I fsck_ffs
with the 4.2BSD and 4.3BSD file system organization.  This
is a revision of the
original paper written by
T. J. Kowalski.
.PP
When a UNIX
operating system is brought up, a consistency
check of the file systems should always be performed.
This precautionary measure helps to insure
a reliable environment for file storage on disk.
If an inconsistency is discovered,
corrective action must be taken.
.I Fsck_ffs
runs in two modes.
Normally it is run non-interactively by the system after
a normal boot.
When running in this mode,
it will only make changes to the file system that are known
to always be correct.
If an unexpected inconsistency is found
.I fsck_ffs
will exit with a non-zero exit status,
leaving the system running single-user.
Typically the operator then runs
.I fsck_ffs
interactively.
When running in this mode,
each problem is listed followed by a suggested corrective action.
The operator must decide whether or not the suggested correction
should be made.
.PP
The purpose of this memo is to dispel the
mystique surrounding
file system inconsistencies.
It first describes the updating of the file system
(the calm before the storm) and
then describes file system corruption (the storm).
Finally,
the set of deterministic corrective actions
used by
.I fsck_ffs
(the Coast Guard
to the rescue) is presented.
.ds RH Overview of the File System
