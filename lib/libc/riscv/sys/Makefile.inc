# $FreeBSD$

SRCS+=	trivial-vdso_tc.c

MDASM=	cerror.S \
	syscall.S \
	vfork.S

# Don't generate default code for these syscalls:
NOASM+=	sbrk.o vfork.o
