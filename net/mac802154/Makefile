# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_MAC802154)	+= mac802154.o
mac802154-objs		:= main.o rx.o tx.o mac_cmd.o mib.o \
			   iface.o llsec.o util.o cfg.o scan.o trace.o

CFLAGS_trace.o := -I$(src)
