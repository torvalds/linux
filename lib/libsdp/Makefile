# $Id: Makefile,v 1.2 2003/09/07 20:34:19 max Exp $
# $FreeBSD$

PACKAGE=lib${LIB}
LIB=		sdp
MAN=		sdp.3

WARNS?=		2
CFLAGS+=	-I${.CURDIR}

SHLIB_MAJOR=	4

SRCS=		search.c service.c session.c util.c
INCS=		sdp.h

MLINKS+=	sdp.3 SDP_GET8.3
MLINKS+=	sdp.3 SDP_GET16.3
MLINKS+=	sdp.3 SDP_GET32.3
MLINKS+=	sdp.3 SDP_GET64.3
MLINKS+=	sdp.3 SDP_GET128.3
MLINKS+=	sdp.3 SDP_PUT8.3
MLINKS+=	sdp.3 SDP_PUT16.3
MLINKS+=	sdp.3 SDP_PUT32.3
MLINKS+=	sdp.3 SDP_PUT64.3
MLINKS+=	sdp.3 SDP_PUT128.3
MLINKS+=	sdp.3 sdp_open.3
MLINKS+=	sdp.3 sdp_open_local.3
MLINKS+=	sdp.3 sdp_close.3
MLINKS+=	sdp.3 sdp_error.3
MLINKS+=	sdp.3 sdp_search.3
MLINKS+=	sdp.3 sdp_attr2desc.3
MLINKS+=	sdp.3 sdp_uuid2desc.3
MLINKS+=	sdp.3 sdp_register_service.3
MLINKS+=	sdp.3 sdp_unregister_service.3
MLINKS+=	sdp.3 sdp_change_service.3

.include <bsd.lib.mk>
