# $Id: Makefile,v 1.8 2003/08/14 20:06:20 max Exp $
# $FreeBSD$

CONFS=		hcsecd.conf
CONFSDIR=	/etc/bluetooth
CONFSMODE_hcsecd.conf=	600
PROG=		hcsecd
MAN=		hcsecd.8 hcsecd.conf.5
SRCS=		hcsecd.c lexer.l parser.y
WARNS?=		2
CFLAGS+=	-I${.CURDIR}

LIBADD=		bluetooth

.include <bsd.prog.mk>
