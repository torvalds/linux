#	from: Makefile.inc,v 1.1 1993/09/03 19:04:23 jtc Exp
# $FreeBSD$

.if !defined(COMPAT_32BIT)
SRCS+=	i386_clr_watch.c i386_set_watch.c i386_vm86.c
.endif
SRCS+=	i386_get_fsbase.c i386_get_gsbase.c i386_get_ioperm.c i386_get_ldt.c \
	i386_set_fsbase.c i386_set_gsbase.c i386_set_ioperm.c i386_set_ldt.c

MDASM=	Ovfork.S cerror.S getcontext.S syscall.S

NOASM+=	sbrk.o vfork.o

MAN+=	i386_get_ioperm.2 i386_get_ldt.2 i386_vm86.2
MAN+=	i386_set_watch.3

MLINKS+=i386_get_ioperm.2 i386_set_ioperm.2
MLINKS+=i386_get_ldt.2 i386_set_ldt.2
MLINKS+=i386_set_watch.3 i386_clr_watch.3
