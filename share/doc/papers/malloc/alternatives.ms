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
.ds RH Alternative implementations
.NH
Alternative implementations
.PP
These problems were actually the inspiration for the first alternative
malloc implementations.
Since their main aim was debugging, they would often use techniques
like allocating a guard zone before and after the chunk,
and possibly filling these guard zones
with some pattern, so accesses outside the allocated chunk could be detected
with some decent probability.
Another widely used technique is to use tables to keep track of which
chunks are actually in which state and so on.
.PP
This class of debugging has been taken to its practical extreme by
the product "Purify" which does the entire memory-coloring exercise
and not only keeps track of what is in use and what isn't, but also
detects if the first reference is a read (which would return undefined
values) and other such violations.
.PP
Later actual complete implementations of malloc arrived, but many of
these still based their workings on the basic schema mentioned previously,
disregarding that in the meantime virtual memory and paging have
become the standard environment.
.PP
The most widely used "alternative" malloc is undoubtedly ``gnumalloc''
which has received wide acclaim and certainly runs faster than
most stock mallocs.  It does, however, tend to fare badly in
cases where paging is the norm rather than the exception.
.PP
The particular malloc that prompted this work basically didn't bother 
reusing storage until the kernel forced it to do so by refusing 
further allocations with sbrk(2).
That may make sense if you work alone on your own personal mainframe,
but as a general policy it is less than optimal.
