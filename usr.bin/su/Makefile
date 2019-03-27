#	@(#)Makefile	8.1 (Berkeley) 7/19/93
# $FreeBSD$

.include <src.opts.mk>

PROG=	su

LIBADD=	util pam

.if ${MK_AUDIT} != "no"
CFLAGS+= -DUSE_BSM_AUDIT
LIBADD+=	bsm
.endif

BINOWN=	root
BINMODE=4555
PRECIOUSPROG=

.include <bsd.prog.mk>
