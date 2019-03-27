#
# $FreeBSD$
#
# Copyright (c) 2013 Hans Petter Selasky.
# Copyright (c) 2014 SRI International
# All rights reserved.
#
# This software was developed by SRI International and the University of
# Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
# ("CTSRD"), as part of the DARPA CRASH research programme.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#

USBCOREDIR:=	${.PARSEDIR}
S=${USBCOREDIR}/../../sys

MACHDEP_DIRS=

.if defined(HAVE_EXYNOS_EHCI)
MACHDEP_DIRS+=	${S}/arm/samsung/exynos
.endif

.PATH: \
	${USBCOREDIR} \
	${USBCOREDIR}/storage \
	${S}/dev/usb \
	${S}/dev/usb/controller \
	${S}/dev/usb/serial \
	${S}/dev/usb/storage \
	${S}/dev/usb/template \
	${MACHDEP_DIRS}
.undef S

USB_POOL_SIZE?=	131072

CFLAGS+=	-DUSB_MSCTEST_BULK_SIZE=65536
CFLAGS+=	-DUSB_POOL_SIZE=${USB_POOL_SIZE}


#
# BUSDMA implementation
#
SRCS+=	usb_busdma_loader.c

#
# USB controller drivers
#

KSRCS+=	usb_controller.c

.if defined(HAVE_AT91DCI)
CFLAGS += -DUSB_PCI_PROBE_LIST="\"at91dci\""
KSRCS+=	at91dci.c
.endif

.if defined(HAVE_ATMEGADCI)
CFLAGS += -DUSB_PCI_PROBE_LIST="\"atmegadci\""
KSRCS+=	atmegadci.c
.endif

.if defined(HAVE_AVR32DCI)
CFLAGS += -DUSB_PCI_PROBE_LIST="\"avr32dci\""
KSRCS+=	avr32dci.c
.endif

.if defined(HAVE_DWCOTG)
CFLAGS += -DUSB_PCI_PROBE_LIST="\"dwcotg\""
KSRCS+=	dwcotg.c
.endif

.if defined(HAVE_MUSBOTG)
CFLAGS += -DUSB_PCI_PROBE_LIST="\"musbotg\""
KSRCS+=	musbotg.c
.endif

.if defined(HAVE_EHCI)
CFLAGS += -DUSB_PCI_PROBE_LIST="\"ehci\""
KSRCS+=	ehci.c
.endif

.if defined(HAVE_EXYNOS_EHCI)
CFLAGS += -DUSB_PCI_PROBE_LIST="\"combiner\", \"pad\", \"ehci\""
KSRCS+=	ehci.c
KSRCS+=	exynos5_combiner.c
KSRCS+=	exynos5_pad.c
KSRCS+=	exynos5_ehci.c
.endif

.if defined(HAVE_OHCI)
CFLAGS += -DUSB_PCI_PROBE_LIST="\"ohci\""
KSRCS+=	ohci.c
.endif

.if defined(HAVE_UHCI)
CFLAGS += -DUSB_PCI_PROBE_LIST="\"uhci\""
KSRCS+=	uhci.c
.endif

.if defined(HAVE_XHCI)
CFLAGS += -DUSB_PCI_PROBE_LIST="\"xhci\""
KSRCS+=	xhci.c
.endif

.if defined(HAVE_USS820DCI)
CFLAGS += -DUSB_PCI_PROBE_LIST="\"uss820dci\""
KSRCS+=	uss820dci.c
.endif

.if defined(HAVE_SAF1761OTG)
CFLAGS += -DUSB_PCI_PROBE_LIST="\"saf1761otg\""
CFLAGS += -DUSB_PCI_MEMORY_ADDRESS=0x900000007f100000ULL
CFLAGS += -DUSB_PCI_MEMORY_SIZE=0x40000U
KSRCS+=	saf1761_otg.c
KSRCS+=	saf1761_otg_boot.c
.endif

#
# USB core and templates
#
KSRCS+=	usb_core.c
KSRCS+=	usb_debug.c
KSRCS+=	usb_device.c
KSRCS+=	usb_dynamic.c
KSRCS+=	usb_error.c
KSRCS+=	usb_handle_request.c
KSRCS+=	usb_hid.c
KSRCS+=	usb_hub.c
KSRCS+=	usb_lookup.c
KSRCS+=	usb_msctest.c
KSRCS+=	usb_parse.c
KSRCS+=	usb_request.c
KSRCS+=	usb_transfer.c
KSRCS+=	usb_util.c
KSRCS+=	usb_template.c
KSRCS+=	usb_template_cdce.c
KSRCS+=	usb_template_msc.c
KSRCS+=	usb_template_mtp.c
KSRCS+=	usb_template_modem.c
KSRCS+=	usb_template_mouse.c
KSRCS+=	usb_template_kbd.c
KSRCS+=	usb_template_audio.c
KSRCS+=	usb_template_phone.c
KSRCS+=	usb_template_serialnet.c
KSRCS+=	usb_template_midi.c

#
# USB mass storage support
#
SRCS+=	umass_common.c

.if defined(HAVE_UMASS_LOADER)
CFLAGS+=        -I${.CURDIR}/../common
SRCS+=  umass_loader.c
.endif

