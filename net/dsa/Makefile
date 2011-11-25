# the core
obj-$(CONFIG_NET_DSA) += dsa_core.o
dsa_core-y += dsa.o slave.o

# tagging formats
dsa_core-$(CONFIG_NET_DSA_TAG_DSA) += tag_dsa.o
dsa_core-$(CONFIG_NET_DSA_TAG_EDSA) += tag_edsa.o
dsa_core-$(CONFIG_NET_DSA_TAG_TRAILER) += tag_trailer.o

# switch drivers
obj-$(CONFIG_NET_DSA_MV88E6060) += mv88e6060.o
obj-$(CONFIG_NET_DSA_MV88E6XXX) += mv88e6xxx_drv.o
mv88e6xxx_drv-y += mv88e6xxx.o
ifdef CONFIG_NET_DSA_MV88E6123_61_65
mv88e6xxx_drv-y += mv88e6123_61_65.o
endif
ifdef CONFIG_NET_DSA_MV88E6131
mv88e6xxx_drv-y += mv88e6131.o
endif
