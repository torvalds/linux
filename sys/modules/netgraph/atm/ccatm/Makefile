# $FreeBSD$
#
# Author: Harti Brandt <harti@freebsd.org>
#

LIBBASE= ${SRCTOP}/sys/contrib/ngatm

.PATH: ${SRCTOP}/sys/netgraph/atm/ccatm
.PATH: ${LIBBASE}/netnatm/api

KMOD=	ng_ccatm
SRCS=	ng_ccatm.c cc_conn.c cc_data.c cc_dump.c cc_port.c cc_sig.c	\
	cc_user.c unisap.c

CFLAGS+= -I${LIBBASE} -DCCATM_DEBUG

.include <bsd.kmod.mk>
