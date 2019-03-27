# $FreeBSD$

SHLIBDIR?=	/lib

.include <src.opts.mk>

PACKAGE=casper

.if ${MK_CASPER} != "no"
SHLIB=	casper
SHLIB_MAJOR=	1

SRCS=	libcasper.c
SRCS+=	libcasper_impl.c
SRCS+=	libcasper_service.c
SRCS+=	service.c
SRCS+=	zygote.c
.endif

INCS=	libcasper.h
INCS+=	libcasper_service.h

LIBADD=	nv

CFLAGS+=-I${.CURDIR}

MAN+=	libcasper.3
MAN+=	libcasper_service.3

MLINKS+=libcasper.3 cap_init.3
MLINKS+=libcasper.3 cap_wrap.3
MLINKS+=libcasper.3 cap_unwrap.3
MLINKS+=libcasper.3 cap_sock.3
MLINKS+=libcasper.3 cap_clone.3
MLINKS+=libcasper.3 cap_close.3
MLINKS+=libcasper.3 cap_limit_get.3
MLINKS+=libcasper.3 cap_limit_set.3
MLINKS+=libcasper.3 cap_send_nvlist.3
MLINKS+=libcasper.3 cap_recv_nvlist.3
MLINKS+=libcasper.3 cap_xfer_nvlist.3
MLINKS+=libcasper.3 cap_service_open.3

MLINKS+=libcasper_service.3 CREATE_SERVICE.3

.include <bsd.lib.mk>
