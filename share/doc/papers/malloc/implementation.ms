.\"
.\" ----------------------------------------------------------------------------
.\" "THE BEER-WARE LICENSE" (Revision 42):
.\" <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
.\" can do whatever you want with this stuff. If we meet some day, and you think
.\" this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
.\" ----------------------------------------------------------------------------
.\"
.\" $FreeBSD$
.\"
.ds RH Implementation
.NH
Implementation
.PP
A new malloc(3) implementation was written to meet the goals,
and to the extent possible to address the shortcomings listed previously.
.PP
The source is 1218 lines of C code, and can be found in FreeBSD 2.2
(and probably later versions as well) as src/lib/libc/stdlib/malloc.c.
.PP
The main data structure is the
.I page-directory
which contains a
.B void*
for each page we have control over.
The value can be one of:
.IP
.B MALLOC_NOT_MINE
Another part of the code may call brk(2) to get a piece of the cake.
Consequently, we cannot rely on the memory we get from the kernel
being one consecutive piece of memory, and therefore we need a way to
mark such pages as "untouchable".
.IP
.B MALLOC_FREE
This is a free page.
.IP
.B MALLOC_FIRST
This is the first page in a (multi-)page allocation.
.IP
.B MALLOC_FOLLOW
This is a subsequent page in a multi-page allocation.
.IP
.B
struct pginfo*
.R
A pointer to a structure describing a partitioned page.
.PP
In addition, there exists a linked list of small data structures that
describe the free space as runs of free pages.
.PP
Notice that these structures are not part of the free pages themselves,
but rather allocated with malloc so that the free pages themselves
are never referenced while they are free.
.PP
When a request for storage comes in, it will be treated as a ``page''
allocation if it is bigger than half a page.
The free list will be searched and the first run of free pages that
can satisfy the request is used.  The first page gets set to
.B MALLOC_FIRST
status.  If more than that one page is needed, the rest of them get
.B MALLOC_FOLLOW
status in the page-directory.
.PP
If there were no pages on the free list, brk(2) will be called, and
the pages will get added to the page-directory with status
.B MALLOC_FREE
and the search restarts.
.PP
Freeing a number of pages is done by changing their state in the 
page directory to MALLOC_FREE, and then traversing the free-pages list to
find the right place for this run of pages, possibly collapsing
with the two neighboring runs into one run and, if possible,
releasing some memory back to the kernel by calling brk(2).
.PP
If the request is less than or equal to half of a page, its size will be
rounded up to the nearest power of two before being processed
and if the request is less than some minimum size, it is rounded up to
that size.
.PP
These sub-page allocations are served from pages which are split up
into some number of equal size chunks.
For each of these pages a
.B
struct pginfo
.R
describes the size of the chunks on this page, how many there are,
how many are free and so on.
The description consist of a bitmap of used chunks, and various counters
and numbers used to keep track of the stuff in the page.
.PP
For each size of sub-page allocation, the pginfo structures for the
pages that have free chunks in them form a list.
The heads of these lists are stored in predetermined slots at
the beginning of the page directory to make access fast.
.PP
To allocate a chunk of some size, the head of the list for the
corresponding size is examined, and a free chunk found.  The number
of free chunks on that page is decreased by one and, if zero, the
pginfo structure is unlinked from the list.
.PP
To free a chunk, the page is derived from the pointer, the page table
for that page contains a pointer to the pginfo structure, where the
free bit is set for the chunk, the number of free chunks increased by
one, and if equal to one, the pginfo structure is linked into the
proper place on the list for this size of chunks.
If the count increases to match the number of chunks on the page, the
pginfo structure is unlinked from the list and free(3)'ed and the 
actual page itself is free(3)'ed too.
.PP
To be 100% correct performance-wise these lists should be ordered
according to the recent number of accesses to that page.  This 
information is not available and it would essentially mean a reordering
of the list on every memory reference to keep it up-to-date.
Instead they are ordered according to the address of the pages.
Interestingly enough, in practice this comes out to almost the same 
thing performance-wise.
.PP
It's not that surprising after all, it's the difference between
following the crowd or actively directing where it can go, in both
ways you can end up in the middle of it all.
.PP
The side effect of this compromise is that it also uses less storage,
and the list never has to be reordered, all the ordering happens when
pages are added or deleted.
.PP
It is an interesting twist to the implementation that the
.B
struct pginfo
.R
is allocated with malloc.
That is, "as with malloc" to be painfully correct.
The code knows the special case where the first (couple) of allocations on
the page is actually the pginfo structure and deals with it accordingly.
This avoids some silly "chicken and egg" issues.
.ds RH Bells and whistles.
.NH
Bells and whistles.
.PP
brk(2) is actually not a very fast system call when you ask for storage.
This is mainly because of the need by the kernel to zero the pages before
handing them over, so therefore this implementation does not release 
heap pages until there is a large chunk to release back to the kernel.
Chances are pretty good that we will need it again pretty soon anyway.
Since these pages are not accessed at all, they will soon be paged out
and don't affect anything but swap-space usage.
.PP
The page directory is actually kept in a mmap(2)'ed piece of
anonymous memory.  This avoids some rather silly cases that
would otherwise have to be handled when the page directory
has to be extended.
.PP
One particularly nice feature is that all pointers passed to free(3)
and realloc(3) can be checked conclusively for validity:
First the pointer is masked to find the page.  The page directory
is then examined, it must contain either MALLOC_FIRST, in which
case the pointer must point exactly at the page, or it can contain
a struct pginfo*, in which case the pointer must point to one of
the chunks described by that structure.
Warnings will be printed on
.B stderr
and nothing will be done with
the pointer if it is found to be invalid.
.PP
An environment variable
.B MALLOC_OPTIONS
allows the user some control over the behavior of malloc.
Some of the more interesting options are:
.IP
.B Abort
If malloc fails to allocate storage, core-dump the process with
a message rather than expect it handle this correctly.
It's amazing how few programs actually handle this condition correctly,
and consequently the havoc they can create is the more creative or
destructive.
.IP
.B Dump
Writes malloc statistics to a file called ``malloc.out'' prior
to process termination.
.IP
.B Hint
Pass a hint to the kernel about pages we no longer need through the
madvise(2) system call.  This can help performance on machines that
page heavily by eliminating unnecessary page-ins and page-outs of
unused data.
.IP
.B Realloc
Always do a free and malloc when realloc(3) is called.
For programs doing garbage collection using realloc(3), this makes the
heap collapse faster since malloc will reallocate from the 
lowest available address.
The default
is to leave things alone if the size of the allocation is still in
the same size-class.
.IP
.B Junk
will explicitly fill the allocated area with a particular value
to try to detect if programs rely on it being zero.
.IP
.B Zero
will explicitly zero out the allocated chunk of memory, while any
space after the allocation in the chunk will be filled with the
junk value to try to catch out of the chunk references.
.ds RH The road not taken.
.NH
The road not yet taken.
.PP
A couple of avenues were explored that could be interesting in some
set of circumstances.
.PP
Using mmap(2) instead of brk(2) was actually slower, since brk(2)
knows a lot of the things that mmap has to find out first.
.PP
In general there is little room for further improvement of the
time-overhead of the malloc, further improvements will have to
be in the area of improving paging behavior.
.PP
It is still under consideration to add a feature such that
if realloc is called with two zero arguments, the internal
allocations will be reallocated to perform a garbage collect.
This could be used in certain types of programs to collapse
the memory use, but so far it doesn't seem to be worth the effort.
.PP
Malloc/Free can be a significant point of contention in multi-threaded
programs.  Low-grain locking of the data-structures inside the 
implementation should be implemented to avoid excessive spin-waiting.
