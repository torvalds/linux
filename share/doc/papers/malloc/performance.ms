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
.ds RH Performance
.NH
Performance
.PP
Performance for a malloc(3) implementation comes as two variables:
.IP
A: How much time does it use for searching and manipulating data structures.
We will refer to this as ``overhead time''.
.IP
B: How well does it manage the storage.
This rather vague metric we call ``quality of allocation''.
.PP
The overhead time is easy to measure, just do a lot of malloc/free calls
of various kinds and combination, and compare the results.
.PP
The quality of allocation is not quite as simple as that.
One measure of quality is the size of the process, that should obviously
be minimized.
Another measure is the execution time of the process.
This is not an obvious indicator of quality, but people will generally
agree that it should be minimized as well, and if malloc(3) can do
anything to do so, it should.
Explanation why it is still a good metric follows:
.PP
In a traditional segment/swap kernel, the desirable behavior of a process
is to keep the brk(2) as low as possible, thus minimizing the size of the
data/bss/heap segment, which in turn translates to a smaller process and
a smaller probability of the process being swapped out, qed: faster
execution time as an average.
.PP
In a paging environment this is not a bad choice for a default, but
a couple of details needs to be looked at much more carefully.
.PP
First of all, the size of a process becomes a more vague concept since
only the pages that are actually used need to be in primary storage
for execution to progress, and they only need to be there when used.
That implies that many more processes can fit in the same amount of
primary storage, since most processes have a high degree of locality
of reference and thus only need some fraction of their pages to actually
do their job.
.PP
From this it follows that the interesting size of the process, is some
subset of the total amount of virtual memory occupied by the process.
This number isn't a constant, it varies depending on the whereabouts
of the process, and it may indeed fluctuate wildly over the lifetime
of the process.
.PP
One of the names for this vague concept is ``current working set''.
It has been defined many different ways over the years, mostly to
satisfy and support claims in marketing or benchmark contexts.
.PP
For now we can simply say that it is the number of pages the process
needs in order to run at a sufficiently low paging rate in a congested
primary storage.
(If primary storage isn't congested, this is not really important 
of course, but most systems would be better off using the pages for
disk-cache or similar functions, so from that perspective it will
always be congested.)
If the number of pages is too small, the process will wait for its
pages to be read from secondary storage much of the time, if it's too
big, the space could be used better for something else.
.PP
From the view of any single process, this number of pages is 
"all of my pages", but from the point of view of the OS it should
be tuned to maximise the total throughput of all the processes on
the machine at the time.
This is usually done using various kinds of least-recently-used 
replacement algorithms to select page candidates for replacement.
.PP
With this knowledge, can we decide what the performance goal is for
a modern malloc(3) ?
Well, it's almost as simple as it used to be:
.B
Minimize the number of pages accessed.
.R
.PP
This really is the core of it all.
If the number of accessed pages is smaller, then locality of reference is
higher, and all kinds of caches (which is essentially what the
primary storage is in a VM system) work better.
.PP
It's interesting to notice that the classical malloc fails on this one
because the information about free chunks is kept with the free
chunks themselves.  In some of the benchmarks this came out as all the
pages being paged in every time a malloc call was made, because malloc
had to traverse the free list to find a suitable chunk for the allocation.
If memory is not in use, then you shouldn't access it.
.PP
The secondary goal is more evident:
.B
Try to work in pages.
.R
.PP
That makes it easier for the kernel, and wastes less virtual memory.
Most modern implementations do this when they interact with the
kernel, but few try to avoid objects spanning pages.
.PP
If an object's size
is less than or equal to a page, there is no reason for it to span two pages.
Having objects span pages means that two pages must be
paged in, if that object is accessed.
.PP
With this analysis in the luggage, we can start coding.
