#
# Makefile for the Linux 802.x protocol layers.
#

obj-y			:= p8023.o

# Check the p8022 selections against net/core/Makefile.
obj-$(CONFIG_SYSCTL)	+= sysctl_net_802.o
obj-$(CONFIG_LLC)	+= p8022.o psnap.o
obj-$(CONFIG_TR)	+= p8022.o psnap.o tr.o sysctl_net_802.o
obj-$(CONFIG_NET_FC)	+=                 fc.o
obj-$(CONFIG_FDDI)	+=                 fddi.o
obj-$(CONFIG_HIPPI)	+=                 hippi.o
obj-$(CONFIG_IPX)	+= p8022.o psnap.o
obj-$(CONFIG_ATALK)	+= p8022.o psnap.o
