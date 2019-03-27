# $Id: Makefile,v 1.2 2004/02/13 21:44:41 max Exp $
# $FreeBSD$

.PATH:	${.CURDIR:H}/bthidd

PROG=		bthidcontrol
MAN=		bthidcontrol.8
SRCS=		bthidcontrol.c hid.c lexer.l  parser.y sdp.c
WARNS?=		1
CFLAGS+=	-DBTHIDCONTROL=1 -I${.CURDIR:H}/bthidd -I${SRCTOP}/lib/libsdp \
		-I${SRCTOP}/lib/libbluetooth

LIBADD+=	bluetooth sdp usbhid

.include <bsd.prog.mk>
