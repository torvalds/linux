#	$OpenBSD: Makefile.inc,v 1.12 2025/01/30 21:46:25 kurt Exp $
#	$NetBSD: Makefile.inc,v 1.7 2000/08/20 14:57:16 mrg Exp $

.ifndef __INCLUDED_STAND_MAKEFILE_INC
__INCLUDED_STAND_MAKEFILE_INC=

CEXTRAFLAGS?=-D_LP64

BINDIR=		/usr/mdec

STANDALONE?=	-D_STANDALONE

.if ${MACHINE} == "sparc64"
CPPFLAGS+=	${STANDALONE} -DSUN4U
CPPFLAGS+=	-I.

CFLAGS+=	-fno-stack-protector
CFLAGS+=	-msoft-float
CFLAGS+=	-ffreestanding
.endif

### Figure out what to use for libsa
LIBSADIR=               ${.CURDIR}/../libsa

.if exists(${LIBSADIR}/${__objdir})
LIBSAOBJDIR=            ${LIBSADIR}/${__objdir}
.else
LIBSAOBJDIR=            ${LIBSADIR}
.endif

LIBSA=                  ${LIBSAOBJDIR}/libsa.a

### Figure out what to use for libz
LIBZDIR=                ${.CURDIR}/../libz

.if exists(${LIBZDIR}/${__objdir})
LIBZOBJDIR=             ${LIBZDIR}/${__objdir}
.else
LIBZOBJDIR=             ${LIBZDIR}
.endif

LIBZ=                   ${LIBZOBJDIR}/libz.a

.endif
