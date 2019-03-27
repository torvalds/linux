# $FreeBSD$

SRCS+=	__vdso_gettc.c

MDASM= Ovfork.S cerror.S syscall.S

# Don't generate default code for these syscalls:
NOASM+=	sbrk.o vfork.o
