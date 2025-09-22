#	$OpenBSD: Makefile,v 1.5 2010/11/17 12:31:11 jsing Exp $

PROG=	fsck_msdos
MAN=	fsck_msdos.8
SRCS=	main.c check.c boot.c fat.c dir.c fsutil.c
.PATH: ${.CURDIR}/../fsck
CFLAGS+= -I${.CURDIR}/../fsck
DPADD+=	${LIBUTIL}
LDADD+=	-lutil

.include <bsd.prog.mk>
