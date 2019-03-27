# $FreeBSD$

PROG=	xz

LINKS=	${BINDIR}/xz ${BINDIR}/unxz
LINKS+=	${BINDIR}/xz ${BINDIR}/lzma
LINKS+=	${BINDIR}/xz ${BINDIR}/unlzma
LINKS+=	${BINDIR}/xz ${BINDIR}/xzcat
LINKS+=	${BINDIR}/xz ${BINDIR}/lzcat

MLINKS= xz.1 unxz.1 xz.1 lzma.1 xz.1 unlzma.1 xz.1 xzcat.1 xz.1 lzcat.1

XZDIR=	${SRCTOP}/contrib/xz/src
LZMALIBDIR=	${SRCTOP}/lib/liblzma

.PATH: ${XZDIR}/xz

SRCS=	args.c \
	coder.c \
	file_io.c \
	hardware.c \
	list.c \
	main.c \
	message.c \
	mytime.c \
	options.c \
	signals.c \
	suffix.c \
	util.c

.PATH: ${XZDIR}/common

SRCS+=	tuklib_open_stdxxx.c \
	tuklib_progname.c \
	tuklib_exit.c \
	tuklib_cpucores.c \
	tuklib_mbstr_width.c \
	tuklib_mbstr_fw.c

WARNS?=	3

CFLAGS+=	-DHAVE_CONFIG_H \
		-I${LZMALIBDIR} \
		-I${XZDIR}/common

LIBADD=	lzma

.include <bsd.prog.mk>
