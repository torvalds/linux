#	$OpenBSD: Makefile,v 1.90 2021/10/13 16:56:30 tb Exp $
#	$EOM: Makefile,v 1.78 2000/10/15 21:33:42 niklas Exp $

#
# Copyright (c) 1998, 1999, 2000, 2001 Niklas Hallqvist.  All rights reserved.
# Copyright (c) 2000, 2001 Håkan Olsson.  All rights reserved.
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
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

#
# This code was written under funding by Ericsson Radio Systems.
#

.PATH:		${.CURDIR}/sysdep/openbsd

PROG=		isakmpd
SRCS=		app.c attribute.c cert.c connection.c constants.c conf.c \
		cookie.c crypto.c dh.c doi.c exchange.c exchange_num.c \
		field.c hash.c if.c ike_auth.c ike_main_mode.c \
		ike_phase_1.c ike_quick_mode.c init.c ipsec.c ipsec_fld.c \
		ipsec_num.c isakmpd.c isakmp_doi.c isakmp_fld.c isakmp_num.c \
		key.c log.c message.c \
		prf.c sa.c sysdep.c timer.c transport.c virtual.c udp.c \
		ui.c util.c x509.c \
		pf_key_v2.c policy.c ike_aggressive.c isakmp_cfg.c \
		dpd.c monitor.c monitor_fdpass.c nat_traversal.c udp_encap.c \
		vendor.c

GENERATED=	exchange_num.h ipsec_fld.h ipsec_num.h isakmp_fld.h \
		isakmp_num.h
CLEANFILES=	exchange_num.c exchange_num.h ipsec_num.c ipsec_num.h \
		isakmp_num.c isakmp_num.h ipsec_fld.c ipsec_fld.h \
		isakmp_fld.c isakmp_fld.h
MAN=		isakmpd.8 isakmpd.conf.5 isakmpd.policy.5
CFLAGS+=	-Wall -Wstrict-prototypes -Wmissing-prototypes \
		-Wmissing-declarations -I${.CURDIR} -I.

# Include symbolic debugging info
#DEBUG=		-g

# Enable for DNSSEC use
#CFLAGS+=	USE_DNSSEC
#SRCS+=		dnssec.c
#LWRESLIB=      /usr/local/lib/liblwres.a
#DNSSEC_CFLAGS= -I/usr/local/include -DLWRES

LDADD+=		${LWRESLIB} -lkeynote -lcrypto -lm
DPADD+=		${LWRESLIB} ${LIBKEYNOTE} ${LIBCRYPTO} ${LIBM}

exchange_num.c exchange_num.h: genconstants.sh exchange_num.cst
	/bin/sh ${.CURDIR}/genconstants.sh ${.CURDIR}/exchange_num

ipsec_fld.c ipsec_fld.h: genfields.sh ipsec_fld.fld
	/bin/sh ${.CURDIR}/genfields.sh ${.CURDIR}/ipsec_fld

ipsec_num.c ipsec_num.h: genconstants.sh ipsec_num.cst
	/bin/sh ${.CURDIR}/genconstants.sh ${.CURDIR}/ipsec_num

isakmp_fld.c isakmp_fld.h: genfields.sh isakmp_fld.fld
	/bin/sh ${.CURDIR}/genfields.sh ${.CURDIR}/isakmp_fld

isakmp_num.c isakmp_num.h: genconstants.sh isakmp_num.cst
	/bin/sh ${.CURDIR}/genconstants.sh ${.CURDIR}/isakmp_num

.PHONY: generated
generated: ${GENERATED}

BUILDFIRST = ${GENERATED}

.include <bsd.prog.mk>

# Don't compile isakmpd as static binary by default
LDSTATIC=
