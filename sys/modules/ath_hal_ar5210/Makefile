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

KMOD=	ath_hal_ar5210

# FreeBSD module loading pieces
.PATH:	${SRCTOP}/sys/dev/ath/
SRCS=	ah_osdep_ar5210.c

#
# AR5210 support; these are first generation 11a-only devices.
#
.PATH:	${SRCTOP}/sys/dev/ath/ath_hal/ar5210
SRCS+=	ar5210_attach.c ar5210_beacon.c ar5210_interrupts.c \
	ar5210_keycache.c ar5210_misc.c ar5210_phy.c ar5210_power.c \
	ar5210_recv.c ar5210_reset.c ar5210_xmit.c

SRCS+=  device_if.h bus_if.h pci_if.h opt_inet.h opt_ath.h opt_ah.h opt_wlan.h

CFLAGS+=  -I. -I${SRCTOP}/sys/dev/ath -I${SRCTOP}/sys/dev/ath/ath_hal
CFLAGS+=  -I. -I${SRCTOP}/sys/contrib/dev/ath/ath_hal/

.include <bsd.kmod.mk>
