#$FreeBSD$

.PATH:  ${SRCTOP}/sys/dev/ixl

KMOD    = if_iavf
SRCS    = device_if.h bus_if.h pci_if.h ifdi_if.h
SRCS    += opt_inet.h opt_inet6.h opt_rss.h opt_ixl.h opt_iflib.h opt_global.h
SRCS    += if_iavf.c iavf_vc.c ixl_txrx.c i40e_osdep.c

# Shared source
SRCS    += i40e_common.c i40e_nvm.c i40e_adminq.c

# Debug messages / sysctls
# CFLAGS += -DIXL_DEBUG
# Enable asserts and other debugging facilities
# CFLAGS += -DINVARIANTS -DINVARIANTS_SUPPORT -DWITNESS

LINKS=	${KMODDIR}/${KMOD}.ko ${KMODDIR}/if_ixlv.ko

.include <bsd.kmod.mk>
