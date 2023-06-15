# SPDX-License-Identifier: GPL-2.0

# the stubs are built-in whenever DSA is built-in or module
ifdef CONFIG_NET_DSA
obj-y := stubs.o
endif

# the core
obj-$(CONFIG_NET_DSA) += dsa_core.o
dsa_core-y += \
	devlink.o \
	dsa.o \
	master.o \
	netlink.o \
	port.o \
	slave.o \
	switch.o \
	tag.o \
	tag_8021q.o \
	trace.o

# tagging formats
obj-$(CONFIG_NET_DSA_TAG_AR9331) += tag_ar9331.o
obj-$(CONFIG_NET_DSA_TAG_BRCM_COMMON) += tag_brcm.o
obj-$(CONFIG_NET_DSA_TAG_DSA_COMMON) += tag_dsa.o
obj-$(CONFIG_NET_DSA_TAG_GSWIP) += tag_gswip.o
obj-$(CONFIG_NET_DSA_TAG_HELLCREEK) += tag_hellcreek.o
obj-$(CONFIG_NET_DSA_TAG_KSZ) += tag_ksz.o
obj-$(CONFIG_NET_DSA_TAG_LAN9303) += tag_lan9303.o
obj-$(CONFIG_NET_DSA_TAG_MTK) += tag_mtk.o
obj-$(CONFIG_NET_DSA_TAG_NONE) += tag_none.o
obj-$(CONFIG_NET_DSA_TAG_OCELOT) += tag_ocelot.o
obj-$(CONFIG_NET_DSA_TAG_OCELOT_8021Q) += tag_ocelot_8021q.o
obj-$(CONFIG_NET_DSA_TAG_QCA) += tag_qca.o
obj-$(CONFIG_NET_DSA_TAG_RTL4_A) += tag_rtl4_a.o
obj-$(CONFIG_NET_DSA_TAG_RTL8_4) += tag_rtl8_4.o
obj-$(CONFIG_NET_DSA_TAG_RZN1_A5PSW) += tag_rzn1_a5psw.o
obj-$(CONFIG_NET_DSA_TAG_SJA1105) += tag_sja1105.o
obj-$(CONFIG_NET_DSA_TAG_TRAILER) += tag_trailer.o
obj-$(CONFIG_NET_DSA_TAG_XRS700X) += tag_xrs700x.o

# for tracing framework to find trace.h
CFLAGS_trace.o := -I$(src)
