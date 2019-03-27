# $FreeBSD$

.include <bsd.init.mk>

PROG=	btxldr
INTERNALPROG=
SRCS=	btxldr.S

CFLAGS+=-DLOADER_ADDRESS=${LOADER_ADDRESS}
CFLAGS+=-I${BOOTSRC}/i386/common

.if defined(BTXLDR_VERBOSE)
CFLAGS+=-DBTXLDR_VERBOSE
.endif

ORG=${LOADER_ADDRESS}
LDFLAGS+=${LDFLAGS_BIN}

.include <bsd.prog.mk>
