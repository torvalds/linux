# $FreeBSD$

.include <src.opts.mk>

.PATH: ${.CURDIR}/core ${.CURDIR}/misc

PACKAGE=runtime
PROG=		geom
SRCS=		geom.c subr.c
MAN=		geom.8
CFLAGS+=	-I${.CURDIR} -I${.CURDIR}/core
CFLAGS+=	-DGEOM_CLASS_DIR=\"${GEOM_CLASS_DIR}\"

LIBADD=		geom util

.if defined(RESCUE) || defined(RELEASE_CRUNCH)
.PATH:	${SRCTOP}/lib/geom/part \
	${SRCTOP}/lib/geom/label

SRCS+=	geom_label.c geom_part.c
MAN=

WARNS?=	2
CFLAGS+=-DSTATIC_GEOM_CLASSES
.else
.include "${SRCTOP}/lib/geom/Makefile.classes"
LINKS=		${GEOM_CLASSES:S|^|${BINDIR}/geom ${BINDIR}/g|}
.endif

.include <bsd.prog.mk>
