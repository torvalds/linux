#	from @(#)Makefile.inc	8.1 (Berkeley) 6/4/93
# $FreeBSD$

# gmon sources
.PATH: ${LIBC_SRCTOP}/gmon

SRCS+=	gmon.c mcount.c

SYM_MAPS+=${LIBC_SRCTOP}/gmon/Symbol.map

MAN+=	moncontrol.3

MLINKS+=moncontrol.3 monstartup.3

# mcount cannot be compiled with profiling
mcount.po: mcount.o
	cp mcount.o mcount.po
