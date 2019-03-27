# $Id: Makefile,v 1.5 2003/09/08 02:28:35 max Exp $
# $FreeBSD$

.include <src.opts.mk>

SUBDIR= \
	bluetooth-config \
	bt3cfw \
	btpand \
	hccontrol \
	hcsecd \
	hcseriald \
	l2control \
	l2ping \
	rfcomm_pppd \
	sdpcontrol \
	sdpd

.if ${MK_USB} != "no"
SUBDIR+=	ath3kfw
SUBDIR+=	bcmfw
SUBDIR+=	bthidcontrol
SUBDIR+=	bthidd
.endif

.include <bsd.subdir.mk>

