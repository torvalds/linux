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
.\"	@(#)d.t	8.1 (Berkeley) 6/8/93
.\"
.\".ds RH "Data Structure Sizing Rules
.bp
.LG
.B
.ce
APPENDIX D. VAX KERNEL DATA STRUCTURE SIZING RULES
.sp
.R
.NL
.PP
Certain system data structures are sized at compile time
according to the maximum number of simultaneous users expected,
while others are calculated at boot time based on the
physical resources present, e.g. memory.  This appendix lists
both sets of rules and also includes some hints on changing
built-in limitations on certain data structures.
.SH
Compile time rules
.PP
The file \fI/sys/conf\|/param.c\fP contains the definitions of
almost all data structures sized at compile time.  This file
is copied into the directory of each configured system to allow
configuration-dependent rules and values to be maintained.
(Each copy normally depends on the copy in /sys/conf,
and global modifications cause the file to be recopied unless
the makefile is modified.)
The rules implied by its contents are summarized below (here
MAXUSERS refers to the value defined in the configuration file
in the ``maxusers'' rule).
Most limits are computed at compile time and stored in global variables
for use by other modules; they may generally be patched in the system
binary image before rebooting to test new values.
.IP \fBnproc\fP
.br
The maximum number of processes which may be running at any time.
It is referred to in other calculations as NPROC and is defined to be
.DS
20 + 8 * MAXUSERS
.DE
.IP \fBntext\fP
.br
The maximum number of active shared text segments.
The constant is intended to allow for network servers and common commands
that remain in the table.
It is defined as
.DS
36 + MAXUSERS.
.DE
.IP \fBninode\fP
.br
The maximum number of files in the file system which may be
active at any time.  This includes files in use by users, as 
well as directory files being read or written by the system
and files associated with bound sockets in the UNIX IPC domain.
It is defined as
.DS
(NPROC + 16 + MAXUSERS) + 32
.DE
.IP \fBnfile\fP
.br
The number of ``file table'' structures.  One file
table structure is used for each open, unshared, file descriptor.
Multiple file descriptors may reference a single file table
entry when they are created through a \fIdup\fP call, or as the
result of a \fIfork\fP.  This is defined to be
.DS
16 * (NPROC + 16 + MAXUSERS) / 10 + 32
.DE
.IP \fBncallout\fP
.br
The number of ``callout'' structures.  One callout
structure is used per internal system event handled with
a timeout.  Timeouts are used for terminal delays,
watchdog routines in device drivers, protocol timeout processing, etc.
This is defined as
.DS
16 + NPROC
.DE
.IP \fBnclist\fP
.br
The number of ``c-list'' structures.  C-list structures are
used in terminal I/O, and currently each holds 60 characters.
Their number is defined as
.DS
60 + 12 * MAXUSERS
.DE
.IP \fBnmbclusters\fP
.br
The maximum number of pages which may be allocated by the network.  
This is defined as 256 (a quarter megabyte of memory) in /sys/h/mbuf.h.
In practice, the network rarely uses this much memory.  It starts off
by allocating 8 kilobytes of memory, then requesting more as 
required.  This value represents an upper bound.
.IP \fBnquota\fP
.br
The number of ``quota'' structures allocated.  Quota structures
are present only when disc quotas are configured in the system.  One
quota structure is kept per user.  This is defined to be
.DS
(MAXUSERS * 9) / 7 + 3
.DE
.IP \fBndquot\fP
.br
The number of ``dquot'' structures allocated.  Dquot structures
are present only when disc quotas are configured in the system.
One dquot structure is required per user, per active file system quota.
That is, when a user manipulates a file on a file system on which
quotas are enabled, the information regarding the user's quotas on
that file system must be in-core.  This information is cached, so
that not all information must be present in-core all the time.
This is defined as
.DS
NINODE + (MAXUSERS * NMOUNT) / 4
.DE
where NMOUNT is the maximum number of mountable file systems.
.LP
In addition to the above values, the system page tables (used to
map virtual memory in the kernel's address space) are sized at
compile time by the SYSPTSIZE definition in the file /sys/vax/vmparam.h.
This is defined to be
.DS
20 + MAXUSERS
.DE
pages of page tables. 
Its definition affects
the size of many data structures allocated at boot time because
it constrains the amount of virtual memory which may be addressed
by the running system.  This is often the limiting factor
in the size of the buffer cache, in which case a message is printed
when the system configures at boot time.
.SH
Run-time calculations
.PP
The most important data structures sized at run-time are those used in
the buffer cache.  Allocation is done by allocating physical memory
(and system virtual memory) immediately after the system
has been started up; look in the file /sys/vax/machdep.c.
The amount of physical memory which may be allocated to the buffer
cache is constrained by the size of the system page tables, among
other things.  While the system may calculate
a large amount of memory to be allocated to the buffer cache,
if the system page
table is too small to map this physical
memory into the virtual address space
of the system, only as much as can be mapped will be used.
.PP
The buffer cache is comprised of a number of ``buffer headers''
and a pool of pages attached to these headers.  Buffer headers
are divided into two categories: those used for swapping and
paging, and those used for normal file I/O.  The system tries
to allocate 10% of the first two megabytes and 5% of the remaining
available physical memory for the buffer
cache (where \fIavailable\fP does not count that space occupied by
the system's text and data segments).  If this results in fewer
than 16 pages of memory allocated, then 16 pages are allocated.
This value is kept in the initialized variable \fIbufpages\fP
so that it may be patched in the binary image (to allow tuning
without recompiling the system),
or the default may be overridden with a configuration-file option.
For example, the option \fBoptions BUFPAGES="3200"\fP
causes 3200 pages (3.2M bytes) to be used by the buffer cache.
A sufficient number of file I/O buffer headers are then allocated
to allow each to hold 2 pages each.
Each buffer maps 8K bytes.
If the number of buffer pages is larger than can be mapped
by the buffer headers, the number of pages is reduced.
The number of buffer headers allocated
is stored in the global variable \fInbuf\fP,
which may be patched before the system is booted.
The system option \fBoptions NBUF="1000"\fP forces the allocation
of 1000 buffer headers.
Half as many swap I/O buffer headers as file I/O buffers
are allocated,
but no more than 256.
.SH
System size limitations
.PP
As distributed, the sum of the virtual sizes of the core-resident
processes is limited to 256M bytes.  The size of the text
segment of a single process is currently limited to 6M bytes.
It may be increased to no greater than the data segment size limit
(see below) by redefining MAXTSIZ.
This may be done with a configuration file option,
e.g. \fBoptions MAXTSIZ="(10*1024*1024)"\fP
to set the limit to 10 million bytes.
Other per-process limits discussed here may be changed with similar options
with names given in parentheses.
Soft, user-changeable limits are set to 512K bytes for stack (DFLSSIZ)
and 6M bytes for the data segment (DFLDSIZ) by default;
these may be increased up to the hard limit
with the \fIsetrlimit\fP\|(2) system call.
The data and stack segment size hard limits are set by a system configuration
option to one of 17M, 33M or 64M bytes.
One of these sizes is chosen based on the definition of MAXDSIZ;
with no option, the limit is 17M bytes; with an option
\fBoptions MAXDSIZ="(32*1024*1024)"\fP (or any value between 17M and 33M),
the limit is increased to 33M bytes, and values larger than 33M
result in a limit of 64M bytes.
You must be careful in doing this that you have adequate paging space.
As normally configured , the system has 16M or 32M bytes per paging area,
depending on disk size.
The best way to get more space is to provide multiple, thereby
interleaved, paging areas.
Increasing the virtual memory limits results in interleaving of
swap space in larger sections (from 500K bytes to 1M or 2M bytes).
.PP
By default, the virtual memory system allocates enough memory
for system page tables mapping user page tables
to allow 256 megabytes of simultaneous active virtual memory.
That is, the sum of the virtual memory sizes of all (completely- or partially-)
resident processes can not exceed this limit.
If the limit is exceeded, some process(es) must be swapped out.
To increase the amount of resident virtual space possible,
you can alter the constant USRPTSIZE (in
/sys/vax/vmparam.h).
Each page of system page tables allows 8 megabytes of user virtual memory.
.PP
Because the file system block numbers are stored in
page table \fIpg_blkno\fP
entries, the maximum size of a file system is limited to
2^24 1024 byte blocks.  Thus no file system can be larger than 8 gigabytes.
.PP
The number of mountable file systems is set at 20 by the definition
of NMOUNT in /sys/h/param.h.
This should be sufficient; if not, the value can be increased up to 255.
If you have many disks, it makes sense to make some of
them single file systems, and the paging areas don't count in this total.
.PP
The limit to the number of files that a process may have open simultaneously
is set to 64.
This limit is set by the NOFILE definition in /sys/h/param.h.
It may be increased arbitrarily, with the caveat that the user structure
expands by 5 bytes for each file, and thus UPAGES (/sys/vax/machparam.h)
must be increased accordingly.
.PP
The amount of physical memory is currently limited to 64 Mb
by the size of the index fields in the core-map (/sys/h/cmap.h).
The limit may be increased by following instructions in that file
to enlarge those fields.
