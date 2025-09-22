#	$OpenBSD: Makefile,v 1.30 2018/10/16 18:20:58 kettenis Exp $

MAN=	biosboot.8

.if ${MACHINE} == "i386"
S=	${.CURDIR}/../../../..
SADIR=	${.CURDIR}/..

PROG=	biosboot
SRCS=	biosboot.S
LD=ld
LDFLAGS=-nostdlib -Ttext 0 -N -x -Bstatic -nopie -znorelro
LDFLAGS+=-T ${.CURDIR}/ld.script
INSTALL_STRIP=

biosboot.o: assym.h

${PROG}: $(OBJS)
	@rm -f $(PROG)
	$(LD) $(LDFLAGS) -o $(PROG) $(OBJS)
	@size $(PROG)

CPPFLAGS+=-DLOADADDR=$(LOADADDR) -DLINKADDR=$(LINKADDR) -DBOOTMAGIC=$(BOOTMAGIC)
CPPFLAGS+=${DEBUGFLAGS}
CFLAGS+=-fno-pie
AFLAGS+=${NO_INTEGR_AS}
#AFLAGS+=-Wa,-a
AFLAGS+=-fno-pie
.else
NOPROG=
.endif

.include <bsd.prog.mk>
