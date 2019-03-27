#
# $FreeBSD$
#
# Copyright (c) 2008 Hans Petter Selasky. All rights reserved.
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

SYSDIR?=${SRCTOP}/sys
.include "${SYSDIR}/conf/kern.opts.mk"

#
# Check for common USB debug flags to pass when building the USB
# modules in this directory:
#
.if defined(USB_DEBUG)
MAKE+=" DEBUG_FLAGS+=-DUSB_DEBUG"
.endif

.if defined(USB_DEBUG) && defined(USB_REQ_DEBUG)
MAKE+=" DEBUG_FLAGS+=-DUSB_REQ_DEBUG"
.endif

# Modules that include binary-only blobs of microcode should be selectable by
# MK_SOURCELESS_UCODE option (see below).

SUBDIR = usb
SUBDIR += ${_dwc_otg} ehci ${_musb} ohci uhci xhci ${_uss820dci} \
	  ${_atmegadci} ${_avr32dci} ${_rsu} ${_rsufw} ${_saf1761otg}
SUBDIR += ${_rum} ${_run} ${_runfw} ${_uath} upgt usie ural ${_zyd} ${_urtw} 
SUBDIR += atp cfumass uhid uhid_snes ukbd ums udbp ufm uep wmt wsp ugold uled
SUBDIR += ucom u3g uark ubsa ubser uchcom ucycom ufoma uftdi ugensa uipaq ulpt \
	  umct umcs umodem umoscom uplcom uslcom uvisor uvscom
SUBDIR += udl
SUBDIR += uether aue axe axge cdce cue ${_kue} mos rue smsc udav uhso ipheth
SUBDIR += muge
SUBDIR += ure urndis
SUBDIR += usfs umass urio
SUBDIR += quirk template
SUBDIR += ${_g_audio} ${_g_keyboard} ${_g_modem} ${_g_mouse}

.if ${MK_USB_GADGET_EXAMPLES} == "yes"
_g_audio=	g_audio
_g_keyboard=	g_keyboard
_g_modem=	g_modem
_g_mouse=	g_mouse
.endif

.if ${MK_SOURCELESS_UCODE} != "no"
_rum=		rum
_uath=		uath
_zyd=		zyd
_kue=		kue
_run=		run
_rsu=		rsu

.if ${MACHINE_CPUARCH} != "mips"
_rsufw=		rsufw
_runfw=		runfw
.endif
.endif

.if ${MACHINE_CPUARCH} == "amd64"
_urtw=		urtw
.endif

.if ${MACHINE_CPUARCH} == "arm"
_atmegadci=	atmegadci
_dwc_otg=	dwc_otg
_musb=		musb
_uss820dci=	uss820dci
.endif

.if ${MACHINE_CPUARCH} == "i386"
_urtw=		urtw
.endif

.if ${MACHINE_CPUARCH} == "avr32"
_avr32dci=	avr32dci
.endif

.if ${MACHINE_CPUARCH} == "mips"
_saf1761otg=	saf1761otg
.endif

.include <bsd.subdir.mk>
