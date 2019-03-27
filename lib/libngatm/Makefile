# $FreeBSD$
#
# Author: Harti Brandt <harti@freebsd.org>
#
PACKAGE=lib${LIB}
LIB=	ngatm
SHLIB_MAJOR= 4
MAN=	libngatm.3 uniaddr.3 unifunc.3 unimsg.3 unisap.3 unistruct.3

# source of the library lives in contrib
SDIR=	${SRCTOP}/sys
CTRB=	${SRCTOP}/contrib/ngatm
LIBBASE= ${SDIR}/contrib/ngatm

CFLAGS+= -I${LIBBASE} -I${.OBJDIR} -I${CTRB}/libngatm
# CFLAGS+= -DSSCOP_DEBUG -DSSCFU_DEBUG -DUNI_DEBUG -DCCATM_DEBUG

.PATH: ${LIBBASE}/netnatm ${LIBBASE}/netnatm/saal ${LIBBASE}/netnatm/misc \
	${LIBBASE}/netnatm/msg ${LIBBASE}/netnatm/sig ${LIBBASE}/netnatm/api
.PATH: ${CTRB}/libngatm ${CTRB}/man

SRCS=	unimsg.c unimsg_common.c straddr.c				\
	traffic.c uni_ie.c uni_msg.c					\
	saal_sscop.c saal_sscfu.c					\
	sig_call.c sig_coord.c sig_party.c sig_print.c sig_reset.c	\
	sig_uni.c sig_unimsgcpy.c sig_verify.c				\
	cc_conn.c cc_user.c cc_sig.c cc_data.c cc_port.c unisap.c	\
	cc_dump.c

# Includes
INCSGROUPS= INCSATM INCSSAAL INCSMSG INCSSIG INCSAPI

# common files
INCSATMDIR= $(INCLUDEDIR)/netnatm
INCSATM= unimsg.h addr.h

# signaling AAL
INCSSAALDIR= $(INCLUDEDIR)/netnatm/saal
INCSSAAL= saal/sscfu.h saal/sscfudef.h saal/sscop.h saal/sscopdef.h

# message parsing
INCSMSGDIR= $(INCLUDEDIR)/netnatm/msg
INCSMSG= msg/uni_config.h msg/uni_hdr.h msg/uni_ie.h msg/uni_msg.h	\
	 msg/unimsglib.h msg/uniprint.h msg/unistruct.h

# signaling layer
INCSSIGDIR= $(INCLUDEDIR)/netnatm/sig
INCSSIG= sig/uni.h sig/unidef.h sig/unisig.h

# call control layer
INCSAPIDIR= $(INCLUDEDIR)/netnatm/api
INCSAPI= api/atmapi.h api/ccatm.h api/unisap.h

.include <bsd.lib.mk>
