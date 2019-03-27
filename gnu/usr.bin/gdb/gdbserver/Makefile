# $FreeBSD$

# Not elf specific so don't install in /usr/libexec/elf
BINDIR=/usr/bin

GDBDIR=	${SRCTOP}/contrib/gdb
.PATH:	${GDBDIR}/gdb/signals
.PATH:	${GDBDIR}/gdb/gdbserver
.PATH:	${GDBDIR}/gdb

PROG=	gdbserver

SRCS=	inferiors.c mem-break.c regcache.c remote-utils.c	\
	server.c signals.c target.c utils.c
SRCS+=	fbsd-low.c

SRCS+=	fbsd-${MACHINE_CPUARCH}-low.c reg-${MACHINE_CPUARCH}.c
.if ${MACHINE_CPUARCH} == "i386" || ${MACHINE_CPUARCH} == "amd64"
SRCS+= i387-fp.c
.endif
#CFLAGS+=	-I${.CURDIR}/../arch/${MACHINE_ARCH}
CFLAGS+=	-I${GDBDIR}/gdb/gdbserver
CFLAGS+=	-I${GDBDIR}/gdb/regformats
CFLAGS+=	-DNO_MMALLOC -DGDBSERVER

.include <bsd.prog.mk>
