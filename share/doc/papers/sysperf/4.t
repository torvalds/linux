.\" Copyright (c) 1985 The Regents of the University of California.
.\" All rights reserved.
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
.\"	@(#)4.t	5.1 (Berkeley) 4/17/91
.\"
.\"	$FreeBSD$
.\"
.ds RH Performance Improvements
.NH
Performance Improvements
.PP
This section outlines the changes made to the system
since the 4.2BSD distribution.
The changes reported here were made in response
to the problems described in Section 3.
The improvements fall into two major classes;
changes to the kernel that are described in this section,
and changes to the system libraries and utilities that are
described in the following section.
.NH 2
Performance Improvements in the Kernel
.PP
Our goal has been to optimize system performance
for our general timesharing environment.
Since most sites running 4.2BSD have been forced to take
advantage of declining
memory costs rather than replace their existing machines with
ones that are more powerful, we have
chosen to optimize running time at the expense of memory.
This tradeoff may need to be reconsidered for personal workstations
that have smaller memories and higher latency disks.
Decreases in the running time of the system may be unnoticeable
because of higher paging rates incurred by a larger kernel.
Where possible, we have allowed the size of caches to be controlled
so that systems with limited memory may reduce them as appropriate.
.NH 3
Name Cacheing
.PP
Our initial profiling studies showed that more than one quarter
of the time in the system was spent in the
pathname translation routine, \fInamei\fP,
translating path names to inodes\u\s-21\s0\d\**.
.FS
\** \u\s-21\s0\d Inode is an abbreviation for ``Index node''.
Each file on the system is described by an inode;
the inode maintains access permissions, and an array of pointers to
the disk blocks that hold the data associated with the file.
.FE
An inspection of \fInamei\fP shows that
it consists of two nested loops.
The outer loop is traversed once per pathname component.
The inner loop performs a linear search through a directory looking
for a particular pathname component.
.PP
Our first idea was to reduce the number of iterations
around the inner loop of \fInamei\fP by observing that many programs
step through a directory performing an operation on each entry in turn.
To improve performance for processes doing directory scans,
the system keeps track of the directory offset of the last component of the
most recently translated path name for each process.
If the next name the process requests is in the same directory,
the search is started from the offset that the previous name was found
(instead of from the beginning of the directory).
Changing directories invalidates the cache, as
does modifying the directory.
For programs that step sequentially through a directory with
.EQ
delim $$
.EN
$N$ files, search time decreases from $O ( N sup 2 )$ to $O(N)$.
.EQ
delim off
.EN
.PP
The cost of the cache is about 20 lines of code
(about 0.2 kilobytes)
and 16 bytes per process, with the cached data
stored in a process's \fIuser\fP vector.
.PP
As a quick benchmark to verify the maximum effectiveness of the
cache we ran ``ls \-l''
on a directory containing 600 files.
Before the per-process cache this command
used 22.3 seconds of system time.
After adding the cache the program used the same amount
of user time, but the system time dropped to 3.3 seconds.
.PP
This change prompted our rerunning a profiled system
on a machine containing the new \fInamei\fP.
The results showed that the time in \fInamei\fP
dropped by only 2.6 ms/call and
still accounted for 36% of the system call time,
18% of the kernel, or about 10% of all the machine cycles.
This amounted to a drop in system time from 57% to about 55%.
The results are shown in Table 9.
.KF
.DS L
.TS
center box;
l r r.
part	time	% of kernel
_
self	11.0 ms/call	9.2%
child	10.6 ms/call	8.9%
_
total	21.6 ms/call	18.1%
.TE
.ce
Table 9. Call times for \fInamei\fP with per-process cache.
.DE
.KE
.PP
The small performance improvement
was caused by a low cache hit ratio.
Although the cache was 90% effective when hit,
it was only usable on about 25% of the names being translated.
An additional reason for the small improvement was that
although the amount of time spent in \fInamei\fP itself
decreased substantially,
more time was spent in the routines that it called
since each directory had to be accessed twice;
once to search from the middle to the end,
and once to search from the beginning to the middle.
.PP
Frequent requests for a small set of names are best handled
with a cache of recent name translations\**.
.FS
\** The cache is keyed on a name and the
inode and device number of the directory that contains it.
Associated with each entry is a pointer to the corresponding
entry in the inode table.
.FE
This has the effect of eliminating the inner loop of \fInamei\fP.
For each path name component,
\fInamei\fP first looks in its cache of recent translations
for the needed name.
If it exists, the directory search can be completely eliminated.
.PP
The system already maintained a cache of recently accessed inodes,
so the initial name cache
maintained a simple name-inode association that was used to
check each component of a path name during name translations.
We considered implementing the cache by tagging each inode
with its most recently translated name,
but eventually decided to have a separate data structure that
kept names with pointers to the inode table.
Tagging inodes has two drawbacks;
many inodes such as those associated with login ports remain in
the inode table for a long period of time, but are never looked
up by name.
Other inodes, such as those describing directories are looked up
frequently by many different names (\fIe.g.\fP ``..'').
By keeping a separate table of names, the cache can
truly reflect the most recently used names.
An added benefit is that the table can be sized independently
of the inode table, so that machines with small amounts of memory
can reduce the size of the cache (or even eliminate it)
without modifying the inode table structure.
.PP
Another issue to be considered is how the name cache should
hold references to the inode table.
Normally processes hold ``hard references'' by incrementing the
reference count in the inode they reference.
Since the system reuses only inodes with zero reference counts,
a hard reference insures that the inode pointer will remain valid.
However, if the name cache holds hard references,
it is limited to some fraction of the size of the inode table,
since some inodes must be left free for new files.
It also makes it impossible for other parts of the kernel
to verify sole use of a device or file.
These reasons made it impractical to use hard references
without affecting the behavior of the inode caching scheme.
Thus, we chose instead to keep ``soft references'' protected
by a \fIcapability\fP \- a 32-bit number
guaranteed to be unique\u\s-22\s0\d \**.
.FS
\** \u\s-22\s0\d When all the numbers have been exhausted, all outstanding
capabilities are purged and numbering starts over from scratch.
Purging is possible as all capabilities are easily found in kernel memory.
.FE
When an entry is made in the name cache,
the capability of its inode is copied to the name cache entry.
When an inode is reused it is issued a new capability.
When a name cache hit occurs,
the capability of the name cache entry is compared
with the capability of the inode that it references.
If the capabilities do not match, the name cache entry is invalid.
Since the name cache holds only soft references,
it may be sized independent of the size of the inode table.
A final benefit of using capabilities is that all
cached names for an inode may be invalidated without
searching through the entire cache;
instead all you need to do is assign a new capability to the inode.
.PP
The cost of the name cache is about 200 lines of code
(about 1.2 kilobytes)
and 48 bytes per cache entry.
Depending on the size of the system,
about 200 to 1000 entries will normally be configured,
using 10-50 kilobytes of physical memory.
The name cache is resident in memory at all times.
.PP
After adding the system wide name cache we reran ``ls \-l''
on the same directory.
The user time remained the same,
however the system time rose slightly to 3.7 seconds.
This was not surprising as \fInamei\fP
now had to maintain the cache,
but was never able to make any use of it.
.PP
Another profiled system was created and measurements
were collected over a 17 hour period.  These measurements
showed a 13 ms/call decrease in \fInamei\fP, with
\fInamei\fP accounting for only 26% of the system call time,
13% of the time in the kernel,
or about 7% of all the machine cycles.
System time dropped from 55% to about 49%.
The results are shown in Table 10.
.KF
.DS L
.TS
center box;
l r r.
part	time	% of kernel
_
self	4.2 ms/call	6.2%
child	4.4 ms/call	6.6%
_
total	8.6 ms/call	12.8%
.TE
.ce
Table 10.  Call times for \fInamei\fP with both caches.
.DE
.KE
.PP
On our general time sharing systems we find that during the twelve
hour period from 8AM to 8PM the system does 500,000 to 1,000,000
name translations.
Statistics on the performance of both caches show that
the large performance improvement is
caused by the high hit ratio.
The name cache has a hit rate of 70%-80%;
the directory offset cache gets a hit rate of 5%-15%.
The combined hit rate of the two caches almost always adds up to 85%.
With the addition of the two caches,
the percentage of system time devoted to name translation has
dropped from 25% to less than 13%.
While the system wide cache reduces both the amount of time in
the routines that \fInamei\fP calls as well as \fInamei\fP itself
(since fewer directories need to be accessed or searched),
it is interesting to note that the actual percentage of system
time spent in \fInamei\fP itself increases even though the
actual time per call decreases.
This is because less total time is being spent in the kernel,
hence a smaller absolute time becomes a larger total percentage.
.NH 3
Intelligent Auto Siloing
.PP
Most terminal input hardware can run in two modes:
it can either generate an interrupt each time a character is received,
or collect characters in a silo that the system then periodically drains.
To provide quick response for interactive input and flow control,
a silo must be checked 30 to 50 times per second.
Ascii terminals normally exhibit
an input rate of less than 30 characters per second.
At this input rate
they are most efficiently handled with interrupt per character mode,
since this generates fewer interrupts than draining the input silos
of the terminal multiplexors at each clock interrupt.
When input is being generated by another machine
or a malfunctioning terminal connection, however,
the input rate is usually more than 50 characters per second.
It is more efficient to use a device's silo input mode,
since this generates fewer interrupts than handling each character
as a separate interrupt.
Since a given dialup port may switch between uucp logins and user logins,
it is impossible to statically select the most efficient input mode to use.
.PP
We therefore changed the terminal multiplexor handlers
to dynamically choose between the use of the silo and the use of
per-character interrupts.
At low input rates the handler processes characters on an
interrupt basis, avoiding the overhead
of checking each interface on each clock interrupt.
During periods of sustained input, the handler enables the silo
and starts a timer to drain input.
This timer runs less frequently than the clock interrupts,
and is used only when there is a substantial amount of input.
The transition from using silos to an interrupt per character is
damped to minimize the number of transitions with bursty traffic
(such as in network communication).
Input characters serve to flush the silo, preventing long latency.
By switching between these two modes of operation dynamically,
the overhead of checking the silos is incurred only
when necessary.
.PP
In addition to the savings in the terminal handlers,
the clock interrupt routine is no longer required to schedule
a software interrupt after each hardware interrupt to drain the silos.
The software-interrupt level portion of the clock routine is only
needed when timers expire or the current user process is collecting
an execution profile.
Thus, the number of interrupts attributable to clock processing
is substantially reduced.
.NH 3
Process Table Management
.PP
As systems have grown larger, the size of the process table
has grown far past 200 entries.
With large tables, linear searches must be eliminated
from any frequently used facility.
The kernel process table is now multi-threaded to allow selective searching
of active and zombie processes.
A third list threads unused process table slots.
Free slots can be obtained in constant time by taking one
from the front of the free list.
The number of processes used by a given user may be computed by scanning
only the active list.
Since the 4.2BSD release,
the kernel maintained linked lists of the descendents of each process.
This linkage is now exploited when dealing with process exit;
parents seeking the exit status of children now avoid linear search
of the process table, but examine only their direct descendents.
In addition, the previous algorithm for finding all descendents of an exiting
process used multiple linear scans of the process table.
This has been changed to follow the links between child process and siblings.
.PP
When forking a new process,
the system must assign it a unique process identifier.
The system previously scanned the entire process table each time it created
a new process to locate an identifier that was not already in use.
Now, to avoid scanning the process table for each new process,
the system computes a range of unused identifiers
that can be directly assigned.
Only when the set of identifiers is exhausted is another process table
scan required.
.NH 3
Scheduling
.PP
Previously the scheduler scanned the entire process table
once per second to recompute process priorities.
Processes that had run for their entire time slice had their
priority lowered.
Processes that had not used their time slice, or that had
been sleeping for the past second had their priority raised.
On systems running many processes,
the scheduler represented nearly 20% of the system time.
To reduce this overhead,
the scheduler has been changed to consider only
runnable processes when recomputing priorities.
To insure that processes sleeping for more than a second
still get their appropriate priority boost,
their priority is recomputed when they are placed back on the run queue.
Since the set of runnable process is typically only a small fraction
of the total number of processes on the system,
the cost of invoking the scheduler drops proportionally.
.NH 3
Clock Handling
.PP
The hardware clock interrupts the processor 100 times per second
at high priority.
As most of the clock-based events need not be done at high priority,
the system schedules a lower priority software interrupt to do the less
time-critical events such as cpu scheduling and timeout processing.
Often there are no such events, and the software interrupt handler
finds nothing to do and returns.
The high priority event now checks to see if there are low priority
events to process;
if there is nothing to do, the software interrupt is not requested.
Often, the high priority interrupt occurs during a period when the
machine had been running at low priority.
Rather than posting a software interrupt that would occur as
soon as it returns,
the hardware clock interrupt handler simply lowers the processor priority
and calls the software clock routines directly.
Between these two optimizations, nearly 80 of the 100 software
interrupts per second can be eliminated.
.NH 3
File System
.PP
The file system uses a large block size, typically 4096 or 8192 bytes.
To allow small files to be stored efficiently, the large blocks can
be broken into smaller fragments, typically multiples of 1024 bytes.
To minimize the number of full-sized blocks that must be broken
into fragments, the file system uses a best fit strategy.
Programs that slowly grow files using write of 1024 bytes or less
can force the file system to copy the data to
successively larger and larger fragments until it finally
grows to a full sized block.
The file system still uses a best fit strategy the first time
a fragment is written.
However, the first time that the file system is forced to copy a growing
fragment it places it at the beginning of a full sized block.
Continued growth can be accommodated without further copying
by using up the rest of the block.
If the file ceases to grow, the rest of the block is still
available for holding other fragments.
.PP
When creating a new file name,
the entire directory in which it will reside must be scanned
to insure that the name does not already exist.
For large directories, this scan is time consuming.
Because there was no provision for shortening directories,
a directory that is once over-filled will increase the cost
of file creation even after the over-filling is corrected.
Thus, for example, a congested uucp connection can leave a legacy long
after it is cleared up.
To alleviate the problem, the system now deletes empty blocks
that it finds at the end of a directory while doing a complete
scan to create a new name.
.NH 3
Network
.PP
The default amount of buffer space allocated for stream sockets (including
pipes) has been increased to 4096 bytes.
Stream sockets and pipes now return their buffer sizes in the block size field
of the stat structure.
This information allows the standard I/O library to use more optimal buffering.
Unix domain stream sockets also return a dummy device and inode number
in the stat structure to increase compatibility
with other pipe implementations.
The TCP maximum segment size is calculated according to the destination
and interface in use; non-local connections use a more conservative size
for long-haul networks.
.PP
On multiply-homed hosts, the local address bound by TCP now always corresponds
to the interface that will be used in transmitting data packets for the
connection.
Several bugs in the calculation of round trip timing have been corrected.
TCP now switches to an alternate gateway when an existing route fails,
or when an ICMP redirect message is received.
ICMP source quench messages are used to throttle the transmission
rate of TCP streams by temporarily creating an artificially small
send window, and retransmissions send only a single packet
rather than resending all queued data.
A send policy has been implemented
that decreases the number of small packets outstanding
for network terminal traffic [Nagle84],
providing additional reduction of network congestion.
The overhead of packet routing has been decreased by changes in the routing
code and by caching the most recently used route for each datagram socket.
.PP
The buffer management strategy implemented by \fIsosend\fP has been
changed to make better use of the increased size of the socket buffers
and a better tuned delayed acknowledgement algorithm.
Routing has been modified to include a one element cache of the last
route computed.
Multiple messages send with the same destination now require less processing.
Performance deteriorates because of load in
either the sender host, receiver host, or ether.
Also, any CPU contention degrades substantially
the throughput achievable by user processes [Cabrera85].
We have observed empty VAX 11/750s using up to 90% of their cycles
transmitting network messages.
.NH 3
Exec
.PP
When \fIexec\fP-ing a new process, the kernel creates the new
program's argument list by copying the arguments and environment
from the parent process's address space into the system, then back out
again onto the stack of the newly created process.
These two copy operations were done one byte at a time, but
are now done a string at a time.
This optimization reduced the time to process
an argument list by a factor of ten;
the average time to do an \fIexec\fP call decreased by 25%.
.NH 3
Context Switching
.PP
The kernel used to post a software event when it wanted to force
a process to be rescheduled.
Often the process would be rescheduled for other reasons before
exiting the kernel, delaying the event trap.
At some later time the process would again
be selected to run and would complete its pending system call,
finally causing the event to take place.
The event would cause the scheduler to be invoked a second time
selecting the same process to run.
The fix to this problem is to cancel any software reschedule
events when saving a process context.
This change doubles the speed with which processes
can synchronize using pipes or signals.
.NH 3
Setjmp/Longjmp
.PP
The kernel routine \fIsetjmp\fP, that saves the current system
context in preparation for a non-local goto used to save many more
registers than necessary under most circumstances.
By trimming its operation to save only the minimum state required,
the overhead for system calls decreased by an average of 13%.
.NH 3
Compensating for Lack of Compiler Technology
.PP
The current compilers available for C do not
do any significant optimization.
Good optimizing compilers are unlikely to be built;
the C language is not well suited to optimization
because of its rampant use of unbound pointers.
Thus, many classical optimizations such as common subexpression
analysis and selection of register variables must be done
by hand using ``exterior'' knowledge of when such optimizations are safe.
.PP
Another optimization usually done by optimizing compilers
is inline expansion of small or frequently used routines.
In past Berkeley systems this has been done by using \fIsed\fP to
run over the assembly language and replace calls to small
routines with the code for the body of the routine, often
a single VAX instruction.
While this optimization eliminated the cost of the subroutine
call and return,
it did not eliminate the pushing and popping of several arguments
to the routine.
The \fIsed\fP script has been replaced by a more intelligent expander,
\fIinline\fP, that merges the pushes and pops into moves to registers.
For example, if the C code
.DS
if (scanc(map[i], 1, 47, i - 63))
.DE
is compiled into assembly language it generates the code shown
in the left hand column of Table 11.
The \fIsed\fP inline expander changes this code to that
shown in the middle column.
The newer optimizer eliminates most of the stack
operations to generate the code shown in the right hand column.
.KF
.TS
center, box;
c s s s s s
c s | c s | c s
l l | l l | l l.
Alternative C Language Code Optimizations
_
cc	sed	inline
_
subl3	$64,_i,\-(sp)	subl3	$64,_i,\-(sp)	subl3	$64,_i,r5
pushl	$47	pushl	$47	movl	$47,r4
pushl	$1	pushl	$1	pushl	$1
mull2	$16,_i,r3	mull2	$16,_i,r3	mull2	$16,_i,r3
pushl	\-56(fp)[r3]	pushl	\-56(fp)[r3]	movl	\-56(fp)[r3],r2
calls	$4,_scanc	movl	(sp)+,r5	movl	(sp)+,r3
tstl	r0	movl	(sp)+,r4	scanc	r2,(r3),(r4),r5
jeql	L7	movl	(sp)+,r3	tstl	r0
		movl	(sp)+,r2	jeql	L7
		scanc	r2,(r3),(r4),r5
		tstl	r0
		jeql	L7
