# $FreeBSD$
#
# Author: Shteryana Shopova <syrinx@freebsd.org>

CONTRIB= ${SRCTOP}/contrib/bsnmp
.PATH: ${CONTRIB}/snmp_vacm

MOD=	vacm
SRCS=	vacm_snmp.c
XSYM=	snmpVacmMIB

MAN=	snmp_vacm.3

CFLAGS+= -I${CONTRIB}/lib -I${CONTRIB}/snmpd
CFLAGS+= -DHAVE_ERR_H -DHAVE_GETADDRINFO -DHAVE_STRLCPY -DHAVE_SYS_TREE_H

GENSNMPTREEFLAGS+= -I${CONTRIB}/lib

DEFS=	${MOD}_tree.def
BMIBS=

.include <bsd.snmpmod.mk>
