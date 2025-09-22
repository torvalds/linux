#	$OpenBSD: Makefile.inc,v 1.7 2020/11/13 16:45:11 visa Exp $

BINDIR=		/usr/mdec

STANDALONE?=	-D_STANDALONE

.if ${MACHINE} == "loongson"
CPPFLAGS+=	${STANDALONE}
CPPFLAGS+=	-I.
CFLAGS+=	-fno-builtin -fno-stack-protector -Wall
CFLAGS+=	-Wa,-mfix-loongson2f-btb
AFLAGS+=	-Wa,-mfix-loongson2f-btb
SAABI=		-mips3 -mno-abicalls -G 0 -fno-pic -fno-common
AS?=		as
LD?=		ld
.endif

### Figure out what to use for libsa
LIBSADIR?=               ${.CURDIR}/../libsa

.if exists(${LIBSADIR}/${__objdir})
LIBSAOBJDIR=            ${LIBSADIR}/${__objdir}
.else
LIBSAOBJDIR=            ${LIBSADIR}
.endif

LIBSA=                  ${LIBSAOBJDIR}/libsa.a

### Figure out what to use for libz
LIBZDIR?=                ${.CURDIR}/../libz

.if exists(${LIBZDIR}/${__objdir})
LIBZOBJDIR=             ${LIBZDIR}/${__objdir}
.else
LIBZOBJDIR=             ${LIBZDIR}
.endif

LIBZ=                   ${LIBZOBJDIR}/libz.a
