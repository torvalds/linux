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
.\"	@(#)4.t	8.1 (Berkeley) 6/8/93
.\"
.ds RH Performance
.NH 
Performance
.PP
Ultimately, the proof of the effectiveness of the
algorithms described in the previous section
is the long term performance of the new file system.
.PP
Our empirical studies have shown that the inode layout policy has
been effective.
When running the ``list directory'' command on a large directory
that itself contains many directories (to force the system
to access inodes in multiple cylinder groups),
the number of disk accesses for inodes is cut by a factor of two.
The improvements are even more dramatic for large directories
containing only files,
disk accesses for inodes being cut by a factor of eight.
This is most encouraging for programs such as spooling daemons that
access many small files,
since these programs tend to flood the
disk request queue on the old file system.
.PP
Table 2 summarizes the measured throughput of the new file system.
Several comments need to be made about the conditions under which these
tests were run.
The test programs measure the rate at which user programs can transfer
data to or from a file without performing any processing on it.
These programs must read and write enough data to
insure that buffering in the
operating system does not affect the results.
They are also run at least three times in succession;
the first to get the system into a known state
and the second two to insure that the 
experiment has stabilized and is repeatable.
The tests used and their results are
discussed in detail in [Kridle83]\(dg.
.FS
\(dg A UNIX command that is similar to the reading test that we used is
``cp file /dev/null'', where ``file'' is eight megabytes long.
.FE
The systems were running multi-user but were otherwise quiescent.
There was no contention for either the CPU or the disk arm.
The only difference between the UNIBUS and MASSBUS tests
was the controller.
All tests used an AMPEX Capricorn 330 megabyte Winchester disk.
As Table 2 shows, all file system test runs were on a VAX 11/750.
All file systems had been in production use for at least
a month before being measured.
The same number of system calls were performed in all tests;
the basic system call overhead was a negligible portion of
the total running time of the tests.
.KF
.DS B
.TS
box;
c c|c s s
c c|c c c.
Type of	Processor and	Read
File System	Bus Measured	Speed	Bandwidth	% CPU
_
old 1024	750/UNIBUS	29 Kbytes/sec	29/983 3%	11%
new 4096/1024	750/UNIBUS	221 Kbytes/sec	221/983 22%	43%
new 8192/1024	750/UNIBUS	233 Kbytes/sec	233/983 24%	29%
new 4096/1024	750/MASSBUS	466 Kbytes/sec	466/983 47%	73%
new 8192/1024	750/MASSBUS	466 Kbytes/sec	466/983 47%	54%
.TE
.ce 1
Table 2a \- Reading rates of the old and new UNIX file systems.
.TS
box;
c c|c s s
c c|c c c.
Type of	Processor and	Write
File System	Bus Measured	Speed	Bandwidth	% CPU
_
old 1024	750/UNIBUS	48 Kbytes/sec	48/983 5%	29%
new 4096/1024	750/UNIBUS	142 Kbytes/sec	142/983 14%	43%
new 8192/1024	750/UNIBUS	215 Kbytes/sec	215/983 22%	46%
new 4096/1024	750/MASSBUS	323 Kbytes/sec	323/983 33%	94%
new 8192/1024	750/MASSBUS	466 Kbytes/sec	466/983 47%	95%
.TE
.ce 1
Table 2b \- Writing rates of the old and new UNIX file systems.
.DE
.KE
.PP
Unlike the old file system,
the transfer rates for the new file system do not
appear to change over time.
The throughput rate is tied much more strongly to the
amount of free space that is maintained.
The measurements in Table 2 were based on a file system
with a 10% free space reserve.
Synthetic work loads suggest that throughput deteriorates
to about half the rates given in Table 2 when the file
systems are full.
.PP
The percentage of bandwidth given in Table 2 is a measure
of the effective utilization of the disk by the file system.
An upper bound on the transfer rate from the disk is calculated 
by multiplying the number of bytes on a track by the number
of revolutions of the disk per second.
The bandwidth is calculated by comparing the data rates
the file system is able to achieve as a percentage of this rate.
Using this metric, the old file system is only
able to use about 3\-5% of the disk bandwidth,
while the new file system uses up to 47%
of the bandwidth.
.PP
Both reads and writes are faster in the new system than in the old system.
The biggest factor in this speedup is because of the larger
block size used by the new file system.
The overhead of allocating blocks in the new system is greater
than the overhead of allocating blocks in the old system,
however fewer blocks need to be allocated in the new system
because they are bigger.
The net effect is that the cost per byte allocated is about
the same for both systems.
.PP
In the new file system, the reading rate is always at least
as fast as the writing rate.
This is to be expected since the kernel must do more work when
allocating blocks than when simply reading them.
Note that the write rates are about the same 
as the read rates in the 8192 byte block file system;
the write rates are slower than the read rates in the 4096 byte block
file system.
The slower write rates occur because
the kernel has to do twice as many disk allocations per second,
making the processor unable to keep up with the disk transfer rate.
.PP
In contrast the old file system is about 50%
faster at writing files than reading them.
This is because the write system call is asynchronous and
the kernel can generate disk transfer
requests much faster than they can be serviced,
hence disk transfers queue up in the disk buffer cache.
Because the disk buffer cache is sorted by minimum seek distance,
the average seek between the scheduled disk writes is much
less than it would be if the data blocks were written out
in the random disk order in which they are generated.
However when the file is read,
the read system call is processed synchronously so
the disk blocks must be retrieved from the disk in the
non-optimal seek order in which they are requested.
This forces the disk scheduler to do long
seeks resulting in a lower throughput rate.
.PP
In the new system the blocks of a file are more optimally
ordered on the disk.
Even though reads are still synchronous, 
the requests are presented to the disk in a much better order.
Even though the writes are still asynchronous,
they are already presented to the disk in minimum seek
order so there is no gain to be had by reordering them.
Hence the disk seek latencies that limited the old file system
have little effect in the new file system.
The cost of allocation is the factor in the new system that 
causes writes to be slower than reads.
.PP
The performance of the new file system is currently
limited by memory to memory copy operations
required to move data from disk buffers in the
system's address space to data buffers in the user's
address space.  These copy operations account for
about 40% of the time spent performing an input/output operation.
If the buffers in both address spaces were properly aligned, 
this transfer could be performed without copying by
using the VAX virtual memory management hardware.
This would be especially desirable when transferring
large amounts of data.
We did not implement this because it would change the
user interface to the file system in two major ways:
user programs would be required to allocate buffers on page boundaries, 
and data would disappear from buffers after being written.
.PP
Greater disk throughput could be achieved by rewriting the disk drivers
to chain together kernel buffers.
This would allow contiguous disk blocks to be read
in a single disk transaction.
Many disks used with UNIX systems contain either
32 or 48 512 byte sectors per track.
Each track holds exactly two or three 8192 byte file system blocks,
or four or six 4096 byte file system blocks.
The inability to use contiguous disk blocks
effectively limits the performance
on these disks to less than 50% of the available bandwidth.
If the next block for a file cannot be laid out contiguously,
then the minimum spacing to the next allocatable
block on any platter is between a sixth and a half a revolution.
The implication of this is that the best possible layout without
contiguous blocks uses only half of the bandwidth of any given track.
If each track contains an odd number of sectors, 
then it is possible to resolve the rotational delay to any number of sectors
by finding a block that begins at the desired 
rotational position on another track.
The reason that block chaining has not been implemented is because it
would require rewriting all the disk drivers in the system,
and the current throughput rates are already limited by the
speed of the available processors.
.PP
Currently only one block is allocated to a file at a time.
A technique used by the DEMOS file system
when it finds that a file is growing rapidly,
is to preallocate several blocks at once,
releasing them when the file is closed if they remain unused.
By batching up allocations, the system can reduce the
overhead of allocating at each write,
and it can cut down on the number of disk writes needed to
keep the block pointers on the disk
synchronized with the block allocation [Powell79].
This technique was not included because block allocation 
currently accounts for less than 10% of the time spent in
a write system call and, once again, the
current throughput rates are already limited by the speed
of the available processors.
.ds RH Functional enhancements
.sp 2
.ne 1i
