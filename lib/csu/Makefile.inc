# $FreeBSD$

SSP_CFLAGS=

SED_FIX_NOTE = -i "" -e '/\.note\.tag/s/progbits/note/'

NO_WMISSING_VARIABLE_DECLARATIONS=

.include <src.opts.mk>

.if ${MK_BSD_CRTBEGIN} != "no" && !defined(BUILDING_TESTS)

OBJS+=	crtbegin.o crtbeginS.o crtbeginT.o
OBJS+=	crtend.o crtendS.o

CFLAGS_CRTS=	-DSHARED ${PICFLAG}

crtbegin.o: crtbegin.c
crtbeginS.o: crtbegin.c
crtbeginT.o: crtbegin.c
crtend.o: crtend.c
crtendS.o: crtend.c

crtbegin.o crtend.o crtbeginT.o:
	${CC} ${CFLAGS} -I${.CURDIR} -c -o ${.TARGET} ${.ALLSRC}

crtbeginS.o crtendS.o:
	${CC} ${CFLAGS} -I${.CURDIR} ${CFLAGS_CRTS} -c -o ${.TARGET} ${.ALLSRC}

.endif

.include "../Makefile.inc"
