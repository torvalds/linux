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
.ds RH Malloc and free
.NH
Malloc and free
.PP
The job of malloc(3) is to turn the rather simple
brk(2) facility into a service programs can
actually use without getting hurt.
.PP
The archetypical malloc(3) implementation keeps track of the memory between
the end of the bss section, as defined by the 
.B _end 
symbol, and the current brk(2) point using a linked list of chunks of memory.
Each item on the list has a status as either free or used, a pointer
to the next entry and in most cases to the previous as well, to speed
up inserts and deletes in the list.
.PP
When a malloc(3) request comes in, the list is traversed from the
front and if a free chunk big enough to hold the request is found,
it is returned, if the free chunk is bigger than the size requested,
a new free chunk is made from the excess and put back on the list.
.PP
When a chunk is 
.B free(3) 'ed,
the chunk is found in the list, its status
is changed to free and if one or both of the surrounding chunks
are free, they are collapsed to one.
.PP
A third kind of request, 
.B realloc(3) ,
will resize
a chunk, trying to avoid copying the contents if possible.
It is seldom used, and has only had a significant impact on performance
in a few special situations.
The typical pattern of use is to malloc(3) a chunk of the maximum size 
needed, read in the data and adjust the size of the chunk to match the
size of the data read using realloc(3).
.PP
For reasons of efficiency, the original implementation of malloc(3)
put the small structure used to contain the next and previous pointers
plus the state of the chunk right before the chunk itself.
.PP
As a matter of fact, the canonical malloc(3) implementation can be 
studied in the ``Old testament'', chapter 8 verse 7 [Kernighan & Ritchie]
.PP
Various optimisations can be applied to the above basic algorithm:
.IP
If in freeing a chunk, we end up with the last chunk on the list being
free, we can return that to the kernel by calling brk(2) with the first
address of that chunk and then make the previous chunk the last on the
chain by terminating its ``next'' pointer.
.IP
A best-fit algorithm can be used instead of first-fit at an expense 
of memory, because statistically fewer chances to brk(2) backwards will 
present themselves.
.IP
Splitting the list in two, one for used and one for free chunks, to
speed the searching.
.IP
Putting free chunks on one of several free lists, depending on their size,
to speed allocation.
.IP
\&...
