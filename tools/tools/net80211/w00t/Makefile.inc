# $FreeBSD$

W00T=	../libw00t
# NB: we get crc32 from -lz
DPADD=	${W00T}/libw00t.a
LDFLAGS= -L${W00T}
LDADD=	-lw00t
LIBADD+= crypto z

BINDIR=	/usr/local/bin
CFLAGS=	-g -I${W00T}

MAN=
