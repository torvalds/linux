# $FreeBSD$

.include <src.opts.mk>
MK_SSP=		no

GCCDIR=	${SRCTOP}/contrib/gcc
GCCLIB=	${SRCTOP}/contrib/gcclibs
CCDIR=	${SRCTOP}/gnu/usr.bin/cc
.include "${CCDIR}/Makefile.tgt"
.include "${CCDIR}/cc_tools/Makefile.hdrs"

.PATH: ${GCCDIR}/config/${GCC_CPU} ${GCCDIR}

SRCS=		crtstuff.c ${COMMONHDRS}
OBJS=		crtbegin.o crtend.o crtbeginT.o
SOBJS=		crtbeginS.o crtendS.o
CSTD?=		gnu89
CFLAGS+=	-DIN_GCC -DHAVE_LD_EH_FRAME_HDR -DDT_CONFIG -D__GLIBC__=3
CFLAGS.gcc+=	-finhibit-size-directive -fno-toplevel-reorder
CFLAGS+=	-fno-inline-functions -fno-exceptions \
		-fno-zero-initialized-in-bss -fno-asynchronous-unwind-tables \
		-fno-omit-frame-pointer
CFLAGS+=	-I${GCCLIB}/include -I${GCCDIR}/config -I${GCCDIR} -I. \
		-I${CCDIR}/cc_tools
CRTS_CFLAGS=	-DCRTSTUFFS_O -DSHARED ${PICFLAG}

.if ${TARGET_CPUARCH} == "arm"
CFLAGS+=	-DTARGET_ARM_EABI
.endif

.if ${MACHINE_CPUARCH} == "sparc64"
TGTOBJS=	crtfastmath.o
SRCS+=		crtfastmath.c
.endif
BEGINSRC?=	crtstuff.c
ENDSRC?=	crtstuff.c

FILES=		${OBJS} ${SOBJS} ${TGTOBJS}
FILESMODE=	${LIBMODE}
FILESOWN=	${LIBOWN}
FILESGRP=	${LIBGRP}
FILESDIR=	${LIBDIR}
# These FILES qualify as libraries for the purpose of LIBRARIES_ONLY.
.undef LIBRARIES_ONLY

${OBJS} ${SOBJS}: ${SRCS:M*.h}

CLEANFILES=	${OBJS} ${SOBJS} ${TGTOBJS}

crtbegin.o:	${BEGINSRC}
	${CC} ${CFLAGS} -g0 -DCRT_BEGIN \
		-c -o ${.TARGET} ${.ALLSRC:N*.h}

crtbeginT.o:	${BEGINSRC}
	${CC} ${CFLAGS} -g0 -DCRT_BEGIN -DCRTSTUFFT_O \
		-c -o ${.TARGET} ${.ALLSRC:N*.h}

crtbeginS.o:	${BEGINSRC}
	${CC} ${CFLAGS} -g0 -DCRT_BEGIN ${CRTS_CFLAGS} \
		-c -o ${.TARGET} ${.ALLSRC:N*.h}

crtend.o:	${ENDSRC}
	${CC} ${CFLAGS} -g0 -DCRT_END \
		-c -o ${.TARGET} ${.ALLSRC:N*.h}

crtendS.o:	${ENDSRC}
	${CC} ${CFLAGS} -g0 -DCRT_END ${CRTS_CFLAGS} \
		-c -o ${.TARGET} ${.ALLSRC:N*.h}

COMMONHDRS+=	tm.h tconfig.h options.h
CLEANFILES+=	${COMMONHDRS} optionlist cs-tconfig.h cs-tm.h

.include <bsd.lib.mk>
