# $FreeBSD$

CXGB = ${SRCTOP}/sys/dev/cxgb
.PATH: ${CXGB} ${CXGB}/common ${CXGB}/sys

KMOD=	if_cxgb
SRCS=	cxgb_mc5.c cxgb_vsc8211.c cxgb_ael1002.c cxgb_mv88e1xxx.c 
SRCS+=	cxgb_xgmac.c cxgb_vsc7323.c cxgb_t3_hw.c cxgb_main.c cxgb_aq100x.c
SRCS+=  cxgb_sge.c cxgb_tn1010.c
SRCS+=	device_if.h bus_if.h pci_if.h
SRCS+=	opt_inet.h opt_inet6.h opt_sched.h
SRCS+=	uipc_mvec.c

CFLAGS+= -g -DDEFAULT_JUMBO -I${CXGB}

.include <bsd.kmod.mk>
