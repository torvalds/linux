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
.ds RH The problems
.NH
The problems
.PP
Even though malloc(3) is a lot simpler to use
than the raw brk(2)/sbrk(2) interface,
or maybe exactly because
of that,
a lot of problems arise from its use.
.IP
Writing to memory outside the allocated chunk.
The most likely result being that the data structure used to hold
the links and flags about this chunk or the next one gets thrashed.
.IP
Freeing a pointer to memory not allocated by malloc.
This is often a pointer that points to an object on the stack or in the
data-section, in newer implementations of C it may even be in the text-
section where it is likely to be readonly.
Some malloc implementations detect this, some don't.
.IP
Freeing a modified pointer.  This is a very common mistake, freeing
not the pointer malloc(3) returned, but rather some offset from it.
Some mallocs will handle this correctly if the offset is positive.
.IP
Freeing the same pointer more than once.
.IP
Accessing memory in a chunk after it has been free(3)'ed.
.PP
The handling of these problems have traditionally been weak.
A core-dump was the most common form for "handling", but in rare
cases one could experience the famous "malloc: corrupt arena." 
message before the core-dump.
Even worse though, very often the program will just continue,
possibly giving wrong results.
.PP
An entirely different form of problem is that
the memory returned by malloc(3) can contain any value.
Unfortunately most kernels, correctly, zero out the storage they 
provide with brk(2), and thus the storage malloc returns will be zeroed 
in many cases as well, so programmers are not particular apt to notice 
that their code depends on malloc'ed storage being zeroed.
.PP
With problems this big and error handling this weak, it is not
surprising that problems are hard and time consuming to find and fix.
