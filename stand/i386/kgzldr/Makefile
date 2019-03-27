# $FreeBSD$

.include <bsd.init.mk>

PROG=	kgzldr.o
STRIP=
BINMODE=${LIBMODE}
BINDIR=	${LIBDIR}

SRCS=	start.S boot.c subr_inflate.c lib.c crt.S sio.S
CFLAGS=	-Os
CFLAGS+=-DKZIP
NO_SHARED=
LDFLAGS+=-Wl,-r
.PATH:	${SYSDIR}/kern

BOOT_COMCONSOLE_PORT?=	0x3f8
ACFLAGS+=-Wa,-defsym,SIO_PRT=${BOOT_COMCONSOLE_PORT}

.include <bsd.prog.mk>
