# $Id: Makefile,v 1.5 2003/07/22 18:38:04 max Exp $
# $FreeBSD$

PACKAGE=	lib${LIB}
CONFS=		hosts protocols
CONFSDIR=	/etc/bluetooth
CONFSMODE_protocols=	444
LIB=		bluetooth
MAN=		bluetooth.3

WARNS?=		2
CFLAGS+=	-I${.CURDIR} -I${SRCTOP}/sys

SHLIB_MAJOR=	4

SRCS=		bluetooth.c dev.c hci.c
INCS=		bluetooth.h

MLINKS+=	bluetooth.3 bt_gethostbyname.3
MLINKS+=	bluetooth.3 bt_gethostbyaddr.3
MLINKS+=	bluetooth.3 bt_gethostent.3
MLINKS+=	bluetooth.3 bt_sethostent.3
MLINKS+=	bluetooth.3 bt_endhostent.3

MLINKS+=	bluetooth.3 bt_getprotobyname.3
MLINKS+=	bluetooth.3 bt_getprotobynumber.3
MLINKS+=	bluetooth.3 bt_getprotoent.3
MLINKS+=	bluetooth.3 bt_setprotoent.3
MLINKS+=	bluetooth.3 bt_endprotoent.3

MLINKS+=	bluetooth.3 bt_ntoa.3
MLINKS+=	bluetooth.3 bt_aton.3

MLINKS+=	bluetooth.3 bt_devaddr.3
MLINKS+=	bluetooth.3 bt_devname.3

MLINKS+=	bluetooth.3 bt_devinfo.3
MLINKS+=	bluetooth.3 bt_devenum.3

MLINKS+=	bluetooth.3 bt_devopen.3
MLINKS+=	bluetooth.3 bt_devclose.3
MLINKS+=	bluetooth.3 bt_devsend.3
MLINKS+=	bluetooth.3 bt_devreq.3
MLINKS+=	bluetooth.3 bt_devfilter.3
MLINKS+=	bluetooth.3 bt_devfilter_pkt_set.3
MLINKS+=	bluetooth.3 bt_devfilter_pkt_clr.3
MLINKS+=	bluetooth.3 bt_devfilter_pkt_tst.3
MLINKS+=	bluetooth.3 bt_devfilter_evt_set.3
MLINKS+=	bluetooth.3 bt_devfilter_evt_clr.3
MLINKS+=	bluetooth.3 bt_devfilter_evt_tst.3
MLINKS+=	bluetooth.3 bt_devinquiry.3

MLINKS+=	bluetooth.3 bdaddr_same.3
MLINKS+=	bluetooth.3 bdaddr_any.3
MLINKS+=	bluetooth.3 bdaddr_copy.3

.include <bsd.lib.mk>
