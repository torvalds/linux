#	@(#)Makefile	8.2 (Berkeley) 4/2/94
# $FreeBSD$

.include <src.opts.mk>

.PATH: ${SRCTOP}/usr.sbin/pwd_mkdb ${SRCTOP}/lib/libc/gen

PROG=	chpass
SRCS=	chpass.c edit.c field.c pw_scan.c table.c util.c
BINOWN=	root
BINMODE=4555
PRECIOUSPROG=
.if ${MK_NIS} != "no"
CFLAGS+= -DYP
.endif
#Some people need this, uncomment to activate
#CFLAGS+=-DRESTRICT_FULLNAME_CHANGE
CFLAGS+=-I${SRCTOP}/usr.sbin/pwd_mkdb -I${SRCTOP}/lib/libc/gen -I.

LIBADD=	crypt util
.if ${MK_NIS} != "no"
LIBADD+=	ypclnt
.endif 

SYMLINKS=	chpass ${BINDIR}/chfn
SYMLINKS+=	chpass ${BINDIR}/chsh
.if ${MK_NIS} != "no"
SYMLINKS+=	chpass ${BINDIR}/ypchfn
SYMLINKS+=	chpass ${BINDIR}/ypchpass
SYMLINKS+=	chpass ${BINDIR}/ypchsh
.endif

MLINKS=	chpass.1 chfn.1 chpass.1 chsh.1
.if ${MK_NIS} != "no"
MLINKS+= chpass.1 ypchpass.1 chpass.1 ypchfn.1 chpass.1 ypchsh.1
.endif

beforeinstall:
.for i in chpass chfn chsh ypchpass ypchfn ypchsh
.if exists(${DESTDIR}${BINDIR}/$i)
	-chflags noschg ${DESTDIR}${BINDIR}/$i
.endif
.endfor

.include <bsd.prog.mk>
