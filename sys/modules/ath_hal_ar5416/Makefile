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

KMOD=	ath_hal_ar5416

# FreeBSD module loading pieces
.PATH:	${SRCTOP}/sys/dev/ath/
SRCS=	ah_osdep_ar5416.c

#
# AR5416, AR9130, AR9160, AR9220, AR9280, AR9285, AR9287 support.
#
# NB: 9160 depends on 5416 but 5416 does not require 9160
#
# + 5416 (Owl)
.PATH:	${SRCTOP}/sys/dev/ath/ath_hal/ar5416

SRCS+=	ar5416_ani.c ar5416_attach.c ar5416_beacon.c ar5416_btcoex.c \
	ar5416_cal.c ar5416_cal_iq.c ar5416_cal_adcgain.c ar5416_cal_adcdc.c \
	ar5416_eeprom.c ar5416_gpio.c ar5416_interrupts.c ar5416_keycache.c \
	ar5416_misc.c ar5416_phy.c ar5416_power.c ar5416_radar.c ar5416_spectral.c \
	ar5416_recv.c ar5416_reset.c ar5416_xmit.c

SRCS+=	device_if.h bus_if.h pci_if.h opt_inet.h opt_ath.h opt_ah.h opt_wlan.h

# RF backend for 5416, 9130 and 9160
SRCS+=	ar2133.c

# + AR9160 (Sowl)
.PATH:	${SRCTOP}/sys/dev/ath/ath_hal/ar9001
SRCS+=	ar9160_attach.c

# + AR9130 - (Sowl) - Embedded (AR913x SoC)
#
# This requires AH_SUPPORT_AR9130 in order to function as some
# register values have shifted for this chipset. Definig this however
# (currently) breaks non-AR9130 chipsets - since this is an embedded
# chipset and no other radios are glued to it, this shouldn't pose a
# problem.
SRCS+=	ar9130_attach.c ar9130_eeprom.c ar9130_phy.c

# AR9002 series chips
# + AR9220/AR9280 - Merlin
.PATH:	${SRCTOP}/sys/dev/ath/ath_hal/ar9002
SRCS+=	ar9280.c ar9280_attach.c ar9280_olc.c

# + AR9285 - Kite
SRCS+=	ar9285.c ar9285_reset.c ar9285_attach.c ar9285_cal.c ar9285_phy.c
SRCS+=	ar9285_diversity.c ar9285_btcoex.c

# + AR9287 - Kiwi
.PATH:  ${SRCTOP}/sys/dev/ath/ath_hal
SRCS+=  ah_eeprom_9287.c
.PATH:  ${SRCTOP}/sys/dev/ath/ath_hal/ar9002
SRCS+=  ar9287.c ar9287_reset.c ar9287_attach.c ar9287_cal.c ar9287_olc.c

CFLAGS+=  -I. -I${SRCTOP}/sys/dev/ath -I${SRCTOP}/sys/dev/ath/ath_hal
CFLAGS+=  -I. -I${SRCTOP}/sys/contrib/dev/ath/ath_hal/

.include <bsd.kmod.mk>
