# $FreeBSD$

SHLIBDIR?=	/lib/casper

.include <src.opts.mk>

PACKAGE=libcasper

SHLIB_MAJOR=	1
INCSDIR?=	${INCLUDEDIR}/casper

.if ${MK_CASPER} != "no"
SHLIB=	cap_fileargs

SRCS=	cap_fileargs.c
.endif

INCS=	cap_fileargs.h

LIBADD=	nv

CFLAGS+=-I${.CURDIR}

MAN+=	cap_fileargs.3

MLINKS+=cap_fileargs.3 libcap_fileargs.3
MLINKS+=cap_fileargs.3 fileargs_cinit.3
MLINKS+=cap_fileargs.3 fileargs_cinitnv.3
MLINKS+=cap_fileargs.3 fileargs_fopen.3
MLINKS+=cap_fileargs.3 fileargs_free.3
MLINKS+=cap_fileargs.3 fileargs_init.3
MLINKS+=cap_fileargs.3 fileargs_initnv.3
MLINKS+=cap_fileargs.3 fileargs_open.3

.include <bsd.lib.mk>
