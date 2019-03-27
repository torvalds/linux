# $FreeBSD$

.include "../Makefile.inc0"

.PATH: ${SRCDIR}/binutils

LIB=	binutils
SRCS+=	arlex.l \
	arparse.y \
	arsup.c \
	binemul.c \
	bucomm.c \
	debug.c \
	dwarf.c \
	emul_vanilla.c \
	filemode.c \
	ieee.c \
	rdcoff.c \
	rddbg.c \
	rename.c \
	stabs.c \
	unwind-ia64.c \
	version.c \
	wrstabs.c
CFLAGS+= -DTARGET=\"${TARGET_TUPLE}\"
CFLAGS+= -DBFD_VERSION_STRING=\"${VERSION}\"
CFLAGS+= -D_GNU_SOURCE
CFLAGS+= -DYY_NO_INPUT
CFLAGS+= -I${SRCDIR}/binutils
CFLAGS+= -I${SRCDIR}/bfd
INTERNALLIB=

.include <bsd.lib.mk>
