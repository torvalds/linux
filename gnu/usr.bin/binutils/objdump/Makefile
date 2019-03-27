# $FreeBSD$

.include "../Makefile.inc0"

.PATH: ${SRCDIR}/binutils ${SRCDIR}/binutils/doc

PROG=	objdump
SRCS=	objdump.c prdbg.c
CFLAGS+= -D_GNU_SOURCE
CFLAGS+= -I${.CURDIR}/${GNURELTOP}/libbinutils
CFLAGS+= -I${SRCDIR}/binutils
CFLAGS+= -DBFD_VERSION_STRING=\"${VERSION}\"
DPADD=	${GNURELTOP}/libbinutils/libbinutils${PIE_SUFFIX}.a
DPADD+=	${GNURELTOP}/libopcodes/libopcodes${PIE_SUFFIX}.a
DPADD+=	${GNURELTOP}/libbfd/libbfd${PIE_SUFFIX}.a
DPADD+=	${GNURELTOP}/libiberty/libiberty${PIE_SUFFIX}.a
LDADD=	${DPADD}

.include <bsd.prog.mk>
