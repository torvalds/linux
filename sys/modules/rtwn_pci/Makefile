# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/rtwn/pci

SYSDIR?=${SRCTOP}/sys
.include "${SYSDIR}/conf/kern.opts.mk"

KMOD     = if_rtwn_pci
SRCS	 = rtwn_pci_attach.c rtwn_pci_reg.c rtwn_pci_rx.c rtwn_pci_tx.c \
	   rtwn_pci_attach.h rtwn_pci_reg.h rtwn_pci_rx.h rtwn_pci_tx.h \
	   rtwn_pci_var.h \
	   device_if.h bus_if.h pci_if.h \
	   opt_bus.h opt_rtwn.h opt_wlan.h

.PATH: ${SRCTOP}/sys/dev/rtwn/rtl8192c/pci
SRCS	+= r92ce_attach.c r92ce_calib.c r92ce_fw.c r92ce_init.c r92ce_led.c \
	   r92ce_rx.c r92ce_tx.c \
	   r92ce.h r92ce_priv.h r92ce_reg.h r92ce_rx_desc.h r92ce_tx_desc.h

.PATH: ${SRCTOP}/sys/dev/rtwn/rtl8188e/pci
SRCS    += r88ee_attach.c r88ee_init.c r88ee_rx.c \
	   r88ee.h r88ee_reg.h

opt_rtwn.h:
	@echo "#define RTWN_DEBUG 1" > ${.TARGET}
.if ${MK_SOURCELESS_UCODE} == "no"
	@echo "#define RTWN_WITHOUT_UCODE 1" >> ${.TARGET}
.endif

.include <bsd.kmod.mk>
