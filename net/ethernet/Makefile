#
# Makefile for the Linux Ethernet layer.
#

obj-y					+= eth.o
obj-$(CONFIG_SYSCTL)			+= sysctl_net_ether.o
obj-$(subst m,y,$(CONFIG_IPX))		+= pe2.o
obj-$(subst m,y,$(CONFIG_ATALK))	+= pe2.o