.TE
.ce
Table 11. Alternative inline code expansions.
.KE
.PP
Another optimization involved reevaluating
existing data structures in the context of the current system.
For example, disk buffer hashing was implemented when the system
typically had thirty to fifty buffers.
Most systems today have 200 to 1000 buffers.
Consequently, most of the hash chains contained
ten to a hundred buffers each!
The running time of the low level buffer management primitives was
dramatically improved simply by enlarging the size of the hash table.
.NH 2
Improvements to Libraries and Utilities
.PP
Intuitively, changes to the kernel would seem to have the greatest
payoff since they affect all programs that run on the system.
However, the kernel has been tuned many times before, so the
opportunity for significant improvement was small.
By contrast, many of the libraries and utilities had never been tuned.
For example, we found utilities that spent 90% of their
running time doing single character read system calls.
Changing the utility to use the standard I/O library cut the
running time by a factor of five!
Thus, while most of our time has been spent tuning the kernel,
more than half of the speedups are because of improvements in
other parts of the system.
Some of the more dramatic changes are described in the following
subsections.
.NH 3
Hashed Databases
.PP
UNIX provides a set of database management routines, \fIdbm\fP,
that can be used to speed lookups in large data files
with an external hashed index file.
The original version of dbm was designed to work with only one
database at a time.  These routines were generalized to handle
multiple database files, enabling them to be used in rewrites
of the password and host file lookup routines.  The new routines
used to access the password file significantly improve the running
time of many important programs such as the mail subsystem,
the C-shell (in doing tilde expansion), \fIls \-l\fP, etc.
.NH 3
Buffered I/O
.PP
The new filesystem with its larger block sizes allows better
performance, but it is possible to degrade system performance
by performing numerous small transfers rather than using
appropriately-sized buffers.
The standard I/O library
automatically determines the optimal buffer size for each file.
Some C library routines and commonly-used programs use low-level
I/O or their own buffering, however.
Several important utilities that did not use the standard I/O library
and were buffering I/O using the old optimal buffer size,
1Kbytes; the programs were changed to buffer I/O according to the
optimal file system blocksize.
These include the editor, the assembler, loader, remote file copy,
the text formatting programs, and the C compiler.
.PP
The standard error output has traditionally been unbuffered
to prevent delay in presenting the output to the user,
and to prevent it from being lost if buffers are not flushed.
The inordinate expense of sending single-byte packets through
the network led us to impose a buffering scheme on the standard
error stream.
Within a single call to \fIfprintf\fP, all output is buffered temporarily.
Before the call returns, all output is flushed and the stream is again
marked unbuffered.
As before, the normal block or line buffering mechanisms can be used
instead of the default behavior.
.PP
It is possible for programs with good intentions to unintentionally
defeat the standard I/O library's choice of I/O buffer size by using
the \fIsetbuf\fP call to assign an output buffer.
Because of portability requirements, the default buffer size provided
by \fIsetbuf\fP is 1024 bytes; this can lead, once again, to added
overhead.
One such program with this problem was \fIcat\fP;
there are undoubtedly other standard system utilities with similar problems
as the system has changed much since they were originally written.
.NH 3
Mail System
.PP
The problems discussed in section 3.1.1 prompted significant work
on the entire mail system.  The first problem identified was a bug
in the \fIsyslog\fP program.  The mail delivery program, \fIsendmail\fP
logs all mail transactions through this process with the 4.2BSD interprocess
communication facilities.  \fISyslog\fP then records the information in
a log file.  Unfortunately, \fIsyslog\fP was performing a \fIsync\fP
operation after each message it received, whether it was logged to a file
or not.  This wreaked havoc on the effectiveness of the
buffer cache and explained, to a large
extent, why sending mail to large distribution lists generated such a
heavy load on the system (one syslog message was generated for each
message recipient causing almost a continuous sequence of sync operations).
.PP
The hashed data base files were
installed in all mail programs, resulting in an order of magnitude
speedup on large distribution lists.  The code in \fI/bin/mail\fP
that notifies the \fIcomsat\fP program when mail has been delivered to
a user was changed to cache host table lookups, resulting in a similar
speedup on large distribution lists.
.PP
Next, the file locking facilities
provided in 4.2BSD, \fIflock\fP\|(2), were used in place of the old
locking mechanism.
The mail system previously used \fIlink\fP and \fIunlink\fP in
implementing file locking primitives.
Because these operations usually modify the contents of directories
they require synchronous disk operations and cannot take
advantage of the name cache maintained by the system.
Unlink requires that the entry be found in the directory so that
it can be removed;
link requires that the directory be scanned to insure that the name
does not already exist.
By contrast the advisory locking facility in 4.2BSD is
efficient because it is all done with in-memory tables.
Thus, the mail system was modified to use the file locking primitives.
This yielded another 10% cut in the basic overhead of delivering mail.
Extensive profiling and tuning of \fIsendmail\fP and
compiling it without debugging code reduced the overhead by another 20%.
.NH 3
Network Servers
.PP
With the introduction of the network facilities in 4.2BSD,
a myriad of services became available, each of which
required its own daemon process.
Many of these daemons were rarely if ever used,
yet they lay asleep in the process table consuming
system resources and generally slowing down response.
Rather than having many servers started at boot time, a single server,
\fIinetd\fP was substituted.
This process reads a simple configuration file
that specifies the services the system is willing to support
and listens for service requests on each service's Internet port.
When a client requests service the appropriate server is created
and passed a service connection as its standard input.  Servers
that require the identity of their client may use the \fIgetpeername\fP
system call; likewise \fIgetsockname\fP may be used to find out
a server's local address without consulting data base files.
This scheme is attractive for several reasons:
.IP \(bu 3
it eliminates
as many as a dozen processes, easing system overhead and
allowing the file and text tables to be made smaller,
.IP \(bu 3
servers need not contain the code required to handle connection
queueing, simplifying the programs, and
.IP \(bu 3
installing and replacing servers becomes simpler.
.PP
With an increased numbers of networks, both local and external to Berkeley,
we found that the overhead of the routing process was becoming
inordinately high.
Several changes were made in the routing daemon to reduce this load.
Routes to external networks are no longer exchanged by routers
on the internal machines, only a route to a default gateway.
This reduces the amount of network traffic and the time required
to process routing messages.
In addition, the routing daemon was profiled
and functions responsible for large amounts
of time were optimized.
The major changes were a faster hashing scheme,
and inline expansions of the ubiquitous byte-swapping functions.
.PP
Under certain circumstances, when output was blocked,
attempts by the remote login process
to send output to the user were rejected by the system,
although a prior \fIselect\fP call had indicated that data could be sent.
This resulted in continuous attempts to write the data until the remote
user restarted output.
This problem was initially avoided in the remote login handler,
and the original problem in the kernel has since been corrected.
.NH 3
The C Run-time Library
.PP
Several people have found poorly tuned code
in frequently used routines in the C library [Lankford84].
In particular the running time of the string routines can be
cut in half by rewriting them using the VAX string instructions.
The memory allocation routines have been tuned to waste less
memory for memory allocations with sizes that are a power of two.
Certain library routines that did file input in one-character reads
have been corrected.
Other library routines including \fIfread\fP and \fIfwrite\fP
have been rewritten for efficiency.
.NH 3
Csh
.PP
The C-shell was converted to run on 4.2BSD by
writing a set of routines to simulate the old jobs library.
While this provided a functioning C-shell,
it was grossly inefficient, generating up
to twenty system calls per prompt.
The C-shell has been modified to use the new signal
facilities directly,
cutting the number of system calls per prompt in half.
Additional tuning was done with the help of profiling
to cut the cost of frequently used facilities.
