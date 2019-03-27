# $FreeBSD$

PACKAGE=lib${LIB}
SHLIBDIR?=/lib

.include <src.opts.mk>

LIB=	ulog
SHLIB_MAJOR= 0
INCS=	ulog.h utempter.h
SRCS=	ulog.h ulog_login.c ulog_login_pseudo.c utempter.c

MAN=	ulog_login.3 utempter_add_record.3
MLINKS+=ulog_login.3 ulog_login_pseudo.3 \
	ulog_login.3 ulog_logout.3 \
	ulog_login.3 ulog_logout_pseudo.3 \
	utempter_add_record.3 utempter_remove_added_record.3 \
	utempter_add_record.3 utempter_remove_record.3 \
	utempter_add_record.3 addToUtmp.3 \
	utempter_remove_added_record.3 removeFromUtmp.3 \
	utempter_remove_record.3 removeLineFromUtmp.3

LIBADD=	md

VERSION_DEF= ${SRCTOP}/lib/libc/Versions.def
SYMBOL_MAPS= ${.CURDIR}/Symbol.map

.if ${MK_INSTALLLIB} != "no"
SYMLINKS+=libulog.a ${LIBDIR}/libutempter.a
.endif
.if !defined(NO_PIC)
SYMLINKS+=libulog.so ${LIBDIR}/libutempter.so
.endif
.if ${MK_PROFILE} != "no"
SYMLINKS+=libulog_p.a ${LIBDIR}/libutempter_p.a
.endif

.include <bsd.lib.mk>
