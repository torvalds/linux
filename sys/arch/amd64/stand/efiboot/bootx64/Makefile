#	$OpenBSD: Makefile,v 1.4 2021/06/04 00:09:34 krw Exp $

.include <bsd.own.mk>

PROG=		BOOTX64.EFI
NOMAN=		#
OBJFMT=		efi-app-x86_64
SOFTRAID=	yes
COPTS+=		-I${EFIDIR}/include/amd64
SRCS+=		start_amd64.S run_i386.S mdrandom.c
LDSCRIPT=	ldscript.amd64

.include "${.CURDIR}/../Makefile.common"

