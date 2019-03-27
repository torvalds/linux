# $FreeBSD$

.PATH: ${SRCTOP}/sys/netpfil/pf

KMOD=	pf
SRCS=	pf.c pf_if.c pf_lb.c pf_osfp.c pf_ioctl.c pf_norm.c pf_table.c \
	pf_ruleset.c in4_cksum.c \
	bus_if.h device_if.h \
	opt_pf.h opt_inet.h opt_inet6.h opt_bpf.h opt_global.h

.if !defined(KERNBUILDDIR)
# pflog can be loaded as a module, have the additional checks turned on
# pfsync can be loaded as a module, have the additional checks turned on
opt_pf.h:
	echo "#define DEV_PF 1" > ${.TARGET}
	echo "#define DEV_PFLOG 1" >> ${.TARGET}
	echo "#define DEV_PFSYNC 1" >> ${.TARGET}

.if defined(VIMAGE)
opt_global.h:
	echo "#define VIMAGE 1" >> ${.TARGET}
.endif
.endif

.include <bsd.kmod.mk>
