# $FreeBSD$

MIASM:=	${MIASM:Nfreebsd[467]_*}

SRCS+=	__vdso_gettc.c

MDASM=	cerror.S \
	syscall.S \
	vfork.S

# Don't generate default code for these syscalls:
NOASM+=	sbrk.o \
	vfork.o
