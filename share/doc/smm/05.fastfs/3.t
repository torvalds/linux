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
.\"	@(#)3.t	8.1 (Berkeley) 6/8/93
.\"
.\"	$FreeBSD$
.\"
.ds RH New file system
.NH
New file system organization
.PP
In the new file system organization (as in the
old file system organization),
each disk drive contains one or more file systems.
A file system is described by its super-block,
located at the beginning of the file system's disk partition.
Because the super-block contains critical data,
it is replicated to protect against catastrophic loss.
This is done when the file system is created;
since the super-block data does not change,
the copies need not be referenced unless a head crash
or other hard disk error causes the default super-block
to be unusable.
.PP
To insure that it is possible to create files as large as
.if n 2 ** 32
.if t $2 sup 32$
bytes with only two levels of indirection,
the minimum size of a file system block is 4096 bytes.
The size of file system blocks can be any power of two
greater than or equal to 4096.
The block size of a file system is recorded in the
file system's super-block
so it is possible for file systems with different block sizes
to be simultaneously accessible on the same system.
The block size must be decided at the time that
the file system is created;
it cannot be subsequently changed without rebuilding the file system.
.PP
The new file system organization divides a disk partition
into one or more areas called
.I "cylinder groups".
A cylinder group is comprised of one or more consecutive
cylinders on a disk.
Associated with each cylinder group is some bookkeeping information
that includes a redundant copy of the super-block,
space for inodes,
a bit map describing available blocks in the cylinder group,
and summary information describing the usage of data blocks
within the cylinder group.
The bit map of available blocks in the cylinder group replaces
the traditional file system's free list.
For each cylinder group a static number of inodes
is allocated at file system creation time.
The default policy is to allocate one inode for each 2048
bytes of space in the cylinder group, expecting this
to be far more than will ever be needed.
.PP
All the cylinder group bookkeeping information could be
placed at the beginning of each cylinder group.
However if this approach were used,
all the redundant information would be on the top platter.
A single hardware failure that destroyed the top platter
could cause the loss of all redundant copies of the super-block.
Thus the cylinder group bookkeeping information
begins at a varying offset from the beginning of the cylinder group.
The offset for each successive cylinder group is calculated to be
about one track further from the beginning of the cylinder group
than the preceding cylinder group.
In this way the redundant
information spirals down into the pack so that any single track, cylinder,
or platter can be lost without losing all copies of the super-block.
Except for the first cylinder group,
the space between the beginning of the cylinder group
and the beginning of the cylinder group information
is used for data blocks.\(dg
.FS
\(dg While it appears that the first cylinder group could be laid
out with its super-block at the ``known'' location,
this would not work for file systems
with blocks sizes of 16 kilobytes or greater.
This is because of a requirement that the first 8 kilobytes of the disk
be reserved for a bootstrap program and a separate requirement that
the cylinder group information begin on a file system block boundary.
To start the cylinder group on a file system block boundary,
file systems with block sizes larger than 8 kilobytes
would have to leave an empty space between the end of
the boot block and the beginning of the cylinder group.
Without knowing the size of the file system blocks,
the system would not know what roundup function to use
to find the beginning of the first cylinder group.
.FE
.NH 2
Optimizing storage utilization
.PP
Data is laid out so that larger blocks can be transferred
in a single disk transaction, greatly increasing file system throughput.
As an example, consider a file in the new file system
composed of 4096 byte data blocks.
In the old file system this file would be composed of 1024 byte blocks.
By increasing the block size, disk accesses in the new file
system may transfer up to four times as much information per
disk transaction.
In large files, several
4096 byte blocks may be allocated from the same cylinder so that
even larger data transfers are possible before requiring a seek.
.PP
The main problem with
larger blocks is that most UNIX
file systems are composed of many small files.
A uniformly large block size wastes space.
Table 1 shows the effect of file system
block size on the amount of wasted space in the file system.
The files measured to obtain these figures reside on
one of our time sharing
systems that has roughly 1.2 gigabytes of on-line storage.
The measurements are based on the active user file systems containing
about 920 megabytes of formatted space.
.KF
.DS B
.TS
box;
l|l|l
a|n|l.
Space used	% waste	Organization
_
775.2 Mb	0.0	Data only, no separation between files
807.8 Mb	4.2	Data only, each file starts on 512 byte boundary
828.7 Mb	6.9	Data + inodes, 512 byte block UNIX file system
866.5 Mb	11.8	Data + inodes, 1024 byte block UNIX file system
948.5 Mb	22.4	Data + inodes, 2048 byte block UNIX file system
1128.3 Mb	45.6	Data + inodes, 4096 byte block UNIX file system
.TE
Table 1 \- Amount of wasted space as a function of block size.
.DE
.KE
The space wasted is calculated to be the percentage of space
on the disk not containing user data.
As the block size on the disk
increases, the waste rises quickly, to an intolerable
45.6% waste with 4096 byte file system blocks.
.PP
To be able to use large blocks without undue waste,
small files must be stored in a more efficient way.
The new file system accomplishes this goal by allowing the division
of a single file system block into one or more
.I "fragments".
The file system fragment size is specified
at the time that the file system is created;
each file system block can optionally be broken into
2, 4, or 8 fragments, each of which is addressable.
The lower bound on the size of these fragments is constrained
by the disk sector size,
typically 512 bytes.
The block map associated with each cylinder group
records the space available in a cylinder group
at the fragment level;
to determine if a block is available, aligned fragments are examined.
Figure 1 shows a piece of a map from a 4096/1024 file system.
.KF
.DS B
.TS
box;
l|c c c c.
Bits in map	XXXX	XXOO	OOXX	OOOO
Fragment numbers	0-3	4-7	8-11	12-15
Block numbers	0	1	2	3
.TE
Figure 1 \- Example layout of blocks and fragments in a 4096/1024 file system.
.DE
.KE
Each bit in the map records the status of a fragment;
an ``X'' shows that the fragment is in use,
while an ``O'' shows that the fragment is available for allocation.
In this example,
fragments 0\-5, 10, and 11 are in use,
while fragments 6\-9, and 12\-15 are free.
Fragments of adjoining blocks cannot be used as a full block,
even if they are large enough.
In this example,
fragments 6\-9 cannot be allocated as a full block;
only fragments 12\-15 can be coalesced into a full block.
.PP
On a file system with a block size of 4096 bytes
and a fragment size of 1024 bytes,
a file is represented by zero or more 4096 byte blocks of data,
and possibly a single fragmented block.
If a file system block must be fragmented to obtain
space for a small amount of data,
the remaining fragments of the block are made
available for allocation to other files.
As an example consider an 11000 byte file stored on
a 4096/1024 byte file system.
This file would uses two full size blocks and one
three fragment portion of another block.
If no block with three aligned fragments is
available at the time the file is created,
a full size block is split yielding the necessary
fragments and a single unused fragment.
This remaining fragment can be allocated to another file as needed.
.PP
Space is allocated to a file when a program does a \fIwrite\fP
system call.
Each time data is written to a file, the system checks to see if
the size of the file has increased*.
.FS
* A program may be overwriting data in the middle of an existing file
in which case space would already have been allocated.
.FE
If the file needs to be expanded to hold the new data,
one of three conditions exists:
.IP 1)
There is enough space left in an already allocated
block or fragment to hold the new data.
The new data is written into the available space.
.IP 2)
The file contains no fragmented blocks (and the last
block in the file
contains insufficient space to hold the new data).
If space exists in a block already allocated,
the space is filled with new data.
If the remainder of the new data contains more than
a full block of data, a full block is allocated and
the first full block of new data is written there.
This process is repeated until less than a full block
of new data remains.
If the remaining new data to be written will
fit in less than a full block,
a block with the necessary fragments is located,
otherwise a full block is located.
The remaining new data is written into the located space.
.IP 3)
The file contains one or more fragments (and the
fragments contain insufficient space to hold the new data).
If the size of the new data plus the size of the data
already in the fragments exceeds the size of a full block,
a new block is allocated.
The contents of the fragments are copied
to the beginning of the block
and the remainder of the block is filled with new data.
The process then continues as in (2) above.
Otherwise, if the new data to be written will
fit in less than a full block,
a block with the necessary fragments is located,
otherwise a full block is located.
The contents of the existing fragments
appended with the new data
are written into the allocated space.
.PP
The problem with expanding a file one fragment at a
a time is that data may be copied many times as a
fragmented block expands to a full block.
Fragment reallocation can be minimized
if the user program writes a full block at a time,
except for a partial block at the end of the file.
Since file systems with different block sizes may reside on
the same system,
the file system interface has been extended to provide
application programs the optimal size for a read or write.
For files the optimal size is the block size of the file system
on which the file is being accessed.
For other objects, such as pipes and sockets,
the optimal size is the underlying buffer size.
This feature is used by the Standard
Input/Output Library,
a package used by most user programs.
This feature is also used by
certain system utilities such as archivers and loaders
that do their own input and output management
and need the highest possible file system bandwidth.
.PP
The amount of wasted space in the 4096/1024 byte new file system
organization is empirically observed to be about the same as in the
1024 byte old file system organization.
A file system with 4096 byte blocks and 512 byte fragments
has about the same amount of wasted space as the 512 byte
block UNIX file system.
The new file system uses less space
than the 512 byte or 1024 byte
file systems for indexing information for
large files and the same amount of space
for small files.
These savings are offset by the need to use
more space for keeping track of available free blocks.
The net result is about the same disk utilization
when a new file system's fragment size
equals an old file system's block size.
.PP
In order for the layout policies to be effective,
a file system cannot be kept completely full.
For each file system there is a parameter, termed
the free space reserve, that
gives the minimum acceptable percentage of file system
blocks that should be free.
If the number of free blocks drops below this level
only the system administrator can continue to allocate blocks.
The value of this parameter may be changed at any time,
even when the file system is mounted and active.
The transfer rates that appear in section 4 were measured on file
systems kept less than 90% full (a reserve of 10%).
If the number of free blocks falls to zero,
the file system throughput tends to be cut in half,
because of the inability of the file system to localize
blocks in a file.
If a file system's performance degrades because
of overfilling, it may be restored by removing
files until the amount of free space once again
reaches the minimum acceptable level.
Access rates for files created during periods of little
free space may be restored by moving their data once enough
space is available.
The free space reserve must be added to the
percentage of waste when comparing the organizations given
in Table 1.
Thus, the percentage of waste in
an old 1024 byte UNIX file system is roughly
comparable to a new 4096/512 byte file system
with the free space reserve set at 5%.
(Compare 11.8% wasted with the old file system
to 6.9% waste + 5% reserved space in the
new file system.)
.NH 2
File system parameterization
.PP
Except for the initial creation of the free list,
the old file system ignores the parameters of the underlying hardware.
It has no information about either the physical characteristics
of the mass storage device,
or the hardware that interacts with it.
A goal of the new file system is to parameterize the
processor capabilities and
mass storage characteristics
so that blocks can be allocated in an
optimum configuration-dependent way.
Parameters used include the speed of the processor,
the hardware support for mass storage transfers,
and the characteristics of the mass storage devices.
Disk technology is constantly improving and
a given installation can have several different disk technologies
running on a single processor.
Each file system is parameterized so that it can be
adapted to the characteristics of the disk on which
it is placed.
.PP
For mass storage devices such as disks,
the new file system tries to allocate new blocks
on the same cylinder as the previous block in the same file.
Optimally, these new blocks will also be
rotationally well positioned.
The distance between ``rotationally optimal'' blocks varies greatly;
it can be a consecutive block
or a rotationally delayed block
depending on system characteristics.
On a processor with an input/output channel that does not require
any processor intervention between mass storage transfer requests,
two consecutive disk blocks can often be accessed
without suffering lost time because of an intervening disk revolution.
For processors without input/output channels,
the main processor must field an interrupt and
prepare for a new disk transfer.
The expected time to service this interrupt and
schedule a new disk transfer depends on the
speed of the main processor.
.PP
The physical characteristics of each disk include
the number of blocks per track and the rate at which
the disk spins.
The allocation routines use this information to calculate
the number of milliseconds required to skip over a block.
The characteristics of the processor include
the expected time to service an interrupt and schedule a
new disk transfer.
Given a block allocated to a file,
the allocation routines calculate the number of blocks to
skip over so that the next block in the file will
come into position under the disk head in the expected
amount of time that it takes to start a new
disk transfer operation.
For programs that sequentially access large amounts of data,
this strategy minimizes the amount of time spent waiting for
the disk to position itself.
.PP
To ease the calculation of finding rotationally optimal blocks,
the cylinder group summary information includes
a count of the available blocks in a cylinder
group at different rotational positions.
Eight rotational positions are distinguished,
so the resolution of the
summary information is 2 milliseconds for a typical 3600
revolution per minute drive.
The super-block contains a vector of lists called
.I "rotational layout tables".
The vector is indexed by rotational position.
Each component of the vector
lists the index into the block map for every data block contained
in its rotational position.
When looking for an allocatable block,
the system first looks through the summary counts for a rotational
position with a non-zero block count.
It then uses the index of the rotational position to find the appropriate
list to use to index through
only the relevant parts of the block map to find a free block.
.PP
The parameter that defines the
minimum number of milliseconds between the completion of a data
transfer and the initiation of
another data transfer on the same cylinder
can be changed at any time,
even when the file system is mounted and active.
If a file system is parameterized to lay out blocks with
a rotational separation of 2 milliseconds,
and the disk pack is then moved to a system that has a
processor requiring 4 milliseconds to schedule a disk operation,
the throughput will drop precipitously because of lost disk revolutions
on nearly every block.
If the eventual target machine is known,
the file system can be parameterized for it
even though it is initially created on a different processor.
Even if the move is not known in advance,
the rotational layout delay can be reconfigured after the disk is moved
so that all further allocation is done based on the
characteristics of the new host.
.NH 2
Layout policies
.PP
The file system layout policies are divided into two distinct parts.
At the top level are global policies that use file system
wide summary information to make decisions regarding
the placement of new inodes and data blocks.
These routines are responsible for deciding the
placement of new directories and files.
They also calculate rotationally optimal block layouts,
and decide when to force a long seek to a new cylinder group
because there are insufficient blocks left
in the current cylinder group to do reasonable layouts.
Below the global policy routines are
the local allocation routines that use a locally optimal scheme to
lay out data blocks.
.PP
Two methods for improving file system performance are to increase
the locality of reference to minimize seek latency
as described by [Trivedi80], and
to improve the layout of data to make larger transfers possible
as described by [Nevalainen77].
The global layout policies try to improve performance
by clustering related information.
They cannot attempt to localize all data references,
but must also try to spread unrelated data
among different cylinder groups.
If too much localization is attempted,
the local cylinder group may run out of space
forcing the data to be scattered to non-local cylinder groups.
Taken to an extreme,
total localization can result in a single huge cluster of data
resembling the old file system.
The global policies try to balance the two conflicting
goals of localizing data that is concurrently accessed
while spreading out unrelated data.
.PP
One allocatable resource is inodes.
Inodes are used to describe both files and directories.
Inodes of files in the same directory are frequently accessed together.
For example, the ``list directory'' command often accesses
the inode for each file in a directory.
The layout policy tries to place all the inodes of
files in a directory in the same cylinder group.
To ensure that files are distributed throughout the disk,
a different policy is used for directory allocation.
A new directory is placed in a cylinder group that has a greater
than average number of free inodes,
and the smallest number of directories already in it.
The intent of this policy is to allow the inode clustering policy
to succeed most of the time.
The allocation of inodes within a cylinder group is done using a
next free strategy.
Although this allocates the inodes randomly within a cylinder group,
all the inodes for a particular cylinder group can be read with
8 to 16 disk transfers.
(At most 16 disk transfers are required because a cylinder
group may have no more than 2048 inodes.)
This puts a small and constant upper bound on the number of
disk transfers required to access the inodes
for all the files in a directory.
In contrast, the old file system typically requires
one disk transfer to fetch the inode for each file in a directory.
.PP
The other major resource is data blocks.
Since data blocks for a file are typically accessed together,
the policy routines try to place all data
blocks for a file in the same cylinder group,
preferably at rotationally optimal positions in the same cylinder.
The problem with allocating all the data blocks
in the same cylinder group is that large files will
quickly use up available space in the cylinder group,
forcing a spill over to other areas.
Further, using all the space in a cylinder group
causes future allocations for any file in the cylinder group
to also spill to other areas.
Ideally none of the cylinder groups should ever become completely full.
The heuristic solution chosen is to
redirect block allocation
to a different cylinder group
when a file exceeds 48 kilobytes,
and at every megabyte thereafter.*
.FS
* The first spill over point at 48 kilobytes is the point
at which a file on a 4096 byte block file system first
requires a single indirect block.  This appears to be
a natural first point at which to redirect block allocation.
The other spillover points are chosen with the intent of
forcing block allocation to be redirected when a
file has used about 25% of the data blocks in a cylinder group.
In observing the new file system in day to day use, the heuristics appear
to work well in minimizing the number of completely filled
cylinder groups.
.FE
The newly chosen cylinder group is selected from those cylinder
groups that have a greater than average number of free blocks left.
Although big files tend to be spread out over the disk,
a megabyte of data is typically accessible before
a long seek must be performed,
and the cost of one long seek per megabyte is small.
.PP
The global policy routines call local allocation routines with
requests for specific blocks.
The local allocation routines will
always allocate the requested block
if it is free, otherwise it
allocates a free block of the requested size that is
rotationally closest to the requested block.
If the global layout policies had complete information,
they could always request unused blocks and
the allocation routines would be reduced to simple bookkeeping.
However, maintaining complete information is costly;
thus the implementation of the global layout policy
uses heuristics that employ only partial information.
.PP
If a requested block is not available, the local allocator uses
a four level allocation strategy:
.IP 1)
Use the next available block rotationally closest
to the requested block on the same cylinder.  It is assumed
here that head switching time is zero.  On disk
controllers where this is not the case, it may be possible
to incorporate the time required to switch between disk platters
when constructing the rotational layout tables.  This, however,
has not yet been tried.
.IP 2)
If there are no blocks available on the same cylinder,
use a block within the same cylinder group.
.IP 3)
If that cylinder group is entirely full,
quadratically hash the cylinder group number to choose
another cylinder group to look for a free block.
.IP 4)
Finally if the hash fails, apply an exhaustive search
to all cylinder groups.
.PP
Quadratic hash is used because of its speed in finding
unused slots in nearly full hash tables [Knuth75].
File systems that are parameterized to maintain at least
10% free space rarely use this strategy.
File systems that are run without maintaining any free
space typically have so few free blocks that almost any
allocation is random;
the most important characteristic of
the strategy used under such conditions is that the strategy be fast.
.ds RH Performance
.sp 2
.ne 1i
