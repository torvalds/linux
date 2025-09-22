#	$OpenBSD: Makefile,v 1.5 2016/07/30 03:25:48 guenther Exp $

.include <bsd.own.mk>

MAN=	mbr.8

.if ${MACHINE} == "landisk"
PROG=	mbr
SRCS=	mbr.S
AFLAGS+=-nostdinc -I${.OBJDIR} -I${.CURDIR}/.. -I${.CURDIR}/../../.. #-Wa,a
AFLAGS+=-fno-pie
LDFLAGS+=-N -e start -Ttext 0x8c200200 -nopie -znorelro
OBJCOPY?=objcopy

INSTALL_STRIP=
S=	${.CURDIR}/../../../..

# Uncomment this to make mbr talk to a serial port.
#CPPFLAGS+=-DSERIAL=0

${PROG}: $(OBJS) $(DPADD)
	$(LD) $(LDFLAGS) -o $(PROG) $(OBJS) $(LDADD)
	@size $(PROG)
	@if [ -x ${.OBJDIR}/${PROG} ]; then \
		${OBJCOPY} -O binary ${PROG} ${.OBJDIR}/.tmp;\
		mv -f ${.OBJDIR}/.tmp ${.OBJDIR}/${PROG}; \
		ls -l ${.OBJDIR}/${PROG}; \
	fi

.else
NOPROG=
.endif

.include <bsd.prog.mk>
