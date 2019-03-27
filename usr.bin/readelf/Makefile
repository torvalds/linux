# $FreeBSD$

ELFTCDIR=	${SRCTOP}/contrib/elftoolchain
READELFDIR=	${ELFTCDIR}/readelf

.PATH: ${READELFDIR}

PROG=	readelf
SRCS=	readelf.c

LIBADD=	dwarf elftc elf

CFLAGS+=-I${ELFTCDIR}/libelftc -I${ELFTCDIR}/common

# This same hack is in lib/libelf/Makefile and lib/libdwarf/Makefile
# We need to link against the correct version of these files. One
# solution is to include SRCTOP/sys in the include path. This causes
# problems when a header file in sys depends on a file in another
# part of the tree, e.g. a machine dependent header.
#
SRCS+=		sys/elf32.h sys/elf64.h sys/elf_common.h
CLEANDIRS=	sys
CFLAGS+=	-I.
sys/elf32.h sys/elf64.h sys/elf_common.h: ${SRCTOP}/sys/${.TARGET} .NOMETA
	mkdir -p ${.OBJDIR}/sys
	ln -sf ${.ALLSRC} ${.TARGET}

.include <bsd.prog.mk>
