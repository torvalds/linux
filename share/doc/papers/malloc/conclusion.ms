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
.ds RH Conclusion and experience.
.NH
Conclusion and experience.
.PP
In general the performance differences between gnumalloc and this
malloc are not that big.
The major difference comes when primary storage is seriously 
over-committed, in which case gnumalloc
wastes time paging in pages it's not going to use.
In such cases as much as a factor of five in wall-clock time has 
been seen in difference.
Apart from that gnumalloc and this implementation are pretty
much head-on performance-wise.
.PP
Several legacy programs in the BSD 4.4 Lite distribution had
code that depended on the memory returned from malloc
being zeroed.  In a couple of cases, free(3) was called more than
once for the same allocation, and a few cases even called free(3)
with pointers to objects in the data section or on the stack.
.PP
A couple of users have reported that using this malloc on other
platforms yielded "pretty impressive results", but no hard benchmarks
have been made.
.ds RH Acknowledgements & references.
.NH
Acknowledgements & references.
.PP
The first implementation of this algorithm was actually a file system,
done in assembler using 5-hole ``Baudot'' paper tape for a drum storage
device attached to a 20 bit germanium transistor computer with 2000 words
of memory, but that was many years ago.
.PP
Peter Wemm <peter@FreeBSD.org> came up with the idea to store the
page-directory in mmap(2)'ed memory instead of in the heap.
This has proven to be a good move.
.PP
Lars Fredriksen <fredriks@mcs.com> found and identified a
fence-post bug in the code.
