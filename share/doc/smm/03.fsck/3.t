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
.\"	@(#)3.t	8.1 (Berkeley) 6/5/93
.\"
.ds RH Fixing corrupted file systems
.NH
Fixing corrupted file systems
.PP
A file system
can become corrupted in several ways.
The most common of these ways are
improper shutdown procedures
and hardware failures.
.PP
File systems may become corrupted during an
.I "unclean halt" .
This happens when proper shutdown
procedures are not observed,
physically write-protecting a mounted file system,
or a mounted file system is taken off-line.
The most common operator procedural failure is forgetting to
.I sync
the system before halting the CPU.
.PP
File systems may become further corrupted if proper startup
procedures are not observed, e.g.,
not checking a file system for inconsistencies,
and not repairing inconsistencies.
Allowing a corrupted file system to be used (and, thus, to be modified
further) can be disastrous.
.PP
Any piece of hardware can fail at any time.
Failures
can be as subtle as a bad block
on a disk pack, or as blatant as a non-functional disk-controller.
.NH 2
Detecting and correcting corruption
.PP
Normally
.I fsck_ffs
is run non-interactively.
In this mode it will only fix
corruptions that are expected to occur from an unclean halt.
These actions are a proper subset of the actions that 
.I fsck_ffs
will take when it is running interactively.
Throughout this paper we assume that 
.I fsck_ffs 
is being run interactively,
and all possible errors can be encountered.
When an inconsistency is discovered in this mode,
.I fsck_ffs
reports the inconsistency for the operator to
chose a corrective action.
.PP
A quiescent\(dd
.FS
\(dd I.e., unmounted and not being written on.
.FE
file system may be checked for structural integrity
by performing consistency checks on the
redundant data intrinsic to a file system.
The redundant data is either read from
the file system,
or computed from other known values.
The file system
.B must
be in a quiescent state when
.I fsck_ffs
is run,
since
.I fsck_ffs
is a multi-pass program.
.PP
In the following sections,
we discuss methods to discover inconsistencies
and possible corrective actions
for the cylinder group blocks, the inodes, the indirect blocks, and
the data blocks containing directory entries.
.NH 2
Super-block checking
.PP
The most commonly corrupted item in a file system
is the summary information
associated with the super-block.
The summary information is prone to corruption
because it is modified with every change to the file
system's blocks or inodes,
and is usually corrupted
after an unclean halt.
.PP
The super-block is checked for inconsistencies
involving file-system size, number of inodes,
free-block count, and the free-inode count.
The file-system size must be larger than the
number of blocks used by the super-block
and the number of blocks used by the list of inodes.
The file-system size and layout information
are the most critical pieces of information for
.I fsck_ffs .
While there is no way to actually check these sizes,
since they are statically determined by
.I newfs ,
.I fsck_ffs
can check that these sizes are within reasonable bounds.
All other file system checks require that these sizes be correct.
If
.I fsck_ffs 
detects corruption in the static parameters of the default super-block,
.I fsck_ffs
requests the operator to specify the location of an alternate super-block.
.NH 2
Free block checking
.PP
.I Fsck_ffs
checks that all the blocks
marked as free in the cylinder group block maps
are not claimed by any files.
When all the blocks have been initially accounted for,
.I fsck_ffs
checks that
the number of free blocks
plus the number of blocks claimed by the inodes
equals the total number of blocks in the file system.
.PP
If anything is wrong with the block allocation maps,
.I fsck_ffs
will rebuild them,
based on the list it has computed of allocated blocks.
.PP
The summary information associated with the super-block
counts the total number of free blocks within the file system.
.I Fsck_ffs
compares this count to the
number of free blocks it found within the file system.
If the two counts do not agree, then
.I fsck_ffs
replaces the incorrect count in the summary information
by the actual free-block count.
.PP
The summary information
counts the total number of free inodes within the file system.
.I Fsck_ffs
compares this count to the number
of free inodes it found within the file system.
If the two counts do not agree, then
.I fsck_ffs
replaces the incorrect count in the
summary information by the actual free-inode count.
.NH 2
Checking the inode state
.PP
An individual inode is not as likely to be corrupted as
the allocation information.
However, because of the great number of active inodes,
a few of the inodes are usually corrupted.
.PP
The list of inodes in the file system
is checked sequentially starting with inode 2
(inode 0 marks unused inodes;
inode 1 is saved for future generations)
and progressing through the last inode in the file system.
The state of each inode is checked for
inconsistencies involving format and type,
link count,
duplicate blocks,
bad blocks,
and inode size.
.PP
Each inode contains a mode word.
This mode word describes the type and state of the inode.
Inodes must be one of six types:
regular inode, directory inode, symbolic link inode,
special block inode, special character inode, or socket inode.
Inodes may be found in one of three allocation states:
unallocated, allocated, and neither unallocated nor allocated.
This last state suggests an incorrectly formated inode.
An inode can get in this state if
bad data is written into the inode list.
The only possible corrective action is for
.I fsck_ffs
is to clear the inode.
.NH 2
Inode links
.PP
Each inode counts the
total number of directory entries
linked to the inode.
.I Fsck_ffs
verifies the link count of each inode
by starting at the root of the file system,
and descending through the directory structure.
The actual link count for each inode
is calculated during the descent.
.PP
If the stored link count is non-zero and the actual
link count is zero,
then no directory entry appears for the inode.
If this happens,
.I fsck_ffs
will place the disconnected file in the
.I lost+found
directory.
If the stored and actual link counts are non-zero and unequal,
a directory entry may have been added or removed without the inode being
updated.
If this happens,
.I fsck_ffs
replaces the incorrect stored link count by the actual link count.
.PP
Each inode contains a list,
or pointers to
lists (indirect blocks),
of all the blocks claimed by the inode.
Since indirect blocks are owned by an inode,
inconsistencies in indirect blocks directly
affect the inode that owns it.
.PP
.I Fsck_ffs
compares each block number claimed by an inode
against a list of already allocated blocks.
If another inode already claims a block number,
then the block number is added to a list of
.I "duplicate blocks" .
Otherwise, the list of allocated blocks
is updated to include the block number.
.PP
If there are any duplicate blocks,
.I fsck_ffs
will perform a partial second
pass over the inode list
to find the inode of the duplicated block.
The second pass is needed,
since without examining the files associated with
these inodes for correct content,
not enough information is available
to determine which inode is corrupted and should be cleared.
If this condition does arise
(only hardware failure will cause it),
then the inode with the earliest
modify time is usually incorrect,
and should be cleared.
If this happens,
.I fsck_ffs
prompts the operator to clear both inodes.
The operator must decide which one should be kept
and which one should be cleared.
.PP
.I Fsck_ffs
checks the range of each block number claimed by an inode.
If the block number is
lower than the first data block in the file system,
or greater than the last data block,
then the block number is a
.I "bad block number" .
Many bad blocks in an inode are usually caused by
an indirect block that was not written to the file system,
a condition which can only occur if there has been a hardware failure.
If an inode contains bad block numbers,
.I fsck_ffs
prompts the operator to clear it.
.NH 2
Inode data size
.PP
Each inode contains a count of the number of data blocks
that it contains.
The number of actual data blocks
is the sum of the allocated data blocks
and the indirect blocks.
.I Fsck_ffs
computes the actual number of data blocks
and compares that block count against
the actual number of blocks the inode claims.
If an inode contains an incorrect count
.I fsck_ffs
prompts the operator to fix it.
.PP
Each inode contains a thirty-two bit size field.
The size is the number of data bytes
in the file associated with the inode.
The consistency of the byte size field is roughly checked
by computing from the size field the maximum number of blocks
that should be associated with the inode,
and comparing that expected block count against
the actual number of blocks the inode claims.
.NH 2
Checking the data associated with an inode
.PP
An inode can directly or indirectly
reference three kinds of data blocks.
All referenced blocks must be the same kind.
The three types of data blocks are:
plain data blocks, symbolic link data blocks, and directory data blocks.
Plain data blocks
contain the information stored in a file;
symbolic link data blocks
contain the path name stored in a link.
Directory data blocks contain directory entries.
.I Fsck_ffs
can only check the validity of directory data blocks.
.PP
Each directory data block is checked for
several types of inconsistencies.
These inconsistencies include
directory inode numbers pointing to unallocated inodes,
directory inode numbers that are greater than
the number of inodes in the file system,
incorrect directory inode numbers for ``\fB.\fP'' and ``\fB..\fP'',
and directories that are not attached to the file system.
If the inode number in a directory data block
references an unallocated inode,
then
.I fsck_ffs
will remove that directory entry.
Again,
this condition can only arise when there has been a hardware failure.
.PP
.I Fsck_ffs
also checks for directories with unallocated blocks (holes).
Such directories should never be created.
When found,
.I fsck_ffs
will prompt the user to adjust the length of the offending directory
which is done by shortening the size of the directory to the end of the
last allocated block preceding the hole.
Unfortunately, this means that another Phase 1 run has to be done. 
.I Fsck_ffs
will remind the user to rerun fsck_ffs after repairing a
directory containing an unallocated block.
.PP
If a directory entry inode number references
outside the inode list, then
.I fsck_ffs
will remove that directory entry.
This condition occurs if bad data is written into a directory data block.
.PP
The directory inode number entry for ``\fB.\fP''
must be the first entry in the directory data block.
The inode number for ``\fB.\fP''
must reference itself;
e.g., it must equal the inode number
for the directory data block.
The directory inode number entry
for ``\fB..\fP'' must be
the second entry in the directory data block.
Its value must equal the inode number for the
parent of the directory entry
(or the inode number of the directory
data block if the directory is the
root directory).
If the directory inode numbers are
incorrect,
.I fsck_ffs
will replace them with the correct values.
If there are multiple hard links to a directory,
the first one encountered is considered the real parent
to which ``\fB..\fP'' should point;
\fIfsck_ffs\fP recommends deletion for the subsequently discovered names.
.NH 2
File system connectivity
.PP
.I Fsck_ffs
checks the general connectivity of the file system.
If directories are not linked into the file system, then
.I fsck_ffs
links the directory back into the file system in the
.I lost+found
directory.
This condition only occurs when there has been a hardware failure.
.ds RH "References"
.SH
\s+2Acknowledgements\s0
.PP
I thank Bill Joy, Sam Leffler, Robert Elz and Dennis Ritchie 
for their suggestions and help in implementing the new file system.
Thanks also to Robert Henry for his editorial input to
get this document together.
Finally we thank our sponsors,
the National Science Foundation under grant MCS80-05144,
and the Defense Advance Research Projects Agency (DoD) under
Arpa Order No. 4031 monitored by Naval Electronic System Command under
Contract No. N00039-82-C-0235. (Kirk McKusick, July 1983)
.PP
I would like to thank Larry A. Wehr for advice that lead
to the first version of
.I fsck_ffs
and Rick B. Brandt for adapting
.I fsck_ffs
to
UNIX/TS. (T. Kowalski, July 1979)
.sp 2
.SH
\s+2References\s0
.LP
.IP [Dolotta78] 20
Dolotta, T. A., and Olsson, S. B. eds.,
.I "UNIX User's Manual, Edition 1.1\^" ,
January 1978.
.IP [Joy83] 20
Joy, W., Cooper, E., Fabry, R., Leffler, S., McKusick, M., and Mosher, D.
4.2BSD System Manual,
.I "University of California at Berkeley" ,
.I "Computer Systems Research Group Technical Report"
#4, 1982.
.IP [McKusick84] 20
McKusick, M., Joy, W., Leffler, S., and Fabry, R.
A Fast File System for UNIX,
\fIACM Transactions on Computer Systems 2\fP, 3.
pp. 181-197, August 1984.
.IP [Ritchie78] 20
Ritchie, D. M., and Thompson, K.,
The UNIX Time-Sharing System,
.I "The Bell System Technical Journal"
.B 57 ,
6 (July-August 1978, Part 2), pp. 1905-29.
.IP [Thompson78] 20
Thompson, K.,
UNIX Implementation,
.I "The Bell System Technical Journal\^"
.B 57 ,
6 (July-August 1978, Part 2), pp. 1931-46.
.ds RH Appendix A \- Fsck_ffs Error Conditions
.bp
