# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/rtwn/usb

SYSDIR?=${SRCTOP}/sys
.include "${SYSDIR}/conf/kern.opts.mk"

KMOD     = if_rtwn_usb
SRCS	 = rtwn_usb_attach.c rtwn_usb_ep.c rtwn_usb_reg.c rtwn_usb_rx.c \
	   rtwn_usb_tx.c rtwn_usb_attach.h rtwn_usb_ep.h rtwn_usb_reg.h \
	   rtwn_usb_rx.h rtwn_usb_tx.h rtwn_usb_var.h \
	   bus_if.h device_if.h \
	   opt_bus.h opt_rtwn.h opt_usb.h opt_wlan.h usb_if.h usbdevs.h

.PATH: ${SRCTOP}/sys/dev/rtwn/rtl8188e/usb
SRCS	+= r88eu_attach.c r88eu_init.c \
	   r88eu.h r88eu_reg.h

.PATH: ${SRCTOP}/sys/dev/rtwn/rtl8192c/usb
SRCS	+= r92cu_attach.c r92cu_init.c r92cu_led.c r92cu_rx.c r92cu_tx.c \
	   r92cu.h r92cu_priv.h r92cu_reg.h r92cu_tx_desc.h

.PATH: ${SRCTOP}/sys/dev/rtwn/rtl8192e/usb
SRCS	+= r92eu_attach.c r92eu_init.c r92eu.h r92eu_reg.h

.PATH: ${SRCTOP}/sys/dev/rtwn/rtl8812a/usb
SRCS	+= r12au_attach.c r12au_init.c r12au_rx.c r12au_tx.c \
	   r12au.h r12au_reg.h r12au_tx_desc.h

.PATH: ${SRCTOP}/sys/dev/rtwn/rtl8821a/usb
SRCS	+= r21au_attach.c r21au_init.c r21au_dfs.c \
	   r21au.h r21au_reg.h

opt_rtwn.h:
	@echo "#define RTWN_DEBUG 1" > ${.TARGET}
.if ${MK_SOURCELESS_UCODE} == "no"
	@echo "#define RTWN_WITHOUT_UCODE 1" >> ${.TARGET}
.endif

.include <bsd.kmod.mk>
