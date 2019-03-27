#
# $FreeBSD$
#

PACKAGE=lib${LIB}
LIB=		z
SHLIBDIR?=	/lib
SHLIB_MAJOR=	6
MAN=		zlib.3 zopen.3
MLINKS+=	zopen.3 zdopen.3

ZLIBSRC=	${SRCTOP}/contrib/zlib

.PATH:		${ZLIBSRC}

#CFLAGS=-O -DMAX_WBITS=14 -DMAX_MEM_LEVEL=7
#CFLAGS=-g -DDEBUG
#CFLAGS=-O3 -Wall -Wwrite-strings -Wpointer-arith -Wconversion \
#           -Wstrict-prototypes -Wmissing-prototypes

CFLAGS+=	-DHAS_snprintf -DHAS_vsnprintf -I${.CURDIR}

WARNS?=		3

CLEANFILES+=	example.o example foo.gz minigzip.o minigzip

SRCS+=		adler32.c
SRCS+=		compress.c
SRCS+=		crc32.c
SRCS+=		deflate.c
SRCS+=		gzclose.c
SRCS+=		gzlib.c
SRCS+=		gzread.c
SRCS+=		gzwrite.c
SRCS+=		infback.c
SRCS+=		inffast.c
SRCS+=		inflate.c
SRCS+=		inftrees.c
SRCS+=		trees.c
SRCS+=		uncompr.c
SRCS+=		zopen.c
SRCS+=		zutil.c

.if ${MACHINE_ARCH} == "i386" || ${MACHINE_ARCH} == "amd64"
CFLAGS+=	-DUNALIGNED_OK
.endif

VERSION_DEF=	${.CURDIR}/Versions.def
SYMBOL_MAPS=	${.CURDIR}/Symbol.map
CFLAGS+=	-DSYMBOL_VERSIONING

INCS=		zconf.h zlib.h

.PATH:		${ZLIBSRC}/test

minigzip:	all minigzip.o
	$(CC) -o minigzip minigzip.o -L. -lz

example:	all example.o
	$(CC) -o example example.o -L. -lz

test: example minigzip
	(export LD_LIBRARY_PATH=. ; ./example )
	(export LD_LIBRARY_PATH=. ; \
		echo hello world | ./minigzip | ./minigzip -d )

FILES=		zlib.pc
FILESDIR=	${LIBDATADIR}/pkgconfig

.include <bsd.lib.mk>

## XXX: clang integrated-as doesn't grok .intel_syntax directives yet
#ACFLAGS.gvmat64.S=	${CLANG_NO_IAS}
