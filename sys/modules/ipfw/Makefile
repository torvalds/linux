# $FreeBSD$

.PATH: ${SRCTOP}/sys/netpfil/ipfw

KMOD=	ipfw
SRCS=	ip_fw2.c ip_fw_pfil.c ip_fw_bpf.c
SRCS+=	ip_fw_dynamic.c ip_fw_log.c ip_fw_eaction.c
SRCS+=	ip_fw_sockopt.c ip_fw_table.c ip_fw_table_algo.c ip_fw_iface.c
SRCS+=	ip_fw_table_value.c
SRCS+=	opt_inet.h opt_inet6.h opt_ipdivert.h opt_ipfw.h

CFLAGS+= -DIPFIREWALL -I${SRCTOP}/sys/contrib/ck/include
#
#If you want it verbose
#CFLAGS+= -DIPFIREWALL_VERBOSE
#CFLAGS+= -DIPFIREWALL_VERBOSE_LIMIT=100
#
#If you want it to pass all packets by default
#CFLAGS+= -DIPFIREWALL_DEFAULT_TO_ACCEPT
#

.include <bsd.kmod.mk>
