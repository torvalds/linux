# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_IEEE802154) += ieee802154.o
obj-$(CONFIG_IEEE802154_SOCKET) += ieee802154_socket.o
obj-y += 6lowpan/

ieee802154-y := netlink.o nl-mac.o nl-phy.o nl_policy.o core.o \
                header_ops.o sysfs.o nl802154.o trace.o pan.o
ieee802154_socket-y := socket.o

CFLAGS_trace.o := -I$(src)
