# $FreeBSD$

.PATH: ${.CURDIR:H}/common

SRCS=		crti.S crtn.S
OBJS=		${SRCS:N*.h:R:S/$/.o/g}
OBJS+=		gcrt1.o crt1.o Scrt1.o
CFLAGS+=	-I${.CURDIR} -I${.CURDIR:H}/common \
		-I${SRCTOP}/lib/libc/include
CFLAGS+=	-DCRT_IRELOC_REL

FILES=		${OBJS}
FILESMODE=	${LIBMODE}
FILESOWN=	${LIBOWN}
FILESGRP=	${LIBGRP}
FILESDIR=	${LIBDIR}
# These FILES qualify as libraries for the purpose of LIBRARIES_ONLY.
.undef LIBRARIES_ONLY

CLEANFILES=	${OBJS} crt1_c.o crt1_s.o gcrt1_c.o Scrt1_c.o
CLEANFILES+=	crt1_c.s gcrt1_c.s Scrt1_c.s

# See the comment in lib/csu/common/crtbrand.c for the reason crt1_c.c is not
# directly compiled to .o files.

gcrt1_c.s: crt1_c.c
	${CC} ${CFLAGS} -DGCRT -S -o ${.TARGET} ${.CURDIR}/crt1_c.c
	sed ${SED_FIX_NOTE} ${.TARGET}

gcrt1_c.o: gcrt1_c.s
	${CC} ${CFLAGS:N-g} ${ACFLAGS} -c -o ${.TARGET} gcrt1_c.s

gcrt1.o: gcrt1_c.o crt1_s.o
	${LD} ${_LDFLAGS} -o gcrt1.o -r crt1_s.o gcrt1_c.o

crt1_c.s: crt1_c.c
	${CC} ${CFLAGS} -S -o ${.TARGET} ${.CURDIR}/crt1_c.c
	sed ${SED_FIX_NOTE} ${.TARGET}

crt1_c.o: crt1_c.s
	${CC} ${CFLAGS:N-g} ${ACFLAGS} -c -o ${.TARGET} crt1_c.s

crt1.o:	crt1_c.o crt1_s.o
	${LD} ${_LDFLAGS} -o crt1.o -r crt1_s.o crt1_c.o
	${OBJCOPY} --localize-symbol _start1 crt1.o

Scrt1_c.s: crt1_c.c
	${CC} ${CFLAGS} -fPIC -DPIC -S -o ${.TARGET} ${.CURDIR}/crt1_c.c
	sed ${SED_FIX_NOTE} ${.TARGET}

Scrt1_c.o: Scrt1_c.s
	${CC} ${CFLAGS:N-g} ${ACFLAGS} -c -o ${.TARGET} Scrt1_c.s

Scrt1.o: Scrt1_c.o crt1_s.o
	${LD} ${_LDFLAGS} -o Scrt1.o -r crt1_s.o Scrt1_c.o
	${OBJCOPY} --localize-symbol _start1 Scrt1.o

.include <bsd.lib.mk>
