# $FreeBSD$

.PATH: ${SRCTOP}/sys/contrib/dev/ipw

KMOD=	ipw_monitor
FIRMWS=	ipw_monitor:ipw_monitor:130
FIRMWARE_LICENSE=	intel_ipw

CLEANFILES=	ipw_monitor

ipw_monitor: ipw2100-1.3-p.fw.uu
	uudecode -p ${.ALLSRC} > ${.TARGET}

.include <bsd.kmod.mk>

