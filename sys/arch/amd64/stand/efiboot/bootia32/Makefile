#	$OpenBSD: Makefile,v 1.4 2020/06/14 16:00:11 deraadt Exp $

.include <bsd.own.mk>

PROG=		BOOTIA32.EFI
NOMAN=		#
OBJFMT=		efi-app-ia32
SOFTRAID=	yes
SRCS+=		start_i386.S mdrandom.c
LDSCRIPT=	ldscript.i386

COPTS=		-I${EFIDIR}/include/i386

.include "${.CURDIR}/../Makefile.common"

CFLAGS+=	-m32
AFLAGS+=	-m32
LDFLAGS+=	-N
