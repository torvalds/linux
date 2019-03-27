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
.ds RH The kernel and memory
.NH
The kernel and memory
.PP
Brk(2) isn't a particularly convenient interface,
it was probably made more to fit the memory model of the 
hardware being used, than to fill the needs of the programmers.
.PP
Before paged and/or virtual memory systems became
common, the most popular memory management facility used for
UNIX was segments.
This was also very often the only vehicle for imposing protection on 
various parts of memory.
Depending on the hardware, segments can be anything, and consequently 
how the kernels exploited them varied a lot from UNIX to UNIX and from
machine to machine.
.PP
Typically a process would have one segment for the text section, one
for the data and bss section combined and one for the stack.
On some systems the text shared a segment with the data and bss, and was
consequently just as writable as them.
.PP
In this setup all the brk(2) system call has to do is to find the
right amount of free storage, possibly moving things around in physical
memory, maybe even swapping out a segment or two to make space,
and change the upper limit on the data segment according to the address given.
.PP
In a more modern page based virtual memory implementation this is still
pretty much the situation, except that the granularity is now pages:
The kernel finds the right number of free pages, possibly paging some
pages out to free them up, and then plugs them into the page-table of 
the process.
.PP
As such the difference is very small, the real difference is that in
the old world of swapping, either the entire process was in primary
storage or it wouldn't be selected to be run.  In a modern VM kernel,
a process might only have a subset of its pages in primary memory,
the rest will be paged in, if and when the process tries to access them.
.PP
Only very few programs deal with the brk(2) interface directly.
The few that do usually have their own memory management facilities.
LISP or FORTH interpreters are good examples.
Most other programs use the
.B malloc(3) 
interface instead, and leave it to the malloc implementation to 
use brk(2) to get storage allocated from the kernel.
