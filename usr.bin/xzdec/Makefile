# $FreeBSD$

PROG=	xzdec

LINKS=	${BINDIR}/xzdec ${BINDIR}/lzdec

MLINKS= xzdec.1 lzmadec.1

XZDIR=	${SRCTOP}/contrib/xz/src
LZMALIBDIR=	${SRCTOP}/lib/liblzma

.PATH: ${XZDIR}/xzdec

SRCS=	xzdec.c

.PATH: ${XZDIR}/common

SRCS+=	tuklib_progname.c \
	tuklib_exit.c

CFLAGS+=	-DHAVE_CONFIG_H \
		-I${LZMALIBDIR} \
		-I${XZDIR}/common

LIBADD=	lzma

.include <bsd.prog.mk>
