# SPDX-License-Identifier: GPL-2.0-only

obj-y				+= ioctl.o common.o

obj-$(CONFIG_ETHTOOL_NETLINK)	+= ethtool_nl.o

ethtool_nl-y	:= netlink.o bitset.o strset.o linkinfo.o linkmodes.o \
		   linkstate.o debug.o wol.o
