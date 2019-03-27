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
.ds RH Introduction
.NH
Introduction
.PP
Most programs need to allocate storage dynamically in addition
to whatever static storage the compiler reserved at compile-time.
To C programmers this fact is rather obvious, but for many years
this was not an accepted and recognized fact, and many languages 
still used today don't support this notion adequately.
.PP
The classic UNIX kernel provides two very simple and powerful
mechanisms for obtaining dynamic storage, the execution stack 
and the heap.
The stack is usually put at the far upper end of the address-space,
from where it grows down as far as needed, though this may depend on
the CPU design.
The heap starts at the end of the
.B bss
segment and grows upwards as needed.
.PP
There isn't really a kernel-interface to the stack as such.
The kernel will allocate some amount of memory for it,
not even telling the process the exact size.
If the process needs more space than that, it will simply try to access
it, hoping that the kernel will detect that an access has been 
attempted outside the allocated memory, and try to extend it.
If the kernel fails to extend the stack, this could be because of lack
of resources or permissions or because it may just be impossible
to do in the first place, the process will usually be shot down by the 
kernel.
.PP
In the C language, there exists a little used interface to the stack,
.B alloca(3) ,
which will explicitly allocate space on the stack.
This is not an interface to the kernel, but merely an adjustment
done to the stack-pointer such that space will be available and
unharmed by any subroutine calls yet to be made while the context
of the current subroutine is intact.
.PP
Due to the nature of normal use of the stack, there is no corresponding
"free" operator, but instead the space is returned when the current
function returns to its caller and the stack frame is dismantled.
This is the cause of much grief, and probably the single most important
reason that alloca(3) is not, and should not be, used widely.
.PP
The heap on the other hand has an explicit kernel-interface in the 
system call
.B brk(2) .
The argument to brk(2) is a pointer to where the process wants the
heap to end.
There is also an interface called
.B sbrk(2)
taking an increment to the current end of the heap, but this is merely a
.B libc
front for brk(2).
.PP
In addition to these two memory resources, modern virtual memory kernels
provide the mmap(2)/munmap(2) interface which allows almost complete
control over any bit of virtual memory in the process address space.
.PP
Because of the generality of the mmap(2) interface and the way the 
data structures representing the regions are laid out, sbrk(2) is actually
faster in use than the equivalent mmap(2) call, simply because
mmap(2) has to search for information that is implicit in the sbrk(2) call.
