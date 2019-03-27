#
# Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer,
#    without modification.
# 2. Redistributions in binary form must reproduce at minimum a disclaimer
#    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
#    redistribution must be conditioned upon including a substantially
#    similar Disclaimer requirement for further binary redistribution.
#
# NO WARRANTY
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
# AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
# THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
# OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
# IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGES.
#
# $FreeBSD$
#

.PATH:	${SRCTOP}/sys/dev/ath
.PATH:	${SRCTOP}/sys/dev/ath/ath_hal

KMOD=	ath_hal_ar9300

# FreeBSD module loading pieces
.PATH:	${SRCTOP}/sys/dev/ath/
SRCS=	ah_osdep_ar9300.c

# + AR9300 HAL
.PATH:  ${SRCTOP}/sys/contrib/dev/ath/ath_hal/ar9300
SRCS+= ar9300_interrupts.c ar9300_radar.c ar9300_ani.c ar9300_keycache.c
SRCS+= ar9300_radio.c ar9300_xmit.c ar9300_attach.c ar9300_mci.c ar9300_stub.c
SRCS+= ar9300_xmit_ds.c ar9300_beacon.c ar9300_misc.c ar9300_recv.c
SRCS+= ar9300_stub_funcs.c ar9300_eeprom.c ar9300_paprd.c ar9300_recv_ds.c
SRCS+= ar9300_freebsd.c ar9300_phy.c ar9300_reset.c ar9300_gpio.c
SRCS+= ar9300_power.c ar9300_timer.c ar9300_spectral.c

SRCS+=	device_if.h bus_if.h pci_if.h opt_inet.h opt_ath.h opt_ah.h opt_wlan.h

CFLAGS+=  -I. -I${SRCTOP}/sys/dev/ath -I${SRCTOP}/sys/dev/ath/ath_hal
CFLAGS+=  -I. -I${SRCTOP}/sys/contrib/dev/ath/ath_hal/

.include <bsd.kmod.mk>

# AR9300 HAL build overrides, as there's still some code to tidy up
CWARNFLAGS.ar9300_eeprom.c=	${NO_WCONSTANT_CONVERSION}
CWARNFLAGS.ar9300_reset.c=	${NO_WSOMETIMES_UNINITIALIZED} -Wno-unused-function
