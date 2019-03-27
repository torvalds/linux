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
.\"	$FreeBSD$
.\"	@(#)4.t	8.1 (Berkeley) 6/5/93
.\"
.ds RH Appendix A \- Fsck_ffs Error Conditions
.NH
Appendix A \- Fsck_ffs Error Conditions
.NH 2 
Conventions
.PP
.I Fsck_ffs
is
a multi-pass file system check program.
Each file system pass invokes a different Phase of the
.I fsck_ffs
program.
After the initial setup,
.I fsck_ffs
performs successive Phases over each file system,
checking blocks and sizes,
path-names,
connectivity,
reference counts,
and the map of free blocks,
(possibly rebuilding it),
and performs some cleanup.
.LP
Normally
.I fsck_ffs
is run non-interactively to
.I preen
the file systems after an unclean halt.
While preen'ing a file system,
it will only fix corruptions that are expected
to occur from an unclean halt.
These actions are a proper subset of the actions that 
.I fsck_ffs
will take when it is running interactively.
Throughout this appendix many errors have several options
that the operator can take.
When an inconsistency is detected,
.I fsck_ffs
reports the error condition to the operator.
If a response is required,
.I fsck_ffs
prints a prompt message and
waits for a response.
When preen'ing most errors are fatal.
For those that are expected,
the response taken is noted.
This appendix explains the meaning of each error condition,
the possible responses, and the related error conditions.
.LP
The error conditions are organized by the
.I Phase
of the
.I fsck_ffs
program in which they can occur.
The error conditions that may occur
in more than one Phase
will be discussed in initialization.
.NH 2 
Initialization
.PP
Before a file system check can be performed, certain
tables have to be set up and certain files opened.
This section concerns itself with the opening of files and
the initialization of tables.
This section lists error conditions resulting from
command line options,
memory requests,
opening of files,
status of files,
file system size checks,
and creation of the scratch file.
All the initialization errors are fatal
when the file system is being preen'ed.
.sp
.LP
.B "\fIC\fP option?"
.br
\fIC\fP is not a legal option to
.I fsck_ffs ;
legal options are \-b, \-c, \-y, \-n, and \-p.
.I Fsck_ffs
terminates on this error condition.
See the
.I fsck_ffs (8)
manual entry for further detail.
.sp
.LP
.B "cannot alloc NNN bytes for blockmap"
.br
.B "cannot alloc NNN bytes for freemap"
.br
.B "cannot alloc NNN bytes for statemap"
.br
.B "cannot alloc NNN bytes for lncntp"
.br
.I Fsck_ffs 's
request for memory for its virtual
memory tables failed.
This should never happen.
.I Fsck_ffs
terminates on this error condition.
See a guru.
.sp
.LP
.B "Can't open checklist file: \fIF\fP"
.br
The file system checklist file
\fIF\fP (usually
.I /etc/fstab )
can not be opened for reading.
.I Fsck_ffs
terminates on this error condition.
Check access modes of \fIF\fP.
.sp
.LP
.B "Can't stat root"
.br
.I Fsck_ffs 's
request for statistics about the root directory ``/'' failed.
This should never happen.
.I Fsck_ffs
terminates on this error condition.
See a guru.
.sp
.LP
.B "Can't stat \fIF\fP"
.br
.B "Can't make sense out of name \fIF\fP"
.br
.I Fsck_ffs 's
request for statistics about the file system \fIF\fP failed.
When running manually,
it ignores this file system
and continues checking the next file system given.
Check access modes of \fIF\fP.
.sp
.LP
.B "Can't open \fIF\fP"
.br
.I Fsck_ffs 's
request attempt to open the file system \fIF\fP failed.
When running manually, it ignores this file system
and continues checking the next file system given.
Check access modes of \fIF\fP.
.sp
.LP
.B "\fIF\fP: (NO WRITE)"
.br
Either the \-n flag was specified or
.I fsck_ffs 's
attempt to open the file system \fIF\fP for writing failed.
When running manually,
all the diagnostics are printed out,
but no modifications are attempted to fix them.
.sp
.LP
.B "file is not a block or character device; OK"
.br
You have given
.I fsck_ffs
a regular file name by mistake.
Check the type of the file specified.
.LP
Possible responses to the OK prompt are:
.IP YES
ignore this error condition.
.IP NO
ignore this file system and continues checking
the next file system given.
.sp
.LP
.B "UNDEFINED OPTIMIZATION IN SUPERBLOCK (SET TO DEFAULT)"
.br
The superblock optimization parameter is neither OPT_TIME
nor OPT_SPACE.
.LP
Possible responses to the SET TO DEFAULT prompt are:
.IP YES
The superblock is set to request optimization to minimize
running time of the system.
(If optimization to minimize disk space utilization is
desired, it can be set using \fItunefs\fP(8).)
.IP NO
ignore this error condition.
.sp
.LP
.B "IMPOSSIBLE MINFREE=\fID\fP IN SUPERBLOCK (SET TO DEFAULT)"
.br
The superblock minimum space percentage is greater than 99%
or less then 0%.
.LP
Possible responses to the SET TO DEFAULT prompt are:
.IP YES
The minfree parameter is set to 10%.
(If some other percentage is desired,
it can be set using \fItunefs\fP(8).)
.IP NO
ignore this error condition.
.sp
.LP
.B "IMPOSSIBLE INTERLEAVE=\fID\fP IN SUPERBLOCK (SET TO DEFAULT)"
.br
The file system interleave is less than or equal to zero.
.LP
Possible responses to the SET TO DEFAULT prompt are:
.IP YES
The interleave parameter is set to 1.
.IP NO
ignore this error condition.
.sp
.LP
.B "IMPOSSIBLE NPSECT=\fID\fP IN SUPERBLOCK (SET TO DEFAULT)"
.br
The number of physical sectors per track is less than the number
of usable sectors per track.
.LP
Possible responses to the SET TO DEFAULT prompt are:
.IP YES
The npsect parameter is set to the number of usable sectors per track.
.IP NO
ignore this error condition.
.sp
.LP
One of the following messages will appear:
.br
.B "MAGIC NUMBER WRONG"
.br
.B "NCG OUT OF RANGE"
.br
.B "CPG OUT OF RANGE"
.br
.B "NCYL DOES NOT JIVE WITH NCG*CPG"
.br
.B "SIZE PREPOSTEROUSLY LARGE"
.br
.B "TRASHED VALUES IN SUPER BLOCK"
.br
and will be followed by the message:
.br
.B "\fIF\fP: BAD SUPER BLOCK: \fIB\fP"
.br
.B "USE -b OPTION TO FSCK_FFS TO SPECIFY LOCATION OF AN ALTERNATE"
.br
.B "SUPER-BLOCK TO SUPPLY NEEDED INFORMATION; SEE fsck_ffs(8)."
.br
The super block has been corrupted. 
An alternative super block must be selected from among those
listed by
.I newfs
(8) when the file system was created.
For file systems with a blocksize less than 32K,
specifying \-b 32 is a good first choice.
.sp
.LP
.B "INTERNAL INCONSISTENCY: \fIM\fP"
.br
.I Fsck_ffs 's
has had an internal panic, whose message is specified as \fIM\fP.
This should never happen.
See a guru.
.sp
.LP
.B "CAN NOT SEEK: BLK \fIB\fP (CONTINUE)"
.br
.I Fsck_ffs 's
request for moving to a specified block number \fIB\fP in
the file system failed.
This should never happen.
See a guru.
.LP
Possible responses to the CONTINUE prompt are:
.IP YES
attempt to continue to run the file system check.
Often,
however the problem will persist.
This error condition will not allow a complete check of the file system.
A second run of
.I fsck_ffs
should be made to re-check this file system.
If the block was part of the virtual memory buffer
cache,
.I fsck_ffs
will terminate with the message ``Fatal I/O error''.
.IP NO
terminate the program.
.sp
.LP
.B "CAN NOT READ: BLK \fIB\fP (CONTINUE)"
.br
.I Fsck_ffs 's
request for reading a specified block number \fIB\fP in
the file system failed.
This should never happen.
See a guru.
.LP
Possible responses to the CONTINUE prompt are:
.IP YES
attempt to continue to run the file system check.
It will retry the read and print out the message:
.br
.B "THE FOLLOWING SECTORS COULD NOT BE READ: \fIN\fP"
.br
where \fIN\fP indicates the sectors that could not be read.
If 
.I fsck_ffs
ever tries to write back one of the blocks on which the read failed
it will print the message:
.br
.B "WRITING ZERO'ED BLOCK \fIN\fP TO DISK"
.br
where \fIN\fP indicates the sector that was written with zero's.
If the disk is experiencing hardware problems, the problem will persist.
This error condition will not allow a complete check of the file system.
A second run of
.I fsck_ffs
should be made to re-check this file system.
If the block was part of the virtual memory buffer
cache,
.I fsck_ffs
will terminate with the message ``Fatal I/O error''.
.IP NO
terminate the program.
.sp
.LP
.B "CAN NOT WRITE: BLK \fIB\fP (CONTINUE)"
.br
.I Fsck_ffs 's
request for writing a specified block number \fIB\fP
in the file system failed.
The disk is write-protected;
check the write protect lock on the drive.
If that is not the problem, see a guru.
.LP
Possible responses to the CONTINUE prompt are:
.IP YES
attempt to continue to run the file system check.
The write operation will be retried with the failed blocks
indicated by the message:
.br
.B "THE FOLLOWING SECTORS COULD NOT BE WRITTEN: \fIN\fP"
.br
where \fIN\fP indicates the sectors that could not be written.
If the disk is experiencing hardware problems, the problem will persist.
This error condition will not allow a complete check of the file system.
A second run of
.I fsck_ffs
should be made to re-check this file system.
If the block was part of the virtual memory buffer
cache,
.I fsck_ffs
will terminate with the message ``Fatal I/O error''.
.IP NO
terminate the program.
.sp
.LP
.B "bad inode number DDD to ginode"
.br
An internal error has attempted to read non-existent inode \fIDDD\fP.
This error causes 
.I fsck_ffs
to exit.
See a guru.
.NH 2 
Phase 1 \- Check Blocks and Sizes
.PP
This phase concerns itself with
the inode list.
This section lists error conditions resulting from
checking inode types,
setting up the zero-link-count table,
examining inode block numbers for bad or duplicate blocks,
checking inode size,
and checking inode format.
All errors in this phase except
.B "INCORRECT BLOCK COUNT"
and
.B "PARTIALLY TRUNCATED INODE"
are fatal if the file system is being preen'ed.
.sp
.LP
.B "UNKNOWN FILE TYPE I=\fII\fP (CLEAR)"
.br
The mode word of the inode \fII\fP indicates that the inode is not a
special block inode, special character inode, socket inode, regular inode,
symbolic link, or directory inode.
.LP
Possible responses to the CLEAR prompt are:
.IP YES
de-allocate inode \fII\fP by zeroing its contents.
This will always invoke the UNALLOCATED error condition in Phase 2
for each directory entry pointing to this inode.
.IP NO
ignore this error condition.
.sp
.LP
.B "PARTIALLY TRUNCATED INODE I=\fII\fP (SALVAGE)"
.br
.I Fsck_ffs
has found inode \fII\fP whose size is shorter than the number of
blocks allocated to it.
This condition should only occur if the system crashes while in the
midst of truncating a file.
When preen'ing the file system, 
.I fsck_ffs
completes the truncation to the specified size.
.LP
Possible responses to SALVAGE are:
.IP YES
complete the truncation to the size specified in the inode.
.IP NO
ignore this error condition.
.sp
.LP
.B "LINK COUNT TABLE OVERFLOW (CONTINUE)"
.br
An internal table for
.I fsck_ffs
containing allocated inodes with a link count of
zero cannot allocate more memory.
Increase the virtual memory for
.I fsck_ffs .
.LP
Possible responses to the CONTINUE prompt are:
.IP YES
continue with the program.
This error condition will not allow a complete check of the file system.
A second run of
.I fsck_ffs
should be made to re-check this file system.
If another allocated inode with a zero link count is found,
this error condition is repeated.
.IP NO
terminate the program.
.sp
.LP
.B "\fIB\fP BAD I=\fII\fP"
.br
Inode \fII\fP contains block number \fIB\fP with a number
lower than the number of the first data block in the file system or
greater than the number of the last block
in the file system.
This error condition may invoke the
.B "EXCESSIVE BAD BLKS"
error condition in Phase 1 (see next paragraph) if
inode \fII\fP has too many block numbers outside the file system range.
This error condition will always invoke the
.B "BAD/DUP"
error condition in Phase 2 and Phase 4.
.sp
.LP
.B "EXCESSIVE BAD BLKS I=\fII\fP (CONTINUE)"
.br
There is more than a tolerable number (usually 10) of blocks with a number
lower than the number of the first data block in the file system or greater than
the number of last block in the file system associated with inode \fII\fP.
.LP
Possible responses to the CONTINUE prompt are:
.IP YES
ignore the rest of the blocks in this inode
and continue checking with the next inode in the file system.
This error condition will not allow a complete check of the file system.
A second run of
.I fsck_ffs
should be made to re-check this file system.
.IP NO
terminate the program.
.sp
.LP
.B "BAD STATE DDD TO BLKERR"
.br
An internal error has scrambled 
.I fsck_ffs 's
state map to have the impossible value \fIDDD\fP.
.I Fsck_ffs
exits immediately. 
See a guru.
.sp
.LP
.B "\fIB\fP DUP I=\fII\fP"
.br
Inode \fII\fP contains block number \fIB\fP that is already claimed by
another inode.
This error condition may invoke the
.B "EXCESSIVE DUP BLKS"
error condition in Phase 1 if
inode \fII\fP has too many block numbers claimed by other inodes.
This error condition will always invoke Phase 1b and the
.B "BAD/DUP"
error condition in Phase 2 and Phase 4.
.sp
.LP
.B "EXCESSIVE DUP BLKS I=\fII\fP (CONTINUE)"
.br
There is more than a tolerable number (usually 10) of blocks claimed by other
inodes.
.LP
Possible responses to the CONTINUE prompt are:
.IP YES
ignore the rest of the blocks in this inode
and continue checking with the next inode in the file system.
This error condition will not allow a complete check of the file system.
A second run of
.I fsck_ffs
should be made to re-check this file system.
.IP NO
terminate the program.
.sp
.LP
.B "DUP TABLE OVERFLOW (CONTINUE)"
.br
An internal table in
.I fsck_ffs
containing duplicate block numbers cannot allocate any more space.
Increase the amount of virtual memory available to
.I fsck_ffs .
.LP
Possible responses to the CONTINUE prompt are:
.IP YES
continue with the program.
This error condition will not allow a complete check of the file system.
A second run of
.I fsck_ffs
should be made to re-check this file system.
If another duplicate block is found, this error condition will repeat.
.IP NO
terminate the program.
.sp
.LP
.B "PARTIALLY ALLOCATED INODE I=\fII\fP (CLEAR)"
.br
Inode \fII\fP is neither allocated nor unallocated.
.LP
Possible responses to the CLEAR prompt are:
.IP YES
de-allocate inode \fII\fP by zeroing its contents.
.IP NO
ignore this error condition.
.sp
.LP
.B "INCORRECT BLOCK COUNT I=\fII\fP (\fIX\fP should be \fIY\fP) (CORRECT)"
.br
The block count for inode \fII\fP is \fIX\fP blocks,
but should be \fIY\fP blocks.
When preen'ing the count is corrected.
.LP
Possible responses to the CORRECT prompt are:
.IP YES
replace the block count of inode \fII\fP with \fIY\fP.
.IP NO
ignore this error condition.
.NH 2 
Phase 1B: Rescan for More Dups
.PP
When a duplicate block is found in the file system, the file system is
rescanned to find the inode that previously claimed that block.
This section lists the error condition when the duplicate block is found.
.sp
.LP
.B "\fIB\fP DUP I=\fII\fP"
.br
Inode \fII\fP contains block number \fIB\fP that
is already claimed by another inode.
This error condition will always invoke the
.B "BAD/DUP"
error condition in Phase 2.
You can determine which inodes have overlapping blocks by examining
this error condition and the DUP error condition in Phase 1.
.NH 2 
Phase 2 \- Check Pathnames
.PP
This phase concerns itself with removing directory entries
pointing to
error conditioned inodes
from Phase 1 and Phase 1b.
This section lists error conditions resulting from
root inode mode and status,
directory inode pointers in range,
and directory entries pointing to bad inodes,
and directory integrity checks.
All errors in this phase are fatal if the file system is being preen'ed,
except for directories not being a multiple of the blocks size
and extraneous hard links.
.sp
.LP
.B "ROOT INODE UNALLOCATED (ALLOCATE)"
.br
The root inode (usually inode number 2) has no allocate mode bits.
This should never happen.
.LP
Possible responses to the ALLOCATE prompt are:
.IP YES
allocate inode 2 as the root inode.
The files and directories usually found in the root will be recovered
in Phase 3 and put into
.I lost+found .
If the attempt to allocate the root fails,
.I fsck_ffs
will exit with the message:
.br
.B "CANNOT ALLOCATE ROOT INODE" .
.IP NO
.I fsck_ffs
will exit.
.sp
.LP
.B "ROOT INODE NOT DIRECTORY (REALLOCATE)"
.br
The root inode (usually inode number 2)
is not directory inode type.
.LP
Possible responses to the REALLOCATE prompt are:
.IP YES
clear the existing contents of the root inode
and reallocate it.
The files and directories usually found in the root will be recovered
in Phase 3 and put into
.I lost+found .
If the attempt to allocate the root fails,
.I fsck_ffs
will exit with the message:
.br
.B "CANNOT ALLOCATE ROOT INODE" .
.IP NO
.I fsck_ffs
will then prompt with
.B "FIX"
.LP
Possible responses to the FIX prompt are:
.IP YES
replace the root inode's type to be a directory.
If the root inode's data blocks are not directory blocks,
many error conditions will be produced.
.IP NO
terminate the program.
.sp
.LP
.B "DUPS/BAD IN ROOT INODE (REALLOCATE)"
.br
Phase 1 or Phase 1b have found duplicate blocks
or bad blocks in the root inode (usually inode number 2) for the file system.
.LP
Possible responses to the REALLOCATE prompt are:
.IP YES
clear the existing contents of the root inode
and reallocate it.
The files and directories usually found in the root will be recovered
in Phase 3 and put into
.I lost+found .
If the attempt to allocate the root fails,
.I fsck_ffs
will exit with the message:
.br
.B "CANNOT ALLOCATE ROOT INODE" .
.IP NO
.I fsck_ffs
will then prompt with
.B "CONTINUE" .
.LP
Possible responses to the CONTINUE prompt are:
.IP YES
ignore the
.B "DUPS/BAD"
error condition in the root inode and
attempt to continue to run the file system check.
If the root inode is not correct,
then this may result in many other error conditions.
.IP NO
terminate the program.
.sp
.LP
.B "NAME TOO LONG \fIF\fP"
.br
An excessively long path name has been found.
This usually indicates loops in the file system name space.
This can occur if the super user has made circular links to directories.
The offending links must be removed (by a guru).
.sp
.LP
.B "I OUT OF RANGE I=\fII\fP NAME=\fIF\fP (REMOVE)"
.br
A directory entry \fIF\fP has an inode number \fII\fP that is greater than
the end of the inode list.
.LP
Possible responses to the REMOVE prompt are:
.IP YES
the directory entry \fIF\fP is removed.
.IP NO
ignore this error condition.
.sp
.LP
.B "UNALLOCATED I=\fII\fP OWNER=\fIO\fP MODE=\fIM\fP SIZE=\fIS\fP MTIME=\fIT\fP \fItype\fP=\fIF\fP (REMOVE)"
.br
A directory or file entry \fIF\fP points to an unallocated inode \fII\fP.
The owner \fIO\fP, mode \fIM\fP, size \fIS\fP, modify time \fIT\fP,
and name \fIF\fP are printed.
.LP
Possible responses to the REMOVE prompt are:
.IP YES
the directory entry \fIF\fP is removed.
.IP NO
ignore this error condition.
.sp
.LP
.B "DUP/BAD I=\fII\fP OWNER=\fIO\fP MODE=\fIM\fP SIZE=\fIS\fP MTIME=\fIT\fP \fItype\fP=\fIF\fP (REMOVE)"
.br
Phase 1 or Phase 1b have found duplicate blocks or bad blocks
associated with directory or file entry \fIF\fP, inode \fII\fP.
The owner \fIO\fP, mode \fIM\fP, size \fIS\fP, modify time \fIT\fP,
and directory name \fIF\fP are printed.
.LP
Possible responses to the REMOVE prompt are:
.IP YES
the directory entry \fIF\fP is removed.
.IP NO
ignore this error condition.
.sp
.LP
.B "ZERO LENGTH DIRECTORY I=\fII\fP OWNER=\fIO\fP MODE=\fIM\fP SIZE=\fIS\fP MTIME=\fIT\fP DIR=\fIF\fP (REMOVE)"
.br
A directory entry \fIF\fP has a size \fIS\fP that is zero.
The owner \fIO\fP, mode \fIM\fP, size \fIS\fP, modify time \fIT\fP,
and directory name \fIF\fP are printed.
.LP
Possible responses to the REMOVE prompt are:
.IP YES
the directory entry \fIF\fP is removed;
this will always invoke the BAD/DUP error condition in Phase 4.
.IP NO
ignore this error condition.
.sp
.LP
.B "DIRECTORY TOO SHORT I=\fII\fP OWNER=\fIO\fP MODE=\fIM\fP SIZE=\fIS\fP MTIME=\fIT\fP DIR=\fIF\fP (FIX)"
.br
A directory \fIF\fP has been found whose size \fIS\fP
is less than the minimum size directory.
The owner \fIO\fP, mode \fIM\fP, size \fIS\fP, modify time \fIT\fP,
and directory name \fIF\fP are printed.
.LP
Possible responses to the FIX prompt are:
.IP YES
increase the size of the directory to the minimum directory size.
.IP NO
ignore this directory.
.sp
.LP
.B "DIRECTORY \fIF\fP LENGTH \fIS\fP NOT MULTIPLE OF \fIB\fP (ADJUST)
.br
A directory \fIF\fP has been found with size \fIS\fP that is not
a multiple of the directory blocksize \fIB\fP.
.LP
Possible responses to the ADJUST prompt are:
.IP YES
the length is rounded up to the appropriate block size.
This error can occur on 4.2BSD file systems.
Thus when preen'ing the file system only a warning is printed
and the directory is adjusted.
.IP NO
ignore the error condition.
.sp
.LP
.B "DIRECTORY CORRUPTED I=\fII\fP OWNER=\fIO\fP MODE=\fIM\fP SIZE=\fIS\fP MTIME=\fIT\fP DIR=\fIF\fP (SALVAGE)"
.br
A directory with an inconsistent internal state has been found.
.LP
Possible responses to the FIX prompt are:
.IP YES
throw away all entries up to the next directory boundary (usually 512-byte)
boundary.
This drastic action can throw away up to 42 entries,
and should be taken only after other recovery efforts have failed.
.IP NO
skip up to the next directory boundary and resume reading,
but do not modify the directory.
.sp
.LP
.B "BAD INODE NUMBER FOR `.' I=\fII\fP OWNER=\fIO\fP MODE=\fIM\fP SIZE=\fIS\fP MTIME=\fIT\fP DIR=\fIF\fP (FIX)"
.br
A directory \fII\fP has been found whose inode number for `.' does
does not equal \fII\fP.
.LP
Possible responses to the FIX prompt are:
.IP YES
change the inode number for `.' to be equal to \fII\fP.
.IP NO
leave the inode number for `.' unchanged.
.sp
.LP
.B "MISSING `.' I=\fII\fP OWNER=\fIO\fP MODE=\fIM\fP SIZE=\fIS\fP MTIME=\fIT\fP DIR=\fIF\fP (FIX)"
.br
A directory \fII\fP has been found whose first entry is unallocated.
.LP
Possible responses to the FIX prompt are:
.IP YES
build an entry for `.' with inode number equal to \fII\fP.
.IP NO
leave the directory unchanged.
.sp
.LP
.B "MISSING `.' I=\fII\fP OWNER=\fIO\fP MODE=\fIM\fP SIZE=\fIS\fP MTIME=\fIT\fP DIR=\fIF\fP"
.br
.B "CANNOT FIX, FIRST ENTRY IN DIRECTORY CONTAINS \fIF\fP"
.br
A directory \fII\fP has been found whose first entry is \fIF\fP.
.I Fsck_ffs
cannot resolve this problem. 
The file system should be mounted and the offending entry \fIF\fP
moved elsewhere.
The file system should then be unmounted and
.I fsck_ffs
should be run again.
.sp
.LP
.B "MISSING `.' I=\fII\fP OWNER=\fIO\fP MODE=\fIM\fP SIZE=\fIS\fP MTIME=\fIT\fP DIR=\fIF\fP"
.br
.B "CANNOT FIX, INSUFFICIENT SPACE TO ADD `.'"
.br
A directory \fII\fP has been found whose first entry is not `.'.
.I Fsck_ffs
cannot resolve this problem as it should never happen.
See a guru.
.sp
.LP
.B "EXTRA `.' ENTRY I=\fII\fP OWNER=\fIO\fP MODE=\fIM\fP SIZE=\fIS\fP MTIME=\fIT\fP DIR=\fIF\fP (FIX)"
.br
A directory \fII\fP has been found that has more than one entry for `.'.
.LP
Possible responses to the FIX prompt are:
.IP YES
remove the extra entry for `.'.
.IP NO
leave the directory unchanged.
.sp
.LP
.B "BAD INODE NUMBER FOR `..' I=\fII\fP OWNER=\fIO\fP MODE=\fIM\fP SIZE=\fIS\fP MTIME=\fIT\fP DIR=\fIF\fP (FIX)"
.br
A directory \fII\fP has been found whose inode number for `..' does
does not equal the parent of \fII\fP.
.LP
Possible responses to the FIX prompt are:
.IP YES
change the inode number for `..' to be equal to the parent of \fII\fP
(``\fB..\fP'' in the root inode points to itself).
.IP NO
leave the inode number for `..' unchanged.
.sp
.LP
.B "MISSING `..' I=\fII\fP OWNER=\fIO\fP MODE=\fIM\fP SIZE=\fIS\fP MTIME=\fIT\fP DIR=\fIF\fP (FIX)"
.br
A directory \fII\fP has been found whose second entry is unallocated.
.LP
Possible responses to the FIX prompt are:
.IP YES
build an entry for `..' with inode number equal to the parent of \fII\fP
(``\fB..\fP'' in the root inode points to itself).
.IP NO
leave the directory unchanged.
.sp
.LP
.B "MISSING `..' I=\fII\fP OWNER=\fIO\fP MODE=\fIM\fP SIZE=\fIS\fP MTIME=\fIT\fP DIR=\fIF\fP"
.br
.B "CANNOT FIX, SECOND ENTRY IN DIRECTORY CONTAINS \fIF\fP"
.br
A directory \fII\fP has been found whose second entry is \fIF\fP.
.I Fsck_ffs
cannot resolve this problem. 
The file system should be mounted and the offending entry \fIF\fP
moved elsewhere.
The file system should then be unmounted and
.I fsck_ffs
should be run again.
.sp
.LP
.B "MISSING `..' I=\fII\fP OWNER=\fIO\fP MODE=\fIM\fP SIZE=\fIS\fP MTIME=\fIT\fP DIR=\fIF\fP"
.br
.B "CANNOT FIX, INSUFFICIENT SPACE TO ADD `..'"
.br
A directory \fII\fP has been found whose second entry is not `..'.
.I Fsck_ffs
cannot resolve this problem.
The file system should be mounted and the second entry in the directory
moved elsewhere.
The file system should then be unmounted and
.I fsck_ffs
should be run again.
.sp
.LP
.B "EXTRA `..' ENTRY I=\fII\fP OWNER=\fIO\fP MODE=\fIM\fP SIZE=\fIS\fP MTIME=\fIT\fP DIR=\fIF\fP (FIX)"
.br
A directory \fII\fP has been found that has more than one entry for `..'.
.LP
Possible responses to the FIX prompt are:
.IP YES
remove the extra entry for `..'.
.IP NO
leave the directory unchanged.
.sp
.LP
.B "\fIN\fP IS AN EXTRANEOUS HARD LINK TO A DIRECTORY \fID\fP (REMOVE)
.br
.I Fsck_ffs
has found a hard link, \fIN\fP, to a directory, \fID\fP.
When preen'ing the extraneous links are ignored.
.LP
Possible responses to the REMOVE prompt are:
.IP YES
delete the extraneous entry, \fIN\fP.
.IP NO
ignore the error condition.
.sp
.LP
.B "BAD INODE \fIS\fP TO DESCEND"
.br
An internal error has caused an impossible state \fIS\fP to be passed to the
routine that descends the file system directory structure.
.I Fsck_ffs
exits.
See a guru.
.sp
.LP
.B "BAD RETURN STATE \fIS\fP FROM DESCEND"
.br
An internal error has caused an impossible state \fIS\fP to be returned
from the routine that descends the file system directory structure.
.I Fsck_ffs
exits.
See a guru.
.sp
.LP
.B "BAD STATE \fIS\fP FOR ROOT INODE"
.br
An internal error has caused an impossible state \fIS\fP to be assigned
to the root inode.
.I Fsck_ffs
exits.
See a guru.
.NH 2 
Phase 3 \- Check Connectivity
.PP
This phase concerns itself with the directory connectivity seen in
Phase 2.
This section lists error conditions resulting from
unreferenced directories,
and missing or full
.I lost+found
directories.
.sp
.LP
.B "UNREF DIR I=\fII\fP OWNER=\fIO\fP MODE=\fIM\fP SIZE=\fIS\fP MTIME=\fIT\fP (RECONNECT)"
.br
The directory inode \fII\fP was not connected to a directory entry
when the file system was traversed.
The owner \fIO\fP, mode \fIM\fP, size \fIS\fP, and
modify time \fIT\fP of directory inode \fII\fP are printed.
When preen'ing, the directory is reconnected if its size is non-zero,
otherwise it is cleared.
.LP
Possible responses to the RECONNECT prompt are:
.IP YES
reconnect directory inode \fII\fP to the file system in the
directory for lost files (usually \fIlost+found\fP).
This may invoke the
.I lost+found
error condition in Phase 3
if there are problems connecting directory inode \fII\fP to \fIlost+found\fP.
This may also invoke the CONNECTED error condition in Phase 3 if the link
was successful.
.IP NO
ignore this error condition.
This will always invoke the UNREF error condition in Phase 4.
.sp
.LP
.B "NO lost+found DIRECTORY (CREATE)"
.br
There is no
.I lost+found
directory in the root directory of the file system;
When preen'ing
.I fsck_ffs
tries to create a \fIlost+found\fP directory.
.LP
Possible responses to the CREATE prompt are:
.IP YES
create a \fIlost+found\fP directory in the root of the file system.
This may raise the message:
.br
.B "NO SPACE LEFT IN / (EXPAND)"
.br
See below for the possible responses.
Inability to create a \fIlost+found\fP directory generates the message:
.br
.B "SORRY. CANNOT CREATE lost+found DIRECTORY"
.br
and aborts the attempt to linkup the lost inode.
This will always invoke the UNREF error condition in Phase 4.
.IP NO
abort the attempt to linkup the lost inode.
This will always invoke the UNREF error condition in Phase 4.
.sp
.LP
.B "lost+found IS NOT A DIRECTORY (REALLOCATE)"
.br
The entry for
.I lost+found
is not a directory.
.LP
Possible responses to the REALLOCATE prompt are:
.IP YES
allocate a directory inode, and change \fIlost+found\fP to reference it.
The previous inode reference by the \fIlost+found\fP name is not cleared.
Thus it will either be reclaimed as an UNREF'ed inode or have its
link count ADJUST'ed later in this Phase.
Inability to create a \fIlost+found\fP directory generates the message:
.br
.B "SORRY. CANNOT CREATE lost+found DIRECTORY"
.br
and aborts the attempt to linkup the lost inode.
This will always invoke the UNREF error condition in Phase 4.
.IP NO
abort the attempt to linkup the lost inode.
This will always invoke the UNREF error condition in Phase 4.
.sp
.LP
.B "NO SPACE LEFT IN /lost+found (EXPAND)"
.br
There is no space to add another entry to the
.I lost+found
directory in the root directory
of the file system.
When preen'ing the 
.I lost+found
directory is expanded.
.LP
Possible responses to the EXPAND prompt are:
.IP YES
the 
.I lost+found
directory is expanded to make room for the new entry.
If the attempted expansion fails
.I fsck_ffs
prints the message:
.br
.B "SORRY. NO SPACE IN lost+found DIRECTORY"
.br
and aborts the attempt to linkup the lost inode.
This will always invoke the UNREF error condition in Phase 4.
Clean out unnecessary entries in
.I lost+found .
This error is fatal if the file system is being preen'ed.
.IP NO
abort the attempt to linkup the lost inode.
This will always invoke the UNREF error condition in Phase 4.
.sp
.LP
.B "DIR I=\fII1\fP CONNECTED. PARENT WAS I=\fII2\fP"
.br
This is an advisory message indicating a directory inode \fII1\fP was
successfully connected to the
.I lost+found
directory.
The parent inode \fII2\fP of the directory inode \fII1\fP is
replaced by the inode number of the
.I lost+found
directory.
.sp
.LP
.B "DIRECTORY \fIF\fP LENGTH \fIS\fP NOT MULTIPLE OF \fIB\fP (ADJUST)
.br
A directory \fIF\fP has been found with size \fIS\fP that is not
a multiple of the directory blocksize \fIB\fP
(this can reoccur in Phase 3 if it is not adjusted in Phase 2).
.LP
Possible responses to the ADJUST prompt are:
.IP YES
the length is rounded up to the appropriate block size.
This error can occur on 4.2BSD file systems.
Thus when preen'ing the file system only a warning is printed
and the directory is adjusted.
.IP NO
ignore the error condition.
.sp
.LP
.B "BAD INODE \fIS\fP TO DESCEND"
.br
An internal error has caused an impossible state \fIS\fP to be passed to the
routine that descends the file system directory structure.
.I Fsck_ffs
exits.
See a guru.
.NH 2 
Phase 4 \- Check Reference Counts
.PP
This phase concerns itself with the link count information
seen in Phase 2 and Phase 3.
This section lists error conditions resulting from
unreferenced files,
missing or full
.I lost+found
directory,
incorrect link counts for files, directories, symbolic links, or special files,
unreferenced files, symbolic links, and directories,
and bad or duplicate blocks in files, symbolic links, and directories.
All errors in this phase are correctable if the file system is being preen'ed
except running out of space in the \fIlost+found\fP directory.
.sp
.LP
.B "UNREF FILE I=\fII\fP OWNER=\fIO\fP MODE=\fIM\fP SIZE=\fIS\fP MTIME=\fIT\fP (RECONNECT)"
.br
Inode \fII\fP was not connected to a directory entry
when the file system was traversed.
The owner \fIO\fP, mode \fIM\fP, size \fIS\fP, and
modify time \fIT\fP of inode \fII\fP are printed.
When preen'ing the file is cleared if either its size or its
link count is zero,
otherwise it is reconnected.
.LP
Possible responses to the RECONNECT prompt are:
.IP YES
reconnect inode \fII\fP to the file system in the directory for
lost files (usually \fIlost+found\fP).
This may invoke the
.I lost+found
error condition in Phase 4
if there are problems connecting inode \fII\fP to
.I lost+found .
.IP NO
ignore this error condition.
This will always invoke the CLEAR error condition in Phase 4.
.sp
.LP
.B "(CLEAR)"
.br
The inode mentioned in the immediately previous error condition can not be
reconnected.
This cannot occur if the file system is being preen'ed,
since lack of space to reconnect files is a fatal error.
.LP
Possible responses to the CLEAR prompt are:
.IP YES
de-allocate the inode mentioned in the immediately previous error condition by zeroing its contents.
.IP NO
ignore this error condition.
.sp
.LP
.B "NO lost+found DIRECTORY (CREATE)"
.br
There is no
.I lost+found
directory in the root directory of the file system;
When preen'ing
.I fsck_ffs
tries to create a \fIlost+found\fP directory.
.LP
Possible responses to the CREATE prompt are:
.IP YES
create a \fIlost+found\fP directory in the root of the file system.
This may raise the message:
.br
.B "NO SPACE LEFT IN / (EXPAND)"
.br
See below for the possible responses.
Inability to create a \fIlost+found\fP directory generates the message:
.br
.B "SORRY. CANNOT CREATE lost+found DIRECTORY"
.br
and aborts the attempt to linkup the lost inode.
This will always invoke the UNREF error condition in Phase 4.
.IP NO
abort the attempt to linkup the lost inode.
This will always invoke the UNREF error condition in Phase 4.
.sp
.LP
.B "lost+found IS NOT A DIRECTORY (REALLOCATE)"
.br
The entry for
.I lost+found
is not a directory.
.LP
Possible responses to the REALLOCATE prompt are:
.IP YES
allocate a directory inode, and change \fIlost+found\fP to reference it.
The previous inode reference by the \fIlost+found\fP name is not cleared.
Thus it will either be reclaimed as an UNREF'ed inode or have its
link count ADJUST'ed later in this Phase.
Inability to create a \fIlost+found\fP directory generates the message:
.br
.B "SORRY. CANNOT CREATE lost+found DIRECTORY"
.br
and aborts the attempt to linkup the lost inode.
This will always invoke the UNREF error condition in Phase 4.
.IP NO
abort the attempt to linkup the lost inode.
This will always invoke the UNREF error condition in Phase 4.
.sp
.LP
.B "NO SPACE LEFT IN /lost+found (EXPAND)"
.br
There is no space to add another entry to the
.I lost+found
directory in the root directory
of the file system.
When preen'ing the 
.I lost+found
directory is expanded.
.LP
Possible responses to the EXPAND prompt are:
.IP YES
the 
.I lost+found
directory is expanded to make room for the new entry.
If the attempted expansion fails
.I fsck_ffs
prints the message:
.br
.B "SORRY. NO SPACE IN lost+found DIRECTORY"
.br
and aborts the attempt to linkup the lost inode.
This will always invoke the UNREF error condition in Phase 4.
Clean out unnecessary entries in
.I lost+found .
This error is fatal if the file system is being preen'ed.
.IP NO
abort the attempt to linkup the lost inode.
This will always invoke the UNREF error condition in Phase 4.
.sp
.LP
.B "LINK COUNT \fItype\fP I=\fII\fP OWNER=\fIO\fP MODE=\fIM\fP SIZE=\fIS\fP MTIME=\fIT\fP COUNT=\fIX\fP SHOULD BE \fIY\fP (ADJUST)"
.br
The link count for inode \fII\fP,
is \fIX\fP but should be \fIY\fP.
The owner \fIO\fP, mode \fIM\fP, size \fIS\fP, and modify time \fIT\fP
are printed.
When preen'ing the link count is adjusted unless the number of references
is increasing, a condition that should never occur unless precipitated
by a hardware failure.
When the number of references is increasing under preen mode,
.I fsck_ffs
exits with the message:
.br
.B "LINK COUNT INCREASING"
.LP
Possible responses to the ADJUST prompt are:
.IP YES
replace the link count of file inode \fII\fP with \fIY\fP.
.IP NO
ignore this error condition.
.sp
.LP
.B "UNREF \fItype\fP I=\fII\fP OWNER=\fIO\fP MODE=\fIM\fP SIZE=\fIS\fP MTIME=\fIT\fP (CLEAR)"
.br
Inode \fII\fP, was not connected to a directory entry when the
file system was traversed.
The owner \fIO\fP, mode \fIM\fP, size \fIS\fP,
and modify time \fIT\fP of inode \fII\fP
are printed.
When preen'ing,
this is a file that was not connected because its size or link count was zero,
hence it is cleared.
.LP
Possible responses to the CLEAR prompt are:
.IP YES
de-allocate inode \fII\fP by zeroing its contents.
.IP NO
ignore this error condition.
.sp
.LP
.B "BAD/DUP \fItype\fP I=\fII\fP OWNER=\fIO\fP MODE=\fIM\fP SIZE=\fIS\fP MTIME=\fIT\fP (CLEAR)"
.br
Phase 1 or Phase 1b have found duplicate blocks
or bad blocks associated with
inode \fII\fP.
The owner \fIO\fP, mode \fIM\fP, size \fIS\fP,
and modify time \fIT\fP of inode \fII\fP
are printed.
This error cannot arise when the file system is being preen'ed,
as it would have caused a fatal error earlier.
.LP
Possible responses to the CLEAR prompt are:
.IP YES
de-allocate inode \fII\fP by zeroing its contents.
.IP NO
ignore this error condition.
.NH 2 
Phase 5 - Check Cyl groups
.PP
This phase concerns itself with the free-block and used-inode maps.
This section lists error conditions resulting from
allocated blocks in the free-block maps,
free blocks missing from free-block maps,
and the total free-block count incorrect.
It also lists error conditions resulting from
free inodes in the used-inode maps,
allocated inodes missing from used-inode maps,
and the total used-inode count incorrect.
.sp
.LP
.B "CG \fIC\fP: BAD MAGIC NUMBER"
.br
The magic number of cylinder group \fIC\fP is wrong.
This usually indicates that the cylinder group maps have been destroyed.
When running manually the cylinder group is marked as needing
to be reconstructed.
This error is fatal if the file system is being preen'ed.
.sp
.LP
.B "BLK(S) MISSING IN BIT MAPS (SALVAGE)"
.br
A cylinder group block map is missing some free blocks.
During preen'ing the maps are reconstructed.
.LP
Possible responses to the SALVAGE prompt are:
.IP YES
reconstruct the free block map.
.IP NO
ignore this error condition.
.sp
.LP
.B "SUMMARY INFORMATION BAD (SALVAGE)"
.br
The summary information was found to be incorrect.
When preen'ing,
the summary information is recomputed.
.LP
Possible responses to the SALVAGE prompt are:
.IP YES
reconstruct the summary information.
.IP NO
ignore this error condition.
.sp
.LP
.B "FREE BLK COUNT(S) WRONG IN SUPERBLOCK (SALVAGE)"
.br
The superblock free block information was found to be incorrect.
When preen'ing,
the superblock free block information is recomputed.
.LP
Possible responses to the SALVAGE prompt are:
.IP YES
reconstruct the superblock free block information.
.IP NO
ignore this error condition.
.NH 2 
Cleanup
.PP
Once a file system has been checked, a few cleanup functions are performed.
This section lists advisory messages about
the file system
and modify status of the file system.
.sp
.LP
.B "\fIV\fP files, \fIW\fP used, \fIX\fP free (\fIY\fP frags, \fIZ\fP blocks)"
.br
This is an advisory message indicating that
the file system checked contained
\fIV\fP files using
\fIW\fP fragment sized blocks leaving
\fIX\fP fragment sized blocks free in the file system.
The numbers in parenthesis breaks the free count down into
\fIY\fP free fragments and
\fIZ\fP free full sized blocks.
.sp
.LP
.B "***** REBOOT UNIX *****"
.br
This is an advisory message indicating that
the root file system has been modified by
.I fsck_ffs.
If UNIX is not rebooted immediately,
the work done by
.I fsck_ffs
may be undone by the in-core copies of tables
UNIX keeps.
When preen'ing,
.I fsck_ffs
will exit with a code of 4.
The standard auto-reboot script distributed with 4.3BSD 
interprets an exit code of 4 by issuing a reboot system call.
.sp
.LP
.B "***** FILE SYSTEM WAS MODIFIED *****"
.br
This is an advisory message indicating that
the current file system was modified by
.I fsck_ffs.
If this file system is mounted or is the current root file system,
.I fsck_ffs
should be halted and UNIX rebooted.
If UNIX is not rebooted immediately,
the work done by
.I fsck_ffs
may be undone by the in-core copies of tables
UNIX keeps.
