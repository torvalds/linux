# $FreeBSD$

PROG=	etdump

MAKEFS_SRC=	${SRCTOP}/usr.sbin/makefs

CFLAGS+=	-I${SRCTOP}/sys/fs/cd9660 -I${MAKEFS_SRC} \
		-I${MAKEFS_SRC}/cd9660

.PATH:	${MAKEFS_SRC}/cd9660

SRCS=		etdump.c output_shell.c output_text.c cd9660_conversion.c

.include <bsd.prog.mk>
