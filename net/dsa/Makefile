# the core
obj-$(CONFIG_NET_DSA) += dsa_core.o
dsa_core-y += dsa.o slave.o dsa2.o switch.o legacy.o

# tagging formats
dsa_core-$(CONFIG_NET_DSA_TAG_BRCM) += tag_brcm.o
dsa_core-$(CONFIG_NET_DSA_TAG_DSA) += tag_dsa.o
dsa_core-$(CONFIG_NET_DSA_TAG_EDSA) += tag_edsa.o
dsa_core-$(CONFIG_NET_DSA_TAG_LAN9303) += tag_lan9303.o
dsa_core-$(CONFIG_NET_DSA_TAG_MTK) += tag_mtk.o
dsa_core-$(CONFIG_NET_DSA_TAG_QCA) += tag_qca.o
dsa_core-$(CONFIG_NET_DSA_TAG_TRAILER) += tag_trailer.o
