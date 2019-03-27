# $FreeBSD$

PROG=	mkuzip
MAN=	mkuzip.8
SRCS=	mkuzip.c mkuz_blockcache.c mkuz_lzma.c mkuz_zlib.c mkuz_conveyor.c \
	    mkuz_blk.c mkuz_fqueue.c mkuz_time.c mkuz_insize.c

#CFLAGS+=	-DMKUZ_DEBUG

LIBADD=	z md lzma pthread

.include <bsd.prog.mk>
