#	$NetBSD: Makefile,v 1.5 1999/07/23 09:44:38 mrg Exp $
#	$FreeBSD$

PACKAGE=lib${LIB}
LIB=	usbhid
MAN=	usbhid.3

SHLIB_MAJOR= 4

MLINKS=	usbhid.3 libusbhid.3 usbhid.3 hid_get_report_desc.3 \
	usbhid.3 hid_dispose_report_desc.3 \
	usbhid.3 hid_start_parse.3 usbhid.3 hid_end_parse.3 \
	usbhid.3 hid_get_item.3 usbhid.3 hid_report_size.3 \
	usbhid.3 hid_locate.3 \
	usbhid.3 hid_usage_page.3 usbhid.3 hid_usage_in_page.3 \
	usbhid.3 hid_init.3 \
	usbhid.3 hid_get_data.3 usbhid.3 hid_set_data.3

SRCS=	descr.c descr_compat.c parse.c usage.c data.c

INCS=	usbhid.h

.if defined(COMPAT_32BIT)
CFLAGS+=	-DCOMPAT_32BIT
.endif

.include <bsd.lib.mk>
