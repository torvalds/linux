#
# Makefile for the L2TP.
#

obj-$(CONFIG_L2TP) += l2tp_core.o

# Build l2tp as modules if L2TP is M
obj-$(subst y,$(CONFIG_L2TP),$(CONFIG_PPPOL2TP)) += l2tp_ppp.o
