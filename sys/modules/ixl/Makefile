#$FreeBSD$

.PATH:  ${SRCTOP}/sys/dev/ixl

KMOD    = if_ixl
SRCS    = device_if.h bus_if.h pci_if.h ifdi_if.h
SRCS    += opt_inet.h opt_inet6.h opt_rss.h opt_ixl.h opt_iflib.h
SRCS    += if_ixl.c ixl_pf_main.c ixl_pf_qmgr.c ixl_txrx.c ixl_pf_i2c.c i40e_osdep.c
SRCS.PCI_IOV += pci_iov_if.h ixl_pf_iov.c

# Shared source
SRCS    += i40e_common.c i40e_nvm.c i40e_adminq.c i40e_lan_hmc.c i40e_hmc.c i40e_dcb.c

# Debug messages / sysctls
# CFLAGS += -DIXL_DEBUG

# Enable asserts and other debugging facilities
# CFLAGS += -DINVARIANTS -DINVARIANTS_SUPPORT -DWITNESS

# Enable iWARP client interface
# CFLAGS += -DIXL_IW
# SRCS	+= ixl_iw.c

.include <bsd.kmod.mk>
