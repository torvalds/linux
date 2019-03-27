# $FreeBSD$

PROG=   crunchide
SRCS=	crunchide.c exec_elf32.c exec_elf64.c

CFLAGS+=-DNLIST_ELF32 -DNLIST_ELF64
exec_elf64.o: exec_elf32.c

.include <bsd.prog.mk>
