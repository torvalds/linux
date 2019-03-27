# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/rtwn

SYSDIR?=${SRCTOP}/sys
.include "${SYSDIR}/conf/kern.opts.mk"

KMOD     = rtwn
SRCS     = if_rtwn.c if_rtwn_tx.c if_rtwn_rx.c if_rtwn_beacon.c \
	   if_rtwn_calib.c if_rtwn_cam.c if_rtwn_task.c if_rtwn_efuse.c \
	   if_rtwn_fw.c if_rtwn_nop.h if_rtwnreg.h if_rtwnvar.h if_rtwn_tx.h \
	   if_rtwn_rx.h if_rtwn_beacon.h if_rtwn_calib.h if_rtwn_cam.h \
	   if_rtwn_task.h if_rtwn_efuse.h if_rtwn_fw.h \
	   bus_if.h device_if.h \
	   opt_bus.h opt_rtwn.h opt_wlan.h

.PATH:	   ${SRCTOP}/sys/dev/rtwn/rtl8192c
SRCS	+= r92c_attach.c r92c_beacon.c r92c_calib.c r92c_chan.c r92c_fw.c \
	   r92c_init.c r92c_llt.c r92c_rf.c r92c_rom.c r92c_rx.c r92c_tx.c \
	   r92c.h r92c_priv.h r92c_reg.h r92c_var.h r92c_rom_defs.h \
	   r92c_rom_image.h r92c_fw_cmd.h r92c_rx_desc.h r92c_tx_desc.h

.PATH:	   ${SRCTOP}/sys/dev/rtwn/rtl8188e
SRCS	+= r88e_beacon.c r88e_calib.c r88e_chan.c r88e_fw.c r88e_init.c \
	   r88e_led.c r88e_rf.c r88e_rom.c r88e_rx.c r88e_tx.c r88e.h \
	   r88e_priv.h r88e_reg.h r88e_rom_defs.h r88e_rom_image.h \
	   r88e_fw_cmd.h r88e_rx_desc.h r88e_tx_desc.h

.PATH:	   ${SRCTOP}/sys/dev/rtwn/rtl8192e
SRCS	+= r92e_chan.c r92e_fw.c r92e_init.c r92e_led.c r92e_rf.c \
	   r92e_rom.c r92e_rx.c r92e.h r92e_priv.h r92e_reg.h \
	   r92e_rom_image.h r92e_rom_defs.h

.PATH:	   ${SRCTOP}/sys/dev/rtwn/rtl8812a
SRCS	+= r12a_beacon.c r12a_calib.c r12a_caps.c r12a_chan.c r12a_fw.c \
	   r12a_init.c r12a_led.c r12a_rf.c r12a_rom.c r12a_rx.c r12a_tx.c \
	   r12a.h r12a_priv.h r12a_reg.h r12a_var.h r12a_rom_defs.h \
	   r12a_rom_image.h r12a_fw_cmd.h r12a_rx_desc.h r12a_tx_desc.h

.PATH:	   ${SRCTOP}/sys/dev/rtwn/rtl8821a
SRCS	+= r21a_beacon.c r21a_calib.c r21a_chan.c r21a_fw.c r21a_init.c \
	   r21a_led.c r21a_rom.c r21a_rx.c r21a.h r21a_priv.h r21a_reg.h

opt_rtwn.h:
	@echo "#define RTWN_DEBUG 1" > ${.TARGET}
.if ${MK_SOURCELESS_UCODE} == "no"
	@echo "#define RTWN_WITHOUT_UCODE 1" >> ${.TARGET}
.endif

.include <bsd.kmod.mk>
