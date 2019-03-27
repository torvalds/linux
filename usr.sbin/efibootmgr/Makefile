# $FreeBSD$
EFIBOOT=${SRCTOP}/stand/efi
EFIINCL=${SRCTOP}/stand/efi/include
EFIVAR=${SRCTOP}/usr.sbin/efivar
.PATH: ${EFIBOOT}/libefi ${EFIVAR}
CFLAGS+= -I${EFIVAR} -I${EFIINCL}

PROG=efibootmgr
MAN=	efibootmgr.8
SRCS= efichar.c efiutil.c efibootmgr.c

LIBADD= efivar geom

.include <bsd.prog.mk>
