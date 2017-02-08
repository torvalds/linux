# the core
obj-$(CONFIG_NET_DSA) += dsa_core.o
dsa_core-y += dsa.o slave.o dsa2.o switch.o

# tagging formats
dsa_core-$(CONFIG_NET_DSA_TAG_BRCM) += tag_brcm.o
dsa_core-$(CONFIG_NET_DSA_TAG_DSA) += tag_dsa.o
dsa_core-$(CONFIG_NET_DSA_TAG_EDSA) += tag_edsa.o
dsa_core-$(CONFIG_NET_DSA_TAG_TRAILER) += tag_trailer.o
dsa_core-$(CONFIG_NET_DSA_TAG_QCA) += tag_qca.o
